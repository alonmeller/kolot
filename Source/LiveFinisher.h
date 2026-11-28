#pragma once
// Kolot — real-time finishing stage: stereo/mono, room, mic model, low-cut,
// brightness, reverb, then the dry/choir mix and output gain.

#include "KolotTypes.h"
#include <vector>

namespace kolot
{
class LiveFinisher
{
public:
    void prepare (double sampleRate, int dryDelaySamples);
    void reset();
    void setSettings (const FinishSettings&);

    // choirL/R/M: engine output. dryL/dryR: the original track (delayed here to line up).
    // Writes the final stereo result to outL/outR (may alias dryL/dryR).
    void process (const float* choirL, const float* choirR, const float* choirM,
                  const float* dryL, const float* dryR, float* outL, float* outR, int n);

private:
    void rebuildFilters();

    double sr = 48000.0;
    FinishSettings fs;
    bool dirty = true;

    static constexpr int maxFilters = 8;
    juce::IIRFilter filt[2][maxFilters];
    int numFilters = 0;
    float sat = 0.0f;

    std::vector<float> er[2]; juce::int64 erMask = 0, erPos = 0;
    int tapDelay[2][4] {};
    std::vector<float> dry[2]; juce::int64 dryMask = 0, dryPos = 0; int dryDelay = 0;

    juce::Reverb reverb;
    std::vector<float> wet[2];
    float mixSmoothed = 1.0f, outSmoothed = 1.0f;
};
} // namespace kolot
