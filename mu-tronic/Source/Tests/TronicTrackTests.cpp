// The Kick, Bass, Hat and Snare layer types in a mu-Tronic slot (docs/mu-tronic/design.md 2.1.11): their rows are in the
// 16-slot layout under the shared t{N}_ prefix and saved only for a slot that holds one; a locked Track renders the
// stream mu-On records for the same sound; a Kick swapped for a Bass fades out with nothing stuck and no click; a
// restore builds each Track's engine; and their presets come from mu-On's Tracks folder, told apart by engine.

#include "Plugin/PluginProcessor.h"
#include "Persistence/TrackLayerTypes.h"   // chooseRumbleSource
#include "Sequencer/Track.h"             // mu-On's layer library: the layer a Track slot holds
#include "Tests/TrackLayerFixture.h"     // mu-On: the recorded single-track streams shared with mu-On's own tests
#include "Tests/GoldenRender.h"          // mu-core: MockHost
#include "Tests/ProcessorSeamHelpers.h"

#include <cmath>
#include <utility>

namespace
{
    using mu_core::LayerTypeId;

    const LayerTypeId kTrackTypes[] = { LayerTypeId::Kick, LayerTypeId::Bass, LayerTypeId::Hat, LayerTypeId::Snare };

    // The processor with the retire bin exposed.
    struct TrackProcessor : mu_tronic::PluginProcessor
    {
        using mu_tronic::PluginProcessor::retireBin;
    using mu_tronic::PluginProcessor::remapLayerReferences;
    };

    mu_on::Track& trackIn(ProcessorBase& p, int slot) { return static_cast<mu_on::Track&>(*p.getLayer(slot)); }

    int rowCount(const juce::ValueTree& node)
    {
        int n = 0;
        for (int i = 0; i < node.getNumChildren(); ++i)
            if (node.getChild(i).hasType(mu_pp::kRowTag)) ++n;
        return n;
    }

    // The ids of the rows a slot node holds.
    juce::StringArray rowIds(const juce::ValueTree& node)
    {
        juce::StringArray ids;
        for (int i = 0; i < node.getNumChildren(); ++i)
            if (const auto row = node.getChild(i); row.hasType(mu_pp::kRowTag)) ids.add(row.getProperty("id").toString());
        return ids;
    }

    int paramsWithPrefix(ProcessorBase& p, const juce::String& prefix)
    {
        int n = 0;
        for (auto* raw : p.getParameters())
            if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(raw))
                if (withId->getParameterID().startsWith(prefix)) ++n;
        return n;
    }

    float getPlain(juce::AudioProcessorValueTreeState& apvts, const juce::String& id)
    {
        auto* p = apvts.getParameter(id);
        return p->convertFrom0to1(p->getValue());
    }

    // Appends channel `watched`'s stream as the mixer receives it (sized up front: nothing allocates while rendering).
    struct ChannelTap : MixerEngine::ChannelTap
    {
        ChannelTap(int channel, int frames) : watched(channel) { buf.setSize(2, frames); buf.clear(); }
        void capture(int ch, const juce::AudioBuffer<float>& b, int n) noexcept override
        {
            if (ch != watched) return;
            const int len = juce::jmin(n, buf.getNumSamples() - written);
            for (int c = 0; c < 2 && c < b.getNumChannels(); ++c)
                if (len > 0) juce::FloatVectorOperations::copy(buf.getWritePointer(c) + written, b.getReadPointer(c), len);
            written += juce::jmax(0, len);
        }
        int watched;
        juce::AudioBuffer<float> buf;
        int written = 0;
    };

    // Plays a MockHost transport through a processor, one block at a time.
    struct Player
    {
        Player(mu_tronic::PluginProcessor& proc, int block) : p(proc), blockSize(block), buf(2, block)
        {
            host.bpm = 120.0;
            p.setPlayHead(&host);
            p.setRateAndBufferSizeDetails(48000.0, block);
            p.prepareToPlay(48000.0, block);
        }
        ~Player() { p.setPlayHead(nullptr); p.releaseResources(); }

        void block()
        {
            juce::MidiBuffer midi;
            host.timeInSamples = (juce::int64) index * blockSize;
            buf.clear();
            p.processBlock(buf, midi);
            host.ppq += (double) blockSize * host.bpm / (60.0 * 48000.0);
            ++index;
        }

        mu_tronic::PluginProcessor& p;
        int blockSize, index = 0;
        mu_golden::MockHost host;
        juce::AudioBuffer<float> buf;
    };
}

struct MuTronicTrackTests : juce::UnitTest
{
    MuTronicTrackTests() : juce::UnitTest("mu-Tronic Track layer types", "mu-tronic-track") {}

    void runTest() override
    {
        layoutAndRegistry();
        savedStatePerType();
        mixedArpAndTracks();
        restoreBuildsEngines();
        sameStreamAsMuOn();
        kickToBassSwap();
        presetFolders();
        defaultGroove();
        rumbleSourceFollows();
        rumbleSoundsFromItsSource();
    }

    void layoutAndRegistry()
    {
        beginTest("the four Track types are offered with the t prefix, and their rows exist for all sixteen slots");
        mu_seam_test::TempStorage storage;
        TrackProcessor p;
        for (auto type : kTrackTypes)
        {
            const auto* info = p.getLayerTypes().find(type);
            expect(info != nullptr, juce::String(mu_core::layerTypeName(type)) + " is offered");
            if (info == nullptr) continue;
            expectEquals(juce::String(info->name), juce::String(mu_core::layerTypeName(type)));
            expectEquals((int) info->paramPrefixLetter, (int) 't');
            expectEquals(juce::String(info->group), juce::String("mu-On"));
            expectEquals(juce::String::fromUTF8(info->typeTag), juce::String(juce::CharPointer_UTF8("\xCE\xBC" "-On")));
            expect(info->typeFromPreset != nullptr, "the shared .muTrack is told apart by engine");
        }
        expect(p.getLayerTypes().find(LayerTypeId::Rumble) != nullptr, "Rumble is offered too");
        for (int s : { 0, 7, 15 })
            expect(p.apvts.getParameter("t" + juce::String(s) + "_eng") != nullptr && p.apvts.getParameter("t" + juce::String(s) + "_swing") != nullptr);
        expect(p.apvts.getParameter("t16_eng") == nullptr, "no seventeenth slot");
        logMessage("t{N}_ rows per slot: " + juce::String(paramsWithPrefix(p, "t3_")));
        p.flushPendingAsyncUpdates();
    }

    // A slot saves the t{N}_ rows of its live type only; the dormant a / r / v rows never reach the file, and a restore
    // brings back the type, the values and the locked engine.
    void savedStatePerType()
    {
        beginTest("each Track type saves only its t{N}_ rows, by type name, and restores to the same state");
        int tRows = 0;
        {
            mu_seam_test::TempStorage s;
            TrackProcessor probe;
            tRows = paramsWithPrefix(probe, "t3_");
        }
        int slot = 1;
        for (auto type : kTrackTypes)
        {
            mu_seam_test::TempStorage storage;
            TrackProcessor p;
            expect(p.setLayerType(slot, type), juce::String("set ") + mu_core::layerTypeName(type));
            expect(p.getLayerTypeId(slot) == type);
            // Dormant prefixes: an Arp value on this slot, and a Track value on another (Empty) slot, are not written.
            mu_seam_test::setParam(p.apvts, "a" + juce::String(slot) + "_cut", 2200.0f);
            mu_seam_test::setParam(p.apvts, "t" + juce::String(slot + 4) + "_swing", 0.9f);
            mu_seam_test::setParam(p.apvts, "t" + juce::String(slot) + "_swing", 0.7f);

            auto state = p.captureState();
            const auto node = mu_pp::findLayerNode(state, slot);
            expectEquals(node.getProperty("type").toString(), juce::String(mu_core::layerTypeName(type)));
            expectEquals(rowCount(node), tRows, "every t{N}_ row of the slot is saved");
            const auto ids = rowIds(node);
            expect(! ids.contains("cut"), "the dormant Arp row is not saved");
            expect(ids.contains("eng") && ids.contains("swing") && ids.contains("accent"), "the engine and groove rows are saved");
            expectEquals(rowCount(mu_pp::findLayerNode(state, slot + 4)), 0, "an Empty slot writes no rows, whatever its dormant value");
            expectEquals(rowCount(mu_pp::findLayerNode(state, 0)), 0);

            // The locked track's engine row is a record of the type: it names the engine.
            expectEquals((int) std::lround(getPlain(p.apvts, "t" + juce::String(slot) + "_eng")), mu_on::toIndex(mu_on::engineOfType(type)),
                         "t{N}_eng records the type's engine");

            juce::MemoryBlock block;
            p.getStateInformation(block);
            mu_seam_test::TempStorage storage2;
            TrackProcessor q;
            q.setStateInformation(block.getData(), (int) block.getSize());
            expect(q.getLayerTypeId(slot) == type, "the type restores by name");
            expectWithinAbsoluteError(getPlain(q.apvts, "t" + juce::String(slot) + "_swing"), 0.7f, 0.001f);
            expectWithinAbsoluteError(getPlain(q.apvts, "t" + juce::String(slot + 4) + "_swing"), 0.0f, 0.001f, "the dormant value did not travel");
            expect(q.captureState().isEquivalentTo(state), "restoring then capturing reproduces the saved state");
            p.flushPendingAsyncUpdates();
            q.flushPendingAsyncUpdates();
            ++slot;
        }
    }

    void mixedArpAndTracks()
    {
        beginTest("Arp and Track slots in one state: two prefix letters live, each slot saves its own rows");
        mu_seam_test::TempStorage storage;
        TrackProcessor p;
        expect(p.setLayerType(1, LayerTypeId::Arp));
        expect(p.setLayerType(2, LayerTypeId::Kick));
        expect(p.setLayerType(3, LayerTypeId::Bass));
        mu_seam_test::setParam(p.apvts, "a1_cut", 2200.0f);
        mu_seam_test::setParam(p.apvts, "t2_swing", 0.4f);
        mu_seam_test::setParam(p.apvts, "t3_swing", 0.6f);

        auto state = p.captureState();
        expectEquals(rowCount(mu_pp::findLayerNode(state, 1)), 63, "the Arp slot saves its 63 rows");
        const int tRows = paramsWithPrefix(p, "t2_");
        expectEquals(rowCount(mu_pp::findLayerNode(state, 2)), tRows);
        expectEquals(rowCount(mu_pp::findLayerNode(state, 3)), tRows);
        expect(! rowIds(mu_pp::findLayerNode(state, 2)).contains("cut") && ! rowIds(mu_pp::findLayerNode(state, 1)).contains("swing"),
               "no slot carries another family's rows");

        juce::MemoryBlock block;
        p.getStateInformation(block);
        mu_seam_test::TempStorage storage2;
        TrackProcessor q;
        q.setStateInformation(block.getData(), (int) block.getSize());
        expect(q.getLayerTypeId(1) == LayerTypeId::Arp && q.getLayerTypeId(2) == LayerTypeId::Kick && q.getLayerTypeId(3) == LayerTypeId::Bass);
        expectWithinAbsoluteError(getPlain(q.apvts, "a1_cut"), 2200.0f, 1.0f);
        expectWithinAbsoluteError(getPlain(q.apvts, "t2_swing"), 0.4f, 0.001f);
        expectWithinAbsoluteError(getPlain(q.apvts, "t3_swing"), 0.6f, 0.001f);
        expect(q.captureState().isEquivalentTo(state), "restoring then capturing reproduces the mixed state");
        expect(q.canReorderLayers(), "a mixed Arp/Kick swap is in TronicSwapTests");
        p.flushPendingAsyncUpdates();
        q.flushPendingAsyncUpdates();
    }

    // A restore sets each slot's type first; the locked Track's completeSetup then builds the one engine it needs, and
    // a row that names another engine cannot change the type.
    void restoreBuildsEngines()
    {
        beginTest("a restored Track slot builds its locked engine (completeSetup runs), and the type wins over a stray engine row");
        mu_seam_test::TempStorage storage;
        TrackProcessor p;
        p.prepareToPlay(48000.0, 256);
        const std::pair<int, LayerTypeId> placed[] = { { 0, LayerTypeId::Kick }, { 4, LayerTypeId::Bass }, { 9, LayerTypeId::Hat }, { 12, LayerTypeId::Snare } };
        for (const auto& placement : placed) expect(p.setLayerType(placement.first, placement.second));
        juce::MemoryBlock block;
        p.getStateInformation(block);
        p.releaseResources();

        mu_seam_test::TempStorage storage2;
        TrackProcessor q;
        q.prepareToPlay(48000.0, 256);
        q.setStateInformation(block.getData(), (int) block.getSize());
        for (const auto& placement : placed)
        {
            const int slot = placement.first;
            const auto type = placement.second;
            const auto engine = mu_on::engineOfType(type);
            const auto& track = trackIn(q, slot);
            expect(q.getLayerTypeId(slot) == type);
            expect(track.engineLocked && track.engineSetting() == engine, "locked to the type's engine");
            for (int e = 0; e < mu_on::kNumEngineTypes; ++e)
                expect(track.holdsEngine(mu_on::engineFromIndex(e)) == (e == mu_on::toIndex(engine)),
                       juce::String(mu_core::layerTypeName(type)) + " holds exactly its own engine");
            expectEquals((int) std::lround(getPlain(q.apvts, "t" + juce::String(slot) + "_eng")), mu_on::toIndex(engine));
        }

        // A hand-made node: a Kick whose eng row says Bass (index 1).
        auto hand = p.captureState();
        auto node = mu_pp::findLayerNode(hand, 0);
        for (int i = 0; i < node.getNumChildren(); ++i)
            if (auto row = node.getChild(i); row.hasType(mu_pp::kRowTag) && row.getProperty("id").toString() == "eng")
                row.setProperty("x", 1.0, nullptr);
        mu_seam_test::TempStorage storage3;
        TrackProcessor r;
        r.prepareToPlay(48000.0, 256);
        r.applyStateTree(hand);
        expect(r.getLayerTypeId(0) == LayerTypeId::Kick);
        expect(trackIn(r, 0).engineSetting() == mu_on::EngineType::Kick && trackIn(r, 0).holdsEngine(mu_on::EngineType::Kick), "the type wins on restore");
        expectEquals((int) std::lround(getPlain(r.apvts, "t0_eng")), 0, "and the engine row is written back to match");
        {
            // The stray row did not leave a half-built engine behind: the restored Kick renders a block.
            Player player(r, 256);
            player.block();
            expect(std::isfinite(player.buf.getMagnitude(0, 256)), "the restored slot renders a finite block");
            expect(trackIn(r, 0).engineSetting() == mu_on::EngineType::Kick, "and is still a Kick afterwards");
        }
        p.flushPendingAsyncUpdates();
        q.flushPendingAsyncUpdates();
        r.flushPendingAsyncUpdates();
        q.releaseResources();
        r.releaseResources();
    }

    // The recorded single-track streams of mu-On (Tests/TrackLayerFixture.h): a locked Track in a mu-Tronic slot must
    // reproduce them bit for bit, which is "sample-identical to mu-On's own track" since mu-On's tests hold the same hash.
    void sameStreamAsMuOn()
    {
        namespace fx = mu_on_fixture;
        for (auto type : kTrackTypes)
        {
            const auto engine = mu_on::engineOfType(type);
            beginTest(juce::String("a ") + mu_core::layerTypeName(type) + " in a mu-Tronic slot renders the stream mu-On records");
            mu_seam_test::TempStorage storage;
            mu_seam_test::ScopedEnvVar deterministic("MU_DETERMINISTIC_NOISE", "1");

            TrackProcessor p;
            ChannelTap tap(0, fx::kBlock * fx::kBlocks);
            {
                Player player(p, fx::kBlock);
                expect(p.setLayerType(0, type));
                fx::configureSingle(p.apvts, engine);
                fx::configureRow(trackIn(p, 0));
                p.setChannelTapForTest(&tap);
                for (int b = 0; b < fx::kBlocks; ++b) player.block();
                p.setChannelTapForTest(nullptr);
            }
            expectEquals(tap.written, fx::kBlock * fx::kBlocks, "every block reached the mixer");
            fx::StreamHash hash;
            for (int b = 0; b < fx::kBlocks; ++b) hash.add(tap.buf, fx::kBlock, b * fx::kBlock);
            logMessage(juce::String(mu_core::layerTypeName(type)) + " mu-Tronic stream hash " + juce::String::toHexString((juce::int64) hash.h));
            expect(hash.h == fx::kSingleHash[mu_on::toIndex(engine)], "the slot renders the recorded single-track stream");
            expect(hash.peak > 0.01f && hash.peak < 4.0f, "and it is a real, bounded sound");
            p.flushPendingAsyncUpdates();
        }
    }

    // Kick to Bass keeps the t{N}_ prefix: the old Kick sounds out through the bin's fade and reads no parameter, the
    // new Bass (no steps) is silent, nothing is left ringing, and the bin drains.
    void kickToBassSwap()
    {
        beginTest("Kick swapped for Bass in the same slot: the Kick tail fades with no click, nothing sticks, the bin drains");
        constexpr int kBlock = 256, kBlocks = 40, kSwapAt = 3;
        mu_seam_test::TempStorage storage;
        mu_seam_test::ScopedEnvVar deterministic("MU_DETERMINISTIC_NOISE", "1");
        TrackProcessor p;
        ChannelTap tap(0, kBlock * kBlocks);
        {
            Player player(p, kBlock);
            expect(p.setLayerType(0, LayerTypeId::Kick));
            mu_seam_test::setParam(p.apvts, mu_on::engineParamId(0, mu_on::EngineType::Kick, "adec"), 800.0f);   // a long body: still sounding at the swap
            auto& kick = trackIn(p, 0);
            for (int s = 0; s < 16; ++s) { kick.row.setOn(s, s == 0); kick.row.setAccent(s, false); }
            p.setChannelTapForTest(&tap);
            for (int b = 0; b < kSwapAt; ++b) player.block();

            expect(p.setLayerType(0, LayerTypeId::Bass));
            auto& bass = trackIn(p, 0);
            for (int s = 0; s < 16; ++s) bass.row.setOn(s, false);   // the new Bass plays nothing
            for (int b = kSwapAt; b < kBlocks; ++b) player.block();
            p.setChannelTapForTest(nullptr);
        }
        expectEquals(tap.written, kBlock * kBlocks);
        auto peak = [&](int b) { return tap.buf.getMagnitude(0, b * kBlock, kBlock); };
        auto maxStep = [&](int from, int to)
        {
            float m = 0.0f;
            for (int i = juce::jmax(1, from); i < to; ++i) m = juce::jmax(m, std::abs(tap.buf.getSample(0, i) - tap.buf.getSample(0, i - 1)));
            return m;
        };
        expect(peak(kSwapAt - 1) > 0.05f, "the Kick is sounding when it is swapped");
        expect(peak(kSwapAt) > 0.01f, "its tail carries on into the first block after the swap");
        for (int b = kSwapAt; b < kSwapAt + 6; ++b)
            expect(peak(b + 1) <= peak(b) + 1.0e-4f, "the tail only decays (block " + juce::String(b) + ")");
        expectEquals(peak(kSwapAt + 7), 0.0f, "the 25 ms fade has ended: nothing is left ringing");
        expectEquals(peak(kBlocks - 1), 0.0f, "and the new Bass is silent");
        // A hard cut would step by about the signal's level at the swap; the fade steps no more than the Kick itself did.
        const float before = maxStep(kBlock, kSwapAt * kBlock);
        const float across = maxStep(kSwapAt * kBlock - 2, (kSwapAt + 7) * kBlock);
        expect(across <= before * 1.1f + 1.0e-4f, "no click at the swap: the largest step " + juce::String(across) + " vs " + juce::String(before) + " before it");

        p.flushPendingAsyncUpdates();
        expectEquals(p.retireBin.countIn(mu_core::RetireBin::Parked) + p.retireBin.countIn(mu_core::RetireBin::Draining), 0, "the bin has drained");
        expectEquals(p.retireBin.reclaimPending(), 0, "the old Kick is freed");
    }

    // A fresh Track starts on its engine's groove so a new slot makes a sound at once; a restored row, empty or not,
    // replaces it.
    // A Rumble's source is a slot number: a new Rumble picks the first filled non-Rumble slot, and a swap or removal
    // carries the number to wherever that layer went (a gone one falls to the first filled non-Rumble slot).
    void rumbleSourceFollows()
    {
        beginTest("a Rumble picks a source, keeps following it through a remap, and falls back when it is gone");
        mu_seam_test::TempStorage storage;
        TrackProcessor p;
        auto src = [&](int slot) { return (int) std::lround(getPlain(p.apvts, "t" + juce::String(slot) + "_src")); };
        expect(p.setLayerType(5, LayerTypeId::Rumble));
        mu_tronic::chooseRumbleSource(p, 5);
        expectEquals(src(5), 0, "with nothing to listen to the source stays at slot 0 (silence)");
        expect(p.setLayerType(3, LayerTypeId::Hat));
        expect(p.setLayerType(8, LayerTypeId::Arp));
        mu_tronic::chooseRumbleSource(p, 5);
        expectEquals(src(5), 3, "the first filled non-Rumble slot");

        // The layer in slot 3 moves to slot 8 and the one in 8 to 3.
        p.remapLayerReferences([](int i) { return i == 3 ? 8 : (i == 8 ? 3 : i); });
        expectEquals(src(5), 8, "the source follows its layer");
        // Slot 8's layer is gone: the source falls to the first filled non-Rumble slot (the Hat in 3).
        p.remapLayerReferences([](int i) { return i == 8 ? -1 : i; });
        expectEquals(src(5), 3, "a gone source falls back");

        // chooseRumbleSource leaves a source that works.
        mu_seam_test::setParam(p.apvts, "t5_src", 8.0f);
        mu_tronic::chooseRumbleSource(p, 5);
        expectEquals(src(5), 8, "a working source is kept");
        p.flushPendingAsyncUpdates();
    }

    void rumbleSoundsFromItsSource()
    {
        beginTest("a Rumble in a slot processes its source's block, and its state round trips");
        mu_seam_test::TempStorage storage;
        TrackProcessor p;
        expect(p.setLayerType(0, LayerTypeId::Kick));
        expect(p.setLayerType(1, LayerTypeId::Rumble));
        mu_tronic::chooseRumbleSource(p, 1);
        expectEquals((int) std::lround(getPlain(p.apvts, "t1_src")), 0);

        // The Rumble's stream is silent without its source and sounds with it.
        auto peakOfSlot1 = [&](bool sourceOn)
        {
            for (int s = 0; s < 16; ++s) trackIn(p, 0).row.setOn(s, sourceOn && s % 4 == 0);
            ChannelTap tap(1, 48000 / 2);
            p.setChannelTapForTest(&tap);
            {
                Player play(p, 512);
                for (int b = 0; b < 40; ++b) play.block();
            }
            p.setChannelTapForTest(nullptr);
            float peak = 0.0f;
            for (int c = 0; c < 2; ++c) peak = juce::jmax(peak, tap.buf.getMagnitude(c, 0, tap.buf.getNumSamples()));
            return peak;
        };
        expectWithinAbsoluteError(peakOfSlot1(false), 0.0f, 1.0e-6f, "no source hits: the Rumble is silent");
        expect(peakOfSlot1(true) > 0.001f, "the source's kicks reach the Rumble");

        juce::MemoryBlock block;
        p.getStateInformation(block);
        mu_seam_test::TempStorage storage2;
        TrackProcessor q;
        q.setStateInformation(block.getData(), (int) block.getSize());
        expect(q.getLayerTypeId(1) == LayerTypeId::Rumble, "the Rumble type restores by name");
        expectEquals((int) std::lround(getPlain(q.apvts, "t1_src")), 0, "and its source");
        p.flushPendingAsyncUpdates();
        q.flushPendingAsyncUpdates();
    }

    void defaultGroove()
    {
        beginTest("a fresh Track starts on its engine's default groove, and a restored empty row stays empty");
        mu_seam_test::TempStorage storage;
        TrackProcessor p;
        auto stepsOf = [&](int slot)
        {
            juce::String on;
            for (int s = 0; s < 16; ++s) on << (trackIn(p, slot).row.isOn(s) ? (trackIn(p, slot).row.isAccent(s) ? "A" : "x") : ".");
            return on;
        };
        expect(p.setLayerType(0, LayerTypeId::Kick));
        expect(p.setLayerType(1, LayerTypeId::Bass));
        expect(p.setLayerType(2, LayerTypeId::Hat));
        expect(p.setLayerType(3, LayerTypeId::Snare));
        expectEquals(stepsOf(0), juce::String("A...x...x...x..."), "four on the floor, downbeat accented");
        expectEquals(stepsOf(1), juce::String("..x...x...x...x."), "off-beat bass");
        expectEquals(stepsOf(2), juce::String("x.x.x.x.x.x.x.x."), "straight eighths");
        expectEquals(stepsOf(3), juce::String("....x.......x..."), "backbeat");

        for (int s = 0; s < 16; ++s) { trackIn(p, 0).row.setOn(s, false); trackIn(p, 0).row.setAccent(s, false); }
        juce::MemoryBlock block;
        p.getStateInformation(block);
        mu_seam_test::TempStorage storage2;
        TrackProcessor q;
        q.setStateInformation(block.getData(), (int) block.getSize());
        bool any = false;
        for (int s = 0; s < 16; ++s) any = any || trackIn(q, 0).row.isOn(s);
        expect(! any, "the saved empty row is restored empty, not replaced by the groove");
        p.flushPendingAsyncUpdates();
        q.flushPendingAsyncUpdates();
    }

    // A Track type lists mu-On's own Tracks folder, only the presets whose saved engine is its own; a preset of another
    // engine cannot change a locked slot's engine.
    void presetFolders()
    {
        beginTest("layer presets come from mu-On's Tracks folder, filtered to the slot's engine");
        mu_seam_test::TempStorage storage;
        TrackProcessor p;
        const auto tracks = p.getContentDir().getSiblingFile("muOn").getChildFile("Tracks");
        tracks.createDirectory();
        auto writePreset = [&](const char* file, int engine)
        {
            juce::ValueTree preset("MuOnTrack");
            juce::ValueTree row(mu_pp::kRowTag);
            row.setProperty("id", "eng", nullptr);
            row.setProperty("x", (double) engine, nullptr);
            preset.appendChild(row, nullptr);
            tracks.getChildFile(file).replaceWithText(preset.toXmlString());
        };
        writePreset("Boom.muTrack", 0);
        writePreset("Sub.muTrack", 1);
        writePreset("Tick.muTrack", 2);
        tracks.getChildFile("Notes.txt").replaceWithText("not a preset");

        p.setSelectedSlot(0);
        expect(p.getLayerPresetDir() == juce::File(), "an Empty selected slot has no folder");
        expectEquals(p.slotPresetFiles(0).size(), 3, "an Empty slot lists every offered type's presets once");

        expect(p.setLayerType(1, LayerTypeId::Kick));
        expect(p.setLayerType(2, LayerTypeId::Bass));
        expect(p.setLayerType(3, LayerTypeId::Snare));
        p.setSelectedSlot(1);
        expect(p.getLayerPresetDir() == tracks, "the folder is mu-On's, not a copy under mu-Tronic");
        expectEquals(p.getLayerPresetExtension(), juce::String("muTrack"));
        expectEquals(p.slotPresetFiles(1).size(), 1, "a Kick slot lists the Kick presets only");
        expect(p.slotPresetFiles(1)[0].getFileName() == "Boom.muTrack");
        expectEquals(p.slotPresetFiles(2).size(), 1);
        expect(p.slotPresetFiles(2)[0].getFileName() == "Sub.muTrack");
        expectEquals(p.slotPresetFiles(3).size(), 0, "no Snare preset in the folder");
        expect(! p.getContentDir().getChildFile("Tracks").exists(), "mu-Tronic makes no folder of its own");

        // Another engine's preset content as a slot's initial state cannot change the locked engine.
        juce::ValueTree bassNode(mu_pp::kLayerTag);
        juce::ValueTree row(mu_pp::kRowTag);
        row.setProperty("id", "eng", nullptr);
        row.setProperty("x", 1.0, nullptr);
        bassNode.appendChild(row, nullptr);
        expect(p.setLayerType(1, LayerTypeId::Empty));
        expect(p.setLayerType(1, LayerTypeId::Kick, &bassNode));
        expect(trackIn(p, 1).engineSetting() == mu_on::EngineType::Kick && (int) std::lround(getPlain(p.apvts, "t1_eng")) == 0,
               "a Bass preset leaves a locked Kick a Kick");
        p.flushPendingAsyncUpdates();
    }
};

static MuTronicTrackTests muTronicTrackTests;
