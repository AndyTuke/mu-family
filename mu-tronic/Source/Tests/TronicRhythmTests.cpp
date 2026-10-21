// The Rhythm layer type in a mu-Tronic slot (docs/mu-tronic/design.md T1b/T7, 2.4): its rows are in the 16-slot layout and
// saved only for a slot that holds a Rhythm; it clocks itself and renders the stream mu-Clid records; a changed row reaches
// the layer; its sample, the sample library and the browser's preview are the type's shared services; its MIDI output mode
// sends through the processor; a replaced Rhythm fades out and leaves nothing stuck; its .muRhythm presets are mu-Clid's own
// files and carry the layer's data; and type changes under a running audio thread allocate nothing.

#include "Plugin/PluginProcessor.h"
#include "Persistence/RhythmLayerType.h"
#include "Sequencer/RhythmType.h"           // mu-Clid's layer library: the layer, its services and parameter table
#include "UI/RhythmHost.h"                  // and what the layer's content reads from its host
#include "Tests/AllocationProbe.h"          // the operators are defined in TronicArpTests.cpp (one test executable)
#include "Tests/RhythmLayerFixture.h"       // mu-Clid: the recorded Rhythm stream shared with mu-Clid's own tests
#include "Tests/GoldenRender.h"             // mu-core: MockHost
#include "Tests/ProcessorSeamHelpers.h"

#include <atomic>
#include <cmath>
#include <thread>
#include <vector>

namespace
{
    using mu_core::LayerTypeId;

    struct RhythmProcessor : mu_tronic::PluginProcessor
    {
        using mu_tronic::PluginProcessor::drainDeferredWork;
        using mu_tronic::PluginProcessor::retireBin;
        using mu_tronic::PluginProcessor::typedLayerPresetDir;
        using mu_tronic::PluginProcessor::typedLayerPresetExtension;
    };

    mu_clid::SlotRhythm& rhythmIn(ProcessorBase& p, int slot) { return static_cast<mu_clid::SlotRhythm&>(*p.getLayer(slot)); }

    mu_clid::RhythmSlotServices& servicesOf(ProcessorBase& p)
    {
        return *static_cast<mu_clid::RhythmSlotServices*>(p.findLayerServices(LayerTypeId::Rhythm));
    }

    int rowCount(const juce::ValueTree& node)
    {
        int n = 0;
        for (int i = 0; i < node.getNumChildren(); ++i)
            if (node.getChild(i).hasType(mu_pp::kRowTag)) ++n;
        return n;
    }

    float getPlain(juce::AudioProcessorValueTreeState& apvts, const juce::String& id)
    {
        auto* p = apvts.getParameter(id);
        return p->convertFrom0to1(p->getValue());
    }

    // Appends channel 0's stream as the mixer receives it (sized up front: nothing allocates while rendering).
    struct Channel0Tap : MixerEngine::ChannelTap
    {
        explicit Channel0Tap(int frames) { buf.setSize(2, frames); buf.clear(); }
        void capture(int ch, const juce::AudioBuffer<float>& b, int n) noexcept override
        {
            if (ch != 0) return;
            const int len = juce::jmin(n, buf.getNumSamples() - written);
            for (int c = 0; c < 2 && c < b.getNumChannels(); ++c)
                if (len > 0) juce::FloatVectorOperations::copy(buf.getWritePointer(c) + written, b.getReadPointer(c), len);
            written += juce::jmax(0, len);
        }
        juce::AudioBuffer<float> buf;
        int written = 0;
    };

    // The peak each channel handed the mixer since the last reset.
    struct PeakTap : MixerEngine::ChannelTap
    {
        void capture(int ch, const juce::AudioBuffer<float>& b, int n) noexcept override
        {
            if (ch < 0 || ch >= 16) return;
            for (int c = 0; c < 2 && c < b.getNumChannels(); ++c)
                peak[ch] = juce::jmax(peak[ch], b.getMagnitude(c, 0, n));
        }
        void reset() { for (auto& v : peak) v = 0.0f; }
        float peak[16] {};
    };

    // One note event seen in the processor's MIDI output.
    struct SeenNote { bool on; int note, channel, velocity, block, offset; };

    // Runs blocks of `n` frames through a prepared processor on a host transport, collecting the MIDI it sends.
    struct Run
    {
        Run(mu_tronic::PluginProcessor& proc, int blockSize) : p(proc), n(blockSize), buf(2, blockSize)
        {
            host.bpm = 120.0;
            p.setPlayHead(&host);
            p.setRateAndBufferSizeDetails(48000.0, n);
            p.prepareToPlay(48000.0, n);
        }
        ~Run() { p.setPlayHead(nullptr); p.releaseResources(); }

        void block(const juce::MidiBuffer& midi = {})
        {
            juce::MidiBuffer m(midi);
            host.timeInSamples = (juce::int64) index * n;
            buf.clear();
            p.processBlock(buf, m);
            if (host.playing) host.ppq += (double) n * host.bpm / (60.0 * 48000.0);
            peak = juce::jmax(peak, buf.getMagnitude(0, n));
            for (const auto meta : m)
            {
                const auto msg = meta.getMessage();
                if (msg.isNoteOn())       notes.push_back({ true,  msg.getNoteNumber(), msg.getChannel(), (int) msg.getVelocity(), index, meta.samplePosition });
                else if (msg.isNoteOff()) notes.push_back({ false, msg.getNoteNumber(), msg.getChannel(), 0, index, meta.samplePosition });
            }
            ++index;
        }
        int count(bool on) const
        {
            int c = 0;
            for (const auto& s : notes) if (s.on == on) ++c;
            return c;
        }

        mu_tronic::PluginProcessor& p;
        int n, index = 0;
        mu_golden::MockHost host;
        juce::AudioBuffer<float> buf;
        float peak = 0.0f;
        std::vector<SeenNote> notes;
    };

    // mu-Clid's sample folder next to the processor's content folder (shared with mu-Clid), with the fixture sample in it.
    // It does not need the type's services, so a test can put the sample down before the first Rhythm exists.
    juce::File putSample(ProcessorBase& p)
    {
        auto dir = p.getContentDir().getSiblingFile("muClid").getChildFile("Samples");
        dir.createDirectory();
        return mu_clid_fixture::writeSample(dir);
    }
}

struct MuTronicRhythmTests : juce::UnitTest
{
    MuTronicRhythmTests() : juce::UnitTest("mu-Tronic Rhythm layer type", "mu-tronic-rhythm") {}

    void runTest() override
    {
        layoutAndRegistry();
        savedState();
        parameterRowsReachTheLayer();
        newLayerDefaults();
        freshLayerMissingSample();
        slotSaveCard();
        midiModeSilencesStrip();
        sameStreamAsMuClid();
        stoppedTransportAndRestart();
        sampleLibraryAndPreview();
        midiOutputMode();
        swapLeavesNothingStuck();
        presets();
        stagedPreset();
        servicesLifetime();
        typeChangeStress();
        midiBufferRoom();
        retireWhileWritingParameters();
    }

    void layoutAndRegistry()
    {
        beginTest("the Rhythm type is offered, and its rows exist under r{N}_ for all sixteen slots");
        mu_seam_test::TempStorage storage;
        RhythmProcessor p;
        const auto* info = p.getLayerTypes().find(LayerTypeId::Rhythm);
        expect(info != nullptr);
        if (info != nullptr)
        {
            expectEquals(juce::String(info->name), juce::String("Rhythm"));
            expectEquals(juce::String(info->group), juce::String("mu-Clid"));
            expectEquals((int) info->paramPrefixLetter, (int) 'r');
            expectEquals(juce::String(info->presetExtension), juce::String(".muRhythm"));
            expectEquals(juce::String(info->contentFolderName), juce::String("muClid"));
            expectEquals(juce::String(info->presetFolder), juce::String("Rhythms"));
            expectEquals(juce::String(info->presetTag), juce::String("MuClidRhythm"));
            expectEquals(juce::String::fromUTF8(info->typeTag), juce::String(juce::CharPointer_UTF8("\xCE\xBC" "-Clid")));
            expect(info->createServices != nullptr && info->attachServices != nullptr);
            expect(info->presetFileToNode != nullptr && info->presetNodeToFile != nullptr, "the file shape is converted at the boundary");
            expect(info->addGlobalParams == nullptr, "the Rhythm shares no row between its slots");
        }

        int rows0 = 0, rows3 = 0, rows15 = 0;
        for (auto* raw : p.getParameters())
            if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(raw))
            {
                const auto id = withId->getParameterID();
                if (id.startsWith("r0_")) ++rows0;
                if (id.startsWith("r3_")) ++rows3;
                if (id.startsWith("r15_")) ++rows15;
            }
        logMessage("Rhythm rows per slot: " + juce::String(rows0));
        expectGreaterThan(rows0, 50, "the Rhythm table has its rows");
        expectEquals(rows3, rows0, "every slot has the whole table");
        expectEquals(rows15, rows0);
        expect(p.apvts.getParameter("r16_hitsA") == nullptr, "no seventeenth slot");
        expectEquals(rows0, mu_clid::kRhythmParamCount, "and the layer table covers every row the layout declares");
        expect(p.producesMidi(), "the processor sends the MIDI output mode's notes");
    }

    // A slot's node holds the rows of its live type only, and the layer's own data (modulators, sample) comes back with it.
    void savedState()
    {
        beginTest("a slot saves the rows of its live type only; the pattern, voice, modulators and sample round trip");
        mu_seam_test::TempStorage storage;
        RhythmProcessor p;
        expect(p.setLayerType(3, LayerTypeId::Rhythm));
        expect(p.setLayerType(4, LayerTypeId::Rhythm));
        const auto wav = putSample(p);
        servicesOf(p).samples.load(3, wav);
        mu_seam_test::setParam(p.apvts, "r3_hitsA", 7.0f);
        mu_seam_test::setParam(p.apvts, "r3_fltCut", 1800.0f);
        mu_seam_test::setParam(p.apvts, "r7_hitsA", 9.0f);   // slot 7 is Empty: this prefix is dormant
        mu_clid_fixture::configure(p.apvts, rhythmIn(p, 3), 3);

        auto state = p.captureState();
        const auto node3 = mu_pp::findLayerNode(state, 3);
        expectEquals(rowCount(node3), mu_clid::kRhythmParamCount, "a Rhythm slot saves its rows");
        expectEquals(rowCount(mu_pp::findLayerNode(state, 7)), 0, "a dormant prefix is not written");
        expectEquals(rowCount(mu_pp::findLayerNode(state, 0)), 0, "an Empty slot writes no rows");
        expectEquals(node3.getProperty("sample").toString(), juce::String("fixture-pluck.wav"), "the sample is saved relative to mu-Clid's Samples folder");
        expect(node3.getChildWithName("Modulators").isValid(), "and the modulators");
        expectEquals(juce::String(mu_pp::findLayerNode(state, 4).getProperty("sample").toString()), juce::String(), "a Rhythm with no sample saves none");

        juce::MemoryBlock block;
        p.getStateInformation(block);
        mu_seam_test::TempStorage storage2;
        RhythmProcessor q;
        putSample(q);   // the sample folder of the machine that restores
        q.setStateInformation(block.getData(), (int) block.getSize());
        expect(q.getLayerTypeId(3) == LayerTypeId::Rhythm && q.getLayerTypeId(4) == LayerTypeId::Rhythm);
        expectEquals(rhythmIn(q, 3).genA.hits, 5, "the pattern rows reach the layer on a restore");
        expectEquals(rhythmIn(q, 3).genA.rotate, 1);
        expectEquals(rhythmIn(q, 3).genC.steps, 4);
        expect(rhythmIn(q, 3).patternLegato);
        expectWithinAbsoluteError(rhythmIn(q, 3).voiceParams.filterCutoff, 2500.0f, 1.0f, "and so do the voice rows");
        expect(rhythmIn(q, 3).voice->hasSample(), "the sample is loaded again");
        expect(! servicesOf(q).samples.isSampleMissing(3));
        expectEquals(servicesOf(q).samples.path(3), servicesOf(q).getSamplesDir().getChildFile("fixture-pluck.wav").getFullPathName());
        expectEquals((int) rhythmIn(q, 3).modulationMatrix.getAssignments().size(), 1, "the modulator comes back");
        expect(! rhythmIn(q, 4).voice->hasSample());
        auto again = q.captureState();
        expect(again.isEquivalentTo(state), "restoring then capturing reproduces the saved state");

        // A slot that loads Empty leaves the Rhythm rows of the previous tenant out of the next save.
        q.flushPendingAsyncUpdates();
        expect(q.setLayerType(3, LayerTypeId::Empty));
        expectEquals(rowCount(mu_pp::findLayerNode(q.captureState(), 3)), 0);
        p.flushPendingAsyncUpdates();
    }

    // A changed r{N}_ row reaches the layer's fields, its cursor and its voice: at once from the message thread, and by the
    // deferred-work pass from another thread.
    void parameterRowsReachTheLayer()
    {
        beginTest("a changed row reaches the layer: at once on the message thread, queued from another thread");
        mu_seam_test::TempStorage storage;
        RhythmProcessor p;
        expect(p.setLayerType(2, LayerTypeId::Rhythm));
        auto& r = rhythmIn(p, 2);
        mu_seam_test::setParam(p.apvts, "r2_hitsA", 7.0f);
        expectEquals(r.genA.hits, 7, "a hit count");
        mu_seam_test::setParam(p.apvts, "r2_pitchSemi", 5.0f);
        expectEquals(r.voiceParams.pitchSemitones, 5, "a voice row");
        mu_seam_test::setParam(p.apvts, "r2_outMode", 1.0f);
        expectEquals(r.outputMode.load(), 1, "the output mode");
        mu_seam_test::setParam(p.apvts, "r2_midiNote", 61.0f);
        expectEquals(r.midiNote.load(), 61);

        // From another thread: queued, then applied by the deferred-work pass.
        std::thread other([&] { mu_seam_test::setParam(p.apvts, "r2_hitsA", 9.0f); mu_seam_test::setParam(p.apvts, "r2_fltCut", 1234.0f); });
        other.join();
        expectEquals(r.genA.hits, 7, "not applied on the other thread");
        p.drainDeferredWork();
        expectEquals(r.genA.hits, 9, "applied by the pass");
        expectWithinAbsoluteError(r.voiceParams.filterCutoff, 1234.0f, 1.0f);

        // A bulk change applies the fields but leaves the pattern and the voice to the sync that follows it.
        servicesOf(p).bulkChange.store(true);
        mu_seam_test::setParam(p.apvts, "r2_hitsA", 3.0f);
        expectEquals(r.genA.hits, 3);
        servicesOf(p).bulkChange.store(false);
        r.syncFromParameters(p.apvts, 2);
        expectEquals(r.genA.hits, 3);

        // A row of a slot that holds no Rhythm changes nothing, and a Rhythm that arrives later starts from the defaults.
        mu_seam_test::setParam(p.apvts, "r5_hitsA", 11.0f);
        expect(p.getLayerTypeId(5) == LayerTypeId::Empty);
        expect(p.setLayerType(5, LayerTypeId::Rhythm));
        expectEquals(getPlain(p.apvts, "r5_hitsA"), 4.0f, "a new Rhythm starts as mu-Clid's first rhythm: four hits");
        expectEquals(getPlain(p.apvts, "r5_stepsA"), 16.0f, "in sixteen steps");
        expectEquals(rhythmIn(p, 5).genA.hits, 4);
        expectEquals(rhythmIn(p, 5).genA.steps, 16);
        p.flushPendingAsyncUpdates();
    }

    // A layer the user creates starts from the app's own default for its type: the _default file in the type's preset folder
    // when it is that type's, else the built-in start. A restored state or a loaded file always wins.
    void newLayerDefaults()
    {
        beginTest("a new layer starts from the app's _default file or the built-in default; restores and wrong-type files do not");
        mu_seam_test::TempStorage storage;
        RhythmProcessor p;
        const auto rhythms = p.getContentDir().getSiblingFile("muClid").getChildFile("Rhythms");
        const auto tracks  = p.getContentDir().getSiblingFile("muOn").getChildFile("Tracks");
        const auto real = juce::File(MU_TRONIC_FIXTURE_DIR).getChildFile("REAL_rhythm.muRhythm");

        // No default file: the built-in start, a pattern and no sample.
        expect(p.createLayerOfType(1, LayerTypeId::Rhythm));
        expectEquals(rhythmIn(p, 1).genA.hits, 4);
        expectEquals(rhythmIn(p, 1).genA.steps, 16);
        expect(! rhythmIn(p, 1).voice->hasSample());

        // A default file: the new layer takes it, sample included.
        putSample(p);
        rhythms.createDirectory();
        expect(real.copyFileTo(rhythms.getChildFile("_default.muRhythm")));
        expect(p.createLayerOfType(2, LayerTypeId::Rhythm));
        expectEquals(rhythmIn(p, 2).genA.hits, 5, "the default file's pattern");
        expectEquals(rhythmIn(p, 2).genA.rotate, 1);
        expect(rhythmIn(p, 2).voice->hasSample(), "and its sample");

        // A plain setLayerType, a restored state and a loaded file ignore it.
        expect(p.setLayerType(3, LayerTypeId::Rhythm));
        expectEquals(rhythmIn(p, 3).genA.hits, 4, "setLayerType is not a creation from the chooser");
        mu_seam_test::setParam(p.apvts, "r1_hitsA", 0.0f);
        auto state = p.captureState();
        expectEquals(mu_pp::findLayerNode(state, 1).getChildWithName("Modulators").isValid() ? 1 : 0, 1);
        RhythmProcessor q;
        q.applyStateTree(state);
        expect(q.getLayerTypeId(1) == LayerTypeId::Rhythm);
        expectEquals(rhythmIn(q, 1).genA.hits, 0, "an explicitly empty restored layer stays empty");
        expect(! rhythmIn(q, 1).voice->hasSample(), "and has no sample");

        // A corrupt default, and a default of another type, are ignored quietly.
        rhythms.getChildFile("_default.muRhythm").replaceWithText("<not a preset");
        expect(p.createLayerOfType(4, LayerTypeId::Rhythm));
        expectEquals(rhythmIn(p, 4).genA.hits, 4, "a corrupt default falls back to the built-in start");
        rhythms.getChildFile("_default.muRhythm").replaceWithText("<MuClidLayer/>");
        expect(p.createLayerOfType(5, LayerTypeId::Rhythm));
        expectEquals(rhythmIn(p, 5).genA.hits, 4, "a file with another type's tag is ignored");

        tracks.createDirectory();
        expect(juce::File(MU_TRONIC_FIXTURE_DIR).getChildFile("REAL_kick.muTrack").copyFileTo(tracks.getChildFile("_default.muTrack")));
        expect(p.createLayerOfType(6, LayerTypeId::Bass), "a Kick default file is not a Bass's");
        expect(p.getLayerTypeId(6) == LayerTypeId::Bass);
        expect(p.createLayerOfType(7, LayerTypeId::Kick));
        expect(p.getLayerTypeId(7) == LayerTypeId::Kick);
        p.flushPendingAsyncUpdates();
        q.flushPendingAsyncUpdates();
    }

    // A layer made from the chooser whose default file names a sample that is not there starts with no sample (the bar's
    // empty state); data that was loaded keeps its "missing" mark.
    void freshLayerMissingSample()
    {
        beginTest("a fresh Rhythm whose default sample is not found starts with no sample; loaded data keeps the missing mark");
        mu_seam_test::TempStorage storage;
        RhythmProcessor p;
        const auto rhythms = p.getContentDir().getSiblingFile("muClid").getChildFile("Rhythms");
        rhythms.createDirectory();
        const auto real = juce::File(MU_TRONIC_FIXTURE_DIR).getChildFile("REAL_rhythm.muRhythm");
        expect(real.copyFileTo(rhythms.getChildFile("_default.muRhythm")));

        // No sample folder: the default's sample cannot be found.
        expect(p.createLayerOfType(0, LayerTypeId::Rhythm));
        expect(servicesOf(p).samples.isSampleMissing(0), "the file names a sample that is not there");
        mu_tronic::clearUnresolvedSample(p, 0);
        expect(! servicesOf(p).samples.isSampleMissing(0), "the creation path clears it: no warning for a file nobody chose");
        expect(servicesOf(p).samples.path(0).isEmpty() && servicesOf(p).samples.getSampleName(0).isEmpty(), "the bar is empty");
        expectEquals(rhythmIn(p, 0).genA.hits, 5, "the default's pattern is kept");

        // The same file loaded by the user is the user's choice: the mark stays.
        expect(p.createLayerOfType(1, LayerTypeId::Rhythm));
        mu_tronic::clearUnresolvedSample(p, 1);
        p.loadLayerPreset(1, real);
        expect(servicesOf(p).samples.isSampleMissing(1), "a loaded preset whose sample has gone shows it");

        // With the sample in place nothing is cleared, and another type's slot is left alone.
        putSample(p);
        expect(p.createLayerOfType(2, LayerTypeId::Rhythm));
        mu_tronic::clearUnresolvedSample(p, 2);
        expect(rhythmIn(p, 2).voice->hasSample() && servicesOf(p).samples.path(2).isNotEmpty(), "a found sample stays");
        mu_tronic::clearUnresolvedSample(p, 7);   // an Empty slot: nothing happens
        p.flushPendingAsyncUpdates();
    }

    // The slot's Save is mu-Clid's own card: the host writes the file as mu-Clid does (category, description, embedded
    // sample) and "Save as default" writes the file a new Rhythm starts from.
    void slotSaveCard()
    {
        beginTest("a slot's Rhythm save writes mu-Clid's file with category, description and the embedded sample");
        mu_seam_test::TempStorage storage;
        RhythmProcessor p;
        const auto wav = putSample(p);
        expect(p.setLayerType(0, LayerTypeId::Rhythm));
        servicesOf(p).samples.load(0, wav);
        auto* host = dynamic_cast<mu_clid::RhythmHost*>(&servicesOf(p));
        expect(host != nullptr);
        if (host == nullptr) return;

        const auto dir = host->getRhythmPresetsDir();
        dir.createDirectory();
        expect(host->getRhythmLoadedPresetFile(0) == juce::File(), "a fresh layer came from no file");

        const auto file = dir.getChildFile("Saved.muRhythm");
        host->saveRhythmPresetFile(0, file, true, "Drums", "a test");
        const auto xml = juce::XmlDocument::parse(file);
        expect(xml != nullptr);
        if (xml == nullptr) return;
        expectEquals(xml->getStringAttribute("presetCategory"), juce::String("Drums"));
        expectEquals(xml->getStringAttribute("presetDescription"), juce::String("a test"));
        expectEquals(xml->getIntAttribute("presetEmbedSamples"), 1);
        expect(xml->getStringAttribute("sampleData").isNotEmpty(), "the audio travels in the file");
        expect(host->getRhythmPresetCategories().contains("Drums"), "the category is offered to the next save");
        expect(host->getRhythmLoadedPresetFile(0) == file, "the next save starts from that file");

        // Not embedded: the sample is named, not carried.
        const auto linked = dir.getChildFile("Linked.muRhythm");
        host->saveRhythmPresetFile(0, linked, false, {}, {});
        const auto linkedXml = juce::XmlDocument::parse(linked);
        expect(linkedXml != nullptr && ! linkedXml->hasAttribute("sampleData") && linkedXml->getStringAttribute("r0_sample").isNotEmpty());

        // The file opens in another slot (and so in mu-Clid): the pattern and the sample arrive.
        expect(p.setLayerType(1, LayerTypeId::Rhythm));
        p.loadLayerPreset(1, file);
        expect(rhythmIn(p, 1).voice->hasSample(), "the embedded sample loads");

        // "Save as default": the file a new Rhythm starts from, not a preset the layer is now named after.
        host->saveRhythmPresetFile(0, dir.getChildFile("_default.muRhythm"), true, {}, {});
        expect(host->getRhythmLoadedPresetFile(0) == linked, "a default is not the layer's preset");
        expect(p.createLayerOfType(2, LayerTypeId::Rhythm));
        expect(rhythmIn(p, 2).voice->hasSample(), "a new Rhythm starts from it, sample included");
        p.flushPendingAsyncUpdates();
    }

    // A Rhythm set to send MIDI makes no audio, so its mixer strip is drawn inactive: the editor asks this per slot.
    void midiModeSilencesStrip()
    {
        beginTest("a Rhythm in MIDI output mode is silent for the mixer; Sample mode and other types are not");
        mu_seam_test::TempStorage storage;
        RhythmProcessor p;
        expect(p.setLayerType(0, LayerTypeId::Rhythm));
        expect(p.setLayerType(1, LayerTypeId::Kick));
        expect(! mu_tronic::isRhythmMidiOutput(p, 0), "Sample mode is the default");
        mu_seam_test::setParam(p.apvts, "r0_outMode", 1.0f);
        expect(mu_tronic::isRhythmMidiOutput(p, 0), "MIDI mode");
        expect(! mu_tronic::isRhythmMidiOutput(p, 1), "a Kick has no output mode");
        expect(! mu_tronic::isRhythmMidiOutput(p, 5), "an Empty slot has none either");
        mu_seam_test::setParam(p.apvts, "r5_outMode", 1.0f);
        expect(! mu_tronic::isRhythmMidiOutput(p, 5), "a row left behind in an Empty slot does not count");
        mu_seam_test::setParam(p.apvts, "r0_outMode", 0.0f);
        expect(! mu_tronic::isRhythmMidiOutput(p, 0), "back to Sample");
        p.flushPendingAsyncUpdates();
    }

    // The recorded Rhythm stream of mu-Clid (Tests/RhythmLayerFixture.h): the layer in a mu-Tronic slot must reproduce it bit
    // for bit, which is "sample-identical to mu-Clid's own" since mu-Clid's tests hold the same hash.
    void sameStreamAsMuClid()
    {
        beginTest("a Rhythm in a mu-Tronic slot renders the stream mu-Clid records");
        namespace fx = mu_clid_fixture;
        for (int variant = 0; variant < 2; ++variant)
        {
            mu_seam_test::TempStorage storage;
            mu_seam_test::ScopedEnvVar deterministic("MU_DETERMINISTIC_NOISE", "1");

            RhythmProcessor p;
            mu_golden::MockHost host;
            host.bpm = fx::kBpm;
            p.setPlayHead(&host);
            p.setRateAndBufferSizeDetails(fx::kSampleRate, fx::kBlock);
            p.prepareToPlay(fx::kSampleRate, fx::kBlock);
            if (variant == 0)
            {
                expect(p.setLayerType(0, LayerTypeId::Rhythm));
                servicesOf(p).samples.load(0, putSample(p));
                fx::configure(p.apvts, rhythmIn(p, 0), 0);
            }
            else
            {
                // The same sound as the file mu-Clid itself wrote (tests/presets/mu-tronic-slot/REAL_rhythm.muRhythm), loaded
                // into an Empty slot: the file's rows, modulators and sample are all that make the stream.
                putSample(p);
                p.loadLayerPreset(0, juce::File(MU_TRONIC_FIXTURE_DIR).getChildFile("REAL_rhythm.muRhythm"));
                expect(p.getLayerTypeId(0) == LayerTypeId::Rhythm, "the file's type is adopted");
            }
            expect(rhythmIn(p, 0).voice->hasSample());

            Channel0Tap tap(fx::kBlock * fx::kBlocks);
            p.setChannelTapForTest(&tap);
            juce::AudioBuffer<float> buf(2, fx::kBlock);
            for (int b = 0; b < fx::kBlocks; ++b)
            {
                juce::MidiBuffer midi;
                host.timeInSamples = (juce::int64) b * fx::kBlock;
                buf.clear();
                p.processBlock(buf, midi);
                host.ppq += (double) fx::kBlock * fx::kBpm / (60.0 * fx::kSampleRate);
            }
            p.setChannelTapForTest(nullptr);
            p.setPlayHead(nullptr);
            p.releaseResources();

            expectEquals(tap.written, fx::kBlock * fx::kBlocks, "every block reached the mixer");
            fx::StreamHash hash;
            for (int b = 0; b < fx::kBlocks; ++b) hash.add(tap.buf, fx::kBlock, b * fx::kBlock);
            logMessage(juce::String(variant == 0 ? "mu-Tronic Rhythm" : "mu-Clid's own file in a slot") + " stream hash 0x"
                       + juce::String::toHexString((juce::int64) hash.h));
            expect(hash.h == fx::kRecordedHash, juce::String(variant == 0 ? "the slot" : "a slot loaded from mu-Clid's file") + " renders the recorded Rhythm stream");
            expect(hash.peak > 0.01f && hash.peak < 4.0f, "and it is a real, bounded sound");
            p.flushPendingAsyncUpdates();
        }
    }

    // The layer steps only while the transport runs, and a restart begins the pattern again.
    void stoppedTransportAndRestart()
    {
        beginTest("a stopped transport fires no hits; a restart plays the pattern from its start");
        mu_seam_test::TempStorage storage;
        RhythmProcessor p;
        expect(p.setLayerType(0, LayerTypeId::Rhythm));
        servicesOf(p).samples.load(0, putSample(p));
        mu_seam_test::setParam(p.apvts, "r0_hitsA", 4.0f);   // steps 0, 4, 8, 12: a hit on every beat, the first at step 0
        {
            Run run(p, 512);
            run.host.playing = false;
            for (int b = 0; b < 60; ++b) run.block();
            expectEquals(run.peak, 0.0f, "stopped: silent");
            expect(! servicesOf(p).transportPlaying.load(), "and the UI sees a stopped transport");

            run.host.playing = true;
            run.host.ppq = 0.0;
            run.block();
            run.block();
            expect(run.peak > 0.001f, "started: the first step sounds at once");
            expect(servicesOf(p).transportPlaying.load());
            for (int b = 0; b < 60; ++b) run.block();

            run.host.playing = false;
            for (int b = 0; b < 100; ++b) run.block();   // the last hit rings out
            run.peak = 0.0f;
            for (int b = 0; b < 10; ++b) run.block();
            expectEquals(run.peak, 0.0f, "stopped again: silent once the sample has gone");

            run.host.playing = true;
            run.host.ppq = 0.0;
            run.block();
            expect(run.peak > 0.001f, "a restart from the start fires step 0 again");
        }
        p.flushPendingAsyncUpdates();
    }

    // The sample library is the type's service: the slots' paths and engines, the primary folder, and the preview that mixes
    // into the master bus through the processor.
    void sampleLibraryAndPreview()
    {
        beginTest("the sample library is shared: per-slot paths and engines, a new layer carries none, and the preview is heard");
        mu_seam_test::TempStorage storage;
        RhythmProcessor p;
        expect(p.findLayerServices(LayerTypeId::Rhythm) == nullptr, "nothing is built before the first Rhythm");
        expect(p.setLayerType(1, LayerTypeId::Rhythm));
        auto* services = p.findLayerServices(LayerTypeId::Rhythm);
        expect(services != nullptr);
        expect(p.setLayerType(6, LayerTypeId::Rhythm));
        expect(p.findLayerServices(LayerTypeId::Rhythm) == services, "the second Rhythm shares them");
        auto& s = servicesOf(p);

        expect(s.getSamplesDir().getFileName() == "Samples" && s.getSamplesDir().getParentDirectory().getFileName() == "muClid",
               "the samples are mu-Clid's own folder, shared with it");
        const auto wav = putSample(p);
        s.samples.load(1, wav);
        expectEquals(s.samples.path(1), wav.getFullPathName());
        expect(rhythmIn(p, 1).voice->hasSample() && ! rhythmIn(p, 6).voice->hasSample(), "a sample belongs to its slot's layer");
        expect(s.getSampleSlotEngine(1) == rhythmIn(p, 1).voice.get());
        expect(s.getSampleSlotEngine(2) == nullptr, "a slot with no Rhythm has no engine");
        expect(! s.samples.isSampleMissing(1));
        expect(s.getSampleSettingsFile() == p.getAppSettingsFile());

        expect(p.setLayerType(1, LayerTypeId::Empty));
        expect(p.setLayerType(1, LayerTypeId::Rhythm));
        expectEquals(s.samples.path(1), juce::String(), "a new layer carries no sample path");
        expect(! rhythmIn(p, 1).voice->hasSample());

        // The preview: decoded on the message thread, mixed into the master bus by the processor after the mixer.
        {
            Run run(p, 256);
            for (int b = 0; b < 10; ++b) run.block();
            expectEquals(run.peak, 0.0f, "nothing sounds first");
            s.samples.startPreview(wav);
            for (int b = 0; b < 10; ++b) run.block();
            expect(run.peak > 0.01f, "the preview is heard on the master bus");
            s.samples.stopPreview();
            for (int b = 0; b < 4; ++b) run.block();
            run.peak = 0.0f;
            for (int b = 0; b < 10; ++b) run.block();
            expectEquals(run.peak, 0.0f, "and stops");
        }
        p.flushPendingAsyncUpdates();
    }

    // MIDI output mode: notes leave through the processor, with the set note, channel and velocity, and no audio.
    void midiOutputMode()
    {
        beginTest("a Rhythm in MIDI output mode sends its notes through the processor and makes no audio; input is not echoed");
        mu_seam_test::TempStorage storage;
        RhythmProcessor p;
        expect(p.setLayerType(0, LayerTypeId::Rhythm));
        servicesOf(p).samples.load(0, putSample(p));
        mu_seam_test::setParam(p.apvts, "r0_hitsA", 4.0f);
        mu_seam_test::setParam(p.apvts, "r0_outMode", 1.0f);
        mu_seam_test::setParam(p.apvts, "r0_midiNote", 50.0f);
        mu_seam_test::setParam(p.apvts, "r0_midiChan", 3.0f);
        mu_seam_test::setParam(p.apvts, "r0_midiVel", 80.0f);
        {
            Run run(p, 512);
            juce::MidiBuffer input;
            input.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0);
            run.block(input);
            for (int b = 0; b < 190; ++b) run.block();   // about 2 s: four quarter-note hits
            expectGreaterThan(run.count(true), 2, "note-ons were sent");
            for (const auto& n : run.notes)
            {
                expectEquals(n.note, 50, "only the layer's own note: the input note 60 is not echoed");
                expectEquals(n.channel, 3);
                if (n.on) expect(std::abs(n.velocity - 80) <= 1, "velocity is 80 % of 100 (got " + juce::String(n.velocity) + ")");
            }
            expect(std::abs(run.count(true) - run.count(false)) <= 1, "every note-on gets its note-off");
            expect(run.peak < 1.0e-6f, "no audio from this rhythm (peak " + juce::String(run.peak) + ")");

            // The note-off follows about 20 ms after its note-on.
            for (size_t i = 0; i + 1 < run.notes.size(); ++i)
                if (run.notes[i].on && ! run.notes[i + 1].on)
                {
                    const int on  = run.notes[i].block * 512 + run.notes[i].offset;
                    const int off = run.notes[i + 1].block * 512 + run.notes[i + 1].offset;
                    expect(std::abs((off - on) - 960) <= 512, "the note is about 20 ms long (got " + juce::String(off - on) + " samples)");
                }
        }
        p.flushPendingAsyncUpdates();

        // Back to Sample mode: the sample plays again and no notes go out.
        mu_seam_test::setParam(p.apvts, "r0_outMode", 0.0f);
        {
            Run run(p, 512);
            for (int b = 0; b < 100; ++b) run.block();
            expect(run.peak > 0.001f, "the sample plays again");
            expectEquals((int) run.notes.size(), 0, "and no notes are sent");
        }

        // A MIDI note still owed when the layer leaves the slot is sent by the tail.
        mu_seam_test::setParam(p.apvts, "r0_outMode", 1.0f);
        {
            Run run(p, 512);
            run.block();   // the hit on step 0: a note-on, its note-off 20 ms later
            expectEquals(run.count(true), 1);
            expectEquals(run.count(false), 0, "the note-off is still owed (the block is 10.7 ms)");
            expect(p.setLayerType(0, LayerTypeId::Empty));
            run.block();
            expectEquals(run.count(false), 1, "the leaving layer's owed note-off is sent");
        }
        p.flushPendingAsyncUpdates();
    }

    // A Rhythm replaced while it sounds fades out through the shared bin and then nothing of it is left; its successor has its
    // own voice, so the tail and the new layer never share one.
    void swapLeavesNothingStuck()
    {
        beginTest("replacing a sounding Rhythm fades it out; the bin drains and the next Rhythm in the slot renders normally");
        mu_seam_test::TempStorage storage;
        RhythmProcessor p;
        expect(p.setLayerType(0, LayerTypeId::Rhythm));
        servicesOf(p).samples.load(0, putSample(p));
        mu_seam_test::setParam(p.apvts, "r0_hitsA", 16.0f);   // a hit on every step
        mu_seam_test::setParam(p.apvts, "r0_aEnvDec", 5.0f);
        mu_seam_test::setParam(p.apvts, "r0_aEnvSus", 100.0f);
        mu_seam_test::setParam(p.apvts, "r0_aEnvRel", 5.0f);   // a long ring, so the fade is what ends it

        PeakTap tap;
        p.setChannelTapForTest(&tap);
        {
            Run run(p, 256);
            for (int b = 0; b < 40; ++b) run.block();
            tap.reset();
            for (int b = 0; b < 10; ++b) run.block();
            expect(tap.peak[0] > 0.01f, "it sounds before the swap");

            expect(p.setLayerType(0, LayerTypeId::Empty));
            tap.reset();
            for (int b = 0; b < 4; ++b) run.block();
            expect(tap.peak[0] > 0.0f, "the tail is still heard while it fades");
            for (int b = 0; b < 12; ++b) run.block();      // 25 ms is five 256-frame blocks
            tap.reset();
            for (int b = 0; b < 20; ++b) run.block();
            expectEquals(tap.peak[0], 0.0f, "after the fade the slot is silent");
            p.flushPendingAsyncUpdates();
            expect(! p.retireBin.hasPendingWork(), "the bin holds nothing once the grace has passed");

            // Back to a Rhythm in the same slot: it has a voice of its own and sounds.
            expect(p.setLayerType(0, LayerTypeId::Rhythm));
            servicesOf(p).samples.load(0, putSample(p));
            mu_seam_test::setParam(p.apvts, "r0_hitsA", 16.0f);   // a new Rhythm starts from the defaults
            for (int b = 0; b < 40; ++b) run.block();
            tap.reset();
            for (int b = 0; b < 10; ++b) run.block();
            expect(tap.peak[0] > 0.01f, "the new Rhythm sounds");

            // Rhythm to Rhythm inside one fade: Empty, then a Rhythm again at once.
            expect(p.setLayerType(0, LayerTypeId::Empty));
            expect(p.setLayerType(0, LayerTypeId::Rhythm));
            servicesOf(p).samples.load(0, putSample(p));
            mu_seam_test::setParam(p.apvts, "r0_hitsA", 16.0f);
            for (int b = 0; b < 60; ++b) run.block();
            tap.reset();
            for (int b = 0; b < 10; ++b) run.block();
            expect(tap.peak[0] > 0.01f, "a successor put back inside the fade sounds");
        }
        p.setChannelTapForTest(nullptr);
        p.flushPendingAsyncUpdates();
    }

    // Layer presets are mu-Clid's own folder and file shape (.muRhythm, r0_ properties); the layer's data travels with them
    // in both directions.
    void presets()
    {
        beginTest("a Rhythm preset round trips through a slot in mu-Clid's file shape and folder, and mu-Clid's own file loads");
        mu_seam_test::TempStorage storage;
        RhythmProcessor p;
        expect(p.setLayerType(2, LayerTypeId::Rhythm));
        const auto wav = putSample(p);
        servicesOf(p).samples.load(2, wav);
        mu_clid_fixture::configure(p.apvts, rhythmIn(p, 2), 2);   // a pattern with an accent ring, a stepped modulator
        mu_seam_test::setParam(p.apvts, "r2_hitsA", 6.0f);
        mu_seam_test::setParam(p.apvts, "r2_fltType", 5.0f);
        mu_seam_test::setParam(p.apvts, "r2_drvChar", 3.0f);
        mu_seam_test::setParam(p.apvts, "r2_fltCut", 2400.0f);

        p.saveLayerPreset(2, "RhythmRT");
        const auto dir = p.typedLayerPresetDir(2);
        const auto file = dir.getChildFile("RhythmRT." + p.typedLayerPresetExtension(2));
        expect(file.existsAsFile(), "written into the type's folder");
        expectEquals(dir.getFileName(), juce::String("Rhythms"));
        expectEquals(dir.getParentDirectory().getFileName(), juce::String("muClid"), "mu-Clid's own folder, not a copy under mu-Tronic");
        expectEquals(p.typedLayerPresetExtension(2), juce::String("muRhythm"));
        expectEquals(p.slotPresetFiles(2).size(), 1, "the saved file is listed for a Rhythm slot");
        expectEquals(p.slotPresetFiles(0).size(), 1, "and for an Empty one");

        // The file is mu-Clid's shape: its own reader gives the slot's rhythm.
        const auto xml = juce::XmlDocument::parse(file);
        expect(xml != nullptr);
        if (xml != nullptr)
        {
            expectEquals(xml->getTagName(), juce::String("MuClidRhythm"));
            expect(xml->hasAttribute("r0_hitsA") && xml->hasAttribute("r0_sample"), "r0_ properties, as mu-Clid writes");
            expect(xml->getStringAttribute("r0_fltType").containsAnyOf("ABCDEFGHIJKLMNOPQRSTUVWXYZ"), "an algorithm row by its stable name");
            expectEquals(xml->getStringAttribute("r0_patLeg"), juce::String("true"), "a bool as true / false");
            const auto tree = juce::ValueTree::fromXml(*xml);
            const auto read = mu_clid::rhythm_preset::readRhythm(tree, "r0_", {}, "round trip");
            expectEquals(read.genA.hits, 6);
            expectEquals(read.genA.rotate, 1);
            expectEquals(read.genC.steps, 4);
            expectWithinAbsoluteError(read.voiceParams.filterCutoff, 2400.0f, 2.0f);
            expect(read.patternLegato);
            expectEquals((int) read.modulationMatrix.getAssignments().size(), 1, "mu-Clid's reader finds the modulator");
        }

        // Into an Empty slot of another processor: the file's type is adopted and everything arrives.
        mu_seam_test::TempStorage storage2;
        RhythmProcessor q;
        putSample(q);
        juce::String error;
        q.onLoadError = [&error](const juce::String& m) { error = m; };
        int committed = -1;
        q.onLayerPresetCommitted = [&committed](int s) { committed = s; };
        q.loadLayerPreset(5, file);
        expect(q.getLayerTypeId(5) == LayerTypeId::Rhythm, "an Empty slot adopts the file's type");
        expectEquals(error, juce::String());
        expectEquals(committed, 5);
        expectEquals(rhythmIn(q, 5).genA.hits, 6);
        expectEquals(rhythmIn(q, 5).genA.rotate, 1);
        expect(rhythmIn(q, 5).patternLegato);
        expectWithinAbsoluteError(getPlain(q.apvts, "r5_fltCut"), 2400.0f, 2.0f);
        expectEquals(getPlain(q.apvts, "r5_fltType"), getPlain(p.apvts, "r2_fltType"), "an algorithm row survives the file's name for it");
        expectEquals(getPlain(q.apvts, "r5_drvChar"), 3.0f);
        expect(rhythmIn(q, 5).voice->hasSample(), "the sample is found again by its relative path");
        expectEquals((int) rhythmIn(q, 5).modulationMatrix.getAssignments().size(), 1);

        // Into a filled slot (stopped: applies at once); a second load replaces the modulators rather than adding to them.
        expect(q.setLayerType(7, LayerTypeId::Rhythm));
        q.loadLayerPreset(7, file);
        q.loadLayerPreset(7, file);
        expectEquals(rhythmIn(q, 7).genA.hits, 6);
        expectEquals((int) rhythmIn(q, 7).modulationMatrix.getAssignments().size(), 1);

        // What mu-Clid itself wrote, with its sample resolved against the shared Samples folder.
        const auto real = juce::File(MU_TRONIC_FIXTURE_DIR).getChildFile("REAL_rhythm.muRhythm");
        expect(real.existsAsFile());
        const auto realTree = juce::ValueTree::fromXml(*juce::XmlDocument::parse(real));
        const auto expected = mu_clid::rhythm_preset::readRhythm(realTree, "r0_", {}, "mu-Clid's file");
        q.loadLayerPreset(7, real);
        expectEquals(error, juce::String());
        expectEquals(rhythmIn(q, 7).genA.hits, expected.genA.hits);
        expectEquals(rhythmIn(q, 7).genA.rotate, expected.genA.rotate);
        expectEquals(rhythmIn(q, 7).genC.steps, expected.genC.steps);
        expectEquals(rhythmIn(q, 7).voiceParams.pitchSemitones, expected.voiceParams.pitchSemitones);
        expectEquals(rhythmIn(q, 7).voiceParams.filterCutoff, expected.voiceParams.filterCutoff);
        expect(rhythmIn(q, 7).voice->hasSample());

        // The folder's files are listed for a Rhythm slot and for an Empty one; a file of another type is refused by name.
        expectEquals(q.slotPresetFiles(7).size(), 0, "another content folder has none");
        expect(q.setLayerType(9, LayerTypeId::Arp));
        error = {};
        q.loadLayerPreset(9, file);
        expectEquals(error, juce::String("This is a Rhythm preset; this slot is an Arp"));
        expect(q.getLayerTypeId(9) == LayerTypeId::Arp, "and the Arp stays");
        p.flushPendingAsyncUpdates();
        q.flushPendingAsyncUpdates();
    }

    // A preset chosen while the transport plays waits for the pattern's wrap and reads its sample at stage time.
    void stagedPreset()
    {
        beginTest("a preset chosen while playing lands at the pattern's wrap, with its sample read when it was staged");
        mu_seam_test::TempStorage storage;
        RhythmProcessor p;
        const auto wav = putSample(p);   // creates the services' folder; the Rhythm below shares it
        expect(p.setLayerType(0, LayerTypeId::Rhythm));
        const auto real = juce::File(MU_TRONIC_FIXTURE_DIR).getChildFile("REAL_rhythm.muRhythm");
        int committed = -1;
        p.onLayerPresetCommitted = [&committed](int s) { committed = s; };
        {
            Run run(p, 512);
            for (int b = 0; b < 30; ++b) run.block();
            p.loadLayerPreset(0, real);
            expect(p.hasPendingSwap(0), "staged, not applied: the transport runs");
            expectEquals(rhythmIn(p, 0).genA.rotate, 0, "the old pattern is still the layer's");
            expect(! rhythmIn(p, 0).voice->hasSample());

            // The sample was read at stage time: the file can go before the boundary.
            expect(wav.deleteFile());
            for (int b = 0; b < 600 && committed < 0; ++b) { run.block(); p.drainDeferredWork(); }
            expectEquals(committed, 0, "it landed at the wrap");
            expectEquals(rhythmIn(p, 0).genA.rotate, 1);
            expect(rhythmIn(p, 0).voice->hasSample(), "with the sample that was read when it was staged");
            expect(! servicesOf(p).samples.isSampleMissing(0));
            for (int b = 0; b < 100; ++b) run.block();
            expect(run.peak > 0.001f, "and it plays");
        }
        p.flushPendingAsyncUpdates();
    }

    // The processor goes with Rhythms in slots and a replaced one possibly in the bin; the services outlive both and take
    // their parameter listeners off with them.
    void servicesLifetime()
    {
        beginTest("the processor is destroyed with Rhythms in its slots and a tail in the bin, and the services listen to nothing after");
        mu_seam_test::TempStorage storage;
        {
            auto box = std::make_unique<RhythmProcessor>();
            auto& p = *box;
            expect(p.setLayerType(0, LayerTypeId::Rhythm));
            expect(p.setLayerType(1, LayerTypeId::Rhythm));
            servicesOf(p).samples.load(0, putSample(p));
            p.prepareToPlay(48000.0, 256);
            juce::AudioBuffer<float> buf(2, 256);
            juce::MidiBuffer midi;
            mu_golden::MockHost host;
            p.setPlayHead(&host);
            for (int b = 0; b < 8; ++b) { buf.clear(); p.processBlock(buf, midi); host.ppq += 0.01; }
            expect(p.setLayerType(0, LayerTypeId::Empty));   // the old layer fades in the bin
            buf.clear();
            p.processBlock(buf, midi);
            p.setPlayHead(nullptr);
            box.reset();   // layers in slots, a tail possibly in the bin, services with listeners
        }
        expect(true, "no crash");
    }

    // Type changes under a running audio thread, with a Rhythm that keeps playing a sample and one in MIDI mode: no
    // allocation on the audio thread.
    void typeChangeStress()
    {
        beginTest("two-thread stress with real Rhythms in the mix: audio thread allocation-free");
        constexpr int kChanges = 600;
        constexpr int kBlock = 128;

        mu_seam_test::TempStorage storage;
        RhythmProcessor p;
        mu_golden::MockHost host;
        p.setPlayHead(&host);
        p.setRateAndBufferSizeDetails(48000.0, kBlock);
        p.prepareToPlay(48000.0, kBlock);
        expect(p.setLayerType(0, LayerTypeId::Rhythm));   // one Rhythm that stays, playing its sample
        servicesOf(p).samples.load(0, putSample(p));
        mu_seam_test::setParam(p.apvts, "r0_hitsA", 8.0f);
        expect(p.setLayerType(15, LayerTypeId::Rhythm));  // and one that stays in MIDI output mode
        mu_seam_test::setParam(p.apvts, "r15_outMode", 1.0f);
        mu_seam_test::setParam(p.apvts, "r15_hitsA", 8.0f);

        {   // the first block after a prepare sizes the host MIDI buffer (one allocation, at start-up): not counted here
            juce::AudioBuffer<float> warm(2, kBlock);
            juce::MidiBuffer warmMidi;
            p.processBlock(warm, warmMidi);
        }
        std::atomic<bool> stop { false };
        std::atomic<int> blocks { 0 }, allocs { 0 }, frees { 0 }, noteOns { 0 };
        juce::String firstOffender;   // written by the audio thread, read after it has joined
        mu_alloc::captureBudget = 3;
        std::thread audio([&]
        {
            juce::AudioBuffer<float> buf(2, kBlock);
            juce::MidiBuffer midi;
            midi.ensureSize(2048);   // the host's buffer keeps its storage between blocks
            while (! stop.load())
            {
                midi.clear();
                buf.clear();
                {
                    mu_alloc::Armed armed;
                    p.processBlock(buf, midi);
                    armed.stop();
                    allocs += armed.allocs();
                    frees += armed.frees();
                    if (armed.allocs() > 0 && firstOffender.isEmpty()) firstOffender = armed.firstAllocStack();
                }
                for (const auto meta : midi) if (meta.getMessage().isNoteOn()) ++noteOns;
                host.timeInSamples += kBlock;
                host.ppq += (double) kBlock * host.bpm / (60.0 * 48000.0);
                blocks.fetch_add(1);
            }
        });
        bool allOk = true;
        for (int i = 0; i < kChanges; ++i)
        {
            const int slot = 1 + i % 7;
            const LayerTypeId cycle[] = { LayerTypeId::Rhythm, LayerTypeId::Empty, LayerTypeId::Rhythm, LayerTypeId::Arp };
            allOk = p.setLayerType(slot, cycle[(i / 7) % 4]) && allOk;
            // The rows of the playing Rhythm move while it renders: the pattern and the voice are rebuilt under the locks.
            mu_seam_test::setParam(p.apvts, "r0_hitsA", (float) (1 + i % 8));
            mu_seam_test::setParam(p.apvts, "r0_fltCut", 500.0f + (float) (i % 20) * 300.0f);
            if (i % 50 == 0) p.flushPendingAsyncUpdates();
        }
        stop.store(true);
        audio.join();
        p.flushPendingAsyncUpdates();
        p.setPlayHead(nullptr);
        p.releaseResources();
        logMessage("Rhythm type-change stress: " + juce::String(kChanges) + " changes, " + juce::String(blocks.load()) + " blocks, "
                   + juce::String(noteOns.load()) + " note-ons");
        expect(allOk, "every type change succeeded");
        if (firstOffender.isNotEmpty()) logMessage("first allocation on the audio thread:\n" + firstOffender);
        expectEquals(allocs.load() + frees.load(), 0, "the audio thread allocated or freed");
        expectGreaterThan(blocks.load(), 50, "audio blocks ran during the type changes");
        expectGreaterThan(noteOns.load(), 0, "the MIDI-mode Rhythm sent notes");
    }

    // A host MIDI buffer that has never held anything: the first block after prepare sizes it, so the merge of the layers'
    // notes costs at most that one allocation, and none afterwards.
    void midiBufferRoom()
    {
        beginTest("a zero-capacity host MIDI buffer: at most one allocation on block 1, none after, with notes sent");
        mu_seam_test::TempStorage storage;
        RhythmProcessor p;
        mu_golden::MockHost host;
        p.setPlayHead(&host);
        p.setRateAndBufferSizeDetails(48000.0, 256);
        p.prepareToPlay(48000.0, 256);
        expect(p.setLayerType(0, LayerTypeId::Rhythm));
        mu_seam_test::setParam(p.apvts, "r0_hitsA", 16.0f);
        mu_seam_test::setParam(p.apvts, "r0_outMode", 1.0f);

        juce::MidiBuffer midi;   // never sized
        juce::AudioBuffer<float> buf(2, 256);
        int firstBlockAllocs = 0, laterAllocs = 0, notes = 0;
        mu_alloc::captureBudget = 3;
        for (int b = 0; b < 200; ++b)
        {
            midi.clear();
            buf.clear();
            int a = 0;
            {
                mu_alloc::Armed armed;
                p.processBlock(buf, midi);
                armed.stop();
                a = armed.allocs();
            }
            (b == 0 ? firstBlockAllocs : laterAllocs) += a;
            for (const auto meta : midi) if (meta.getMessage().isNoteOn()) ++notes;
            host.timeInSamples += 256;
            host.ppq += 256.0 * host.bpm / (60.0 * 48000.0);
        }
        p.setPlayHead(nullptr);
        p.releaseResources();
        expect(firstBlockAllocs <= 1, "block 1: " + juce::String(firstBlockAllocs) + " allocations");
        expectEquals(laterAllocs, 0, "no allocation after block 1");
        expectGreaterThan(notes, 4, "and the notes went out");
        p.flushPendingAsyncUpdates();
    }

    // A Rhythm retired and put back inside the fade, a sample loaded into the slot and rows written from a second thread
    // the whole time, under a running audio thread: nothing of the tail is touched by the new layer's setup.
    void retireWhileWritingParameters()
    {
        beginTest("retire and put back inside the fade while a second thread writes rows and a sample loads: no crash, no allocation");
        constexpr int kRounds = 150;
        constexpr int kBlock = 128;
        mu_seam_test::TempStorage storage;
        RhythmProcessor p;
        mu_golden::MockHost host;
        p.setPlayHead(&host);
        p.setRateAndBufferSizeDetails(48000.0, kBlock);
        p.prepareToPlay(48000.0, kBlock);
        const auto wav = putSample(p);
        expect(p.setLayerType(0, LayerTypeId::Rhythm));
        servicesOf(p).samples.load(0, wav);
        mu_seam_test::setParam(p.apvts, "r0_hitsA", 16.0f);

        {
            juce::AudioBuffer<float> warm(2, kBlock);
            juce::MidiBuffer warmMidi;
            p.processBlock(warm, warmMidi);   // the first block sizes the host MIDI buffer (not counted)
        }
        std::atomic<bool> stop { false }, stopWriter { false };
        std::atomic<int> blocks { 0 }, allocs { 0 };
        std::thread audio([&]
        {
            juce::AudioBuffer<float> buf(2, kBlock);
            juce::MidiBuffer midi;
            midi.ensureSize(2048);
            while (! stop.load())
            {
                midi.clear();
                buf.clear();
                {
                    mu_alloc::Armed armed;
                    p.processBlock(buf, midi);
                    armed.stop();
                    allocs += armed.allocs() + armed.frees();
                }
                host.timeInSamples += kBlock;
                host.ppq += (double) kBlock * host.bpm / (60.0 * 48000.0);
                blocks.fetch_add(1);
            }
        });
        std::thread writer([&]
        {
            int i = 0;
            while (! stopWriter.load())
            {
                mu_seam_test::setParam(p.apvts, "r0_hitsA", (float) (1 + i % 16));
                mu_seam_test::setParam(p.apvts, "r0_pitchSemi", (float) (i % 12));
                mu_seam_test::setParam(p.apvts, "r0_fltCut", 400.0f + (float) (i % 30) * 200.0f);
                ++i;
                juce::Thread::sleep(1);
            }
        });
        bool allOk = true;
        for (int i = 0; i < kRounds; ++i)
        {
            allOk = p.setLayerType(0, LayerTypeId::Empty) && allOk;
            allOk = p.setLayerType(0, LayerTypeId::Rhythm) && allOk;   // inside the tail's 25 ms fade
            servicesOf(p).samples.load(0, wav);
            p.drainDeferredWork();                                       // the queued rows reach the new layer
            if (i % 25 == 0) p.flushPendingAsyncUpdates();
        }
        stopWriter.store(true);
        writer.join();
        stop.store(true);
        audio.join();
        p.drainDeferredWork();
        p.flushPendingAsyncUpdates();
        p.setPlayHead(nullptr);
        p.releaseResources();
        expect(allOk, "every type change succeeded");
        expectEquals(allocs.load(), 0, "the audio thread allocated or freed");
        expectGreaterThan(blocks.load(), 50, "audio blocks ran");
    }
};

static MuTronicRhythmTests muTronicRhythmTests;
