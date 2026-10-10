// mu-tant voice layer (Pattern) tests: copying a voice (remove / swap) carries its data and
// leaves the destination's engine state alone.

#include <juce_core/juce_core.h>
#include "Sequencer/Pattern.h"

// Pattern's persistence hooks belong to the processor; the test target does not link it.
void mu_tant::Pattern::writeExtras(juce::ValueTree&) const {}
void mu_tant::Pattern::applyExtras(const juce::ValueTree&) {}

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
            dst.owner.ptr = reinterpret_cast<PluginProcessor*>(&owner);   // only compared, never used
            dst.osc1SemiStepped.store(true);
            dst.ring.writeHead.store(123);

            dst = src;
            expect(dst.owner.ptr == reinterpret_cast<PluginProcessor*>(&owner), "the processor link must survive an assignment");
            expect(dst.osc1SemiStepped.load(), "the stepped-pitch flag is derived state, not copied");
            expectEquals(dst.ring.writeHead.load(), 123);   // the level tap keeps running
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
