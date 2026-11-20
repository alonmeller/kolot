#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace kolot
{
using APVTS = juce::AudioProcessorValueTreeState;

static APVTS::ParameterLayout makeLayout()
{
    using namespace juce;
    APVTS::ParameterLayout l;
    auto choice = [&] (const char* id, const char* name, StringArray items, int def)
    { l.add (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, items, def)); };
    auto toggle = [&] (const char* id, const char* name, bool def)
    { l.add (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def)); };

    choice (ParamID::choir, "Choir", choirNames(), 0);
    choice (ParamID::mic,   "Microphone", micNames(), 0);
    choice (ParamID::key,   "Key", keyNames(), 0);
    choice (ParamID::scale, "Scale", scaleNames(), 0);
    toggle (ParamID::octDown,   "Octave Down", false);
    toggle (ParamID::octUp,     "Octave Up", false);
    toggle (ParamID::thirdUp,   "Third Above", false);
    toggle (ParamID::thirdDown, "Third Below", false);
    toggle (ParamID::fifthUp,   "Fifth Above", false);
    toggle (ParamID::fifthDown, "Fifth Below", false);
    toggle (ParamID::stereo,    "Stereo", true);

    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::lowCut, 1 }, "Low Cut",
              NormalisableRange<float> (20.0f, 500.0f, 1.0f, 0.4f), 80.0f,
              AudioParameterFloatAttributes().withLabel ("Hz")
                  .withStringFromValueFunction ([] (float v, int) { return v <= 21.0f ? String ("Off") : String (roundToInt (v)) + " Hz"; })));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::brightness, 1 }, "Brightness",
              NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f,
              AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100)) + "%"; })));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::reverb, 1 }, "Reverb",
              NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.25f,
              AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100)) + "%"; })));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::output, 1 }, "Output",
              NormalisableRange<float> (-24.0f, 6.0f, 0.1f), 0.0f,
              AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));
    return l;
}

static const char* arrangeIDs[] = { ParamID::choir, ParamID::key, ParamID::scale, ParamID::octDown, ParamID::octUp,
                                    ParamID::thirdUp, ParamID::thirdDown, ParamID::fifthUp, ParamID::fifthDown };
static const char* finishIDs[]  = { ParamID::mic, ParamID::stereo, ParamID::lowCut, ParamID::brightness,
                                    ParamID::reverb, ParamID::output };

KolotProcessor::KolotProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "KOLOT", makeLayout()),
      engine (createDefaultEngine())
{
    formats.registerBasicFormats();
    for (auto* id : arrangeIDs) apvts.addParameterListener (id, this);
    for (auto* id : finishIDs)  apvts.addParameterListener (id, this);
    startTimerHz (20);
}

KolotProcessor::~KolotProcessor()
{
    stopTimer();
    progress.cancel = true;
    if (worker.joinable()) worker.join();
    for (auto* id : arrangeIDs) apvts.removeParameterListener (id, this);
    for (auto* id : finishIDs)  apvts.removeParameterListener (id, this);
}

bool KolotProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void KolotProcessor::prepareToPlay (double sr, int) { hostSampleRate = sr; }

void KolotProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals nd;
    midi.clear();
    buffer.clear();
    if (! previewPlaying.load()) return;

    const juce::SpinLock::ScopedTryLockType lock (finishedLock);
    if (! lock.isLocked() || finished == nullptr) return;

    const auto& src = *finished;
    const int len = src.getNumSamples();
    const double ratio = finishedSampleRate / hostSampleRate;
    const int nOut = buffer.getNumChannels();
    const float* l = src.getReadPointer (0);
    const float* r = src.getReadPointer (1);

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const int idx = (int) playPos;
        if (idx + 1 >= len) { previewPlaying = false; playPos = 0.0; break; }
        const float f = (float) (playPos - idx);
        const float sl = l[idx] + f * (l[idx + 1] - l[idx]);
        const float sr = r[idx] + f * (r[idx + 1] - r[idx]);
        if (nOut >= 2) { buffer.setSample (0, i, sl); buffer.setSample (1, i, sr); }
        else if (nOut == 1) buffer.setSample (0, i, 0.5f * (sl + sr));
        playPos += ratio;
    }
    playFraction = len > 0 ? playPos / len : 0.0;
}

double KolotProcessor::getPreviewPosition() const { return playFraction.load(); }

void KolotProcessor::setPreviewPlaying (bool shouldPlay)
{
    if (shouldPlay && finished == nullptr) return;
    if (shouldPlay) { const juce::SpinLock::ScopedLockType sl (finishedLock); playPos = 0.0; }
    previewPlaying = shouldPlay;
}

// ---------------------------------------------------------------------------
ArrangeSettings KolotProcessor::currentArrange() const
{
    auto idx = [this] (const char* id) { return (int) apvts.getRawParameterValue (id)->load(); };
    auto on  = [this] (const char* id) { return apvts.getRawParameterValue (id)->load() > 0.5f; };
    ArrangeSettings a;
    a.model = (ChoirModel) idx (ParamID::choir);
    a.key = idx (ParamID::key);
    a.scale = idx (ParamID::scale);
    a.octaveDown = on (ParamID::octDown);  a.octaveUp = on (ParamID::octUp);
    a.thirdUp = on (ParamID::thirdUp);     a.thirdDown = on (ParamID::thirdDown);
    a.fifthUp = on (ParamID::fifthUp);     a.fifthDown = on (ParamID::fifthDown);
    a.seed = seed;
    return a;
}

FinishSettings KolotProcessor::currentFinish() const
{
    auto v = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    FinishSettings f;
    f.mic = (MicModel) (int) v (ParamID::mic);
    if (! micAvailable ((ChoirModel) (int) v (ParamID::choir), f.mic)) f.mic = MicModel::U67;
    f.stereo = v (ParamID::stereo) > 0.5f;
    f.lowCutHz = v (ParamID::lowCut);
    f.brightness = v (ParamID::brightness);
    f.reverb = v (ParamID::reverb);
    f.outputDb = v (ParamID::output);
    return f;
}

void KolotProcessor::parameterChanged (const juce::String& id, float)
{
    for (auto* a : arrangeIDs) if (id == a) { arrangeDirty = true; return; }
    if (! stems.isValid() && state.load() == State::Done) { arrangeDirty = true; return; }
    finishPending = true;
    lastFinishRequest = juce::Time::getMillisecondCounter();
}

void KolotProcessor::timerCallback()
{
    // Keep the mic choice valid for the selected choir.
    const auto choir = (ChoirModel) (int) apvts.getRawParameterValue (ParamID::choir)->load();
    const auto mic   = (MicModel) (int) apvts.getRawParameterValue (ParamID::mic)->load();
    if (! micAvailable (choir, mic))
        if (auto* p = apvts.getParameter (ParamID::mic))
            p->setValueNotifyingHost (p->convertTo0to1 ((float) (int) MicModel::U67));

    if (finishPending && ! busy && state.load() == State::Done && stems.isValid()
        && juce::Time::getMillisecondCounter() - lastFinishRequest.load() > 250)
    {
        finishPending = false;
        startJob (false);
    }
}

// ---------------------------------------------------------------------------
bool KolotProcessor::loadVocal (const juce::File& f, juce::String& error)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));
    if (reader == nullptr) { error = "Kolot can't read this file. Use WAV, AIFF, FLAC, MP3 or M4A."; return false; }
    if (reader->lengthInSamples > (juce::int64) (reader->sampleRate * 600)) { error = "The vocal is longer than 10 minutes. Trim it and try again."; return false; }
    if (reader->lengthInSamples < (juce::int64) (reader->sampleRate * 0.2)) { error = "The file is too short."; return false; }

    cancelRender();
    const int n = (int) reader->lengthInSamples;
    const int ch = (int) juce::jmax (1u, reader->numChannels);
    juce::AudioBuffer<float> tmp (ch, n);
    reader->read (&tmp, 0, n, 0, true, true);

    vocal.setSize (1, n);
    vocal.clear();
    for (int c = 0; c < ch; ++c) vocal.addFrom (0, 0, tmp, c, 0, n, 1.0f / (float) ch);
    vocalSampleRate = reader->sampleRate;
    vocalFile = f;
    seed = (juce::uint32) f.getFileName().hashCode();

    stems = {};
    previewPlaying = false;
    { const juce::SpinLock::ScopedLockType sl (finishedLock); finished.reset(); }
    {
        const juce::ScopedLock sl (peaksLock);
        vocalPeaks = makePeaks (vocal, 1200);
        choirPeaks.clear();
    }
    lastWrittenIsCurrent = false;
    arrangeDirty = false;
    state = State::Ready;
    ++changeCounter;
    return true;
}

void KolotProcessor::clearVocal()
{
    cancelRender();
    vocal.setSize (0, 0);
    vocalFile = juce::File();
    stems = {};
    previewPlaying = false;
    { const juce::SpinLock::ScopedLockType sl (finishedLock); finished.reset(); }
    { const juce::ScopedLock sl (peaksLock); vocalPeaks.clear(); choirPeaks.clear(); }
    state = State::Empty;
    ++changeCounter;
}

void KolotProcessor::createVocals()
{
    if (vocal.getNumSamples() == 0) return;
    startJob (true);
}

void KolotProcessor::cancelRender()
{
    progress.cancel = true;
    if (worker.joinable()) worker.join();
    progress.cancel = false;
    busy = false;
    if (state.load() == State::Rendering) state = finished != nullptr ? State::Done : State::Ready;
}

void KolotProcessor::startJob (bool full)
{
    cancelRender();
    busy = true;
    if (full)
    {
        state = State::Rendering;
        progress.fraction = 0.0f;
        arrangeDirty = false;
        finishPending = false;
    }
    const auto a = currentArrange();
    const auto f = currentFinish();
    worker = std::thread ([this, full, a, f]
    {
        bool ok = true;
        if (full)
        {
            ChoirStems s;
            ok = engine->render (vocal, vocalSampleRate, a, s, progress);
            if (ok) stems = std::move (s);
        }
        if (ok)
        {
            auto out = std::make_shared<juce::AudioBuffer<float>>();
            ok = finishChoir (stems, f, *out, &progress.cancel);
            if (ok)
            {
                auto peaks = makePeaks (*out, 1200);
                {
                    const juce::SpinLock::ScopedLockType sl (finishedLock);
                    finished = out;
                    finishedSampleRate = stems.sampleRate;
                }
                { const juce::ScopedLock sl (peaksLock); choirPeaks = std::move (peaks); }
                lastWrittenIsCurrent = false;
                state = State::Done;
                ++changeCounter;
            }
        }
        if (! ok && full && ! progress.cancel) state = State::Ready;
        busy = false;
    });
}

// ---------------------------------------------------------------------------
std::vector<std::pair<float, float>> KolotProcessor::makePeaks (const juce::AudioBuffer<float>& b, int bins)
{
    std::vector<std::pair<float, float>> p;
    const int n = b.getNumSamples();
    if (n == 0) return p;
    p.resize ((size_t) bins, { 0.0f, 0.0f });
    for (int i = 0; i < bins; ++i)
    {
        const int a = (int) ((juce::int64) n * i / bins), e = juce::jmax (a + 1, (int) ((juce::int64) n * (i + 1) / bins));
        float lo = 0.0f, hi = 0.0f;
        for (int c = 0; c < b.getNumChannels(); ++c)
        {
            auto r = b.findMinMax (c, a, juce::jmin (e, n) - a);
            lo = juce::jmin (lo, r.getStart()); hi = juce::jmax (hi, r.getEnd());
        }
        p[(size_t) i] = { lo, hi };
    }
    return p;
}

std::vector<std::pair<float, float>> KolotProcessor::getVocalPeaks() const { const juce::ScopedLock sl (peaksLock); return vocalPeaks; }
std::vector<std::pair<float, float>> KolotProcessor::getChoirPeaks() const { const juce::ScopedLock sl (peaksLock); return choirPeaks; }

juce::String KolotProcessor::getStatusText() const
{
    switch (state.load())
    {
        case State::Empty:     return "Drop a dry, mono vocal to begin";
        case State::Ready:     return "Ready - choose a choir and press Create Vocals";
        case State::Rendering: return "Creating choir... " + juce::String (juce::roundToInt (progress.fraction.load() * 100)) + "%";
        case State::Done:      return needsRerender() ? "Settings changed - press Create Vocals to update"
                                                      : "Choir ready - drag it to a track, or press Preview";
    }
    return {};
}

juce::File KolotProcessor::rendersFolder()
{
    auto d = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Kolot").getChildFile ("Renders");
    d.createDirectory();
    return d;
}

bool KolotProcessor::exportChoirTo (const juce::File& file)
{
    std::shared_ptr<juce::AudioBuffer<float>> buf;
    double sr;
    { const juce::SpinLock::ScopedLockType sl (finishedLock); buf = finished; sr = finishedSampleRate; }
    if (buf == nullptr) return false;

    file.deleteFile();
    std::unique_ptr<juce::OutputStream> os (file.createOutputStream());
    if (os == nullptr) return false;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), sr, 2, 24, {}, 0));
    if (w == nullptr) return false;
    os.release();
    return w->writeFromAudioSampleBuffer (*buf, 0, buf->getNumSamples());
}

juce::File KolotProcessor::writeChoirFileForDrag()
{
    if (lastWrittenIsCurrent && lastWrittenFile.existsAsFile()) return lastWrittenFile;
    const auto choir = choirNames()[(int) apvts.getRawParameterValue (ParamID::choir)->load()];
    const auto base = (vocalFile == juce::File() ? juce::String ("Vocal") : vocalFile.getFileNameWithoutExtension())
                      + "_Kolot_" + choir;
    auto f = rendersFolder().getNonexistentChildFile (base, ".wav", false);
    if (! exportChoirTo (f)) return {};
    lastWrittenFile = f;
    lastWrittenIsCurrent = true;
    return f;
}

// ---------------------------------------------------------------------------
void KolotProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto st = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (st.createXml());
    xml->setAttribute ("vocalPath", vocalFile.getFullPathName());
    xml->setAttribute ("choirPath", lastWrittenFile.getFullPathName());
    xml->setAttribute ("choirCurrent", lastWrittenIsCurrent.load());
    copyXmlToBinary (*xml, dest);
}

void KolotProcessor::setStateInformation (const void* data, int size)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, size));
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    const juce::File vf (xml->getStringAttribute ("vocalPath"));
    const juce::File cf (xml->getStringAttribute ("choirPath"));
    const bool choirCurrent = xml->getBoolAttribute ("choirCurrent");
    apvts.replaceState (juce::ValueTree::fromXml (*xml));

    juce::MessageManager::callAsync ([this, vf, cf, choirCurrent]
    {
        juce::String err;
        if (! vf.existsAsFile() || ! loadVocal (vf, err)) return;
        if (choirCurrent && cf.existsAsFile())
        {
            std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (cf));
            if (r == nullptr) return;
            auto b = std::make_shared<juce::AudioBuffer<float>> (2, (int) r->lengthInSamples);
            r->read (b.get(), 0, (int) r->lengthInSamples, 0, true, true);
            {
                const juce::SpinLock::ScopedLockType sl (finishedLock);
                finished = b;
                finishedSampleRate = r->sampleRate;
            }
            { const juce::ScopedLock sl (peaksLock); choirPeaks = makePeaks (*b, 1200); }
            lastWrittenFile = cf;
            lastWrittenIsCurrent = true;
            state = State::Done;
            arrangeDirty = false;
            ++changeCounter;
        }
    });
}

juce::AudioProcessorEditor* KolotProcessor::createEditor() { return new KolotEditor (*this); }

} // namespace kolot

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new kolot::KolotProcessor(); }
