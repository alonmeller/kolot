#pragma once
#include "PluginProcessor.h"
#include "KolotLookAndFeel.h"

namespace kolot
{
// Live strip: what the lead sings now, which notes the choir adds, and levels.
class LiveStrip : public juce::Component
{
public:
    explicit LiveStrip (KolotProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void update();
private:
    KolotProcessor& proc;
    float inH[KolotProcessor::historySize] {}, outH[KolotProcessor::historySize] {};
    float shownHz = 0.0f;
    int   heldNote = -1;
    juce::uint32 lastVoiced = 0;
};

// A choir model card: name, character line and range.
class ChoirCard : public juce::Button
{
public:
    ChoirCard (const juce::String& name, const juce::String& line, const juce::String& range)
        : juce::Button (name), title (name), sub (line), rng (range) {}
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
    void drawGlass (juce::Graphics&, juce::Rectangle<int>, const juce::String& title);

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    KolotProcessor& proc;
    KolotLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };
    juce::Image background;

    LiveStrip live { proc };

    ChoirCard gospel { "Gospel", "Mixed men & women, full and soulful", "E2 - G5" };
    ChoirCard kids   { "Kids",   "Bright children's choir",              "G#2 - G5" };
    ChoirCard shout  { "Shout",  "Male gang vocals for shouted parts",   "E2 - G5" };
    juce::TextButton micBtn[4];

    juce::ComboBox keyBox, scaleBox;
    juce::TextButton octDown { "-1 Oct" }, octUp { "+1 Oct" };
    juce::TextButton thirdUp { "3rd Above" }, thirdDown { "3rd Below" }, fifthUp { "5th Above" }, fifthDown { "5th Below" };
    juce::TextButton stereoBtn { "Stereo" }, monoBtn { "Mono" };

    juce::Slider lowCut, bright, reverb, mix, output;
    juce::Label  lowCutL, brightL, reverbL, mixL, outputL;

    std::unique_ptr<ComboAtt>  keyAtt, scaleAtt;
    std::vector<std::unique_ptr<ButtonAtt>> btnAtts;
    std::vector<std::unique_ptr<SliderAtt>> sliderAtts;

    juce::Rectangle<int> choirPanel, arrangePanel, finishPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KolotEditor)
};
} // namespace kolot
