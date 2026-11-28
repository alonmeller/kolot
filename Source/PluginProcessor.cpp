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
    auto pct = [] (float v, int) { return String (roundToInt (v * 100)) + "%"; };

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
              AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::reverb, 1 }, "Reverb",
              NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.25f,
              AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::mix, 1 }, "Mix",
              NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f,
              AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::output, 1 }, "Output",
              NormalisableRange<float> (-24.0f, 6.0f, 0.1f), 0.0f,
              AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));
    return l;
}

KolotProcessor::KolotProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "KOLOT", makeLayout())
{
    for (auto& a : inHist) a = 0.0f;
    for (auto& a : outHist) a = 0.0f;
    startTimerHz (10);
}

KolotProcessor::~KolotProcessor() { stopTimer(); }

bool KolotProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in  = l.getMainInputChannelSet();
    const auto out = l.getMainOutputChannelSet();
    const bool inOk  = in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
    const bool outOk = out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
    return inOk && outOk;
}

void KolotProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    choir.prepare (sampleRate);
    finisher.prepare (sampleRate, choir.getLatencySamples());
    setLatencySamples (choir.getLatencySamples());
    latencyMs = 1000.0 * choir.getLatencySamples() / sampleRate;
    const int n = juce::jmax (512, samplesPerBlock);
    monoIn.setSize (1, n);
    choirBuf.setSize (3, n);
}

void KolotProcessor::reset()
{
    choir.reset();
    finisher.reset();
}

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
    a.seed = 20260;
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
    f.mix = v (ParamID::mix);
    f.outputDb = v (ParamID::output);
    return f;
}

void KolotProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int total  = buffer.getNumSamples();

    choir.setArrange (currentArrange());
    finisher.setSettings (currentFinish());

    const int cap = monoIn.getNumSamples();
    for (int done = 0; done < total;)
    {
        const int n = juce::jmin (cap, total - done);

        // Sum the track to mono for analysis.
        float* m = monoIn.getWritePointer (0);
        juce::FloatVectorOperations::clear (m, n);
        if (numIn > 0)
        {
            for (int c = 0; c < numIn; ++c)
                juce::FloatVectorOperations::addWithMultiply (m, buffer.getReadPointer (c, done), 1.0f / (float) numIn, n);
        }

        float* cl = choirBuf.getWritePointer (0);
        float* cr = choirBuf.getWritePointer (1);
        float* cm = choirBuf.getWritePointer (2);
        choir.process (m, cl, cr, cm, n);

        float* oL = buffer.getWritePointer (0, done);
        float* oR = numOut > 1 ? buffer.getWritePointer (1, done) : oL;
        const float* dL = numIn > 0 ? buffer.getReadPointer (0, done) : m;
        const float* dR = numIn > 1 ? buffer.getReadPointer (1, done) : dL;

        if (numOut > 1)
            finisher.process (cl, cr, cm, dL, dR, oL, oR, n);
        else
        {
            // Mono out: render stereo into the choir buffer, then fold down.
            finisher.process (cl, cr, cm, dL, dR, cl, cr, n);
            for (int i = 0; i < n; ++i) oL[i] = 0.5f * (cl[i] + cr[i]);
        }

        // Live display history (~30 points per second).
        histIn  = juce::jmax (histIn,  juce::FloatVectorOperations::findMaximum (m, n));
        histOut = juce::jmax (histOut, juce::FloatVectorOperations::findMaximum (oL, n));
        histCounter += n;
        if (histCounter >= (int) (getSampleRate() / 30.0))
        {
            histCounter = 0;
            const int p = (histPos.load() + 1) % historySize;
            inHist[(size_t) p]  = histIn;
            outHist[(size_t) p] = histOut;
            histPos = p;
            histIn = histOut = 0.0f;
        }
        done += n;
    }
    for (int c = 2; c < numOut; ++c) buffer.clear (c, 0, total);
    leadHz = choir.getLeadHz();
}

void KolotProcessor::getHistory (float* inLevels, float* outLevels) const
{
    const int p = histPos.load();
    for (int i = 0; i < historySize; ++i)
    {
        const size_t j = (size_t) ((p + 1 + i) % historySize);
        inLevels[i]  = inHist[j].load();
        outLevels[i] = outHist[j].load();
    }
}

void KolotProcessor::timerCallback()
{
    // Keep the mic choice valid for the selected choir.
    const auto c = (ChoirModel) (int) apvts.getRawParameterValue (ParamID::choir)->load();
    const auto m = (MicModel) (int) apvts.getRawParameterValue (ParamID::mic)->load();
    if (! micAvailable (c, m))
        if (auto* p = apvts.getParameter (ParamID::mic))
            p->setValueNotifyingHost (p->convertTo0to1 ((float) (int) MicModel::U67));
}

void KolotProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto st = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (st.createXml());
    copyXmlToBinary (*xml, dest);
}

void KolotProcessor::setStateInformation (const void* data, int size)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, size));
    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* KolotProcessor::createEditor() { return new KolotEditor (*this); }

} // namespace kolot

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new kolot::KolotProcessor(); }
