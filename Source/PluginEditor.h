#pragma once
#include "PluginProcessor.h"
#include "KolotLookAndFeel.h"

namespace kolot
{
// Waveform area: drop / browse the vocal, see it, drag the choir out.
class WaveformPanel : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    explicit WaveformPanel (KolotProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
    void refresh() { vocalPeaks = proc.getVocalPeaks(); choirPeaks = proc.getChoirPeaks(); repaint(); }
    std::function<void (const juce::String&)> onError;

private:
    juce::Rectangle<int> dragChip() const;
    juce::Rectangle<int> clearChip() const;
    void browse();
    void load (const juce::File&);
    static void drawPeaks (juce::Graphics&, juce::Rectangle<float>, const std::vector<std::pair<float, float>>&,
                           juce::Colour, float gain);

    KolotProcessor& proc;
    std::vector<std::pair<float, float>> vocalPeaks, choirPeaks;
    std::unique_ptr<juce::FileChooser> chooser;
    bool dragOver = false, dragStarted = false;
};

// A choir model card: name, character line and range.
class ChoirCard : public juce::Button
{
public:
    ChoirCard (const juce::String& name, const juce::String& line, const juce::String& range)
        : juce::Button (name), title (name), sub (line), rng (range) { setClickingTogglesState (false); }
    void paintButton (juce::Graphics&, bool over, bool down) override;
private:
    juce::String title, sub, rng;
};

class KolotEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit KolotEditor (KolotProcessor&);
    ~KolotEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void setChoice (const char* id, int index);
    int  getChoice (const char* id) const;
    void drawPanel (juce::Graphics&, juce::Rectangle<int>, const juce::String& title);

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    KolotProcessor& proc;
    KolotLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    WaveformPanel wave { proc };

    ChoirCard gospel { "Gospel", "Mixed men & women, full and soulful", "E2 - G5" };
    ChoirCard kids   { "Kids",   "Bright children's choir",              "G#2 - G5" };
    ChoirCard shout  { "Shout",  "Male gang vocals for shouted parts",   "E2 - G5" };
    juce::TextButton micBtn[4];

    juce::ComboBox keyBox, scaleBox;
    juce::TextButton octDown { "-1 Oct" }, octUp { "+1 Oct" };
    juce::TextButton thirdUp { "3rd Above" }, thirdDown { "3rd Below" }, fifthUp { "5th Above" }, fifthDown { "5th Below" };
    juce::TextButton stereoBtn { "Stereo" }, monoBtn { "Mono" };

    juce::Slider lowCut, bright, reverb, output;
    juce::Label  lowCutL, brightL, reverbL, outputL;

    juce::TextButton preview { "Preview" }, exportBtn { "Export..." }, create { "Create Vocals" };
    juce::Label status;
    juce::String errorText;
    juce::uint32 errorUntil = 0;

    std::unique_ptr<ComboAtt>  keyAtt, scaleAtt;
    std::vector<std::unique_ptr<ButtonAtt>> btnAtts;
    std::vector<std::unique_ptr<SliderAtt>> sliderAtts;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Rectangle<int> choirPanel, arrangePanel, finishPanel;
    int lastChange = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KolotEditor)
};
} // namespace kolot
