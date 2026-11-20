// kolot-render — renders a choir from a vocal file without a DAW.
// Usage: kolot-render in.wav out.wav [gospel|kids|shout] [key 0-11] [scale 0-8]
//                     [flags: od ou 3u 3d 5u 5d mono] [mic 0-3]
#include <juce_audio_formats/juce_audio_formats.h>
#include "ChoirEngine.h"
#include "Finisher.h"
#include <iostream>

int main (int argc, char** argv)
{
    using namespace kolot;
    if (argc < 3) { std::cerr << "usage: kolot-render in.wav out.wav [choir] [key] [scale] [flags] [mic]\n"; return 1; }

    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])));
    if (! r) { std::cerr << "can't read input\n"; return 1; }
    const int n = (int) r->lengthInSamples;
    juce::AudioBuffer<float> tmp ((int) r->numChannels, n), mono (1, n);
    r->read (&tmp, 0, n, 0, true, true);
    mono.clear();
    for (int c = 0; c < tmp.getNumChannels(); ++c) mono.addFrom (0, 0, tmp, c, 0, n, 1.0f / tmp.getNumChannels());

    ArrangeSettings a; FinishSettings f;
    const juce::String choir = argc > 3 ? argv[3] : "gospel";
    a.model = choir == "kids" ? ChoirModel::Kids : choir == "shout" ? ChoirModel::Shout : ChoirModel::Gospel;
    if (argc > 4) a.key = juce::String (argv[4]).getIntValue();
    if (argc > 5) a.scale = juce::String (argv[5]).getIntValue();
    const juce::String flags = argc > 6 ? argv[6] : "";
    a.octaveDown = flags.contains ("od"); a.octaveUp = flags.contains ("ou");
    a.thirdUp = flags.contains ("3u");    a.thirdDown = flags.contains ("3d");
    a.fifthUp = flags.contains ("5u");    a.fifthDown = flags.contains ("5d");
    f.stereo = ! flags.contains ("mono");
    a.testSingleVoice = flags.contains ("single");
    a.testDropLead = flags.contains ("nolead");
    if (flags.contains ("dry")) { f.reverb = 0; f.lowCutHz = 20; f.mic = MicModel::Schoeps; }
    if (argc > 7 && ! flags.contains ("dry")) f.mic = (MicModel) juce::String (argv[7]).getIntValue();

    auto engine = createDefaultEngine();
    RenderProgress prog; ChoirStems stems;
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    if (! engine->render (mono, r->sampleRate, a, stems, prog)) { std::cerr << "render failed\n"; return 2; }
    const auto t1 = juce::Time::getMillisecondCounterHiRes();
    juce::AudioBuffer<float> out;
    finishChoir (stems, f, out);
    const auto t2 = juce::Time::getMillisecondCounterHiRes();
    std::cout << "choir " << (t1 - t0) << " ms, finish " << (t2 - t1) << " ms, input " << n / r->sampleRate << " s\n";

    auto of = juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]);
    of.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (of.createOutputStream());
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), r->sampleRate, 2, 24, {}, 0));
    os.release();
    w->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());
    return 0;
}
