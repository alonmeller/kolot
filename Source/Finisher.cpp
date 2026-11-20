#include "Finisher.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>

namespace kolot
{
namespace
{
using Coeffs = juce::IIRCoefficients;

struct Chain
{
    std::vector<juce::IIRFilter> f;
    void add (const Coeffs& c) { f.emplace_back(); f.back().setCoefficients (c); }
    void process (float* d, int n) { for (auto& x : f) x.processSamples (d, n); }
};

// Mic character: EQ curve + optional tube-style saturation amount.
float buildMic (Chain& ch, MicModel m, double sr)
{
    const auto db = [] (float v) { return juce::Decibels::decibelsToGain (v); };
    switch (m)
    {
        case MicModel::U67:        // warm, full low-mids, soft top
            ch.add (Coeffs::makeLowShelf  (sr, 120.0, 0.7, db (1.5f)));
            ch.add (Coeffs::makePeakFilter (sr, 260.0, 0.8, db (1.0f)));
            ch.add (Coeffs::makePeakFilter (sr, 4500.0, 1.0, db (1.5f)));
            ch.add (Coeffs::makeHighShelf (sr, 12000.0, 0.7, db (-2.0f)));
            return 0.25f;
        case MicModel::C12C24:     // airy, open top, forward presence
            ch.add (Coeffs::makePeakFilter (sr, 200.0, 0.9, db (0.5f)));
            ch.add (Coeffs::makePeakFilter (sr, 6000.0, 0.9, db (2.0f)));
            ch.add (Coeffs::makeHighShelf (sr, 10000.0, 0.7, db (3.0f)));
            return 0.18f;
        case MicModel::Schoeps:    // neutral, tight lows, slight top lift
            ch.add (Coeffs::makeHighPass  (sr, 45.0));
            ch.add (Coeffs::makeHighShelf (sr, 9000.0, 0.7, db (1.5f)));
            return 0.0f;
        case MicModel::Telefunken: // bright upper mids, silky air
            ch.add (Coeffs::makeLowShelf  (sr, 150.0, 0.7, db (-1.0f)));
            ch.add (Coeffs::makePeakFilter (sr, 3500.0, 1.2, db (2.5f)));
            ch.add (Coeffs::makeHighShelf (sr, 11000.0, 0.7, db (2.5f)));
            return 0.2f;
    }
    return 0.0f;
}

inline float tube (float x, float amt)
{
    if (amt <= 0.0f) return x;
    const float k = 1.6f;
    const float sat = std::tanh (k * x + 0.08f) - std::tanh (0.08f);
    return (1.0f - amt) * x + amt * sat / std::tanh (k);
}
} // namespace

bool finishChoir (const ChoirStems& stems, const FinishSettings& fs,
                  juce::AudioBuffer<float>& out, const std::atomic<bool>* cancel)
{
    if (! stems.isValid()) return false;
    const double sr = stems.sampleRate;
    const int n = stems.stereo.getNumSamples();
    const int tail = (int) (sr * (0.4 + 3.0 * fs.reverb));
    const int len = n + tail;

    out.setSize (2, len, false, false, true);
    out.clear();
    if (fs.stereo)
    {
        out.copyFrom (0, 0, stems.stereo, 0, 0, n);
        out.copyFrom (1, 0, stems.stereo, 1, 0, n);
    }
    else
    {
        out.copyFrom (0, 0, stems.mono, 0, 0, n);
        out.applyGain (0, 0, n, 0.85f);
        out.copyFrom (1, 0, out, 0, 0, n);
    }

    // Room: a few early reflections, as if ~20 singers stand in a real space.
    {
        static const float tapsMs[2][4] = { { 7.3f, 13.1f, 19.7f, 29.3f }, { 9.1f, 15.7f, 23.3f, 31.9f } };
        static const float tapsG[4]     = { 0.22f, 0.16f, 0.11f, 0.08f };
        juce::AudioBuffer<float> dry (out);
        for (int c = 0; c < 2; ++c)
        {
            const float* src = dry.getReadPointer (fs.stereo ? 1 - c : 0);
            float* dst = out.getWritePointer (c);
            for (int t = 0; t < 4; ++t)
            {
                const int d = (int) (tapsMs[c][t] * 0.001 * sr);
                for (int i = d; i < len; ++i) dst[i] += src[i - d] * tapsG[t];
            }
        }
    }
    if (cancel && cancel->load()) return false;

    // Mic, low-cut, brightness — per channel.
    for (int c = 0; c < 2; ++c)
    {
        Chain ch;
        const float sat = buildMic (ch, fs.mic, sr);
        if (fs.lowCutHz > 21.0f)
        {
            ch.add (Coeffs::makeHighPass (sr, fs.lowCutHz, 0.707));
            ch.add (Coeffs::makeHighPass (sr, fs.lowCutHz, 0.707));
        }
        if (std::abs (fs.brightness) > 0.001f)
        {
            const float g = juce::Decibels::decibelsToGain (9.0f * fs.brightness);
            ch.add (Coeffs::makeHighShelf (sr, 5000.0, 0.7, g));
            ch.add (Coeffs::makePeakFilter (sr, 2800.0, 0.8, juce::Decibels::decibelsToGain (2.5f * fs.brightness)));
        }
        float* d = out.getWritePointer (c);
        if (sat > 0.0f) for (int i = 0; i < len; ++i) d[i] = tube (d[i], sat);
        ch.process (d, len);
    }
    if (cancel && cancel->load()) return false;

    // Reverb.
    if (fs.reverb > 0.001f)
    {
        juce::Reverb rv;
        juce::Reverb::Parameters p;
        p.roomSize   = 0.62f + 0.3f * fs.reverb;
        p.damping    = 0.45f;
        p.width      = fs.stereo ? 1.0f : 0.0f;
        p.wetLevel   = 0.55f * fs.reverb;
        p.dryLevel   = 1.0f - 0.35f * fs.reverb;
        p.freezeMode = 0.0f;
        rv.setParameters (p);
        rv.setSampleRate (sr);
        const int block = 4096;
        for (int i = 0; i < len; i += block)
            rv.processStereo (out.getWritePointer (0, i), out.getWritePointer (1, i), juce::jmin (block, len - i));
    }

    // Normalise to -1 dBFS, then output gain with a soft safety ceiling.
    float pk = juce::jmax (out.getMagnitude (0, 0, len), out.getMagnitude (1, 0, len));
    if (pk > 1e-6f) out.applyGain (juce::Decibels::decibelsToGain (-1.0f) / pk);
    const float g = juce::Decibels::decibelsToGain (fs.outputDb);
    for (int c = 0; c < 2; ++c)
    {
        float* d = out.getWritePointer (c);
        for (int i = 0; i < len; ++i)
        {
            float s = d[i] * g;
            if (std::abs (s) > 0.95f) s = std::copysign (0.95f + 0.05f * std::tanh ((std::abs (s) - 0.95f) / 0.05f), s);
            d[i] = s;
        }
    }

    // Trim the silent tail.
    int last = len - 1;
    const float thr = juce::Decibels::decibelsToGain (-70.0f);
    while (last > n && std::abs (out.getSample (0, last)) < thr && std::abs (out.getSample (1, last)) < thr) --last;
    out.setSize (2, last + 1, true, false, true);
    return true;
}
} // namespace kolot
