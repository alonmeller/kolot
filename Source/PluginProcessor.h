#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "KolotTypes.h"
#include "ChoirEngine.h"
#include "Finisher.h"
#include <thread>

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
    inline constexpr const char* output     = "output";
}

class KolotProcessor : public juce::AudioProcessor,
                       private juce::AudioProcessorValueTreeState::Listener,
                       private juce::Timer
{
public:
    KolotProcessor();
    ~KolotProcessor() override;

    // AudioProcessor
    void prepareToPlay (double sampleRate, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Kolot"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // Kolot API (message thread)
    juce::AudioProcessorValueTreeState apvts;

    bool loadVocal (const juce::File& f, juce::String& error);
    void clearVocal();
    void createVocals();                 // full render (choir + finish)
    void cancelRender();
    void setPreviewPlaying (bool shouldPlay);
    bool isPreviewPlaying() const        { return previewPlaying.load(); }
    double getPreviewPosition() const;   // 0..1

    juce::File writeChoirFileForDrag();  // writes (once) and returns the current choir WAV
    bool exportChoirTo (const juce::File& f);

    enum class State { Empty, Ready, Rendering, Done };
    State  getState() const              { return state.load(); }
    float  getProgress() const           { return progress.fraction.load(); }
    bool   needsRerender() const         { return arrangeDirty.load() && getState() == State::Done; }
    juce::String getStatusText() const;
    juce::File   getVocalFile() const    { return vocalFile; }
    double       getVocalSeconds() const { return vocalSampleRate > 0 ? vocal.getNumSamples() / vocalSampleRate : 0.0; }

    // Thumbnails for the UI (min/max per bin), regenerated on change.
    std::vector<std::pair<float, float>> getVocalPeaks() const;
    std::vector<std::pair<float, float>> getChoirPeaks() const;
    int getChangeCounter() const         { return changeCounter.load(); }

    ArrangeSettings currentArrange() const;
    FinishSettings  currentFinish()  const;

    static juce::File rendersFolder();

private:
    void parameterChanged (const juce::String& id, float) override;
    void timerCallback() override;
    void startJob (bool fullRender);
    void runJob (bool fullRender);
    static std::vector<std::pair<float, float>> makePeaks (const juce::AudioBuffer<float>&, int bins);

    juce::AudioFormatManager formats;
    std::unique_ptr<ChoirEngine> engine;

    juce::File vocalFile;
    juce::AudioBuffer<float> vocal;      // mono
    double vocalSampleRate = 0.0;

    ChoirStems stems;                    // owned by the worker while rendering
    std::shared_ptr<juce::AudioBuffer<float>> finished;   // final stereo choir
    double finishedSampleRate = 48000.0;
    juce::SpinLock finishedLock;

    juce::File lastWrittenFile;
    std::atomic<bool> lastWrittenIsCurrent { false }, busy { false };

    std::thread worker;
    std::atomic<State> state { State::Empty };
    RenderProgress progress;
    std::atomic<bool> finishPending { false }, arrangeDirty { false };
    std::atomic<juce::uint32> lastFinishRequest { 0 };
    juce::uint32 seed = 1;
    std::atomic<int> changeCounter { 0 };

    std::vector<std::pair<float, float>> vocalPeaks, choirPeaks;
    mutable juce::CriticalSection peaksLock;

    // Playback
    std::atomic<bool> previewPlaying { false };
    double playPos = 0.0;                // in finished-buffer samples
    std::atomic<double> playFraction { 0.0 };
    double hostSampleRate = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KolotProcessor)
};
} // namespace kolot
