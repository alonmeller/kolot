#pragma once
// Kolot — choir engines.
//
// ChoirEngine is the swappable "voice generator". Today Kolot ships
// DspChoirEngine (pitch-synchronous resynthesis of ~20 singers).
// A neural engine (e.g. an RVC / Neutone timbre-transfer model trained on
// recordings you own, with the singers' consent) can be added later by
// implementing the same interface — see README, section "AI engine".

#include "KolotTypes.h"
#include <memory>
#include <vector>

namespace kolot
{

class ChoirEngine
{
public:
    virtual ~ChoirEngine() = default;
    virtual juce::String name() const = 0;

    // Turns one dry monophonic vocal into choir stems. Runs off the audio thread.
    // Returns false if cancelled or the input is unusable.
    virtual bool render (const juce::AudioBuffer<float>& monoVocal, double sampleRate,
                         const ArrangeSettings& settings, ChoirStems& out,
                         RenderProgress& progress) = 0;
};

// ---------------------------------------------------------------------------
// Pitch analysis (YIN), shared by every engine that needs it.
struct PitchTrack
{
    double sampleRate = 48000.0;
    int    hop = 480;                 // in input samples
    std::vector<float> f0;            // Hz per frame, 0 = unvoiced
    std::vector<float> rms;           // per frame
    float  peakRms = 0.0f;

    int   numFrames() const { return (int) f0.size(); }
    float f0At (double sample) const;
    float rmsAt (double sample) const;
};

PitchTrack analysePitch (const float* x, int n, double sampleRate);

// Per-frame semitone shift for a harmony/octave layer.
// kind: 0 = unison, 1 = octave (steps = ±1), 2 = diatonic third, 3 = diatonic fifth
std::vector<float> computeLayerShift (const PitchTrack& track, int key, int scale,
                                      int kind, int direction);

// ---------------------------------------------------------------------------
class DspChoirEngine : public ChoirEngine
{
public:
    juce::String name() const override { return "Kolot DSP Ensemble"; }
    bool render (const juce::AudioBuffer<float>& monoVocal, double sampleRate,
                 const ArrangeSettings& settings, ChoirStems& out,
                 RenderProgress& progress) override;
};

std::unique_ptr<ChoirEngine> createDefaultEngine();

} // namespace kolot
