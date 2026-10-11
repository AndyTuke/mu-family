// Master-loop wrap detection: a loop-length change or a reset request between blocks must not read
// as a wrap, because a false wrap commits a staged hot-swap early.

#include <juce_core/juce_core.h>
#include "Sequencer/GeneratorAliases.h"

using namespace mu_clid;

class SequencerEngineWrapTest : public juce::UnitTest
{
public:
    SequencerEngineWrapTest() : juce::UnitTest("Sequencer master-loop wrap", "SequencerEngine") {}

    void runTest() override
    {
        // Beat position of the start of 16th-note step `s`.
        auto beat = [](int s) { return s * SequencerEngine::StepLengthBeats; };

        beginTest("A genuine wrap is reported once");
        {
            SequencerEngine e;
            e.addRhythm(Rhythm{});
            e.setMasterLoopSteps(16);
            expect(! e.processBlock(beat(14)).masterLoopWrapped);
            expect(! e.processBlock(beat(15)).masterLoopWrapped);
            expect(e.processBlock(beat(16)).masterLoopWrapped);
            expect(! e.processBlock(beat(17)).masterLoopWrapped);
        }

        beginTest("Changing the loop length between blocks gives no false wrap");
        {
            SequencerEngine e;
            e.addRhythm(Rhythm{});
            e.setMasterLoopSteps(32);
            e.processBlock(beat(20));                        // effective step 20
            e.setMasterLoopSteps(16);                        // next block would be step 5 < 20
            expect(! e.processBlock(beat(21)).masterLoopWrapped);
            expect(! e.processBlock(beat(22)).masterLoopWrapped);
            expect(e.processBlock(beat(32)).masterLoopWrapped);   // and the next real wrap still shows
        }

        beginTest("Setting the same loop length keeps the wrap history");
        {
            SequencerEngine e;
            e.addRhythm(Rhythm{});
            e.setMasterLoopSteps(16);
            e.processBlock(beat(15));
            e.setMasterLoopSteps(16);
            expect(e.processBlock(beat(16)).masterLoopWrapped);
        }

        beginTest("A reset request drops the history for the next block only");
        {
            SequencerEngine e;
            e.addRhythm(Rhythm{});
            e.setMasterLoopSteps(16);
            e.processBlock(beat(15));
            e.resetWrapDetector();
            expect(! e.processBlock(beat(16)).masterLoopWrapped);
            e.processBlock(beat(17));
            expect(e.processBlock(beat(32)).masterLoopWrapped);
        }
    }
};

static SequencerEngineWrapTest sequencerEngineWrapTest;
