#include "LiveFinisher.h"
#include <cmath>

namespace kolot
{
using Coeffs = juce::IIRCoefficients;

static juce::int64 pow2Above (double v) { juce::int64 p = 1; while ((double) p < v) p <<= 1; return p; }

void LiveFinisher::prepare (double sampleRate, int dryDelaySamples)
{
    sr = sampleRate;
    dryDelay = dryDelaySamples;

    const auto erSize = pow2Above (sr * 0.06);
    erMask = erSize - 1;
    const auto drySize = pow2Above (dryDelay + 8192.0);
    dryMask = drySize - 1;
    for (int c = 0; c < 2; ++c)
    {
        er[c].assign ((size_t) erSize, 0.0f);
        dry[c].assign ((size_t) drySize, 0.0f);
        wet[c].assign (8192, 0.0f);
    }
    static const float tapsMs[2][4] = { { 7.3f, 13.1f, 19.7f, 29.3f }, { 9.1f, 15.7f, 23.3f, 31.9f } };
    for (int c = 0; c < 2; ++c)
        for (int t = 0; t < 4; ++t)
            tapDelay[c][t] = (int) (tapsMs[c][t] * 0.001 * sr);

    reverb.setSampleRate (sr);
    dirty = true;
    reset();
}

void LiveFinisher::reset()
{
    for (int c = 0; c < 2; ++c)
    {
        std::fill (er[c].begin(), er[c].end(), 0.0f);
        std::fill (dry[c].begin(), dry[c].end(), 0.0f);
        for (auto& f : filt[c]) f.reset();
    }
    erPos = dryPos = 0;
    reverb.reset();
}

void LiveFinisher::setSettings (const FinishSettings& s)
{
    if (s != fs || dirty) { fs = s; dirty = true; }
}

void LiveFinisher::rebuildFilters()
{
    dirty = false;
    const auto db = [] (float v) { return juce::Decibels::decibelsToGain (v); };
    Coeffs c[maxFilters];
    int n = 0;
    switch (fs.mic)
    {
        case MicModel::U67:        // warm, full low-mids, soft top
            c[n++] = Coeffs::makeLowShelf  (sr, 120.0, 0.7, db (1.5f));
            c[n++] = Coeffs::makePeakFilter (sr, 260.0, 0.8, db (1.0f));
            c[n++] = Coeffs::makePeakFilter (sr, 4500.0, 1.0, db (1.5f));
            c[n++] = Coeffs::makeHighShelf (sr, 12000.0, 0.7, db (-2.0f));
            sat = 0.25f; break;
        case MicModel::C12C24:     // airy, open top, forward presence
            c[n++] = Coeffs::makePeakFilter (sr, 200.0, 0.9, db (0.5f));
            c[n++] = Coeffs::makePeakFilter (sr, 6000.0, 0.9, db (2.0f));
            c[n++] = Coeffs::makeHighShelf (sr, 10000.0, 0.7, db (3.0f));
            sat = 0.18f; break;
        case MicModel::Schoeps:    // neutral, tight lows, slight top lift
            c[n++] = Coeffs::makeHighPass  (sr, 45.0);
            c[n++] = Coeffs::makeHighShelf (sr, 9000.0, 0.7, db (1.5f));
            sat = 0.0f; break;
        case MicModel::Telefunken: // bright upper mids, silky air
            c[n++] = Coeffs::makeLowShelf  (sr, 150.0, 0.7, db (-1.0f));
            c[n++] = Coeffs::makePeakFilter (sr, 3500.0, 1.2, db (2.5f));
            c[n++] = Coeffs::makeHighShelf (sr, 11000.0, 0.7, db (2.5f));
            sat = 0.2f; break;
    }
    if (fs.lowCutHz > 21.0f)
    {
        c[n++] = Coeffs::makeHighPass (sr, fs.lowCutHz, 0.707);
        c[n++] = Coeffs::makeHighPass (sr, fs.lowCutHz, 0.707);
    }
    if (std::abs (fs.brightness) > 0.001f)
    {
        c[n++] = Coeffs::makeHighShelf (sr, 5000.0, 0.7, db (9.0f * fs.brightness));
        if (n < maxFilters) c[n++] = Coeffs::makePeakFilter (sr, 2800.0, 0.8, db (2.5f * fs.brightness));
    }
    if (n != numFilters) for (int ch = 0; ch < 2; ++ch) for (auto& f : filt[ch]) f.reset();
    numFilters = n;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < n; ++i) filt[ch][i].setCoefficients (c[i]);

    juce::Reverb::Parameters p;
    p.roomSize   = 0.62f + 0.3f * fs.reverb;
    p.damping    = 0.45f;
    p.width      = fs.stereo ? 1.0f : 0.0f;
    // juce::Reverb scales dry by 2 and wet by 3 internally — compensate so the
    // choir keeps the same loudness whatever the reverb amount.
    p.wetLevel   = 0.5f * 0.55f * fs.reverb;
    p.dryLevel   = 0.5f * (1.0f - 0.35f * fs.reverb);
    p.freezeMode = 0.0f;
    reverb.setParameters (p);
}

static inline float tube (float x, float amt)
{
    if (amt <= 0.0f) return x;
    const float k = 1.6f;
    const float s = std::tanh (k * x + 0.08f) - std::tanh (0.08f);
    return (1.0f - amt) * x + amt * s / std::tanh (k);
}

void LiveFinisher::process (const float* cL, const float* cR, const float* cM,
                            const float* dL, const float* dR, float* oL, float* oR, int total)
{
    if (dirty) rebuildFilters();
    static const float tapG[4] = { 0.22f, 0.16f, 0.11f, 0.08f };
    const float mixTarget = fs.mix;
    const float outTarget = juce::Decibels::decibelsToGain (fs.outputDb);

    for (int done = 0; done < total;)
    {
        const int n = juce::jmin (8192, total - done);
        float* w0 = wet[0].data();
        float* w1 = wet[1].data();

        // Source + room (early reflections, cross-fed between sides).
        for (int i = 0; i < n; ++i)
        {
            const float l = fs.stereo ? cL[done + i] : cM[done + i] * 0.85f;
            const float r = fs.stereo ? cR[done + i] : l;
            er[0][(size_t) (erPos & erMask)] = l;
            er[1][(size_t) (erPos & erMask)] = r;
            float el = l, rr = r;
            for (int t = 0; t < 4; ++t)
            {
                el += tapG[t] * er[1][(size_t) ((erPos - tapDelay[0][t]) & erMask)];
                rr += tapG[t] * er[0][(size_t) ((erPos - tapDelay[1][t]) & erMask)];
            }
            ++erPos;
            w0[i] = el * 0.8f;
            w1[i] = rr * 0.8f;
        }

        // Mic character, low-cut, brightness.
        for (int ch = 0; ch < 2; ++ch)
        {
            float* d = ch == 0 ? w0 : w1;
            if (sat > 0.0f) for (int i = 0; i < n; ++i) d[i] = tube (d[i], sat);
            for (int f = 0; f < numFilters; ++f) filt[ch][f].processSamples (d, n);
        }

        if (fs.reverb > 0.001f) reverb.processStereo (w0, w1, n);

        // Mix with the (latency-aligned) original, then output gain + soft ceiling.
        for (int i = 0; i < n; ++i)
        {
            dry[0][(size_t) (dryPos & dryMask)] = dL[done + i];
            dry[1][(size_t) (dryPos & dryMask)] = dR[done + i];
            const float xl = dry[0][(size_t) ((dryPos - dryDelay) & dryMask)];
            const float xr = dry[1][(size_t) ((dryPos - dryDelay) & dryMask)];
            ++dryPos;

            mixSmoothed += 0.002f * (mixTarget - mixSmoothed);
            outSmoothed += 0.002f * (outTarget - outSmoothed);
            const float wm = std::sin (mixSmoothed * juce::MathConstants<float>::halfPi);
            const float dm = std::cos (mixSmoothed * juce::MathConstants<float>::halfPi);
            float l = (xl * dm + w0[i] * wm) * outSmoothed;
            float r = (xr * dm + w1[i] * wm) * outSmoothed;
            auto ceil = [] (float s)
            {
                const float a = std::abs (s);
                return a > 0.95f ? std::copysign (0.95f + 0.05f * std::tanh ((a - 0.95f) / 0.05f), s) : s;
            };
            oL[done + i] = ceil (l);
            oR[done + i] = ceil (r);
        }
        done += n;
    }
}
} // namespace kolot
