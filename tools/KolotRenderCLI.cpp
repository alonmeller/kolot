// kolot-render — streams a file through Kolot's real-time engine, block by block,
// exactly as a DAW would, and writes the result (latency removed).
// Usage: kolot-render in.wav out.wav [gospel|kids|shout] [key 0-11] [scale 0-8]
//                     [flags: od ou 3u 3d 5u 5d mono single nolead dry] [mic 0-3] [block]
#include <juce_audio_formats/juce_audio_formats.h>
#include "RealtimeChoir.h"
#include "LiveFinisher.h"
#include <iostream>

int main (int argc, char** argv)
{
    using namespace kolot;
    if (argc < 3) { std::cerr << "usage: kolot-render in.wav out.wav [choir] [key] [scale] [flags] [mic] [block]\n"; return 1; }

    juce::AudioFormatManager fm; fm.registerBasicFormats();
    auto cwd = juce::File::getCurrentWorkingDirectory();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (cwd.getChildFile (argv[1])));
    if (! r) { std::cerr << "can't read input\n"; return 1; }
    const int n = (int) r->lengthInSamples;
    const double sr = r->sampleRate;
    juce::AudioBuffer<float> src ((int) r->numChannels, n);
    r->read (&src, 0, n, 0, true, true);

    ArrangeSettings a; FinishSettings f;
    const juce::String choir = argc > 3 ? argv[3] : "gospel";
    a.model = choir == "kids" ? ChoirModel::Kids : choir == "shout" ? ChoirModel::Shout : ChoirModel::Gospel;
    if (argc > 4) a.key = juce::String (argv[4]).getIntValue();
    if (argc > 5) a.scale = juce::String (argv[5]).getIntValue();
    const juce::String flags = argc > 6 ? argv[6] : "";
    a.octaveDown = flags.contains ("od"); a.octaveUp = flags.contains ("ou");
    a.thirdUp = flags.contains ("3u");    a.thirdDown = flags.contains ("3d");
    a.fifthUp = flags.contains ("5u");    a.fifthDown = flags.contains ("5d");
    a.testSingleVoice = flags.contains ("single");
    a.testDropLead = flags.contains ("nolead");
    f.stereo = ! flags.contains ("mono");
    if (argc > 7) f.mic = (MicModel) juce::String (argv[7]).getIntValue();
    if (flags.contains ("dry")) { f.reverb = 0; f.lowCutHz = 20; f.mic = MicModel::Schoeps; }
    const int block = argc > 8 ? juce::String (argv[8]).getIntValue() : 256;

    RealtimeChoir eng; LiveFinisher fin;
    eng.prepare (sr);
    const int lat = eng.getLatencySamples();
    fin.prepare (sr, lat);
    eng.setArrange (a);
    fin.setSettings (f);

    const int total = n + lat + (int) (sr * 1.0);
    juce::AudioBuffer<float> out (2, total), mono (1, block), c (3, block), dry (2, block);
    out.clear();
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    for (int pos = 0; pos < total; pos += block)
    {
        const int len = juce::jmin (block, total - pos);
        mono.clear(); dry.clear();
        for (int ch = 0; ch < juce::jmin (2, src.getNumChannels()); ++ch)
        {
            const int avail = juce::jlimit (0, len, n - pos);
            if (avail > 0) dry.copyFrom (ch, 0, src, ch, pos, avail);
        }
        if (src.getNumChannels() == 1) dry.copyFrom (1, 0, dry, 0, 0, len);
        mono.addFrom (0, 0, dry, 0, 0, len, 0.5f);
        mono.addFrom (0, 0, dry, 1, 0, len, 0.5f);
        eng.setArrange (a);
        eng.process (mono.getReadPointer (0), c.getWritePointer (0), c.getWritePointer (1), c.getWritePointer (2), len);
        fin.process (c.getReadPointer (0), c.getReadPointer (1), c.getReadPointer (2),
                     dry.getReadPointer (0), dry.getReadPointer (1),
                     out.getWritePointer (0, pos), out.getWritePointer (1, pos), len);
    }
    const auto t1 = juce::Time::getMillisecondCounterHiRes();
    std::cout << "latency " << lat << " samples (" << 1000.0 * lat / sr << " ms), "
              << (t1 - t0) << " ms for " << n / sr << " s of audio ("
              << (n / sr) / ((t1 - t0) / 1000.0) << "x real time)\n";

    // Remove the latency so the file lines up with the input.
    juce::AudioBuffer<float> aligned (2, total - lat);
    aligned.copyFrom (0, 0, out, 0, lat, total - lat);
    aligned.copyFrom (1, 0, out, 1, lat, total - lat);

    auto of = cwd.getChildFile (argv[2]);
    of.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (of.createOutputStream());
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), sr, 2, 24, {}, 0));
    os.release();
    w->writeFromAudioSampleBuffer (aligned, 0, aligned.getNumSamples());
    return 0;
}
