#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "KolotTypes.h"
#include "RealtimeChoir.h"
#include "LiveFinisher.h"
#include <array>

namespace kolot
{
namespace ParamID
{
    inline constexpr const char* choir      = "choir";
    inline constexpr const char* mic        = "mic";
    inline constexpr const char* key        = "key";
    inline constexpr const char* scale      = "scale";
    inline constexpr const char* octDown    = "octDown";
    inline constexpr const char* octUp      = "octUp";
    inline constexpr const char* thirdUp    = "thirdUp";
    inline constexpr const char* thirdDown  = "thirdDown";
    inline constexpr const char* fifthUp    = "fifthUp";
    inline constexpr const char* fifthDown  = "fifthDown";
    inline constexpr const char* stereo     = "stereo";
    inline constexpr const char* lowCut     = "lowCut";
    inline constexpr const char* brightness = "brightness";
    inline constexpr const char* reverb     = "reverb";
    inline constexpr const char* mix        = "mix";
    inline constexpr const char* output     = "output";
}

class KolotProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    KolotProcessor();
    ~KolotProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Kolot"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    ArrangeSettings currentArrange() const;
    FinishSettings  currentFinish()  const;

    // For the live display (read on the message thread).
    float getLeadHz() const      { return leadHz.load(); }
    double getLatencyMs() const  { return latencyMs.load(); }
    static constexpr int historySize = 160;
    void getHistory (float* inLevels, float* outLevels) const;

private:
    void timerCallback() override;

    RealtimeChoir choir;
    LiveFinisher  finisher;
    juce::AudioBuffer<float> monoIn, choirBuf;

    std::atomic<float> leadHz { 0.0f };
    std::atomic<double> latencyMs { 0.0 };
    std::array<std::atomic<float>, historySize> inHist {}, outHist {};
    std::atomic<int> histPos { 0 };
    int histCounter = 0;
    float histIn = 0.0f, histOut = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KolotProcessor)
};
} // namespace kolot
