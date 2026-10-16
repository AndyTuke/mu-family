// The Pattern layer with no processor at all (extraction-plan 4.1, the self-containment proof): this target is
// compiled from mu-core's layer-side sources, the mu-tant-layer library and this file, with no Plugin/ source.
// It builds a Pattern, prepares and binds it to a minimal host (a parameter tree, one slot engine and the bank),
// renders it with hand-built RenderContexts and compares the stream with the one mu-tant-tests records through
// the real processor (Tests/PatternLayerFixture.h). A layer that secretly needed the processor would fail to link
// here; one the processor fed differently would not match. It also checks the layer's own Note-mode gate, which
// the standalone leaves off (decision A3).

#include "Sequencer/Pattern.h"
#include "Persistence/PatternParams.h"
#include "UI/PatternHost.h"
#include "Audio/Wavetable/WavetableBank.h"   // mu-core: the shared bank (a service the host owns)
#include "Tests/PatternLayerFixture.h"

namespace
{
    // Just enough of an AudioProcessor to own a parameter tree for one slot of the Pattern type, and just enough
    // of a PatternHost to hand the layer its slot engine (the render needs nothing else from a host).
    struct LayerHost : juce::AudioProcessor, mu_tant::PatternHost
    {
        const juce::String getName() const override          { return "pattern-layer-host"; }
        void prepareToPlay(double, int) override              {}
        void releaseResources() override                      {}
        void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
        juce::AudioProcessorEditor* createEditor() override   { return nullptr; }
        bool hasEditor() const override                       { return false; }
        bool acceptsMidi() const override                     { return true; }
        bool producesMidi() const override                    { return false; }
        double getTailLengthSeconds() const override          { return 0.0; }
        int getNumPrograms() override                         { return 1; }
        int getCurrentProgram() override                      { return 0; }
        void setCurrentProgram(int) override                  {}
        const juce::String getProgramName(int) override       { return {}; }
        void changeProgramName(int, const juce::String&) override {}
        void getStateInformation(juce::MemoryBlock&) override {}
        void setStateInformation(const void*, int) override   {}

        static juce::AudioProcessorValueTreeState::ParameterLayout layout()
        {
            juce::AudioProcessorValueTreeState::ParameterLayout l;
            mu_tant::addPatternGlobalParams(l);
            mu_tant::addLayerParams(l, 0);
            return l;
        }
        juce::AudioProcessorValueTreeState apvts { *this, nullptr, "PatternLayer", layout() };
        mu_wavetable::WavetableBank bank;
        mu_tant::VoiceEngine engine;
        juce::CriticalSection bankLock;
        mu_tant::Pattern* layer = nullptr;

        // PatternHost: the layer's own needs; the UI's are not exercised here.
        juce::AudioProcessorValueTreeState& getPatternParameters() noexcept override { return apvts; }
        mu_tant::Pattern& getPatternLayer(int) noexcept override                     { return *layer; }
        float  getPatternModLive(int, int snap) const noexcept override              { return layer->getModLive(snap); }
        const std::atomic<float>* getPatternInsertGainReduction(int) const override  { return layer->insertGainReduction(); }
        bool   isPatternTransportPlaying() const noexcept override                   { return true; }
        double getPatternTransportBeat() const noexcept override                     { return 0.0; }
        juce::File   getPatternWavetablesDir() const override                        { return {}; }
        juce::String getPatternWavetablePath(int, int) const override                { return {}; }
        bool   isPatternWavetableMissing(int, int) const override                    { return false; }
        void   loadPatternWavetable(int, int, const juce::File&) override            {}
        void   clearPatternWavetable(int, int) override                              {}
        int    resolvePatternWavetable(const juce::String&) override                 { return -1; }
        mu_tant::VoiceEngine* getPatternEngine(int) noexcept override                { return &engine; }
        const juce::CriticalSection* getPatternBankLock() const noexcept override    { return &bankLock; }
    };

    // Renders kBlocks of the fixture through `pattern` with the contexts the shared driver builds for a playing host
    // block of kBlock frames at slot 0, the keys as the block's MIDI view.
    mu_tant_fixture::StreamHash renderFixture(mu_tant::Pattern& pattern, int blocks)
    {
        namespace fx = mu_tant_fixture;
        const double bps = (fx::kBpm / 60.0) / fx::kSampleRate;
        double ppq = 0.0;
        std::vector<mu_core::MidiEvent> events;
        juce::AudioBuffer<float> out(2, fx::kBlock);
        fx::StreamHash hash;
        for (int b = 0; b < blocks; ++b)
        {
            events.clear();
            for (const auto& k : fx::keys())
                if (k.block == b) events.push_back({ 0, 3, { k.status, k.note, k.velocity } });

            auto ctx = mu_core::makeRenderContext();
            ctx.sampleRate        = fx::kSampleRate;
            ctx.blockSize         = fx::kBlock;
            ctx.preparedBlockSize = fx::kBlock;
            ctx.slot              = 0;
            ctx.bpm               = fx::kBpm;
            ctx.startBeat         = ppq;
            ctx.beatsPerSample    = bps;
            ctx.callBeats         = bps * (double) fx::kBlock;
            ctx.beatWrap          = mu_tant::Pattern::kBeatSpace;
            ctx.playing           = 1;
            ctx.justStarted       = b == 0 ? 1 : 0;
            ctx.midi              = { events.data(), (int32_t) events.size(), 0, 0xFFFF, 0, 0, 0 };

            out.clear();
            pattern.render(ctx, out, fx::kBlock);
            hash.add(out, fx::kBlock);
            ppq += (double) fx::kBlock * fx::kBpm / (60.0 * fx::kSampleRate);   // as the host advances it
        }
        return hash;
    }

    struct Rig
    {
        Rig()
        {
            namespace fx = mu_tant_fixture;
            host.bank.loadFactoryBank();
            host.engine.prepare(fx::kSampleRate, fx::kBlock);
            host.engine.setBank(&host.bank);
            host.engine.setSlotIndex(0, 0);   // salt 0: the deterministic noise the recorded stream used
            host.layer = &pattern;
            pattern.host.ptr = &host;
            pattern.insert->setSlotIndex(0);
            pattern.prepare(fx::kSampleRate, fx::kBlock);
            pattern.bindParams(host.apvts, 0);
            pattern.initNoteMode(1);
            fx::configure(host.apvts, pattern, 0);
        }
        LayerHost host;
        mu_tant::Pattern pattern;
    };
}

struct MuTantLayerLibraryTests : juce::UnitTest
{
    MuTantLayerLibraryTests() : juce::UnitTest("mu-Tant Pattern layer without a processor", "mu-tant-layer") {}

    void runTest() override
    {
        namespace fx = mu_tant_fixture;

        beginTest("A Pattern built, prepared, bound and rendered from hand-built contexts gives the recorded stream");
        {
            Rig rig;
            const auto hash = renderFixture(rig.pattern, fx::kBlocks);
            logMessage("Pattern layer stream hash " + juce::String::toHexString((juce::int64) hash.h));
            expect(hash.h == fx::kRecordedHash, "the layer alone renders the stream the processor records");
            expect(hash.peak > 0.01f && hash.peak < 4.0f, "and it is a real, bounded sound");
        }

        beginTest("The layer's own Note-mode gate (off in the standalone): shut before the first key, open while held, shut after the release");
        {
            Rig gated;
            gated.pattern.gatesOwnOutput = true;
            juce::AudioBuffer<float> out(2, fx::kBlock);
            const double bps = (fx::kBpm / 60.0) / fx::kSampleRate;
            float peakBefore = 0.0f, peakHeld = 0.0f, peakAfter = 0.0f;
            std::vector<mu_core::MidiEvent> events;
            for (int b = 0; b < 140; ++b)
            {
                events.clear();
                for (const auto& k : fx::keys())
                    if (k.block == b) events.push_back({ 0, 3, { k.status, k.note, k.velocity } });
                auto ctx = mu_core::makeRenderContext();
                ctx.sampleRate = fx::kSampleRate; ctx.blockSize = fx::kBlock; ctx.preparedBlockSize = fx::kBlock;
                ctx.bpm = fx::kBpm; ctx.startBeat = bps * (double) (b * fx::kBlock); ctx.beatsPerSample = bps;
                ctx.callBeats = bps * (double) fx::kBlock; ctx.playing = 1;
                ctx.midi = { events.data(), (int32_t) events.size(), 0, 0xFFFF, 0, 0, 0 };
                gated.pattern.setNoteMode(1);
                gatedBypass(gated);
                out.clear();
                gated.pattern.render(ctx, out, fx::kBlock);
                const float pk = juce::jmax(out.getMagnitude(0, 0, fx::kBlock), out.getMagnitude(1, 0, fx::kBlock));
                if (b < 4)                 peakBefore = juce::jmax(peakBefore, pk);
                else if (b >= 10 && b < 90) peakHeld  = juce::jmax(peakHeld, pk);
                else if (b >= 92)          peakAfter  = juce::jmax(peakAfter, pk);
            }
            logMessage("own gate: peak before the key " + juce::String(peakBefore) + ", held " + juce::String(peakHeld) + ", after " + juce::String(peakAfter));
            expectEquals(peakBefore, 0.0f, "no key held: exactly silent");
            expect(peakHeld > 0.01f, "a key held: the layer sounds");
            expectEquals(peakAfter, 0.0f, "after the last release and the 8 ms ramp: exactly silent");
        }
    }

    // The gater open, so the own-output gate is the only thing that silences the layer.
    static void gatedBypass(Rig& r)
    {
        if (auto* p = r.host.apvts.getParameter(mu_tant::layerParamId(0, "gate_bypass")))
            if (p->getValue() < 0.5f) p->setValueNotifyingHost(1.0f);
    }
};

static MuTantLayerLibraryTests muTantLayerLibraryTests;
