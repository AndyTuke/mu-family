// mu-link-harness — a headless "virtual mu-link" for automated audio verification.
//
// It stands in for the hardware device: it creates the shared-memory bus + runs the real
// ServerEngine, but instead of playing to a soundcard it captures the summed master to a
// WAV file (real-time-paced so clients' render-ahead behaves exactly as in production), and
// analyses the result (RMS + per-frequency Goertzel) to PASS/FAIL with an exit code. No
// audio hardware required, so it runs anywhere and is repeatable.
//
// Producers (any combination):
//   --internal-tone <hz>   in-process MuLinkClient sine (hermetic; proves engine+ring+client
//                          with no external process or device — ideal for CI)
//   --spawn "<cmd>"        launch a real client process (e.g. mu-link-tone, or any mu app's
//                          standalone, which auto-attaches) — the faithful end-to-end path
//   (none)                 just capture whatever attaches while the harness runs, so you can
//                          launch an app by hand against it and grab its output
//
// Examples:
//   mu-link-harness --internal-tone 440 --seconds 2 --expect-hz 440 --out tone.wav
//   mu-link-harness --spawn "C:\…\mu-link-tone.exe 660" --seconds 2 --expect-hz 660
//   mu-link-harness --seconds 8 --out muclid.wav        (then launch mu-Clid by hand)

#include <juce_audio_formats/juce_audio_formats.h>

#include "../Ipc/MuLinkServerMemory.h"
#include "../Server/ServerEngine.h"
#include "Link/MuLinkClient.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

using namespace mu_link;

namespace
{
//==============================================================================
struct Args
{
    juce::String        out;                 // optional WAV path
    double              seconds   = 3.0;
    int                 rate      = 48000;
    int                 block     = 512;
    double              warmupMs  = 400.0;   // settle time for clients to attach + fill
    std::vector<double> internalTones;       // --internal-tone
    juce::StringArray   spawnCmds;           // --spawn
    std::vector<double> expectHz;            // --expect-hz (assert present)
    double              minRms    = 0.01;    // assert overall RMS ≥ this (catches silence)
    double              toneAmpThresh = 0.03; // assert each expected tone's amplitude ≥ this
};

double nextNumber(int& i, int argc, char** argv, double fallback)
{
    if (i + 1 < argc) { ++i; return juce::String(argv[i]).getDoubleValue(); }
    return fallback;
}

Args parseArgs(int argc, char** argv)
{
    Args a;
    for (int i = 1; i < argc; ++i)
    {
        const juce::String arg(argv[i]);
        if      (arg == "--out"           && i + 1 < argc) a.out = argv[++i];
        else if (arg == "--seconds")        a.seconds  = nextNumber(i, argc, argv, a.seconds);
        else if (arg == "--rate")           a.rate     = (int) nextNumber(i, argc, argv, a.rate);
        else if (arg == "--block")          a.block    = (int) nextNumber(i, argc, argv, a.block);
        else if (arg == "--warmup-ms")      a.warmupMs = nextNumber(i, argc, argv, a.warmupMs);
        else if (arg == "--internal-tone")  a.internalTones.push_back(nextNumber(i, argc, argv, 440.0));
        else if (arg == "--spawn"         && i + 1 < argc) a.spawnCmds.add(argv[++i]);
        else if (arg == "--expect-hz")      a.expectHz.push_back(nextNumber(i, argc, argv, 0.0));
        else if (arg == "--min-rms")        a.minRms   = nextNumber(i, argc, argv, a.minRms);
        else if (arg == "--tone-amp")       a.toneAmpThresh = nextNumber(i, argc, argv, a.toneAmpThresh);
    }
    return a;
}

//==============================================================================
// Running Goertzel detector for one frequency — accumulates across the whole capture.
struct Goertzel
{
    void prepare(double freq, double sampleRate)
    {
        coeff = 2.0 * std::cos(2.0 * juce::MathConstants<double>::pi * freq / sampleRate);
        s1 = s2 = 0.0;
    }
    void push(float x)
    {
        const double s = (double) x + coeff * s1 - s2;
        s2 = s1; s1 = s;
    }
    // Estimated amplitude of this frequency over N processed samples.
    double amplitude(std::int64_t n) const
    {
        if (n <= 0) return 0.0;
        const double power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
        return (2.0 / (double) n) * std::sqrt(juce::jmax(0.0, power));
    }
    double coeff = 0.0, s1 = 0.0, s2 = 0.0;
};
} // namespace

//==============================================================================
int main(int argc, char** argv)
{
    const Args args = parseArgs(argc, argv);

    // 1. Stand up the bus + engine (no hardware device).
    MuLinkServerMemory mem;
    if (! mem.create())
    {
        std::cerr << "mu-link-harness: failed to create the shared-memory bus "
                     "(is a real mu-link already running?).\n";
        return 2;
    }
    ServerEngine engine;
    engine.attachMemory(&mem);
    engine.prepare((double) args.rate, args.block, 120.0);
    engine.setPlaying(true);

    // 2. Optional WAV writer.
    std::unique_ptr<juce::AudioFormatWriter> writer;
    if (args.out.isNotEmpty())
    {
        juce::File outFile(juce::File::getCurrentWorkingDirectory().getChildFile(args.out));
        if (juce::File::isAbsolutePath(args.out)) outFile = juce::File(args.out);
        outFile.deleteFile();
        std::unique_ptr<juce::OutputStream> stream = outFile.createOutputStream();
        if (stream != nullptr)
        {
            juce::WavAudioFormat wav;
            writer = wav.createWriterFor(stream,                 // takes ownership on success
                         juce::AudioFormatWriterOptions{}
                             .withSampleRate((double) args.rate)
                             .withNumChannels(2)
                             .withBitsPerSample(24));
        }
    }

    // 3. Producers.
    std::vector<std::unique_ptr<MuLinkClient>> internalClients;
    std::vector<std::shared_ptr<double>>       phases;
    for (double hz : args.internalTones)
    {
        auto client = std::make_unique<MuLinkClient>();
        auto phase  = std::make_shared<double>(0.0);
        phases.push_back(phase);
        client->onRender([hz, phase, &args] (float* const* out, int ch, int n, const TransportSnapshot&)
        {
            const double inc = 2.0 * juce::MathConstants<double>::pi * hz / (double) args.rate;
            for (int i = 0; i < n; ++i)
            {
                const float s = (float) (0.2 * std::sin(*phase));
                *phase += inc;
                if (*phase >= 2.0 * juce::MathConstants<double>::pi) *phase -= 2.0 * juce::MathConstants<double>::pi;
                for (int c = 0; c < ch; ++c) out[c][i] = s;
            }
        });
        if (client->attach("harness-tone", kMaxChannels))
            internalClients.push_back(std::move(client));
        else
            std::cerr << "mu-link-harness: internal tone client failed to attach.\n";
    }

    juce::OwnedArray<juce::ChildProcess> kids;
    for (const auto& cmd : args.spawnCmds)
    {
        auto* kid = new juce::ChildProcess();
        if (kid->start(cmd))
            kids.add(kid);
        else { std::cerr << "mu-link-harness: failed to spawn: " << cmd << "\n"; delete kid; }
    }

    // 4. Warm up so clients attach + fill their rings before we start measuring.
    juce::Thread::sleep((int) args.warmupMs);

    // 5. Real-time-paced capture loop. Pacing keeps the producers' render-ahead realistic;
    //    the deep ring absorbs sleep jitter so there are no spurious underruns.
    std::vector<Goertzel> detectors(args.expectHz.size());
    for (std::size_t k = 0; k < args.expectHz.size(); ++k)
        detectors[k].prepare(args.expectHz[k], (double) args.rate);

    juce::AudioBuffer<float> buf(2, args.block);
    const int    totalBlocks = juce::jmax(1, (int) std::ceil(args.seconds * args.rate / args.block));
    double       sumSq = 0.0, peak = 0.0;
    std::int64_t samplesAnalysed = 0;
    const double startMs = juce::Time::getMillisecondCounterHiRes();

    for (int b = 0; b < totalBlocks; ++b)
    {
        const double targetMs = startMs + (double) b * args.block / args.rate * 1000.0;
        const double now      = juce::Time::getMillisecondCounterHiRes();
        if (now < targetMs) juce::Thread::sleep((int) (targetMs - now));

        float* ch[2] = { buf.getWritePointer(0), buf.getWritePointer(1) };
        engine.renderBlock(ch, 2, args.block);

        if (writer) writer->writeFromAudioSampleBuffer(buf, 0, args.block);

        const float* L = buf.getReadPointer(0);
        for (int i = 0; i < args.block; ++i)
        {
            const float x = L[i];
            sumSq += (double) x * (double) x;
            peak   = juce::jmax(peak, (double) std::abs(x));
            for (auto& d : detectors) d.push(x);
            ++samplesAnalysed;
        }
    }

    writer.reset();   // flush + close the WAV

    // 6. Stop producers.
    for (auto& c : internalClients) c->detach();
    for (auto* kid : kids) kid->kill();

    // 7. Report which clients were seen + the measurements.
    const double rms = std::sqrt(sumSq / juce::jmax<std::int64_t>(1, samplesAnalysed));
    std::cout << "\n=== mu-link-harness report ===\n";
    std::cout << "captured " << args.seconds << " s @ " << args.rate << " Hz, block " << args.block << "\n";
    {
        auto& reg = mem.registry();
        std::cout << "clients seen:";
        bool any = false;
        for (int s = 0; s < kMaxClients; ++s)
            if (reg.slots[s].active.load() != 0)
            { std::cout << " [" << s << "] " << juce::String(reg.slots[s].name); any = true; }
        std::cout << (any ? "\n" : " (none still attached at end)\n");
    }
    if (args.out.isNotEmpty()) std::cout << "wrote: " << args.out << "\n";
    std::cout << "RMS=" << rms << "  peak=" << peak << "\n";

    bool pass = (rms >= args.minRms);
    if (rms < args.minRms)
        std::cout << "FAIL: RMS " << rms << " < min-rms " << args.minRms << " (silence?)\n";

    for (std::size_t k = 0; k < args.expectHz.size(); ++k)
    {
        const double amp = detectors[k].amplitude(samplesAnalysed);
        const bool   ok  = amp >= args.toneAmpThresh;
        pass = pass && ok;
        std::cout << (ok ? "ok   " : "FAIL ") << args.expectHz[k] << " Hz amplitude=" << amp
                  << " (thresh " << args.toneAmpThresh << ")\n";
    }

    std::cout << (pass ? "RESULT: PASS\n" : "RESULT: FAIL\n");
    return pass ? 0 : 1;
}
