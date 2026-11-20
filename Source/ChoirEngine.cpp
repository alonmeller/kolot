// Kolot — DSP choir engine.
//
// Pipeline:
//   1. YIN pitch tracking (decimated to ~16 kHz) with octave-error repair.
//   2. Pitch marks on the original signal (one per glottal period).
//   3. For every virtual singer: TD-PSOLA resynthesis with its own pitch
//      interval (unison / octave / diatonic 3rd / 5th), detune, slow drift,
//      vibrato, formant shift (voice size / age), timing offset and level.
//   4. Singers are spread across the stereo field and summed (parallel).

#include "ChoirEngine.h"
#include <cmath>
#include <thread>
#include <algorithm>
#include <numeric>

namespace kolot
{

static inline float midiFromHz (float hz) { return 69.0f + 12.0f * std::log2 (hz / 440.0f); }

// ============================================================================
float PitchTrack::f0At (double sample) const
{
    if (f0.empty()) return 0.0f;
    const double fpos = sample / (double) hop;
    const int i = juce::jlimit (0, numFrames() - 1, (int) std::floor (fpos));
    const int j = juce::jmin (numFrames() - 1, i + 1);
    const float frac = (float) (fpos - std::floor (fpos));
    const float a = f0[(size_t) i], b = f0[(size_t) j];
    if (a > 0.0f && b > 0.0f) return a * std::pow (b / a, frac);   // log-linear
    return frac < 0.5f ? a : b;
}

float PitchTrack::rmsAt (double sample) const
{
    if (rms.empty()) return 0.0f;
    const double fpos = sample / (double) hop;
    const int i = juce::jlimit (0, (int) rms.size() - 1, (int) std::floor (fpos));
    const int j = juce::jmin ((int) rms.size() - 1, i + 1);
    const float frac = (float) (fpos - std::floor (fpos));
    return rms[(size_t) i] + frac * (rms[(size_t) j] - rms[(size_t) i]);
}

static float median (std::vector<float> v)
{
    if (v.empty()) return 0.0f;
    std::nth_element (v.begin(), v.begin() + (long) v.size() / 2, v.end());
    return v[v.size() / 2];
}

PitchTrack analysePitch (const float* x, int n, double sr)
{
    PitchTrack tr;
    tr.sampleRate = sr;

    const int D = juce::jmax (1, juce::roundToInt (sr / 16000.0));
    const double srd = sr / D;

    // Low-pass then decimate.
    std::vector<float> tmp (x, x + n);
    {
        juce::IIRFilter f1, f2;
        auto c = juce::IIRCoefficients::makeLowPass (sr, juce::jmin (2500.0, 0.45 * srd));
        f1.setCoefficients (c); f2.setCoefficients (c);
        f1.processSamples (tmp.data(), n);
        f2.processSamples (tmp.data(), n);
    }

    const int tauMin = juce::jmax (2, (int) (srd / 1100.0));
    const int tauMax = (int) (srd / 65.0);
    const int W      = juce::roundToInt (0.025 * srd);
    const int pad    = W + tauMax + 4;
    const int nd     = (n + D - 1) / D;

    std::vector<float> xd ((size_t) (nd + 2 * pad), 0.0f);
    for (int i = 0; i < nd; ++i) xd[(size_t) (i + pad)] = tmp[(size_t) (i * D)];

    const int hopD = juce::jmax (1, juce::roundToInt (0.01 * srd));
    tr.hop = hopD * D;
    const int frames = n / tr.hop + 1;
    tr.f0.assign ((size_t) frames, 0.0f);
    tr.rms.assign ((size_t) frames, 0.0f);

    // RMS on the original signal.
    for (int fr = 0; fr < frames; ++fr)
    {
        const int c = fr * tr.hop;
        const int a = juce::jmax (0, c - tr.hop), b = juce::jmin (n, c + tr.hop);
        double s = 0.0;
        for (int i = a; i < b; ++i) s += (double) x[i] * x[i];
        tr.rms[(size_t) fr] = b > a ? (float) std::sqrt (s / (b - a)) : 0.0f;
        tr.peakRms = juce::jmax (tr.peakRms, tr.rms[(size_t) fr]);
    }

    std::vector<double> d ((size_t) tauMax + 2), cm ((size_t) tauMax + 2);
    const float silence = tr.peakRms * 0.01f;   // -40 dB relative

    for (int fr = 0; fr < frames; ++fr)
    {
        if (tr.rms[(size_t) fr] < silence) continue;
        const float* a = xd.data() + pad + fr * hopD - W / 2;

        for (int tau = 1; tau <= tauMax; ++tau)
        {
            double s = 0.0;
            const float* b = a + tau;
            for (int j = 0; j < W; ++j) { const double q = a[j] - b[j]; s += q * q; }
            d[(size_t) tau] = s;
        }
        cm[0] = 1.0;
        double run = 0.0;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            run += d[(size_t) tau];
            cm[(size_t) tau] = run > 0.0 ? d[(size_t) tau] * tau / run : 1.0;
        }

        int best = -1;
        for (int tau = tauMin; tau < tauMax; ++tau)
        {
            if (cm[(size_t) tau] < 0.15)
            {
                while (tau + 1 < tauMax && cm[(size_t) tau + 1] < cm[(size_t) tau]) ++tau;
                best = tau;
                break;
            }
        }
        if (best < 0) continue;

        double t = best;
        if (best > 1 && best < tauMax)
        {
            const double s0 = cm[(size_t) best - 1], s1 = cm[(size_t) best], s2 = cm[(size_t) best + 1];
            const double den = s0 - 2.0 * s1 + s2;
            if (std::abs (den) > 1e-12) t += 0.5 * (s0 - s2) / den;
        }
        tr.f0[(size_t) fr] = (float) (srd / t);
    }

    // --- clean-up: octave errors, short islands, small gaps, median smoothing.
    auto& f = tr.f0;
    std::vector<float> fixed = f;
    for (int i = 0; i < frames; ++i)
    {
        if (f[(size_t) i] <= 0.0f) continue;
        std::vector<float> nb;
        for (int k = juce::jmax (0, i - 10); k <= juce::jmin (frames - 1, i + 10); ++k)
            if (f[(size_t) k] > 0.0f) nb.push_back (f[(size_t) k]);
        const float m = median (nb);
        float v = f[(size_t) i];
        if (v > 1.8f * m) v *= 0.5f;
        else if (v < 0.56f * m) v *= 2.0f;
        fixed[(size_t) i] = v;
    }
    f = fixed;

    for (int i = 0; i < frames;)          // remove voiced islands shorter than 3 frames
    {
        if (f[(size_t) i] <= 0.0f) { ++i; continue; }
        int j = i;
        while (j < frames && f[(size_t) j] > 0.0f) ++j;
        if (j - i < 3) for (int k = i; k < j; ++k) f[(size_t) k] = 0.0f;
        i = j;
    }
    for (int i = 1; i < frames - 1; ++i)  // bridge 1–2 frame gaps
    {
        if (f[(size_t) i] > 0.0f || f[(size_t) i - 1] <= 0.0f) continue;
        int j = i;
        while (j < frames && f[(size_t) j] <= 0.0f && j - i < 3) ++j;
        if (j < frames && j - i <= 2 && f[(size_t) j] > 0.0f && tr.rms[(size_t) i] > silence)
            for (int k = i; k < j; ++k)
                f[(size_t) k] = f[(size_t) i - 1] + (f[(size_t) j] - f[(size_t) i - 1]) * (float) (k - i + 1) / (float) (j - i + 1);
    }
    fixed = f;
    for (int i = 0; i < frames; ++i)      // 5-point median over voiced frames
    {
        if (f[(size_t) i] <= 0.0f) continue;
        std::vector<float> nb;
        for (int k = juce::jmax (0, i - 2); k <= juce::jmin (frames - 1, i + 2); ++k)
            if (f[(size_t) k] > 0.0f) nb.push_back (f[(size_t) k]);
        fixed[(size_t) i] = median (nb);
    }
    f = fixed;
    return tr;
}

// ============================================================================
static const std::vector<std::vector<int>>& scaleTables()
{
    static const std::vector<std::vector<int>> t = {
        { 0, 2, 4, 5, 7, 9, 11 },   // Major
        { 0, 2, 3, 5, 7, 8, 10 },   // Minor
        { 0, 2, 3, 5, 7, 8, 11 },   // Harmonic minor
        { 0, 2, 3, 5, 7, 9, 11 },   // Melodic minor
        { 0, 2, 3, 5, 7, 9, 10 },   // Dorian
        { 0, 2, 4, 5, 7, 9, 10 },   // Mixolydian
        { 0, 1, 3, 5, 7, 8, 10 },   // Phrygian
        { 0, 2, 4, 6, 7, 9, 11 },   // Lydian
        {}                          // Chromatic (fixed intervals)
    };
    return t;
}

std::vector<float> computeLayerShift (const PitchTrack& track, int key, int scale, int kind, int direction)
{
    const int frames = track.numFrames();
    std::vector<float> shift ((size_t) frames, 0.0f);
    if (kind == 0) return shift;
    if (kind == 1) { std::fill (shift.begin(), shift.end(), 12.0f * (float) direction); return shift; }

    const auto& tables = scaleTables();
    const auto& sc = tables[(size_t) juce::jlimit (0, (int) tables.size() - 1, scale)];

    if (sc.empty())   // chromatic: fixed major third / perfect fifth
    {
        const float v = (kind == 2 ? 4.0f : 7.0f) * (float) direction;
        std::fill (shift.begin(), shift.end(), v);
        return shift;
    }

    const int size  = (int) sc.size();
    const int steps = (kind == 2 ? 2 : 4) * direction;
    float held = (kind == 2 ? 4.0f : 7.0f) * (float) direction;
    int lastNote = -1000;

    for (int fr = 0; fr < frames; ++fr)
    {
        const float hz = track.f0[(size_t) fr];
        if (hz > 0.0f)
        {
            const float m = midiFromHz (hz);
            if (std::abs (m - (float) lastNote) > 0.6f) lastNote = juce::roundToInt (m);
            const int pc = ((lastNote - key) % 12 + 12) % 12;

            int bestI = 0, bestDist = 99;
            for (int i = 0; i < size; ++i)
            {
                int dd = pc - sc[(size_t) i];
                if (dd > 6) dd -= 12;
                if (dd < -6) dd += 12;
                if (std::abs (dd) < std::abs (bestDist)) { bestDist = dd; bestI = i; }
            }
            const int j   = bestI + steps;
            const int oct = (int) std::floor ((double) j / size);
            const int jj  = ((j % size) + size) % size;
            held = (float) (sc[(size_t) jj] + 12 * oct - sc[(size_t) bestI]);
        }
        shift[(size_t) fr] = held;
    }

    // Median-7 to avoid flicker at note boundaries.
    std::vector<float> out = shift;
    for (int i = 0; i < frames; ++i)
    {
        std::vector<float> nb;
        for (int k = juce::jmax (0, i - 3); k <= juce::jmin (frames - 1, i + 3); ++k)
            nb.push_back (shift[(size_t) k]);
        out[(size_t) i] = median (nb);
    }
    return out;
}

// ============================================================================
namespace
{
struct Mark { double pos; float period; bool voiced; };

std::vector<Mark> computeMarks (const std::vector<float>& x, const PitchTrack& tr)
{
    std::vector<Mark> marks;
    const int n = (int) x.size();
    const double sr = tr.sampleRate;
    const double uv = 0.005 * sr;
    double t = 0.0, last = -1e9;

    while (t < n)
    {
        const float f = tr.f0At (t);
        if (f > 0.0f)
        {
            const double T = sr / f;
            int lo = (int) juce::jmax (last + 0.6 * T, t - 0.2 * T);
            int hi = (int) juce::jmin ((double) n - 1, t + 0.2 * T);
            lo = juce::jlimit (0, n - 1, lo);
            int best = juce::jlimit (0, n - 1, (int) t);
            if (hi > lo)
            {
                float bv = -1e9f;
                for (int i = lo; i <= hi; ++i) if (x[(size_t) i] > bv) { bv = x[(size_t) i]; best = i; }
            }
            marks.push_back ({ (double) best, (float) T, true });
            last = best;
            t = best + T;
        }
        else
        {
            marks.push_back ({ t, (float) uv, false });
            last = t;
            t += uv;
        }
    }
    return marks;
}

// Sum of three slow sines — smooth, non-repeating-sounding drift in [-1, 1].
struct SmoothNoise
{
    float f[3] {}, ph[3] {};
    void init (juce::Random& r, float fLo, float fHi)
    {
        for (int i = 0; i < 3; ++i)
        {
            f[i]  = fLo + r.nextFloat() * (fHi - fLo);
            ph[i] = r.nextFloat() * juce::MathConstants<float>::twoPi;
        }
    }
    float eval (double tSec) const
    {
        float s = 0.0f;
        for (int i = 0; i < 3; ++i)
            s += std::sin ((float) (juce::MathConstants<double>::twoPi * f[i] * tSec) + ph[i]);
        return s / 3.0f;
    }
};

struct ModelParams
{
    float phiLo1, phiHi1, phiLo2, phiHi2;  // two sections (low / high voices)
    float vibLo, vibHi, rateLo, rateHi;    // vibrato depth (cents) and rate (Hz)
    float detune;                          // ± cents
    float timingMs;                        // 0..timingMs offset
    float drive;                           // 0 = clean
    float driftCents;
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

struct Voice
{
    int   layer = 0;
    float phi = 1.0f, vibCents = 0.0f, vibRate = 5.5f, vibPhase = 0.0f;
    float detuneCents = 0.0f, baseOffsetMs = 0.0f, gain = 1.0f, pan = 0.0f;
    SmoothNoise drift, timing, amp;
};

struct Layer { std::vector<float> shift; int voices; float gain; };

inline float interp (const std::vector<float>& x, double pos)
{
    const int i = (int) std::floor (pos);
    if (i < 0 || i + 1 >= (int) x.size()) return 0.0f;
    const float f = (float) (pos - i);
    return x[(size_t) i] + f * (x[(size_t) i + 1] - x[(size_t) i]);
}

void renderVoice (const std::vector<float>& x, const std::vector<Mark>& marks,
                  const std::vector<double>& markPos, const PitchTrack& tr,
                  const Voice& v, const Layer& layer, Range range, float driftCents,
                  float drive, std::vector<float>& out)
{
    const double sr = tr.sampleRate;
    const int n = (int) x.size();
    const int outLen = (int) out.size();
    std::fill (out.begin(), out.end(), 0.0f);

    double ts = 0.0;
    bool wasVoiced = false;
    double onset = 0.0;

    while (ts < outLen - 1)
    {
        const double tSec = ts / sr;
        const double off  = juce::jmax (0.0, (v.baseOffsetMs + 4.0 * v.timing.eval (tSec)) * sr / 1000.0);
        const double ta   = ts - off;
        if (ta < 0.0) { ts += 32.0; continue; }
        if (ta >= n) break;

        auto it = std::lower_bound (markPos.begin(), markPos.end(), ta);
        size_t k = (size_t) (it - markPos.begin());
        if (k >= marks.size()) k = marks.size() - 1;
        if (k > 0 && std::abs (markPos[k - 1] - ta) < std::abs (markPos[k] - ta)) --k;
        const Mark& m = marks[k];

        double Ts, L;
        float phi = 1.0f;
        if (m.voiced)
        {
            if (! wasVoiced) onset = tSec;
            const int fr = juce::jlimit (0, (int) layer.shift.size() - 1, (int) (m.pos / tr.hop));
            float shift = layer.shift[(size_t) fr];
            const float lead = midiFromHz (juce::jmax (40.0f, tr.f0At (m.pos)));
            float target = lead + shift;
            while (target > (float) range.hi + 0.5f) { shift -= 12.0f; target -= 12.0f; }
            while (target < (float) range.lo - 0.5f) { shift += 12.0f; target += 12.0f; }

            const float vibEnv = juce::jlimit (0.0f, 1.0f, (float) ((tSec - onset - 0.25) / 0.4));
            const float cents  = v.detuneCents + driftCents * v.drift.eval (tSec)
                               + v.vibCents * vibEnv * std::sin ((float) (juce::MathConstants<double>::twoPi * v.vibRate * tSec) + v.vibPhase);
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
        wasVoiced = m.voiced;

        const float rel  = tr.peakRms > 0.0f ? tr.rmsAt (m.pos) / tr.peakRms : 0.0f;
        const float gate = juce::jlimit (0.0f, 1.0f, (rel - 0.004f) / 0.008f);

        if (gate > 0.0f)
        {
            const float g   = gate * v.gain * juce::Decibels::decibelsToGain (1.2f * v.amp.eval (tSec))
                            * (float) (Ts / (0.5 * L));
            const int Li    = juce::jmax (4, (int) L);
            const int start = (int) std::lround (ts - Li * 0.5);
            const double inv = 1.0 / Li;
            for (int i = 0; i < Li; ++i)
            {
                const int o = start + i;
                if (o < 0 || o >= outLen) continue;
                const float w = 0.5f - 0.5f * (float) std::cos (juce::MathConstants<double>::twoPi * i * inv);
                out[(size_t) o] += g * w * interp (x, m.pos + (i - Li * 0.5) * phi);
            }
        }
        ts += juce::jmax (8.0, Ts);
    }

    if (drive > 0.0f)
    {
        const float norm = 1.0f / std::tanh (drive);
        for (auto& s : out) s = std::tanh (drive * s) * norm * 0.8f;
    }
}
} // namespace

// ============================================================================
bool DspChoirEngine::render (const juce::AudioBuffer<float>& monoVocal, double sr,
                             const ArrangeSettings& st, ChoirStems& out, RenderProgress& progress)
{
    const int n = monoVocal.getNumSamples();
    if (n < (int) (sr * 0.1) || monoVocal.getNumChannels() < 1) return false;

    // Copy, remove DC / rumble, normalise to 0.9 peak.
    std::vector<float> x (monoVocal.getReadPointer (0), monoVocal.getReadPointer (0) + n);
    {
        juce::IIRFilter hp;
        hp.setCoefficients (juce::IIRCoefficients::makeHighPass (sr, 40.0));
        hp.processSamples (x.data(), n);
        float pk = 0.0f;
        for (auto s : x) pk = juce::jmax (pk, std::abs (s));
        if (pk < 1e-5f) return false;
        for (auto& s : x) s *= 0.9f / pk;
    }
    progress.fraction = 0.03f;

    const PitchTrack track = analysePitch (x.data(), n, sr);
    if (progress.cancel) return false;
    progress.fraction = 0.15f;

    const auto marks = computeMarks (x, track);
    std::vector<double> markPos; markPos.reserve (marks.size());
    for (auto& m : marks) markPos.push_back (m.pos);

    // Layers.
    std::vector<Layer> layers;
    const bool shout = st.model == ChoirModel::Shout;
    layers.push_back ({ computeLayerShift (track, st.key, st.scale, 0, 0), shout ? 14 : 16, 1.0f });
    if (st.octaveDown) layers.push_back ({ computeLayerShift (track, st.key, st.scale, 1, -1), 8, 0.80f });
    if (st.octaveUp)   layers.push_back ({ computeLayerShift (track, st.key, st.scale, 1, +1), 8, 0.70f });
    if (st.thirdUp)    layers.push_back ({ computeLayerShift (track, st.key, st.scale, 2, +1), 8, 0.75f });
    if (st.thirdDown)  layers.push_back ({ computeLayerShift (track, st.key, st.scale, 2, -1), 8, 0.75f });
    if (st.fifthUp)    layers.push_back ({ computeLayerShift (track, st.key, st.scale, 3, +1), 8, 0.72f });
    if (st.fifthDown)  layers.push_back ({ computeLayerShift (track, st.key, st.scale, 3, -1), 8, 0.72f });
    if (st.testDropLead && layers.size() > 1) layers.erase (layers.begin());
    progress.fraction = 0.2f;

    // Singers.
    const ModelParams mp = paramsFor (st.model);
    juce::Random rnd ((juce::int64) st.seed * 7919 + 17);
    std::vector<Voice> voices;
    for (int li = 0; li < (int) layers.size(); ++li)
    {
        if (st.testSingleVoice) layers[(size_t) li].voices = 1;
        for (int k = 0; k < layers[(size_t) li].voices; ++k)
        {
            Voice v;
            v.layer = li;
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
            voices.push_back (v);
        }
    }
    // The first singer of the main layer stays tight on the lead.
    if (! voices.empty()) { voices[0].baseOffsetMs = 2.0f; voices[0].detuneCents *= 0.3f; }

    // Spread across the stereo field (shuffled so layers interleave).
    {
        const int V = (int) voices.size();
        std::vector<float> pans ((size_t) V);
        for (int i = 0; i < V; ++i) pans[(size_t) i] = V > 1 ? -0.9f + 1.8f * (float) i / (float) (V - 1) : 0.0f;
        for (int i = V - 1; i > 0; --i) std::swap (pans[(size_t) i], pans[(size_t) rnd.nextInt (i + 1)]);
        for (int i = 0; i < V; ++i) voices[(size_t) i].pan = pans[(size_t) i];
    }

    const Range range = choirRange (st.model);
    const int outLen = n + (int) (sr * 0.15);
    const int numThreads = juce::jlimit (1, 8, (int) std::thread::hardware_concurrency());

    struct Acc { std::vector<float> l, r, m, tmp; };
    std::vector<Acc> acc ((size_t) numThreads);
    for (auto& a : acc) { a.l.assign ((size_t) outLen, 0.f); a.r = a.l; a.m = a.l; a.tmp = a.l; }

    std::atomic<int> next { 0 }, done { 0 };
    const int V = (int) voices.size();

    auto worker = [&] (int ti)
    {
        auto& a = acc[(size_t) ti];
        for (;;)
        {
            const int vi = next.fetch_add (1);
            if (vi >= V || progress.cancel) return;
            const Voice& v = voices[(size_t) vi];
            const Layer& L = layers[(size_t) v.layer];
            renderVoice (x, marks, markPos, track, v, L, range, st.testSingleVoice ? 0.0f : mp.driftCents, mp.drive, a.tmp);

            const float lg = L.gain / std::sqrt ((float) L.voices);
            const float th = (v.pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            const float gl = std::cos (th) * lg, gr = std::sin (th) * lg;
            for (int i = 0; i < outLen; ++i)
            {
                const float s = a.tmp[(size_t) i];
                a.l[(size_t) i] += s * gl;
                a.r[(size_t) i] += s * gr;
                a.m[(size_t) i] += s * lg;
            }
            const int d = ++done;
            progress.fraction = 0.2f + 0.75f * (float) d / (float) V;
        }
    };

    std::vector<std::thread> pool;
    for (int t = 1; t < numThreads; ++t) pool.emplace_back (worker, t);
    worker (0);
    for (auto& t : pool) t.join();
    if (progress.cancel) return false;

    out.sampleRate = sr;
    out.stereo.setSize (2, outLen);
    out.mono.setSize (1, outLen);
    out.stereo.clear(); out.mono.clear();
    for (auto& a : acc)
    {
        out.stereo.addFrom (0, 0, a.l.data(), outLen);
        out.stereo.addFrom (1, 0, a.r.data(), outLen);
        out.mono.addFrom   (0, 0, a.m.data(), outLen);
    }

    auto normalise = [] (juce::AudioBuffer<float>& b, float target)
    {
        float pk = 0.0f;
        for (int c = 0; c < b.getNumChannels(); ++c) pk = juce::jmax (pk, b.getMagnitude (c, 0, b.getNumSamples()));
        if (pk > 1e-6f) b.applyGain (target / pk);
    };
    normalise (out.stereo, 0.7f);
    normalise (out.mono, 0.7f);

    progress.fraction = 1.0f;
    return true;
}

std::unique_ptr<ChoirEngine> createDefaultEngine() { return std::make_unique<DspChoirEngine>(); }

} // namespace kolot
