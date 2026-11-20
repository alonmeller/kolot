#pragma once
// Kolot — shared types for the choir engine, the finisher and the plugin.

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <functional>

namespace kolot
{

enum class ChoirModel { Gospel = 0, Kids, Shout };
enum class MicModel   { U67 = 0, C12C24, Schoeps, Telefunken };

inline juce::StringArray choirNames()  { return { "Gospel", "Kids", "Shout" }; }
inline juce::StringArray micNames()    { return { "U67", "C12/C24", "Schoeps", "Telefunken" }; }
inline juce::StringArray keyNames()    { return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; }
inline juce::StringArray scaleNames()
{
    return { "Major", "Minor", "Harmonic Minor", "Melodic Minor",
             "Dorian", "Mixolydian", "Phrygian", "Lydian", "Chromatic" };
}

// Which mic models each choir offers (same pairing as the reference workflow):
// U67 + C12/C24 on all; Schoeps on Gospel and Shout; Telefunken on Kids.
inline bool micAvailable (ChoirModel c, MicModel m)
{
    if (m == MicModel::U67 || m == MicModel::C12C24) return true;
    if (m == MicModel::Schoeps)    return c != ChoirModel::Kids;
    if (m == MicModel::Telefunken) return c == ChoirModel::Kids;
    return false;
}

// Singing range of each choir model, as MIDI notes.
struct Range { int lo, hi; };
inline Range choirRange (ChoirModel c)
{
    switch (c)
    {
        case ChoirModel::Kids: return { 44, 79 };  // G#2 .. G5
        default:               return { 40, 79 };  // E2  .. G5
    }
}

struct ArrangeSettings
{
    ChoirModel model = ChoirModel::Gospel;
    int  key   = 0;       // 0 = C
    int  scale = 0;       // index into scaleNames()
    bool octaveDown = false, octaveUp = false;
    bool thirdUp = false, thirdDown = false, fifthUp = false, fifthDown = false;
    juce::uint32 seed = 1;
    bool testSingleVoice = false;
    bool testDropLead = false;      // diagnostics: render only the added layers   // diagnostics: one clean singer per layer
};

struct FinishSettings
{
    MicModel mic      = MicModel::U67;
    bool  stereo      = true;
    float lowCutHz    = 80.0f;   // 20 = off
    float brightness  = 0.0f;    // -1 .. +1
    float reverb      = 0.25f;   // 0 .. 1
    float outputDb    = 0.0f;
};

// Output of the (expensive) choir stage: a stereo spread and a mono sum.
struct ChoirStems
{
    juce::AudioBuffer<float> stereo;  // 2 channels
    juce::AudioBuffer<float> mono;    // 1 channel
    double sampleRate = 48000.0;
    bool isValid() const { return stereo.getNumSamples() > 0; }
};

struct RenderProgress
{
    std::atomic<float> fraction { 0.0f };
    std::atomic<bool>  cancel   { false };
};

} // namespace kolot
