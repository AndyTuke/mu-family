// Structural edits on the REAL mu-Clid PluginProcessor, from the audio thread's side:
//  - continuity: a worker calls processBlock at roughly host pace while the test thread adds, reorders, resets
//    and deletes rhythms; the parameter push of an edit must not silence the mix (only the short locked
//    phase may), and the window the in-flight flag stays up is measured and logged;
//  - modulation memory: a slot whose tenant changed starts the modulation change-detection clean, so a
//    modulated tenant that moved re-runs its pass and a reset slot does not keep the old tenant's overrides.

#include "Plugin/PluginProcessor.h"
#include "Tests/ProcessorSeamHelpers.h"

#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>

namespace mu_clid
{
    // Befriended by PluginProcessor: reads (and for the retry case, sets) the per-slot change-detection state.
    struct ModulationMemoryProbe
    {
        static bool            prevHad (PluginProcessor& p, int i) { return p.prevMatrixHadAssignments[(size_t) i]; }
        static EuclidOverrides last    (PluginProcessor& p, int i) { return p.lastEuclidOverrides[(size_t) i]; }
        static EuclidOverrides prev    (PluginProcessor& p, int i) { return p.prevEuclidOverrides[(size_t) i]; }

        // A pending retry: the last block's overrides were not applied to the pattern yet (message thread, quiet).
        static void makeRetryPending(PluginProcessor& p, int i)
        {
            p.prevEuclidOverrides[(size_t) i].a.hits = p.lastEuclidOverrides[(size_t) i].a.hits + 3;
        }
    };
}

namespace
{
    using namespace mu_seam_test;
    using Clock = std::chrono::steady_clock;

    struct EditProcessor : mu_clid::PluginProcessor
    {
        using mu_clid::PluginProcessor::drainDeferredWork;
    };

    juce::File presetsDir()
    {
        auto dir = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
        for (int i = 0; i < 10 && dir.exists(); ++i, dir = dir.getParentDirectory())
            if (dir.getChildFile("tests").getChildFile("presets").isDirectory())
                return dir.getChildFile("tests").getChildFile("presets");
        return {};
    }

    // A long, steady tone: every hit sustains for seconds, so a hit never leaves a gap by itself.
    void writeSample(EditProcessor& p, const juce::String& name, double freq)
    {
        auto f = p.getSamplesDir().getChildFile(name);
        f.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> out = f.createOutputStream();
        if (out == nullptr) return;
        auto w = wav.createWriterFor(out, juce::AudioFormatWriterOptions{}.withSampleRate(44100.0)
                                                                          .withNumChannels(1)
                                                                          .withBitsPerSample(16));
        if (w == nullptr) return;
        juce::AudioBuffer<float> b(1, 44100 * 4);
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample(0, i, 0.4f * (float) std::sin(juce::MathConstants<double>::twoPi * freq * i / 44100.0));
        w->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
    }

    // Stands in for a host's own parameter-change handling (automation lanes, UI): every value pushed
    // costs a little time, so a parameter push is as slow as it is in a DAW.
    struct SlowHostListener : juce::AudioProcessorParameter::Listener
    {
        void parameterValueChanged(int, float) override
        {
            const auto until = Clock::now() + std::chrono::microseconds(80);
            while (Clock::now() < until) {}
        }
        void parameterGestureChanged(int, bool) override {}
    };

    // Calls processBlock at about host pace on a worker; for each block it records the peak and whether the
    // in-flight flag was up, and in the gaps between blocks it polls the flag to time its windows.
    class PacedRenderer
    {
    public:
        PacedRenderer(EditProcessor& proc, int blockSize) : p(proc), size(blockSize)
        {
            peaks.reserve(kMaxBlocks);
            flagged.reserve(kMaxBlocks);
            windowsMs.reserve(256);
        }
        ~PacedRenderer() { stop(); }

        void start()
        {
            worker = std::thread([this]
            {
                juce::AudioBuffer<float> buf(2, size);
                juce::MidiBuffer midi;
                bool wasUp = false;
                Clock::time_point upSince;
                auto notePoll = [&]
                {
                    const bool up = p.layerEditInFlight.load(std::memory_order_acquire);
                    if (up && ! wasUp) upSince = Clock::now();
                    if (! up && wasUp && windowsMs.size() < windowsMs.capacity())
                        windowsMs.push_back(std::chrono::duration<double, std::milli>(Clock::now() - upSince).count());
                    wasUp = up;
                    return up;
                };
                while (! stopFlag.load(std::memory_order_acquire) && (int) peaks.size() < kMaxBlocks)
                {
                    const bool upAtStart = notePoll();
                    buf.clear();
                    midi.clear();
                    p.processBlock(buf, midi);
                    peaks.push_back(buf.getMagnitude(0, size));
                    flagged.push_back(upAtStart);
                    published.store((int) peaks.size(), std::memory_order_release);
                    const auto until = Clock::now() + std::chrono::milliseconds(4);
                    while (Clock::now() < until)
                    {
                        notePoll();
                        std::this_thread::yield();   // spin so a microsecond window is still seen
                    }
                }
                done.store(true, std::memory_order_release);
            });
        }

        void stop()
        {
            if (! worker.joinable()) return;
            stopFlag.store(true, std::memory_order_release);
            worker.join();
        }

        int blocks() const { return published.load(std::memory_order_acquire); }
        void waitBlocks(int n) const
        {
            const int target = blocks() + n;
            const auto deadline = Clock::now() + std::chrono::seconds(30);
            while (blocks() < target && ! done.load(std::memory_order_acquire) && Clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        // Longest run of near-silent blocks in [from, to).
        int longestSilentRun(int from, int to) const
        {
            int best = 0, run = 0;
            for (int i = from; i < to && i < (int) peaks.size(); ++i)
            {
                run = peaks[(size_t) i] < 1.0e-6f ? run + 1 : 0;
                best = std::max(best, run);
            }
            return best;
        }

        static constexpr int kMaxBlocks = 6000;
        EditProcessor& p;
        const int size;
        std::vector<float> peaks;
        std::vector<bool>  flagged;
        std::vector<double> windowsMs;
        std::atomic<int>  published { 0 };
        std::atomic<bool> stopFlag { false }, done { false };
        std::thread       worker;
    };
}

struct ClidEditContinuityTests : juce::UnitTest
{
    ClidEditContinuityTests() : juce::UnitTest("mu-Clid edit continuity and modulation memory", "mu-clid-processor") {}

    void runTest() override
    {
        continuityDuringEdits();
        modulationMemoryAfterRemove();
        modulationMemoryAfterReset();
    }

    void continuityDuringEdits()
    {
        beginTest("add, delete, reorder and reset mid-play do not drop blocks beyond the locked phase");
        const auto presets = presetsDir();
        expect(presets.isDirectory(), "tests/presets found");
        if (! presets.isDirectory()) return;

        TempStorage storage;
        EditProcessor p;
        constexpr int kBlock = 512;
        p.prepareToPlay(48000.0, kBlock);
        RunningPlayHead host(48000.0, kBlock);
        p.setPlayHead(&host);
        p.getSamplesDir().createDirectory();
        writeSample(p, "Hat 01.wav", 5200.0);
        writeSample(p, "Kick 01.wav", 70.0);
        writeSample(p, "Synth C 01.wav", 261.6);

        p.loadPreset(presets.getChildFile("A3_filter.muClid"));
        p.addLayer();
        p.addLayer();
        // Slot 0 is the guard: T13 sounds without a gap (its notes overlap, the mix never falls below about
        // 0.4 while it plays), and no edit below touches slot 0. So the whole mix can only go quiet if an
        // edit silences it. The other slots rest between hits (T11) or are new and empty (no sample yet),
        // so they are the ones the edits move, delete and reset.
        p.applyRhythmPreset(presets.getChildFile("T13.muRhythm"), 0);
        p.applyRhythmPreset(presets.getChildFile("T11.muRhythm"), 1);

        SlowHostListener hostListener;
        for (auto* prm : p.getParameters()) prm->addListener(&hostListener);

        PacedRenderer r(p, kBlock);
        r.start();
        r.waitBlocks(120);
        const int editsFrom = r.blocks();

        auto settle = [&] { p.drainDeferredWork(); r.waitBlocks(12); };
        std::vector<double> editMs;
        auto timed = [&](auto&& edit)
        {
            const auto t0 = Clock::now();
            edit();
            editMs.push_back(std::chrono::duration<double, std::milli>(Clock::now() - t0).count());
            settle();
        };
        timed([&] { p.addLayer(); });                  // add
        timed([&] { p.swapLayers(1, 3); });            // reorder
        timed([&] { p.removeLayer(2); });              // delete
        timed([&] { p.resetLayer(2); });               // reset
        timed([&] { p.addLayer(); });
        timed([&] { p.swapLayers(1, 3); });
        timed([&] { p.removeLayer(1); });
        timed([&] { p.addLayer(); });
        r.waitBlocks(20);
        const int editsTo = r.blocks();
        r.stop();
        for (auto* prm : p.getParameters()) prm->removeListener(&hostListener);
        p.setPlayHead(nullptr);

        const int natural = r.longestSilentRun(20, editsFrom);
        const int during  = r.longestSilentRun(editsFrom, editsTo);
        int flaggedBlocks = 0;
        for (int i = editsFrom; i < editsTo; ++i) flaggedBlocks += r.flagged[(size_t) i] ? 1 : 0;
        const double widest = r.windowsMs.empty() ? 0.0 : *std::max_element(r.windowsMs.begin(), r.windowsMs.end());
        const double slowestEdit = *std::max_element(editMs.begin(), editMs.end());

        logMessage("blocks " + juce::String(editsTo) + ", natural silent run " + juce::String(natural)
                   + ", silent run during the edits " + juce::String(during)
                   + ", blocks that began with the flag up " + juce::String(flaggedBlocks));
        logMessage("in-flight flag windows: " + juce::String((int) r.windowsMs.size()) + " seen, widest "
                   + juce::String(widest, 2) + " ms; slowest whole edit " + juce::String(slowestEdit, 2) + " ms (block = "
                   + juce::String(1000.0 * kBlock / 48000.0, 2) + " ms)");

        expect(editsTo - editsFrom > 80, "the worker kept rendering through the edits");
        expect(natural < 4, "the guard slot leaves no natural gap, so a silent block is a dropped one");
        expect(during <= natural + 2, "no run of silent blocks longer than the locked phase during the edits");
        // The locked phase is microseconds; a flag held through the parameter push (ms) shows as many flagged blocks.
        expect(flaggedBlocks <= 1, "almost no block begins with the in-flight flag up");
        expect(widest < 1000.0 * kBlock / 48000.0, "the flag window is shorter than one block");
    }

    static void setUpModulated(EditProcessor& p, int slot)
    {
        auto& rhythm = p.layerAs<mu_clid::Rhythm>(slot);
        // Single-threaded here: no audio thread is running, so the modulation lock is not needed.
        rhythm.controlSequences[0].stepValues = { 100.0f, 100.0f, 100.0f, 100.0f };
        ModulationAssignment a;
        a.id = "retry" + std::to_string(slot);
        a.sourceId = "cs0_output";
        a.destinationId = "euclid.a.hits";
        a.depth = 100.0f;
        rhythm.modulationMatrix.addAssignment(a);
    }

    static void runBlocks(EditProcessor& p, int n, int blockSize)
    {
        juce::AudioBuffer<float> buf(2, blockSize);
        juce::MidiBuffer midi;
        for (int i = 0; i < n; ++i)
        {
            buf.clear();
            midi.clear();
            p.processBlock(buf, midi);
        }
    }

    void modulationMemoryAfterRemove()
    {
        beginTest("a modulated tenant that moved on a delete re-runs its pass instead of inheriting the old slot's memory");
        using Probe = mu_clid::ModulationMemoryProbe;
        TempStorage storage;
        EditProcessor p;
        constexpr int kBlock = 256;
        p.prepareToPlay(48000.0, kBlock);
        RunningPlayHead host(48000.0, kBlock);
        p.setPlayHead(&host);
        p.addLayer();
        p.addLayer();

        setUpModulated(p, 2);
        runBlocks(p, 8, kBlock);
        expect(Probe::prevHad(p, 2), "the modulated tenant ran its pass at slot 2");
        const auto modulated = Probe::last(p, 2);
        expect(modulated != mu_clid::EuclidOverrides{}, "and wrote overrides");
        Probe::makeRetryPending(p, 2);                       // the last pattern recompute did not land yet
        expect(Probe::prev(p, 2) != Probe::last(p, 2));

        expect(p.removeLayer(1));                            // the neighbour goes; the tenant moves to slot 1
        for (int slot : { 1, 2 })
        {
            expect(! Probe::prevHad(p, slot), "slot " + juce::String(slot) + ": the previous-pass flag is clean");
            expect(Probe::prev(p, slot) == mu_clid::EuclidOverrides{} && Probe::last(p, slot) == mu_clid::EuclidOverrides{},
                   "slot " + juce::String(slot) + ": the overrides are back to defaults, equal, so nothing pends");
        }

        runBlocks(p, 6, kBlock);
        expect(Probe::prevHad(p, 1), "the moved tenant ran its pass again");
        expect(Probe::last(p, 1) == modulated, "with the same modulated overrides");
        expect(Probe::prev(p, 1) == Probe::last(p, 1), "and its pattern recompute was applied (no retry left pending)");
        p.setPlayHead(nullptr);
    }

    void modulationMemoryAfterReset()
    {
        beginTest("a reset slot does not keep the old tenant's modulated overrides, and plays its base pattern");
        using Probe = mu_clid::ModulationMemoryProbe;
        TempStorage storage;
        EditProcessor p;
        constexpr int kBlock = 256;
        p.prepareToPlay(48000.0, kBlock);
        RunningPlayHead host(48000.0, kBlock);
        p.setPlayHead(&host);
        p.addLayer();

        setUpModulated(p, 1);
        runBlocks(p, 8, kBlock);
        expect(Probe::prevHad(p, 1));
        Probe::makeRetryPending(p, 1);

        p.resetLayer(1);
        expect(p.layerAs<mu_clid::Rhythm>(1).modulationMatrix.getAssignments().empty(), "the reset rhythm has no assignments");
        expect(! Probe::prevHad(p, 1) && Probe::prev(p, 1) == mu_clid::EuclidOverrides{} && Probe::last(p, 1) == mu_clid::EuclidOverrides{},
               "the change-detection memory is clean");
        runBlocks(p, 6, kBlock);
        expect(! Probe::prevHad(p, 1) && Probe::prev(p, 1) == Probe::last(p, 1), "and stays idle: no stale retry rewrites the pattern");
        expectEquals(p.sequencer.getPatternLength(1), p.layerAs<mu_clid::Rhythm>(1).genA.steps, "the pattern is the base one");
        p.setPlayHead(nullptr);
    }
};

static ClidEditContinuityTests clidEditContinuityTests;
