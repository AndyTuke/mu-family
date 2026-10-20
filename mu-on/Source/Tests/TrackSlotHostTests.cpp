// The five Track types in a typed-slot host (docs/mu-tronic/design.md 2.1.11): the real Track class created through the
// registry, locked to its type's engine, set up by completeSetup, left through a Fade tail, and its presets found by
// the type-aware folder listing. The host is a minimal Published-mode ProcessorBase (the shape mu-Tronic gives them),
// so these run before mu-Tronic registers a Track type.

#include "Plugin/ProcessorBase.h"
#include "Plugin/MixerFxParams.h"
#include "Sequencer/TrackTypes.h"
#include "Tests/GoldenRender.h"         // mu-core: MockHost
#include "Tests/ProcessorSeamHelpers.h"

#include <array>
#include <cmath>
#include <cstring>

namespace
{
    using mu_core::LayerTypeId;
    using mu_on::EngineType;

    // A ProcessorBase with eight slots, all Empty to start, that offers the five Track types.
    struct SlotHost : ProcessorBase
    {
        SlotHost()
            : ProcessorBase(BusesProperties().withOutput("Out", juce::AudioChannelSet::stereo()), makeLayout())
        {
            setLayerAccess(mu_core::LayerAccess::Published);
            keepSpareLayers = true;
            for (const auto* info : { &mu_on::kKickType, &mu_on::kBassType, &mu_on::kHatType, &mu_on::kSnareType, &mu_on::kRumbleType })
                layerTypes.add(*info);
            std::vector<juce::StringArray> sets;
            for (int s = 0; s < mu_limits::kMaxLayers; ++s) sets.push_back(juce::StringArray { mu_on::layerParamPrefix(s) });
            initLayerStatePrefixSets(sets);
            initAppSettings("muTronicTest");   // a content folder, so the typed preset folders resolve
            for (int i = 0; i < mu_limits::kMaxLayers; ++i)
            {
                layers[(size_t) i] = std::make_unique<Layer>();
                layers[(size_t) i]->colourIndex = i;
                layers[(size_t) i]->bindParams(apvts, i);
            }
            publishLayers(kLive);
            startFxParamSync();
        }
        ~SlotHost() override
        {
            prepareForDestruction();
            for (auto& l : layers) l.reset();
        }

        static constexpr int kLive = 4;

        static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
        {
            juce::AudioProcessorValueTreeState::ParameterLayout layout;
            for (int i = 0; i < mu_limits::kMaxLayers; ++i)
            {
                mu_on::addLayerParams(layout, i);
                mu_mixfx::addChannelStripParams(layout, "ch" + juce::String(i) + "_", "Ch ");
            }
            return layout;
        }

        mu_on::Track& track(int slot) { return static_cast<mu_on::Track&>(*layers[(size_t) slot]); }

        using ProcessorBase::retireBin;
        using ProcessorBase::flushPendingAsyncUpdates;
        using ProcessorBase::typedSlotPresetFiles;
        using ProcessorBase::typedLayerPresetDir;
        using ProcessorBase::typedLayerPresetExtension;
        using ProcessorBase::presetDirOf;

        bool layersAreSlots() const override { return true; }
        std::unique_ptr<Layer> createLayer(int) override { return std::make_unique<Layer>(); }
        juce::String getLayerName(int) const override { return {}; }
        int getLayerColourIndex(int i) const override { return layers[(size_t) i] ? layers[(size_t) i]->colourIndex : 0; }
        juce::File   getLayerPresetDir()       const override { return {}; }
        juce::String getLayerPresetExtension() const override { return ".muTrack"; }
        juce::String getFullPresetExtension()  const override { return ".muTest"; }
        void applyFullMidiPreset(const juce::File&) override {}

        void prepareToPlay(double sr, int bs) override
        {
            mixerEngine.prepare(sr, bs);
            fxChain.prepare(sr, bs);
            prepareLayerRender(sr, bs);
        }
        void releaseResources() override { releaseLayerRender(); }
        void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
        {
            juce::ScopedNoDenormals noDenormals;
            const auto t = resolveBlockTransport(midi, buffer.getNumSamples());
            renderAndMix(buffer, midi, t);
            flagLayerSwapBoundaries(t);
        }

        const juce::String getName() const override          { return "track-slot-host"; }
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
    };

    // Appends each channel's stream as the mixer receives it (sized up front: nothing allocates while rendering).
    struct AppendTap : MixerEngine::ChannelTap
    {
        explicit AppendTap(int frames) { for (auto& b : bufs) b.setSize(2, frames); written.fill(0); }
        void capture(int ch, const juce::AudioBuffer<float>& buf, int n) noexcept override
        {
            if (ch < 0 || ch >= MixerEngine::MaxChannels) return;
            auto& dst = bufs[(size_t) ch];
            const int at = written[(size_t) ch];
            const int len = juce::jmin(n, dst.getNumSamples() - at);
            for (int c = 0; c < 2 && c < buf.getNumChannels(); ++c)
                if (len > 0) juce::FloatVectorOperations::copy(dst.getWritePointer(c) + at, buf.getReadPointer(c), len);
            written[(size_t) ch] = at + juce::jmax(0, len);
        }
        float peak(int ch, int block, int blockSize) const
        {
            return bufs[(size_t) ch].getMagnitude(block * blockSize, blockSize);
        }
        std::array<juce::AudioBuffer<float>, MixerEngine::MaxChannels> bufs;
        std::array<int, MixerEngine::MaxChannels> written {};
    };

    // Plays a SlotHost at 120 bpm, 48 kHz, blocks of 512, with the channel tap attached.
    struct Run
    {
        static constexpr int kBlock = 512;
        static constexpr double kRate = 48000.0;
        explicit Run(SlotHost& host, int blocks) : p(host), tap(blocks * kBlock)
        {
            p.setPlayHead(&mock);
            p.setRateAndBufferSizeDetails(kRate, kBlock);
            p.prepareToPlay(kRate, kBlock);
            p.mixerEngine.channelTap = &tap;
            work.setSize(2, kBlock);
        }
        ~Run()
        {
            p.mixerEngine.channelTap = nullptr;
            p.setPlayHead(nullptr);
            p.releaseResources();
        }
        void block()
        {
            work.clear();
            midi.clear();
            p.processBlock(work, midi);
            mock.ppq += (double) kBlock * mock.bpm / (60.0 * kRate);
            mock.timeInSamples += kBlock;
        }
        SlotHost& p;
        AppendTap tap;
        mu_golden::MockHost mock;
        juce::AudioBuffer<float> work;
        juce::MidiBuffer midi;
    };

    float plain(juce::AudioProcessorValueTreeState& apvts, const juce::String& id)
    {
        auto* p = apvts.getParameter(id);
        return p->convertFrom0to1(p->getValue());
    }

    void setRow(mu_on::Track& t, std::initializer_list<int> steps)
    {
        t.row.clear();
        for (int s : steps) t.row.setOn(s, true);
    }

    // A track preset tree as a type's presets are saved: the engine row names the type.
    juce::ValueTree presetOf(EngineType engine)
    {
        juce::ValueTree preset("MuOnTrack");
        juce::ValueTree row(mu_pp::kRowTag);
        row.setProperty("id", "eng", nullptr);
        row.setProperty("x", mu_on::toIndex(engine), nullptr);
        preset.appendChild(row, nullptr);
        return preset;
    }
}

struct MuOnTrackSlotHostTests : juce::UnitTest
{
    MuOnTrackSlotHostTests() : juce::UnitTest("mu-On Track types in a typed-slot host", "mu-on-processor") {}

    void runTest() override
    {
        setupThroughTheRegistry();
        sameDataSwapFadeTail();
        rumbleFadeTail();
        typedPresetFolder();
        selectedSlot();
    }

    // setLayerType builds the real Track through the registry (locked) and completeSetup runs once, after the bind.
    void setupThroughTheRegistry()
    {
        beginTest("setLayerType creates a locked Track; completeSetup writes t{N}_eng to its engine and builds only that engine");
        mu_seam_test::TempStorage storage;
        SlotHost p;
        mu_seam_test::setParam(p.apvts, "t2_eng", 4.0f);   // a stray value left in the slot's row
        expect(p.setLayerType(2, LayerTypeId::Hat));
        auto& t = p.track(2);
        expect(t.engineLocked && t.engineSetting() == EngineType::Hat && t.typeId() == (int) LayerTypeId::Hat, "a locked Hat");
        expectEquals((int) std::lround(plain(p.apvts, "t2_eng")), mu_on::toIndex(EngineType::Hat), "the row names the engine");
        for (int e = 0; e < mu_on::kNumEngineTypes; ++e)
            expect(t.holdsEngine(mu_on::engineFromIndex(e)) == (e == mu_on::toIndex(EngineType::Hat)), "only the Hat is built, engine " + juce::String(e));

        // Hat to Bass: the same t2_ prefix, a different type; the row follows.
        expect(p.setLayerType(2, LayerTypeId::Bass));
        expect(p.track(2).engineSetting() == EngineType::Bass && p.track(2).holdsEngine(EngineType::Bass), "now a Bass");
        expectEquals((int) std::lround(plain(p.apvts, "t2_eng")), mu_on::toIndex(EngineType::Bass), "and the row follows");

        // A stray write cannot change the type.
        mu_seam_test::setParam(p.apvts, "t2_eng", 0.0f);
        expect(p.track(2).typeId() == (int) LayerTypeId::Bass, "the parameter is only a record");
        p.flushPendingAsyncUpdates();
    }

    // A Kick leaves for a Bass in the same prefix: the Kick keeps ringing from what it was last given, fading in 25 ms.
    void sameDataSwapFadeTail()
    {
        beginTest("Kick to Bass (the same t0_ prefix): the Kick's tail sounds out under the fade on its own parameters, then the bin drains");
        constexpr int kBlocks = 70;
        mu_seam_test::TempStorage storage;
        SlotHost p;
        expect(p.setLayerType(0, LayerTypeId::Kick));
        setRow(p.track(0), { 0, 4, 8, 12 });
        // A short decay (20 ms): the swap resets t0_kick_adec to its 180 ms default, so a tail that read the parameter
        // would ring slower than the engine it was handed.
        mu_seam_test::setParam(p.apvts, mu_on::engineParamId(0, EngineType::Kick, "adec"), 20.0f);
        Run run(p, kBlocks);
        for (int b = 0; b < 47; ++b) run.block();   // the second beat's hit lands in block 46
        expect(run.tap.peak(0, 46, Run::kBlock) > 0.1f, "the Kick is sounding at the swap");

        expect(p.setLayerType(0, LayerTypeId::Bass));
        expect(plain(p.apvts, mu_on::engineParamId(0, EngineType::Kick, "adec")) > 100.0f, "the swap reset the slot's rows to defaults");
        expect(p.retireBin.countIn(mu_core::RetireBin::Parked) + p.retireBin.countIn(mu_core::RetireBin::Draining) > 0, "the Kick is parked");
        for (int b = 47; b < kBlocks; ++b) run.block();

        const float first = run.tap.peak(0, 47, Run::kBlock);
        const float third = run.tap.peak(0, 49, Run::kBlock);
        logMessage("kick tail peaks: block 47 = " + juce::String(first, 4) + ", block 49 = " + juce::String(third, 4));
        expect(first > 0.1f, "the first block after the swap carries the tail");
        expect(third < 0.09f * first, "it decays on the 20 ms envelope the engine was given, under the fade, not on the reset default");
        expectEquals(run.tap.peak(0, 51, Run::kBlock), 0.0f, "the 25 ms fade is spent: the new (empty) Bass is all that is left");
        expectEquals(run.tap.peak(0, kBlocks - 1, Run::kBlock), 0.0f, "and it stays silent");
        p.flushPendingAsyncUpdates();
        expectEquals(p.retireBin.countIn(mu_core::RetireBin::Parked) + p.retireBin.countIn(mu_core::RetireBin::Draining), 0, "the bin drained");
        expectEquals(p.retireBin.reclaimPending(), 0, "the old layer is freed");
    }

    // A Rumble has no peer in a tail: it processes the silence buffer, so its delays and reverb ring out and fade.
    void rumbleFadeTail()
    {
        beginTest("a Rumble leaves: its delay and reverb tail fades in 25 ms and the Kick it processed is untouched");
        constexpr int kBlocks = 70;
        auto play = [&](bool swap, AppendTap*& tapOut, std::unique_ptr<SlotHost>& host, std::unique_ptr<Run>& run)
        {
            host = std::make_unique<SlotHost>();
            auto& p = *host;
            expect(p.setLayerType(0, LayerTypeId::Kick));
            expect(p.setLayerType(1, LayerTypeId::Rumble));
            setRow(p.track(0), { 0, 4, 8, 12 });
            mu_seam_test::setParam(p.apvts, "t1_src", 0.0f);
            mu_seam_test::setParam(p.apvts, mu_on::engineParamId(1, EngineType::Rumble, "revmix"), 1.0f);
            run = std::make_unique<Run>(p, kBlocks);
            for (int b = 0; b < 47; ++b) run->block();
            if (swap) expect(p.setLayerType(1, LayerTypeId::Hat));
            for (int b = 47; b < kBlocks; ++b) run->block();
            tapOut = &run->tap;
        };
        std::unique_ptr<SlotHost> hostA, hostB;
        std::unique_ptr<Run> runA, runB;
        AppendTap* tapA = nullptr; AppendTap* tapB = nullptr;
        play(true, tapA, hostA, runA);
        play(false, tapB, hostB, runB);

        expect(tapB->peak(1, 46, Run::kBlock) > 0.0f, "the Rumble is sounding before the swap (reference run)");
        expect(tapA->peak(1, 47, Run::kBlock) > 0.0f, "the first block after the swap carries the Rumble's tail");
        expectEquals(tapA->peak(1, 51, Run::kBlock), 0.0f, "the fade is spent");
        expectEquals(tapA->peak(1, kBlocks - 1, Run::kBlock), 0.0f, "and the slot stays silent (the new Hat has no steps)");
        int differing = 0;
        for (int c = 0; c < 2; ++c)
            differing += std::memcmp(tapA->bufs[0].getReadPointer(c), tapB->bufs[0].getReadPointer(c),
                                     sizeof(float) * (size_t) (kBlocks * Run::kBlock)) != 0 ? 1 : 0;
        expectEquals(differing, 0, "the Kick on slot 0 is bit-identical with and without the neighbour's swap");
        hostA->flushPendingAsyncUpdates();
        expectEquals(hostA->retireBin.countIn(mu_core::RetireBin::Parked) + hostA->retireBin.countIn(mu_core::RetireBin::Draining), 0, "the bin drained");
    }

    // Five types share .muTrack in one folder: a filled slot lists only its own type's files, an Empty slot lists them all.
    void typedPresetFolder()
    {
        beginTest("typedSlotPresetFiles: files are told apart by the engine they saved; an Empty slot lists every type's, an unreadable file none");
        mu_seam_test::TempStorage storage;
        SlotHost p;
        const auto dir = p.presetDirOf(mu_on::kKickType);
        expect(dir.getParentDirectory().getFileName() == "muOn" && dir.getFileName() == "Tracks", "<content>/muOn/Tracks: " + dir.getFullPathName());
        dir.createDirectory();

        auto save = [&dir](const juce::String& fileName, const juce::ValueTree& tree)
        {
            const auto f = dir.getChildFile(fileName);
            if (auto xml = tree.createXml()) xml->writeTo(f);
            return f;
        };
        const auto kick  = save("a-kick.muTrack",  presetOf(EngineType::Kick));
        const auto kick2 = save("b-kick.muTrack",  presetOf(EngineType::Kick));
        const auto hat   = save("c-hat.muTrack",   presetOf(EngineType::Hat));
        const auto rum   = save("d-rumble.muTrack", presetOf(EngineType::Rumble));
        dir.getChildFile("e-broken.muTrack").replaceWithText("not xml at all");
        save("f-other.muArp", presetOf(EngineType::Kick));   // another extension: never listed here

        expect(p.setLayerType(0, LayerTypeId::Kick));
        expect(p.setLayerType(1, LayerTypeId::Hat));
        auto kicks = p.typedSlotPresetFiles(0);
        expect(kicks.size() == 2 && kicks.contains(kick) && kicks.contains(kick2), "a Kick slot lists the two Kick files");
        auto hats = p.typedSlotPresetFiles(1);
        expect(hats.size() == 1 && hats.contains(hat), "a Hat slot lists the Hat file");
        expect(p.typedSlotPresetFiles(2).size() == 4 && p.typedSlotPresetFiles(2).contains(rum), "an Empty slot lists every readable Track file once");
        expect(! p.typedSlotPresetFiles(0).contains(dir.getChildFile("e-broken.muTrack")), "the unreadable file maps to no type");

        expectEquals(p.typedLayerPresetExtension(0), juce::String("muTrack"));
        expect(p.typedLayerPresetDir(0) == dir, "the slot's folder is the type's");
        expect(p.typedLayerPresetDir(3) == juce::File(), "an Empty slot has no single folder");
    }

    void selectedSlot()
    {
        beginTest("setSelectedSlot stores the shown slot, clamped to the layer range");
        mu_seam_test::TempStorage storage;
        SlotHost p;
        expectEquals(p.getSelectedSlot(), 0);
        p.setSelectedSlot(3);
        expectEquals(p.getSelectedSlot(), 3);
        p.setSelectedSlot(-5);
        expectEquals(p.getSelectedSlot(), 0);
        p.setSelectedSlot(99);
        expectEquals(p.getSelectedSlot(), mu_limits::kMaxLayers - 1);
    }
};

static MuOnTrackSlotHostTests sMuOnTrackSlotHostTests;
