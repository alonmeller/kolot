#include "PluginEditor.h"
#include "BinaryData.h"

namespace kolot
{
using namespace Colours;
static juce::Font F (float h, bool b = false) { return KolotLookAndFeel::font (h, b); }

static juce::String noteName (int n)
{
    return keyNames()[((n % 12) + 12) % 12] + juce::String (n / 12 - 1);
}

static void glass (juce::Graphics& g, juce::Rectangle<float> r, float alpha = 0.74f)
{
    g.setColour (juce::Colour (0xff0b0d16).withAlpha (alpha));
    g.fillRoundedRectangle (r, 12.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.07f), 0, r.getY(),
                                             juce::Colours::white.withAlpha (0.0f), 0, r.getY() + 40, false));
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (juce::Colours::white.withAlpha (0.13f));
    g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);
}

// ============================================================================
void LiveStrip::update()
{
    proc.getHistory (inH, outH);
    const float hz = proc.getLeadHz();
    const auto now = juce::Time::getMillisecondCounter();
    if (hz > 0.0f)
    {
        shownHz = hz;
        const float m = 69.0f + 12.0f * std::log2 (hz / 440.0f);
        if (heldNote < 0 || std::abs (m - (float) heldNote) > 0.6f) heldNote = juce::roundToInt (m);
        lastVoiced = now;
    }
    else if (now - lastVoiced > 600)
    {
        heldNote = -1;
        shownHz = 0.0f;
    }
    repaint();
}

void LiveStrip::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    glass (g, r);
    auto area = getLocalBounds().reduced (18, 12);

    // Lead note
    auto left = area.removeFromLeft (150);
    g.setColour (amber);
    g.setFont (F (12.0f, true));
    g.drawText ("LEAD", left.removeFromTop (16), juce::Justification::centredLeft);
    g.setColour (ivory);
    g.setFont (F (40.0f, true));
    g.drawText (heldNote >= 0 ? noteName (heldNote) : juce::String::fromUTF8 ("\xe2\x80\x94"),
                left.removeFromTop (50), juce::Justification::centredLeft);
    g.setColour (ivoryDim);
    g.setFont (F (12.0f));
    g.drawText (heldNote >= 0 ? juce::String (shownHz, 1) + " Hz" : "Play the track...", left.removeFromTop (18),
                juce::Justification::centredLeft);

    // Level history (right)
    auto graph = area.removeFromRight (300).toFloat();
    g.setColour (amber);
    g.setFont (F (12.0f, true));
    g.drawText ("TRACK  /  CHOIR", graph.removeFromTop (16), juce::Justification::centredLeft);
    graph.removeFromBottom (16);
    {
        auto toY = [&] (float v)
        {
            const float db = juce::jlimit (-60.0f, 0.0f, juce::Decibels::gainToDecibels (v, -60.0f));
            return graph.getBottom() - (db + 60.0f) / 60.0f * graph.getHeight();
        };
        const int N = KolotProcessor::historySize;
        const float dx = graph.getWidth() / (float) (N - 1);
        juce::Path pin, pout;
        pin.startNewSubPath (graph.getX(), graph.getBottom());
        pout.startNewSubPath (graph.getX(), graph.getBottom());
        for (int i = 0; i < N; ++i)
        {
            pin.lineTo  (graph.getX() + i * dx, toY (inH[i]));
            pout.lineTo (graph.getX() + i * dx, toY (outH[i]));
        }
        pin.lineTo (graph.getRight(), graph.getBottom()); pin.closeSubPath();
        pout.lineTo (graph.getRight(), graph.getBottom()); pout.closeSubPath();
        g.setColour (ivory.withAlpha (0.22f));
        g.fillPath (pin);
        g.setColour (amber.withAlpha (0.55f));
        g.fillPath (pout);
        g.setColour (amberHot.withAlpha (0.9f));
        g.strokePath (pout, juce::PathStrokeType (1.0f));
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawHorizontalLine ((int) graph.getBottom(), graph.getX(), graph.getRight());
    }
    g.setColour (ivoryDim);
    g.setFont (F (11.0f));
    g.drawText ("Lookahead " + juce::String (juce::roundToInt (proc.getLatencyMs())) + " ms - Logic compensates automatically",
                juce::Rectangle<float> (graph.getX(), graph.getBottom() + 2, graph.getWidth() + 2, 14), juce::Justification::centredRight);

    // What the choir sings
    area.removeFromLeft (10);
    area.removeFromRight (20);
    g.setColour (amber);
    g.setFont (F (12.0f, true));
    g.drawText ("THE CHOIR SINGS", area.removeFromTop (16), juce::Justification::centredLeft);
    area.removeFromTop (6);

    const auto a = proc.currentArrange();
    struct Chip { juce::String label, note; };
    juce::Array<Chip> chips;
    const auto rg = choirRange (a.model);
    auto fold = [&] (int n) { while (n > rg.hi) n -= 12; while (n < rg.lo) n += 12; return n; };
    auto add = [&] (bool on, const juce::String& lab, int shift)
    {
        if (! on) return;
        chips.add ({ lab, heldNote >= 0 ? noteName (fold (heldNote + shift)) : juce::String ("-") });
    };
    const int n = heldNote >= 0 ? heldNote : 60;
    add (true, "Lead", 0);
    add (a.octaveDown, "-1 Oct", -12);
    add (a.octaveUp, "+1 Oct", 12);
    add (a.thirdUp, "3rd Above", diatonicShift (n, a.key, a.scale, 2, 1));
    add (a.thirdDown, "3rd Below", diatonicShift (n, a.key, a.scale, 2, -1));
    add (a.fifthUp, "5th Above", diatonicShift (n, a.key, a.scale, 3, 1));
    add (a.fifthDown, "5th Below", diatonicShift (n, a.key, a.scale, 3, -1));

    int x = area.getX(), y = area.getY();
    const int cw = 86, ch = 40, gap = 6;
    for (auto& c : chips)
    {
        if (x + cw > area.getRight()) { x = area.getX(); y += ch + gap; }
        juce::Rectangle<float> cr ((float) x, (float) y, (float) cw, (float) ch);
        g.setColour (amber.withAlpha (heldNote >= 0 ? 0.18f : 0.08f));
        g.fillRoundedRectangle (cr, 7.0f);
        g.setColour (amber.withAlpha (0.6f));
        g.drawRoundedRectangle (cr, 7.0f, 1.0f);
        g.setColour (ivory);
        g.setFont (F (17.0f, true));
        g.drawText (c.note, cr.withTrimmedBottom (14), juce::Justification::centred);
        g.setColour (ivoryDim);
        g.setFont (F (10.5f));
        g.drawText (c.label, cr.withTrimmedTop (24), juce::Justification::centred);
        x += cw + gap;
    }
}

// ============================================================================
void ChoirCard::paintButton (juce::Graphics& g, bool over, bool)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool on = getToggleState();
    g.setColour (on ? amber.withAlpha (0.2f) : (over ? juce::Colours::white.withAlpha (0.08f) : juce::Colours::white.withAlpha (0.04f)));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (on ? amber : juce::Colours::white.withAlpha (0.14f));
    g.drawRoundedRectangle (r, 8.0f, on ? 1.6f : 1.0f);

    g.setColour (on ? amberHot : edge.brighter (0.4f));
    g.fillEllipse (r.getX() + 12, r.getCentreY() - 5, 10, 10);

    auto t = getLocalBounds().reduced (32, 6);
    g.setColour (on ? amberHot : ivory);
    g.setFont (F (16.0f, true));
    g.drawText (title, t.removeFromTop (t.getHeight() / 2 + 2), juce::Justification::bottomLeft);
    g.setColour (ivoryDim);
    g.setFont (F (11.5f));
    g.drawText (sub, t, juce::Justification::topLeft);
    g.drawText (rng, getLocalBounds().reduced (12, 6).removeFromTop (getHeight() / 2 + 2), juce::Justification::bottomRight);
}

// ============================================================================
KolotEditor::KolotEditor (KolotProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    background = juce::ImageCache::getFromMemory (BinaryData::background_jpg, BinaryData::background_jpgSize);
    setSize (980, 660);

    addAndMakeVisible (live);

    ChoirCard* cards[] = { &gospel, &kids, &shout };
    for (int i = 0; i < 3; ++i)
    {
        addAndMakeVisible (*cards[i]);
        cards[i]->onClick = [this, i] { setChoice (ParamID::choir, i); };
    }
    for (int i = 0; i < 4; ++i)
    {
        micBtn[i].setButtonText (micNames()[i]);
        micBtn[i].onClick = [this, i] { setChoice (ParamID::mic, i); };
        addAndMakeVisible (micBtn[i]);
    }

    keyBox.addItemList (keyNames(), 1);
    scaleBox.addItemList (scaleNames(), 1);
    addAndMakeVisible (keyBox);
    addAndMakeVisible (scaleBox);
    keyAtt   = std::make_unique<ComboAtt> (proc.apvts, ParamID::key, keyBox);
    scaleAtt = std::make_unique<ComboAtt> (proc.apvts, ParamID::scale, scaleBox);

    std::pair<juce::TextButton*, const char*> toggles[] = {
        { &octDown, ParamID::octDown }, { &octUp, ParamID::octUp },
        { &thirdUp, ParamID::thirdUp }, { &thirdDown, ParamID::thirdDown },
        { &fifthUp, ParamID::fifthUp }, { &fifthDown, ParamID::fifthDown } };
    for (auto& [b, id] : toggles)
    {
        b->setClickingTogglesState (true);
        addAndMakeVisible (*b);
        btnAtts.push_back (std::make_unique<ButtonAtt> (proc.apvts, id, *b));
    }
    octDown.setTooltip ("Adds a section one octave below the lead");
    octUp.setTooltip ("Adds a section one octave above the lead");
    thirdUp.setTooltip ("Adds a section a diatonic third above, in the chosen key and scale");

    addAndMakeVisible (stereoBtn);
    addAndMakeVisible (monoBtn);
    stereoBtn.onClick = [this] { if (auto* q = proc.apvts.getParameter (ParamID::stereo)) q->setValueNotifyingHost (1.0f); };
    monoBtn.onClick   = [this] { if (auto* q = proc.apvts.getParameter (ParamID::stereo)) q->setValueNotifyingHost (0.0f); };

    std::tuple<juce::Slider*, juce::Label*, const char*, const char*> knobs[] = {
        { &lowCut, &lowCutL, ParamID::lowCut, "Low Cut" },  { &bright, &brightL, ParamID::brightness, "Brightness" },
        { &reverb, &reverbL, ParamID::reverb, "Reverb" },   { &mix, &mixL, ParamID::mix, "Mix" },
        { &output, &outputL, ParamID::output, "Output" } };
    for (auto& [s, l, id, name] : knobs)
    {
        s->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
        s->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        s->setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        s->setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        addAndMakeVisible (*s);
        sliderAtts.push_back (std::make_unique<SliderAtt> (proc.apvts, id, *s));
        l->setText (name, juce::dontSendNotification);
        l->setJustificationType (juce::Justification::centred);
        l->setFont (F (12.5f, true));
        l->setColour (juce::Label::textColourId, ivoryDim);
        addAndMakeVisible (*l);
    }
    mix.setTooltip ("0% = only your track, 100% = only the choir");
    output.setDoubleClickReturnValue (true, 0.0);
    bright.setDoubleClickReturnValue (true, 0.0);
    mix.setDoubleClickReturnValue (true, 1.0);

    startTimerHz (30);
    timerCallback();
}

KolotEditor::~KolotEditor() { setLookAndFeel (nullptr); }

void KolotEditor::setChoice (const char* id, int index)
{
    if (auto* p = proc.apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) index));
}

int KolotEditor::getChoice (const char* id) const { return (int) proc.apvts.getRawParameterValue (id)->load(); }

void KolotEditor::drawGlass (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title)
{
    glass (g, r.toFloat());
    g.setFont (F (12.0f, true));
    g.setColour (amber);
    g.drawText (title.toUpperCase(), r.reduced (16, 10).removeFromTop (16), juce::Justification::centredLeft);
}

void KolotEditor::paint (juce::Graphics& g)
{
    if (background.isValid())
        g.drawImage (background, getLocalBounds().toFloat(), juce::RectanglePlacement::fillDestination);
    else
        g.fillAll (bg);

    // Logo, with a soft shadow so it reads over the sky.
    auto shadowText = [&] (const juce::String& s, juce::Rectangle<int> r, juce::Colour c, float size, bool bold)
    {
        g.setFont (F (size, bold));
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.drawText (s, r.translated (0, 2), juce::Justification::centredLeft);
        g.setColour (c);
        g.drawText (s, r, juce::Justification::centredLeft);
    };
    shadowText ("K O L O T", { 24, 14, 200, 36 }, ivory, 28.0f, true);
    shadowText (juce::String::fromUTF8 ("\xd7\xa7\xd7\x95\xd7\x9c\xd7\x95\xd7\xaa"), { 200, 18, 70, 30 }, amberHot, 17.0f, true);
    shadowText ("CHOIR EFFECT  -  your track, sung by a whole choir", { 274, 18, 440, 30 }, ivory.withAlpha (0.8f), 12.0f, false);
    g.setFont (F (12.0f));
    g.setColour (ivory.withAlpha (0.75f));
    g.drawText ("v2.0  -  by Alon Meller", getWidth() - 224, 18, 200, 30, juce::Justification::centredRight);

    drawGlass (g, choirPanel, "Choir");
    drawGlass (g, arrangePanel, "Arrange");
    drawGlass (g, finishPanel, "Finish");

    g.setFont (F (11.5f, true));
    g.setColour (ivoryDim);
    auto lbl = [&] (juce::Component& c, const juce::String& s)
    { g.drawText (s, c.getX(), c.getY() - 18, 200, 16, juce::Justification::centredLeft); };
    lbl (micBtn[0], "MICROPHONE");
    lbl (keyBox, "KEY");
    lbl (scaleBox, "SCALE");
    lbl (octDown, "OCTAVES");
    lbl (thirdUp, "HARMONY");
    lbl (stereoBtn, "IMAGE");
}

void KolotEditor::resized()
{
    const int W = getWidth();
    live.setBounds (20, 64, W - 40, 128);

    const int top = 206, h = 330, gap = 14;
    const int pw = (W - 40 - 2 * gap) / 3;
    choirPanel   = { 20, top, pw, h };
    arrangePanel = { 20 + pw + gap, top, pw, h };
    finishPanel  = { 20 + 2 * (pw + gap), top, pw, h };

    {
        auto r = choirPanel.reduced (14).withTrimmedTop (24);
        gospel.setBounds (r.removeFromTop (52)); r.removeFromTop (8);
        kids.setBounds   (r.removeFromTop (52)); r.removeFromTop (8);
        shout.setBounds  (r.removeFromTop (52)); r.removeFromTop (28);
        auto row1 = r.removeFromTop (30); r.removeFromTop (6);
        auto row2 = r.removeFromTop (30);
        const int bw = (row1.getWidth() - 6) / 2;
        micBtn[0].setBounds (row1.removeFromLeft (bw)); row1.removeFromLeft (6); micBtn[1].setBounds (row1);
        micBtn[2].setBounds (row2.removeFromLeft (bw)); row2.removeFromLeft (6); micBtn[3].setBounds (row2);
    }
    {
        auto r = arrangePanel.reduced (14).withTrimmedTop (42);
        auto row = r.removeFromTop (30);
        keyBox.setBounds (row.removeFromLeft (80)); row.removeFromLeft (10); scaleBox.setBounds (row);
        r.removeFromTop (28);
        row = r.removeFromTop (30);
        const int bw = (row.getWidth() - 6) / 2;
        octDown.setBounds (row.removeFromLeft (bw)); row.removeFromLeft (6); octUp.setBounds (row);
        r.removeFromTop (28);
        row = r.removeFromTop (30);
        thirdUp.setBounds (row.removeFromLeft (bw)); row.removeFromLeft (6); thirdDown.setBounds (row);
        r.removeFromTop (6);
        row = r.removeFromTop (30);
        fifthUp.setBounds (row.removeFromLeft (bw)); row.removeFromLeft (6); fifthDown.setBounds (row);
        r.removeFromTop (28);
        row = r.removeFromTop (30);
        stereoBtn.setBounds (row.removeFromLeft (bw)); row.removeFromLeft (6); monoBtn.setBounds (row);
    }
    {
        auto r = finishPanel.reduced (12).withTrimmedTop (30);
        auto place = [] (juce::Rectangle<int> area, juce::Slider& s, juce::Label& l)
        {
            l.setBounds (area.removeFromTop (18));
            s.setBounds (area);
        };
        auto rowA = r.removeFromTop (r.getHeight() / 2);
        auto rowB = r.withTrimmedTop (8);
        const int kw = rowA.getWidth() / 3;
        place (rowA.removeFromLeft (kw), lowCut, lowCutL);
        place (rowA.removeFromLeft (kw), bright, brightL);
        place (rowA, reverb, reverbL);
        const int kw2 = rowB.getWidth() / 2;
        place (rowB.removeFromLeft (kw2).reduced (14, 0), mix, mixL);
        place (rowB.reduced (14, 0), output, outputL);
    }
}

void KolotEditor::timerCallback()
{
    const int choir = getChoice (ParamID::choir);
    gospel.setToggleState (choir == 0, juce::dontSendNotification);
    kids.setToggleState   (choir == 1, juce::dontSendNotification);
    shout.setToggleState  (choir == 2, juce::dontSendNotification);

    const int mic = getChoice (ParamID::mic);
    for (int i = 0; i < 4; ++i)
    {
        const bool avail = micAvailable ((ChoirModel) choir, (MicModel) i);
        micBtn[i].setEnabled (avail);
        micBtn[i].setToggleState (avail && mic == i, juce::dontSendNotification);
        micBtn[i].setTooltip (avail ? "Microphone character" : "Not available for this choir");
    }
    const bool st = proc.apvts.getRawParameterValue (ParamID::stereo)->load() > 0.5f;
    stereoBtn.setToggleState (st, juce::dontSendNotification);
    monoBtn.setToggleState (! st, juce::dontSendNotification);

    live.update();
}
} // namespace kolot
