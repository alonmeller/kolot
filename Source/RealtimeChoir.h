#pragma once
// Kolot — real-time choir engine.
//
// Listens to the track it is inserted on and sings it back as ~20 singers:
//   1. Streaming YIN pitch tracking (decimated to ~16 kHz).
//   2. Pitch marks placed one per glottal period, as the audio arrives.
//   3. Every virtual singer resynthesises the lead with TD-PSOLA, with its own
//      interval (unison / octave / diatonic 3rd / 5th), detune, drift, vibrato,
//      formant shift (voice size / age), timing offset, level and pan.
//
// The engine looks ahead a little (≈100 ms) so it can analyse before it sings;
// that delay is reported to the host, which compensates for it on playback.

#include "KolotTypes.h"
#include <vector>

namespace kolot
{

// Semitone shift for a harmony of a given note. kind: 2 = third, 3 = fifth; dir ±1.
int diatonicShift (int midiNote, int key, int scale, int kind, int dir);

class RealtimeChoir
{
public:
    static constexpr int maxVoices = 64;
    static constexpr int maxChunk  = 512;

    void prepare (double sampleRate);
    void reset();

    // Call at the start of every block (audio thread). Rebuilds singers on change.
    void setArrange (const ArrangeSettings&);

    // in: mono input. Writes the choir as a stereo spread (outL/outR) and a mono sum.
    void process (const float* in, float* outL, float* outR, float* outMono, int numSamples);

    int   getLatencySamples() const { return latency; }
    float getLeadHz() const         { return leadHz; }
    float getInputLevel() const     { return inLevel; }

private:
    struct Frame
    {
        float f0raw = 0, f0 = 0, f0s = 0, rms = 0;
        float shiftRaw[4] {}, shift[4] {};     // 3rd up, 3rd down, 5th up, 5th down
    };
    struct Mark { double pos; float period; bool voiced; };

    struct SmoothNoise
    {
        float f[3] {}, ph[3] {};
        void init (juce::Random&, float fLo, float fHi);
        float eval (double tSec) const;
    };

    struct Voice
    {
        int   layerKind = 0;   // 0 lead, 1 oct down, 2 oct up, 3 3rd up, 4 3rd down, 5 5th up, 6 5th down
        float layerGain = 1.0f;
        float phi = 1, vibCents = 0, vibRate = 5.5f, vibPhase = 0, detuneCents = 0;
        float baseOffsetMs = 0, gain = 1, gl = 0.7f, gr = 0.7f;
        SmoothNoise drift, timing, amp;
        double ts = 0.0;
        bool   wasVoiced = false;
        double onset = 0.0;
        std::vector<float> ola;
    };

    // input / analysis
    void pushInput (const float* in, int n);
    void analyseFrames();
    void analyseFrame (juce::int64 k);
    void generateMarks();
    void rebuildVoices();
    void renderVoices (int n);

    float sampleAt (double pos) const;
    const Frame& frame (juce::int64 k) const { return frames[(size_t) (k & frameMask)]; }
    Frame&       frame (juce::int64 k)       { return frames[(size_t) (k & frameMask)]; }
    float f0At (double t) const;
    float rmsAt (double t) const;
    float shiftAt (int layerKind, double t) const;
    juce::int64 nearestMark (double t) const;

    double sr = 48000.0;
    int D = 3, hopD = 160, hop = 480, W = 400, tauMin = 14, tauMax = 246;
    int latency = 4800;

    std::vector<float> in;  juce::int64 inMask = 0, written = 0;
    std::vector<float> dec; juce::int64 decMask = 0, decWritten = 0;
    juce::IIRFilter lp1, lp2;
    int decPhase = 0;

    std::vector<Frame> frames; juce::int64 frameMask = 1023, frameCount = 0;
    std::vector<float> yinBuf, yinD, yinCm;
    float peakRms = 0.0f;
    float octHist[10] {}; int octCount = 0, octPos = 0;
    int   lastNote[4] { -1000, -1000, -1000, -1000 };
    float held[4] { 4, -3, 7, -5 };

    std::vector<Mark> marks; juce::int64 markMask = 8191, markHead = 0, markTail = 0;
    double markT = 0.0, lastMarkPos = -1e9;

    ArrangeSettings arrange;
    bool needsRebuild = true;
    Range range { 40, 79 };
    float drive = 0.0f, driftCents = 6.0f;
    std::vector<Voice> voices;
    int numVoices = 0;

    float leadHz = 0.0f, inLevel = 0.0f, inPeakSlow = 0.1f;
    static constexpr int hannSize = 4096;
    std::vector<float> hann;
};

} // namespace kolot
