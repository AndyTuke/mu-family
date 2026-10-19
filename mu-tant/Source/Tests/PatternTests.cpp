// mu-tant voice layer (Pattern) tests: copying a voice (remove / swap) carries its data and
// leaves the destination's engine state alone.

#include <juce_core/juce_core.h>
#include "Sequencer/Pattern.h"
#include "Sequencer/GatePatternSerialise.h"
#include <atomic>
#include <memory>
#include <thread>

class PatternTest : public juce::UnitTest
{
public:
    PatternTest() : juce::UnitTest("mu-tant voice layer (Pattern)", "mu-tant") {}

    void runTest() override
    {
        using namespace mu_tant;

        beginTest("a copy carries the envelopes, user wavetables and modulation");
        {
            Pattern src;
            src.gate.addEnvelope({ 2, 3, 0.1f, 0.0f, 0.0f, false });
            src.filterGate.addEnvelope({ 0, 4, 0.0f, 0.0f, 0.0f, false });
            src.pitchGate.addEnvelope({ 8, 2, 0.0f, 0.0f, 0.0f, false });
            src.osc1UserPath  = "C:/tables/a.wav";
            src.osc1UserIndex = 7;
            src.name          = "voice";

            Pattern dst;
            dst = src;
            expectEquals((int) dst.gate.envelopes.size(), 1);
            expectEquals(dst.gate.envelopes[0].startCell, 2);
            expectEquals((int) dst.filterGate.envelopes.size(), 1);
            expectEquals((int) dst.pitchGate.envelopes.size(), 1);
            expectEquals(dst.osc1UserPath, juce::String("C:/tables/a.wav"));
            expectEquals(dst.osc1UserIndex.load(), 7);
            expectEquals(dst.osc2UserIndex.load(), -1);
            expect(dst.name == "voice", "the shared layer data should copy too");
        }

        beginTest("a copy leaves the destination's engine state alone");
        {
            Pattern src, dst;
            int owner = 0;
            dst.host.ptr = reinterpret_cast<PatternHost*>(&owner);   // only compared, never used
            dst.osc1SemiStepped.store(true);
            dst.ring.writeHead.store(123);

            dst = src;
            expect(dst.host.ptr == reinterpret_cast<PatternHost*>(&owner), "the host link must survive an assignment");
            expect(dst.osc1SemiStepped.load(), "the stepped-pitch flag is derived state, not copied");
            expectEquals(dst.ring.writeHead.load(), 123);   // the level tap keeps running
        }

        beginTest("an assignment and an adoptDefaults leave the destination's insert alone");
        {
            Pattern src, dst;
            const InsertProcessor* kept = &*dst.insert;
            dst.insert->grReduction.store(0.5f);
            src.insert->grReduction.store(0.9f);
            dst = src;
            expect(&*dst.insert == kept, "the insert object stays with its slot");
            expectEquals(dst.insert->grReduction.load(), 0.5f);   // the source's meter is not copied
            dst.adoptDefaults(src);
            expectEquals(dst.insert->grReduction.load(), 0.5f);
            expect(dst.insertGainReduction() == &dst.insert->grReduction, "the layer reports its own insert's meter");
        }

        beginTest("a default Pattern is the reset state");
        {
            Pattern p;
            p.gate.addEnvelope({ 1, 1, 0.0f, 0.0f, 0.0f, false });
            p.osc2UserPath  = "x";
            p.osc2UserIndex = 3;
            p = Pattern{};
            expect(p.gate.envelopes.empty(), "the gate should clear");
            expect(p.osc2UserPath.isEmpty(), "the user wavetable path should clear");
            expectEquals(p.osc2UserIndex.load(), -1);
        }

        beginTest("a reset keeps the addresses the editor holds into the modulators");
        {
            Pattern live;
            live.controlSequences[2].stepValues = { 10.0f, 20.0f, 30.0f };
            ControlSequence* before[8];
            for (int i = 0; i < 8; ++i) before[i] = &live.controlSequences[(size_t) i];
            const auto* matrixBefore = &live.modulationMatrix;

            auto fresh = std::make_unique<Pattern>();
            live.adoptDefaults(*fresh);

            bool sameAddresses = true;
            for (int i = 0; i < 8; ++i) sameAddresses = sameAddresses && before[i] == &live.controlSequences[(size_t) i];
            expect(sameAddresses, "a modulator editor's ControlSequence pointers must survive a reset");
            expect(matrixBefore == &live.modulationMatrix, "the matrix address must survive a reset");
            expect(live.controlSequences[2].stepValues.size() != 3, "the live modulator should hold the default data");
            expectEquals((int) fresh->controlSequences[2].stepValues.size(), 3);   // the old data left with `fresh`
        }

        beginTest("a reset hands the old envelopes and paths to the discarded default, with no copy");
        {
            Pattern live;
            for (int c = 0; c < 3; ++c)
            {
                live.gate.addEnvelope({ c * 4, 3, 0.0f, 0.0f, 0.0f, false });
                live.filterGate.addEnvelope({ c * 4, 3, 0.0f, 0.0f, 0.0f, false });
                live.pitchGate.addEnvelope({ c * 4, 3, 0.0f, 0.0f, 0.0f, false });
            }
            live.gate.subdivision = GatePattern::Subdivision::ThirtySecond;
            live.osc1UserPath  = "C:/tables/a-long-path-that-needs-heap/a.wav";
            live.osc1UserIndex = 4;
            const GateEnvelope* gateBuf   = live.gate.envelopes.data();
            const GateEnvelope* filterBuf = live.filterGate.envelopes.data();
            const GateEnvelope* pitchBuf  = live.pitchGate.envelopes.data();
            const juce::String::CharPointerType pathBuf = live.osc1UserPath.getCharPointer();

            auto fresh = std::make_unique<Pattern>();
            live.adoptDefaults(*fresh);

            expect(live.gate.envelopes.empty() && live.filterGate.envelopes.empty() && live.pitchGate.envelopes.empty(),
                   "the live voice should hold the default (empty) envelopes");
            expect(! live.gate.hasEnvelopes.load(), "the lock-free flag must follow the exchange");
            expect(live.gate.subdivision == GatePattern::Subdivision::Sixteenth, "the grid should be the default");
            expect(live.osc1UserPath.isEmpty());
            expectEquals(live.osc1UserIndex.load(), -1);
            expectEquals((int) fresh->gate.envelopes.size(), 3);
            expect(fresh->gate.hasEnvelopes.load());
            expect(fresh->gate.envelopes.data() == gateBuf && fresh->filterGate.envelopes.data() == filterBuf
                   && fresh->pitchGate.envelopes.data() == pitchBuf,
                   "the envelope buffers should move to the discarded default, not be copied");
            expect(fresh->osc1UserPath.getCharPointer() == pathBuf, "the path string should move, not be copied");
        }

        beginTest("a reset empties the envelopes even when an envelope's edit lock stays busy");
        {
            Pattern live;
            live.gate.addEnvelope({ 0, 3, 0.0f, 0.0f, 0.0f, false });
            live.filterGate.addEnvelope({ 1, 2, 0.0f, 0.0f, 0.0f, false });
            live.pitchGate.addEnvelope({ 2, 1, 0.0f, 0.0f, 0.0f, false });
            live.gate.subdivision = GatePattern::Subdivision::ThirtySecond;
            live.gate.gateLevel   = 0.7f;

            // A helper thread holds the gate's lock for the whole reset, as an edit that never ends would.
            std::atomic<bool> held { false }, release { false };
            std::thread editor([&]
            {
                while (! mu_core::spinLockFor(live.gate.editLock, 1000)) {}
                held.store(true);
                while (! release.load()) std::this_thread::yield();
                mu_core::spinUnlock(live.gate.editLock);
            });
            while (! held.load()) std::this_thread::yield();

            auto fresh = std::make_unique<Pattern>();
            live.adoptDefaults(*fresh);   // what resetLayer calls, with the audio excluded

            release.store(true);
            editor.join();

            expect(live.gate.envelopes.empty() && live.filterGate.envelopes.empty() && live.pitchGate.envelopes.empty(),
                   "every envelope should be cleared, including the one whose lock was busy");
            expect(! live.gate.hasEnvelopes.load(), "the lock-free flag must follow the clear");
            expect(live.gate.subdivision == GatePattern::Subdivision::Sixteenth, "the grid should be the default");
            expectEquals(live.gate.gateLevel, 0.0f);
            expect(! live.gate.editLock.load(), "the fallback must not leave the lock taken");
        }

        beginTest("a reset equals a fresh Pattern while another thread keeps deserialising into the gate");
        {
            // A preset load (deserialiseGate) takes editLock around its rebuild, so a reset taking the
            // same lock must never overlap it. The writer stops before the final reset; that reset must
            // then leave exactly the default state.
            Pattern live;
            GatePattern source;
            for (int i = 0; i < 12; ++i) source.addEnvelope({ i * 2, 2, 0.0f, 0.0f, 0.0f, false });
            const auto tree = serialiseGate(source, "Gate");

            std::atomic<bool> stop { false };
            std::thread writer([&]
            {
                while (! stop.load())
                    deserialiseGate(tree, live.gate);
            });
            for (int i = 0; i < 300; ++i)
            {
                auto fresh = std::make_unique<Pattern>();
                live.adoptDefaults(*fresh);
            }
            stop.store(true);
            writer.join();

            auto fresh = std::make_unique<Pattern>();
            live.adoptDefaults(*fresh);
            const Pattern reference;
            expect(live.gate.envelopes.empty() && live.filterGate.envelopes.empty() && live.pitchGate.envelopes.empty(),
                   "the final reset should leave no envelopes");
            expect(! live.gate.hasEnvelopes.load(), "the lock-free flag must follow the clear");
            expect(live.gate.subdivision == reference.gate.subdivision, "the grid should be the default");
            expectEquals(live.gate.patternLengthBars.load(), reference.gate.patternLengthBars.load());
            expect(! live.gate.editLock.load(), "no lock may be left taken");
        }

        beginTest("copyDataFrom holds the destination's lock and skips the copy when it stays busy");
        {
            GatePattern src, dst;
            src.addEnvelope({ 1, 2, 0.0f, 0.0f, 0.0f, false });
            dst.editLock.store(true);                        // an edit in progress on the destination
            expect(! dst.copyDataFrom(src), "a busy destination lock should fail the copy");
            expect(dst.envelopes.empty(), "a failed copy must not touch the destination");
            expect(! src.editLock.load(), "a failed copy must release the source lock");
            dst.editLock.store(false);
            expect(dst.copyDataFrom(src) && dst.envelopes.size() == 1);
            expect(! dst.editLock.load() && ! src.editLock.load(), "both locks should be released");
        }

        beginTest("a swap through a temporary exchanges the voices' data");
        {
            Pattern a, b;
            a.gate.addEnvelope({ 0, 2, 0.0f, 0.0f, 0.0f, false });
            a.osc1UserPath = "a";
            b.pitchGate.addEnvelope({ 4, 1, 0.0f, 0.0f, 0.0f, false });
            b.osc1UserPath = "b";

            const Pattern tmp = a;
            a = b;
            b = tmp;
            expect(a.gate.envelopes.empty() && a.pitchGate.envelopes.size() == 1, "a should now hold b's envelopes");
            expect(b.gate.envelopes.size() == 1 && b.pitchGate.envelopes.empty(), "b should now hold a's envelopes");
            expectEquals(a.osc1UserPath, juce::String("b"));
            expectEquals(b.osc1UserPath, juce::String("a"));
        }
    }
};

static PatternTest patternTest;
