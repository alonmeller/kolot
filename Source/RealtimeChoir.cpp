#include "RealtimeChoir.h"
#include <cmath>
#include <algorithm>

namespace kolot
{

static inline float midiFromHz (float hz) { return 69.0f + 12.0f * std::log2 (hz / 440.0f); }

template <int N>
static float smallMedian (const float (&v)[N], int n)
{
    float t[N];
    for (int i = 0; i < n; ++i) t[i] = v[i];
    std::sort (t, t + n);
    return n > 0 ? t[n / 2] : 0.0f;
}

static juce::int64 nextPow2 (double v)
{
    juce::int64 p = 1;
    while ((double) p < v) p <<= 1;
    return p;
}

// ============================================================================
static const int scaleTable[9][7] = {
    { 0, 2, 4, 5, 7, 9, 11 },   // Major
    { 0, 2, 3, 5, 7, 8, 10 },   // Minor
    { 0, 2, 3, 5, 7, 8, 11 },   // Harmonic minor
    { 0, 2, 3, 5, 7, 9, 11 },   // Melodic minor
    { 0, 2, 3, 5, 7, 9, 10 },   // Dorian
    { 0, 2, 4, 5, 7, 9, 10 },   // Mixolydian
    { 0, 1, 3, 5, 7, 8, 10 },   // Phrygian
    { 0, 2, 4, 6, 7, 9, 11 },   // Lydian
    { 0, 0, 0, 0, 0, 0, 0 }     // Chromatic (fixed intervals)
};

int diatonicShift (int note, int key, int scale, int kind, int dir)
{
    scale = juce::jlimit (0, 8, scale);
    if (scale == 8) return (kind == 2 ? 4 : 7) * dir;
    const int* sc = scaleTable[scale];
    const int pc = ((note - key) % 12 + 12) % 12;
    int bestI = 0, bestDist = 99;
    for (int i = 0; i < 7; ++i)
    {
        int dd = pc - sc[i];
        if (dd > 6) dd -= 12;
        if (dd < -6) dd += 12;
        if (std::abs (dd) < std::abs (bestDist)) { bestDist = dd; bestI = i; }
    }
    const int j   = bestI + (kind == 2 ? 2 : 4) * dir;
    const int oct = (int) std::floor ((double) j / 7.0);
    const int jj  = ((j % 7) + 7) % 7;
    return sc[jj] + 12 * oct - sc[bestI];
}

// ============================================================================
void RealtimeChoir::SmoothNoise::init (juce::Random& r, float fLo, float fHi)
{
    for (int i = 0; i < 3; ++i)
    {
        f[i]  = fLo + r.nextFloat() * (fHi - fLo);
        ph[i] = r.nextFloat() * juce::MathConstants<float>::twoPi;
    }
}

float RealtimeChoir::SmoothNoise::eval (double tSec) const
{
    float s = 0.0f;
    for (int i = 0; i < 3; ++i)
        s += std::sin ((float) std::fmod (juce::MathConstants<double>::twoPi * f[i] * tSec, juce::MathConstants<double>::twoPi) + ph[i]);
    return s / 3.0f;
}

// ============================================================================
void RealtimeChoir::prepare (double sampleRate)
{
    sr = sampleRate;
    D = juce::jmax (1, juce::roundToInt (sr / 16000.0));
    const double srd = sr / D;
    hopD   = juce::jmax (1, juce::roundToInt (0.01 * srd));
    hop    = hopD * D;
    W      = juce::roundToInt (0.025 * srd);
    tauMin = juce::jmax (2, (int) (srd / 1100.0));
    tauMax = (int) (srd / 65.0);

    latency = (W / 2 + tauMax + 2) * D + 6 * hop + (int) (sr / 65.0 * 1.3);

    const auto inSize = nextPow2 (sr * 4.0);
    in.assign ((size_t) inSize, 0.0f); inMask = inSize - 1;
    const auto decSize = nextPow2 (srd * 4.0);
    dec.assign ((size_t) decSize, 0.0f); decMask = decSize - 1;

    auto c = juce::IIRCoefficients::makeLowPass (sr, juce::jmin (2500.0, 0.45 * srd));
    lp1.setCoefficients (c); lp2.setCoefficients (c);

    frames.assign ((size_t) frameMask + 1, Frame {});
    yinBuf.assign ((size_t) (W + tauMax + 4), 0.0f);
    yinD.assign ((size_t) tauMax + 2, 0.0f);
    yinCm.assign ((size_t) tauMax + 2, 0.0f);
    marks.assign ((size_t) markMask + 1, Mark { 0.0, 0.0f, false });

    hann.resize ((size_t) hannSize + 2);
    for (int i = 0; i <= hannSize + 1; ++i)
        hann[(size_t) i] = 0.5f - 0.5f * (float) std::cos (juce::MathConstants<double>::twoPi * i / hannSize);

    const auto olaSize = (size_t) juce::jmax ((juce::int64) 8192, nextPow2 (sr * 0.12));
    voices.resize (maxVoices);
    for (auto& v : voices) v.ola.assign (olaSize, 0.0f);

    reset();
}

void RealtimeChoir::reset()
{
    std::fill (in.begin(), in.end(), 0.0f);
    std::fill (dec.begin(), dec.end(), 0.0f);
    std::fill (frames.begin(), frames.end(), Frame {});
    lp1.reset(); lp2.reset();
    written = decWritten = frameCount = 0;
    decPhase = 0;
    peakRms = 0.0f;
    octCount = octPos = 0;
    for (int i = 0; i < 4; ++i) lastNote[i] = -1000;
    held[0] = 4; held[1] = -3; held[2] = 7; held[3] = -5;
    markHead = markTail = 0;
    markT = 0.0; lastMarkPos = -1e9;
    leadHz = inLevel = 0.0f;
    inPeakSlow = 0.1f;
    needsRebuild = true;
}

void RealtimeChoir::setArrange (const ArrangeSettings& a)
{
    if (a != arrange || needsRebuild)
    {
        arrange = a;
        needsRebuild = true;
    }
}

// ============================================================================
void RealtimeChoir::pushInput (const float* x, int n)
{
    float pk = 0.0f;
    double ss = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const float s = x[i];
        in[(size_t) (written & inMask)] = s;
        ++written;
        const float y = lp2.processSingleSampleRaw (lp1.processSingleSampleRaw (s));
        if (++decPhase >= D)
        {
            decPhase = 0;
            dec[(size_t) (decWritten & decMask)] = y;
            ++decWritten;
        }
        pk = juce::jmax (pk, std::abs (s));
        ss += (double) s * s;
    }
    inPeakSlow = juce::jmax (pk, inPeakSlow * 0.995f);
    inLevel = n > 0 ? (float) std::sqrt (ss / n) : 0.0f;
}

void RealtimeChoir::analyseFrames()
{
    for (;;)
    {
        const juce::int64 k  = frameCount;
        const juce::int64 cD = k * hopD;
        if (decWritten <= cD + W / 2 + tauMax + 2) break;
        if (written <= k * hop + hop) break;
        analyseFrame (k);
        ++frameCount;
    }
    const juce::int64 R = frameCount - 4;
    leadHz = R >= 0 ? frame (R).f0s : 0.0f;
}

void RealtimeChoir::analyseFrame (juce::int64 k)
{
    Frame& fr = frame (k);
    fr = Frame {};

    // RMS on the original signal around the frame centre.
    const juce::int64 c = k * hop;
    double s = 0.0;
    for (juce::int64 i = c - hop; i < c + hop; ++i)
    {
        const float v = i >= 0 ? in[(size_t) (i & inMask)] : 0.0f;
        s += (double) v * v;
    }
    fr.rms = (float) std::sqrt (s / (2.0 * hop));
    peakRms = juce::jmax (fr.rms, peakRms * 0.9995f);
    const float silence = juce::jmax (3.0e-4f, peakRms * 0.01f);

    if (fr.rms >= silence)
    {
        // YIN on the decimated signal.
        const juce::int64 a0 = k * hopD - W / 2;
        const int len = W + tauMax + 1;
        for (int i = 0; i < len; ++i)
        {
            const juce::int64 j = a0 + i;
            yinBuf[(size_t) i] = j >= 0 ? dec[(size_t) (j & decMask)] : 0.0f;
        }
        const float* a = yinBuf.data();
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            double d = 0.0;
            const float* b = a + tau;
            for (int j = 0; j < W; ++j) { const double q = a[j] - b[j]; d += q * q; }
            yinD[(size_t) tau] = (float) d;
        }
        double run = 0.0;
        yinCm[0] = 1.0f;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            run += yinD[(size_t) tau];
            yinCm[(size_t) tau] = run > 0.0 ? (float) (yinD[(size_t) tau] * tau / run) : 1.0f;
        }
        int best = -1;
        for (int tau = tauMin; tau < tauMax; ++tau)
        {
            if (yinCm[(size_t) tau] < 0.15f)
            {
                while (tau + 1 < tauMax && yinCm[(size_t) tau + 1] < yinCm[(size_t) tau]) ++tau;
                best = tau;
                break;
            }
        }
        if (best > 0)
        {
            double t = best;
            if (best > 1 && best < tauMax)
            {
                const double s0 = yinCm[(size_t) best - 1], s1 = yinCm[(size_t) best], s2 = yinCm[(size_t) best + 1];
                const double den = s0 - 2.0 * s1 + s2;
                if (std::abs (den) > 1e-12) t += 0.5 * (s0 - s2) / den;
            }
            fr.f0raw = (float) ((sr / D) / t);
        }
    }

    // Octave-error repair against the recent voiced history.
    fr.f0 = fr.f0raw;
    if (fr.f0 > 0.0f)
    {
        if (octCount >= 3)
        {
            const float m = smallMedian (octHist, octCount);
            if (fr.f0 > 1.8f * m) fr.f0 *= 0.5f;
            else if (fr.f0 < 0.56f * m) fr.f0 *= 2.0f;
        }
        octHist[octPos] = fr.f0;
        octPos = (octPos + 1) % 10;
        octCount = juce::jmin (10, octCount + 1);
    }

    // Harmony intervals (with hysteresis on note changes).
    for (int h = 0; h < 4; ++h)
    {
        if (fr.f0 > 0.0f)
        {
            const float m = midiFromHz (fr.f0);
            if (std::abs (m - (float) lastNote[h]) > 0.6f) lastNote[h] = juce::roundToInt (m);
            held[h] = (float) diatonicShift (lastNote[h], arrange.key, arrange.scale,
                                             h < 2 ? 2 : 3, (h % 2 == 0) ? 1 : -1);
        }
        fr.shiftRaw[h] = held[h];
    }

    // Smoothed pitch for frame k-2 (5-point median over voiced, drop 1-frame islands).
    if (k >= 4)
    {
        Frame& t = frame (k - 2);
        if (t.f0 > 0.0f && (frame (k - 3).f0 > 0.0f || frame (k - 1).f0 > 0.0f))
        {
            float v[5]; int n = 0;
            for (juce::int64 j = k - 4; j <= k; ++j) if (frame (j).f0 > 0.0f) v[n++] = frame (j).f0;
            t.f0s = smallMedian (v, n);
        }
        else t.f0s = 0.0f;
    }
    // Smoothed intervals for frame k-3 (7-point median).
    if (k >= 6)
    {
        Frame& t = frame (k - 3);
        for (int h = 0; h < 4; ++h)
        {
            float v[7]; int n = 0;
            for (juce::int64 j = k - 6; j <= k; ++j) v[n++] = frame (j).shiftRaw[h];
            t.shift[h] = smallMedian (v, n);
        }
    }
}

float RealtimeChoir::f0At (double t) const
{
    const juce::int64 R = frameCount - 4;
    if (R < 1) return 0.0f;
    const double fpos = t / hop;
    const juce::int64 i = juce::jlimit ((juce::int64) 0, R - 1, (juce::int64) std::floor (fpos));
    const float frac = (float) juce::jlimit (0.0, 1.0, fpos - (double) i);
    const float a = frame (i).f0s, b = frame (i + 1).f0s;
    if (a > 0.0f && b > 0.0f) return a * std::pow (b / a, frac);
    return frac < 0.5f ? a : b;
}

float RealtimeChoir::rmsAt (double t) const
{
    const juce::int64 R = frameCount - 1;
    if (R < 1) return 0.0f;
    const double fpos = t / hop;
    const juce::int64 i = juce::jlimit ((juce::int64) 0, R - 1, (juce::int64) std::floor (fpos));
    const float frac = (float) juce::jlimit (0.0, 1.0, fpos - (double) i);
    return frame (i).rms + frac * (frame (i + 1).rms - frame (i).rms);
}

float RealtimeChoir::shiftAt (int kind, double t) const
{
    switch (kind)
    {
        case 0: return 0.0f;
        case 1: return -12.0f;
        case 2: return 12.0f;
        default: break;
    }
    const juce::int64 R = frameCount - 4;
    const juce::int64 i = juce::jlimit ((juce::int64) 0, juce::jmax ((juce::int64) 0, R), (juce::int64) std::llround (t / hop));
    return frame (i).shift[kind - 3];
}

float RealtimeChoir::sampleAt (double pos) const
{
    const juce::int64 i = (juce::int64) std::floor (pos);
    if (i < 0 || i + 1 >= written || written - i > inMask) return 0.0f;
    const float f = (float) (pos - (double) i);
    const float a = in[(size_t) (i & inMask)], b = in[(size_t) ((i + 1) & inMask)];
    return a + f * (b - a);
}

// ============================================================================
void RealtimeChoir::generateMarks()
{
    const juce::int64 R = frameCount - 4;
    const double limit = (double) (R - 1) * hop;
    const double uv = 0.005 * sr;

    while (markT < limit)
    {
        Mark m;
        const float f = f0At (markT);
        if (f > 0.0f)
        {
            const double T = sr / f;
            auto lo = (juce::int64) juce::jmax (lastMarkPos + 0.6 * T, markT - 0.2 * T);
            auto hi = (juce::int64) juce::jmin ((double) written - 2, markT + 0.2 * T);
            lo = juce::jmax ((juce::int64) 0, lo);
            juce::int64 best = (juce::int64) markT;
            if (hi > lo)
            {
                float bv = -1e9f;
                for (juce::int64 i = lo; i <= hi; ++i)
                {
                    const float v = in[(size_t) (i & inMask)];
                    if (v > bv) { bv = v; best = i; }
                }
            }
            m = { (double) best, (float) T, true };
            markT = (double) best + T;
        }
        else
        {
            m = { markT, (float) uv, false };
            markT += uv;
        }
        lastMarkPos = m.pos;
        if (markTail - markHead > markMask) ++markHead;
        marks[(size_t) (markTail & markMask)] = m;
        ++markTail;
    }

    // Forget marks nobody can reach any more.
    const double oldest = (double) written - latency - 0.2 * sr;
    while (markTail - markHead > 2 && marks[(size_t) ((markHead + 1) & markMask)].pos < oldest) ++markHead;
}

juce::int64 RealtimeChoir::nearestMark (double t) const
{
    if (markTail <= markHead) return -1;
    juce::int64 lo = markHead, hi = markTail;   // first index with pos >= t
    while (lo < hi)
    {
        const juce::int64 mid = lo + (hi - lo) / 2;
        if (marks[(size_t) (mid & markMask)].pos < t) lo = mid + 1; else hi = mid;
    }
    if (lo >= markTail) return markTail - 1;
    if (lo > markHead && std::abs (marks[(size_t) ((lo - 1) & markMask)].pos - t) < std::abs (marks[(size_t) (lo & markMask)].pos - t))
        return lo - 1;
    return lo;
}

// ============================================================================
namespace
{
struct ModelParams
{
    float phiLo1, phiHi1, phiLo2, phiHi2, vibLo, vibHi, rateLo, rateHi, detune, timingMs, drive, driftCents;
};

ModelParams paramsFor (ChoirModel m)
{
    switch (m)
    {
        case ChoirModel::Kids:  return { 1.16f, 1.24f, 1.20f, 1.30f,  4.f, 12.f, 5.2f, 6.6f,  7.f, 20.f, 0.0f, 4.f };
        case ChoirModel::Shout: return { 0.88f, 0.96f, 0.94f, 1.02f,  0.f,  4.f, 4.0f, 5.0f, 28.f, 45.f, 3.5f, 10.f };
        default:                return { 0.90f, 0.98f, 1.03f, 1.12f, 12.f, 28.f, 4.8f, 6.2f,  9.f, 28.f, 0.0f, 6.f };
    }
}
} // namespace

void RealtimeChoir::rebuildVoices()
{
    needsRebuild = false;
    const auto& st = arrange;
    const ModelParams mp = paramsFor (st.model);
    drive = mp.drive;
    driftCents = st.testSingleVoice ? 0.0f : mp.driftCents;
    range = choirRange (st.model);

    struct L { int kind, count; float gain; };
    L layers[7]; int nl = 0;
    const bool shout = st.model == ChoirModel::Shout;
    if (! st.testDropLead) layers[nl++] = { 0, shout ? 14 : 16, 1.0f };
    if (st.octaveDown) layers[nl++] = { 1, 8, 0.80f };
    if (st.octaveUp)   layers[nl++] = { 2, 8, 0.70f };
    if (st.thirdUp)    layers[nl++] = { 3, 8, 0.75f };
    if (st.thirdDown)  layers[nl++] = { 4, 8, 0.75f };
    if (st.fifthUp)    layers[nl++] = { 5, 8, 0.72f };
    if (st.fifthDown)  layers[nl++] = { 6, 8, 0.72f };

    juce::Random rnd ((juce::int64) st.seed * 7919 + 17 + (int) st.model * 101);
    numVoices = 0;
    const double now = (double) written;
    for (int li = 0; li < nl; ++li)
    {
        const int count = st.testSingleVoice ? 1 : layers[li].count;
        for (int k = 0; k < count && numVoices < maxVoices; ++k)
        {
            Voice& v = voices[(size_t) numVoices++];
            v.layerKind = layers[li].kind;
            v.layerGain = 0.9f * layers[li].gain / std::sqrt ((float) count);   // ≈ same loudness as the track
            const bool high = (k % 2) == 1;
            const float lo = high ? mp.phiLo2 : mp.phiLo1, hi = high ? mp.phiHi2 : mp.phiHi1;
            v.phi          = lo + rnd.nextFloat() * (hi - lo);
            v.vibCents     = mp.vibLo + rnd.nextFloat() * (mp.vibHi - mp.vibLo);
            v.vibRate      = mp.rateLo + rnd.nextFloat() * (mp.rateHi - mp.rateLo);
            v.vibPhase     = rnd.nextFloat() * juce::MathConstants<float>::twoPi;
            v.detuneCents  = (rnd.nextFloat() * 2.0f - 1.0f) * mp.detune;
            v.baseOffsetMs = rnd.nextFloat() * mp.timingMs;
            v.gain         = 0.8f + 0.2f * rnd.nextFloat();
            v.drift.init  (rnd, 0.08f, 0.6f);
            v.timing.init (rnd, 0.05f, 0.3f);
            v.amp.init    (rnd, 0.1f, 0.9f);
            if (st.testSingleVoice) { v.vibCents = v.detuneCents = v.baseOffsetMs = 0.0f; v.phi = 1.0f; v.gain = 1.0f; }
            v.ts = now;
            v.wasVoiced = false;
            std::fill (v.ola.begin(), v.ola.end(), 0.0f);
        }
    }
    if (numVoices > 0 && voices[0].layerKind == 0) { voices[0].baseOffsetMs = 2.0f; voices[0].detuneCents *= 0.3f; }

    // Spread across the stereo field (shuffled so sections interleave).
    float pans[maxVoices];
    for (int i = 0; i < numVoices; ++i) pans[i] = numVoices > 1 ? -0.9f + 1.8f * (float) i / (float) (numVoices - 1) : 0.0f;
    for (int i = numVoices - 1; i > 0; --i) std::swap (pans[i], pans[rnd.nextInt (i + 1)]);
    for (int i = 0; i < numVoices; ++i)
    {
        const float th = (pans[i] + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        voices[(size_t) i].gl = std::cos (th);
        voices[(size_t) i].gr = std::sin (th);
    }
}

void RealtimeChoir::renderVoices (int n)
{
    const juce::int64 n0 = written - n, blockEnd = written;
    const double twoPi = juce::MathConstants<double>::twoPi;

    for (int vi = 0; vi < numVoices; ++vi)
    {
        Voice& v = voices[(size_t) vi];
        const juce::int64 olaMask = (juce::int64) v.ola.size() - 1;

        for (int guard = 0; guard < 4096; ++guard)
        {
            const double ts = v.ts;
            const double tSec = ts / sr;
            const double off = juce::jmax (0.0, (v.baseOffsetMs + 4.0 * v.timing.eval (tSec)) * sr / 1000.0);
            const double ta = ts - latency - off;
            const juce::int64 k = ta >= 0.0 ? nearestMark (ta) : -1;

            if (k < 0 || ta > lastMarkPos)
            {
                if (ts >= (double) blockEnd) break;
                v.ts += 32.0;
                continue;
            }
            const Mark& m = marks[(size_t) (k & markMask)];

            double Ts, L;
            float phi = 1.0f;
            if (m.voiced)
            {
                if (! v.wasVoiced) v.onset = tSec;
                float shift = shiftAt (v.layerKind, m.pos);
                const float lead = midiFromHz (juce::jmax (40.0f, f0At (m.pos)));
                float target = lead + shift;
                while (target > (float) range.hi + 0.5f) { shift -= 12.0f; target -= 12.0f; }
                while (target < (float) range.lo - 0.5f) { shift += 12.0f; target += 12.0f; }

                const float vibEnv = juce::jlimit (0.0f, 1.0f, (float) ((tSec - v.onset - 0.25) / 0.4));
                const float cents  = v.detuneCents + driftCents * v.drift.eval (tSec)
                                   + v.vibCents * vibEnv * std::sin ((float) std::fmod (twoPi * v.vibRate * tSec, twoPi) + v.vibPhase);
                const double r = std::pow (2.0, (shift + cents / 100.0) / 12.0);
                Ts  = m.period / r;
                phi = v.phi;
                L   = 2.0 * m.period / phi;
            }
            else
            {
                Ts = m.period;
                L  = 2.0 * m.period;
            }

            const int Li = juce::jmax (4, (int) L);
            const juce::int64 start = (juce::int64) std::llround (ts - Li * 0.5);
            if (start >= blockEnd) break;
            v.wasVoiced = m.voiced;

            const float rms  = rmsAt (m.pos);
            const float thr  = juce::jmax (3.0e-4f, peakRms * 0.006f);
            const float gate = juce::jlimit (0.0f, 1.0f, (rms - thr) / thr);
            if (gate > 0.0f)
            {
                const float g = gate * v.gain * juce::Decibels::decibelsToGain (1.2f * v.amp.eval (tSec))
                              * (float) (Ts / (0.5 * L));
                const float step = (float) hannSize / (float) Li;
                for (int i = 0; i < Li; ++i)
                {
                    const juce::int64 o = start + i;
                    if (o < n0) continue;
                    const float hp = i * step;
                    const int hi = (int) hp;
                    const float w = hann[(size_t) hi] + (hp - (float) hi) * (hann[(size_t) hi + 1] - hann[(size_t) hi]);
                    v.ola[(size_t) (o & olaMask)] += g * w * sampleAt (m.pos + (i - Li * 0.5) * phi);
                }
            }
            v.ts += juce::jmax (8.0, Ts);
        }
    }
}

void RealtimeChoir::process (const float* x, float* outL, float* outR, float* outM, int numSamples)
{
    for (int done = 0; done < numSamples;)
    {
        const int n = juce::jmin (maxChunk, numSamples - done);
        pushInput (x + done, n);
        analyseFrames();
        generateMarks();
        if (needsRebuild) rebuildVoices();
        renderVoices (n);

        float* L = outL + done; float* R = outR + done; float* M = outM + done;
        std::fill (L, L + n, 0.0f); std::fill (R, R + n, 0.0f); std::fill (M, M + n, 0.0f);

        const juce::int64 n0 = written - n;
        const float pre = 0.9f / juce::jmax (0.02f, inPeakSlow);
        const float dn  = drive > 0.0f ? 1.0f / std::tanh (drive) : 1.0f;
        for (int vi = 0; vi < numVoices; ++vi)
        {
            Voice& v = voices[(size_t) vi];
            const juce::int64 olaMask = (juce::int64) v.ola.size() - 1;
            const float gl = v.gl * v.layerGain, gr = v.gr * v.layerGain, gm = v.layerGain;
            for (int i = 0; i < n; ++i)
            {
                float& slot = v.ola[(size_t) ((n0 + i) & olaMask)];
                float s = slot;
                slot = 0.0f;
                if (drive > 0.0f) s = std::tanh (drive * s * pre) * dn * 0.8f / pre;
                L[i] += s * gl; R[i] += s * gr; M[i] += s * gm;
            }
        }
        done += n;
    }
}

} // namespace kolot
