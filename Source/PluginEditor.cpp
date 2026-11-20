#include "PluginEditor.h"

namespace kolot
{
using namespace Colours;
static juce::Font F (float h, bool b = false) { return KolotLookAndFeel::font (h, b); }

// ============================================================================
juce::Rectangle<int> WaveformPanel::dragChip() const  { return { getWidth() - 214, getHeight() - 40, 200, 28 }; }
juce::Rectangle<int> WaveformPanel::clearChip() const { return { getWidth() - 34, 10, 24, 24 }; }

void WaveformPanel::drawPeaks (juce::Graphics& g, juce::Rectangle<float> r,
                               const std::vector<std::pair<float, float>>& pk, juce::Colour c, float gain)
{
    if (pk.empty()) return;
    const float mid = r.getCentreY(), half = r.getHeight() * 0.5f;
    const int W = (int) r.getWidth();
    g.setColour (c);
    for (int x = 0; x < W; ++x)
    {
        const size_t i = (size_t) ((double) x / W * (double) pk.size());
        const auto& p = pk[juce::jmin (i, pk.size() - 1)];
        const float top = mid - juce::jmin (1.0f, p.second * gain) * half;
        const float bot = mid - juce::jmax (-1.0f, p.first * gain) * half;
        g.fillRect (r.getX() + (float) x, top, 1.0f, juce::jmax (1.0f, bot - top));
    }
}

void WaveformPanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff141722), 0, 0, juce::Colour (0xff0b0d13), 0, r.getBottom(), false));
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (dragOver ? amber : edge);
    g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, dragOver ? 2.0f : 1.0f);

    auto inner = r.reduced (18.0f, 34.0f);
    const auto state = proc.getState();

    if (vocalPeaks.empty())
    {
        // Empty state: an invitation to drop a vocal.
        g.setColour (ivoryDim.withAlpha (0.25f));
        const float dash[] = { 6.0f, 6.0f };
        juce::Path p; p.addRoundedRectangle (r.reduced (14.0f), 10.0f);
        juce::Path dashed; juce::PathStrokeType (1.2f).createDashedStroke (dashed, p, dash, 2);
        g.fillPath (dashed);
        g.setColour (ivory);
        g.setFont (F (19.0f, true));
        g.drawText ("Drop a dry, mono vocal here", getLocalBounds().withTrimmedBottom (26), juce::Justification::centred);
        g.setColour (ivoryDim);
        g.setFont (F (13.0f));
        g.drawText ("or click to browse  -  WAV, AIFF, FLAC, MP3, M4A  -  one take, no doubles or harmonies",
                    getLocalBounds().withTrimmedTop (34), juce::Justification::centred);
        return;
    }

    // Header line: file name + length.
    g.setColour (ivory);
    g.setFont (F (13.0f, true));
    const auto secs = proc.getVocalSeconds();
    g.drawText (proc.getVocalFile().getFileName() + "   " + juce::String ((int) secs / 60) + ":"
                + juce::String ((int) secs % 60).paddedLeft ('0', 2),
                juce::Rectangle<int> (18, 8, getWidth() - 80, 20), juce::Justification::centredLeft);

    // Clear (x)
    auto cc = clearChip().toFloat();
    g.setColour (ivoryDim);
    g.drawLine (cc.getX() + 7, cc.getY() + 7, cc.getRight() - 7, cc.getBottom() - 7, 1.5f);
    g.drawLine (cc.getRight() - 7, cc.getY() + 7, cc.getX() + 7, cc.getBottom() - 7, 1.5f);

    // Vocal (ivory, faint) under the choir (amber).
    drawPeaks (g, inner, vocalPeaks, ivory.withAlpha (choirPeaks.empty() ? 0.75f : 0.22f), 1.0f);
    if (! choirPeaks.empty())
    {
        juce::Rectangle<float> cr = inner;
        drawPeaks (g, cr, choirPeaks, amber.withAlpha (0.85f), 1.0f);
    }

    // Playhead
    if (proc.isPreviewPlaying())
    {
        const float x = inner.getX() + inner.getWidth() * (float) proc.getPreviewPosition();
        g.setColour (amberHot);
        g.fillRect (x, inner.getY() - 6, 1.5f, inner.getHeight() + 12);
    }

    if (state == KolotProcessor::State::Rendering)
    {
        g.setColour (bg.withAlpha (0.55f));
        g.fillRoundedRectangle (inner.expanded (6.0f), 8.0f);
        g.setColour (amber);
        g.setFont (F (16.0f, true));
        g.drawText ("The choir is learning your part...", inner, juce::Justification::centred);
    }

    // Drag-out chip
    if (state == KolotProcessor::State::Done)
    {
        auto d = dragChip().toFloat();
        g.setColour (amber.withAlpha (0.15f));
        g.fillRoundedRectangle (d, 7.0f);
        g.setColour (amber);
        g.drawRoundedRectangle (d, 7.0f, 1.0f);
        g.setFont (F (13.0f, true));
        g.drawText (juce::String::fromUTF8 ("\xe2\x87\xa2  Drag choir to a track"), d, juce::Justification::centred);
    }
}

void WaveformPanel::mouseMove (const juce::MouseEvent& e)
{
    const bool overChip = proc.getState() == KolotProcessor::State::Done && dragChip().contains (e.getPosition());
    setMouseCursor (overChip ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::PointingHandCursor);
}

void WaveformPanel::mouseDown (const juce::MouseEvent& e)
{
    dragStarted = false;
    if (proc.getState() == KolotProcessor::State::Rendering) return;
    if (! vocalPeaks.empty() && clearChip().contains (e.getPosition())) { proc.clearVocal(); refresh(); return; }
    if (proc.getState() == KolotProcessor::State::Done && dragChip().contains (e.getPosition())) return;
    if (vocalPeaks.empty() || e.mods.isPopupMenu() || e.getNumberOfClicks() > 1) browse();
}

void WaveformPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (dragStarted || proc.getState() != KolotProcessor::State::Done) return;
    if (! dragChip().contains (e.getMouseDownPosition()) || e.getDistanceFromDragStart() < 4) return;
    dragStarted = true;
    const auto f = proc.writeChoirFileForDrag();
    if (f.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
    else if (onError) onError ("Couldn't write the choir file to " + KolotProcessor::rendersFolder().getFullPathName());
}

void WaveformPanel::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    if (! files.isEmpty()) load (juce::File (files[0]));
    repaint();
}

void WaveformPanel::browse()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a dry, mono vocal",
                  juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                  "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.m4a;*.caf");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f.existsAsFile()) load (f);
                          });
}

void WaveformPanel::load (const juce::File& f)
{
    juce::String err;
    if (! proc.loadVocal (f, err) && onError) onError (err);
    refresh();
}

// ============================================================================
void ChoirCard::paintButton (juce::Graphics& g, bool over, bool)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool on = getToggleState();
    g.setColour (on ? amber.withAlpha (0.16f) : (over ? panelHi.brighter (0.06f) : panelHi));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (on ? amber : edge);
    g.drawRoundedRectangle (r, 8.0f, on ? 1.6f : 1.0f);

    // Little "stage light" dot
    g.setColour (on ? amberHot : edge.brighter (0.3f));
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
    setSize (980, 680);

    addAndMakeVisible (wave);
    wave.onError = [this] (const juce::String& e) { errorText = e; errorUntil = juce::Time::getMillisecondCounter() + 6000; };
    wave.refresh();

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
        { &reverb, &reverbL, ParamID::reverb, "Reverb" },   { &output, &outputL, ParamID::output, "Output" } };
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
    output.setDoubleClickReturnValue (true, 0.0);
    bright.setDoubleClickReturnValue (true, 0.0);

    preview.onClick = [this] { proc.setPreviewPlaying (! proc.isPreviewPlaying()); };
    addAndMakeVisible (preview);

    exportBtn.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Export choir", KolotProcessor::rendersFolder().getChildFile (
                       (proc.getVocalFile().getFileNameWithoutExtension().isEmpty() ? juce::String ("Vocal")
                                                                                     : proc.getVocalFile().getFileNameWithoutExtension())
                       + "_Kolot.wav"), "*.wav");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& fc)
                              {
                                  auto f = fc.getResult();
                                  if (f == juce::File()) return;
                                  if (! proc.exportChoirTo (f.withFileExtension ("wav")))
                                  { errorText = "Export failed."; errorUntil = juce::Time::getMillisecondCounter() + 5000; }
                              });
    };
    addAndMakeVisible (exportBtn);

    create.getProperties().set ("big", true);
    create.onClick = [this]
    {
        if (proc.getState() == KolotProcessor::State::Rendering) proc.cancelRender();
        else proc.createVocals();
    };
    addAndMakeVisible (create);

    status.setFont (F (13.0f));
    status.setColour (juce::Label::textColourId, ivoryDim);
    addAndMakeVisible (status);

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

void KolotEditor::drawPanel (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title)
{
    auto f = r.toFloat();
    g.setGradientFill (juce::ColourGradient (panel.brighter (0.03f), 0, f.getY(), panel.darker (0.15f), 0, f.getBottom(), false));
    g.fillRoundedRectangle (f, 12.0f);
    g.setColour (edge);
    g.drawRoundedRectangle (f.reduced (0.5f), 12.0f, 1.0f);
    // engraved title
    g.setFont (F (12.0f, true));
    g.setColour (amber);
    g.drawText (title.toUpperCase(), r.reduced (16, 10).removeFromTop (16), juce::Justification::centredLeft);
}

void KolotEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);
    // Soft "stage light" glow behind the header.
    g.setGradientFill (juce::ColourGradient (amber.withAlpha (0.10f), getWidth() * 0.5f, 0,
                                             amber.withAlpha (0.0f), getWidth() * 0.5f, 220, true));
    g.fillRect (getLocalBounds().removeFromTop (220));

    // Logo
    g.setColour (ivory);
    g.setFont (F (28.0f, true));
    g.drawText ("K O L O T", 24, 14, 220, 36, juce::Justification::centredLeft);
    g.setColour (amber);
    g.setFont (F (17.0f, true));
    g.drawText (juce::String::fromUTF8 ("\xd7\xa7\xd7\x95\xd7\x9c\xd7\x95\xd7\xaa"), 196, 18, 70, 30, juce::Justification::centredLeft);
    g.setColour (ivoryDim);
    g.setFont (F (12.0f));
    g.drawText ("CHOIR ENGINE  -  sings your part with a whole choir", 270, 18, 420, 30, juce::Justification::centredLeft);
    g.drawText ("v1.0  -  by Alon Meller", getWidth() - 224, 18, 200, 30, juce::Justification::centredRight);

    drawPanel (g, choirPanel, "Choir");
    drawPanel (g, arrangePanel, "Arrange");
    drawPanel (g, finishPanel, "Finish");

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
    wave.setBounds (20, 62, W - 40, 190);

    const int top = 266, h = 330, gap = 14;
    const int pw = (W - 40 - 2 * gap) / 3;
    choirPanel   = { 20, top, pw, h };
    arrangePanel = { 20 + pw + gap, top, pw, h };
    finishPanel  = { 20 + 2 * (pw + gap), top, pw, h };

    // Choir panel
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
    // Arrange panel
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
    // Finish panel
    {
        auto r = finishPanel.reduced (14).withTrimmedTop (30);
        auto rowA = r.removeFromTop (r.getHeight() / 2);
        auto rowB = r.withTrimmedTop (8);
        auto place = [] (juce::Rectangle<int> area, juce::Slider& s, juce::Label& l)
        {
            l.setBounds (area.removeFromTop (18));
            s.setBounds (area);
        };
        const int kw = rowA.getWidth() / 2;
        place (rowA.removeFromLeft (kw), lowCut, lowCutL); place (rowA, bright, brightL);
        place (rowB.removeFromLeft (kw), reverb, reverbL); place (rowB, output, outputL);
    }
    // Bottom bar
    auto bar = juce::Rectangle<int> (20, top + h + 16, W - 40, 50);
    create.setBounds (bar.removeFromRight (260));
    bar.removeFromRight (12);
    exportBtn.setBounds (bar.removeFromRight (100).reduced (0, 6));
    bar.removeFromRight (8);
    preview.setBounds (bar.removeFromRight (110).reduced (0, 6));
    status.setBounds (bar);
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

    const auto state = proc.getState();
    const bool rendering = state == KolotProcessor::State::Rendering;
    create.setEnabled (state != KolotProcessor::State::Empty);
    create.setButtonText (rendering ? "Cancel" : (proc.needsRerender() ? "Update Vocals" : "Create Vocals"));
    create.getProperties().set ("progress", rendering ? (double) proc.getProgress() : -1.0);
    create.repaint();

    const bool done = state == KolotProcessor::State::Done;
    preview.setEnabled (done);
    exportBtn.setEnabled (done);
    preview.setButtonText (proc.isPreviewPlaying() ? "Stop" : "Preview");
    preview.setToggleState (proc.isPreviewPlaying(), juce::dontSendNotification);

    if (juce::Time::getMillisecondCounter() < errorUntil)
    {
        status.setColour (juce::Label::textColourId, rose);
        status.setText (errorText, juce::dontSendNotification);
    }
    else
    {
        status.setColour (juce::Label::textColourId, ivoryDim);
        status.setText (proc.getStatusText(), juce::dontSendNotification);
    }

    const int cc = proc.getChangeCounter();
    if (cc != lastChange) { lastChange = cc; wave.refresh(); }
    else if (rendering || proc.isPreviewPlaying()) wave.repaint();
}
} // namespace kolot
