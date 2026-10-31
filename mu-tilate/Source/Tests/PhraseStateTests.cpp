// Saving a Phrase: the slices (with pins), the timeline blocks (with lengths and reverse) and the loop reference
// survive a round trip through the layer node, a missing child is the default, and a layer whose loop arrives later
// applies its saved edits only to a loop of the same length.

#include "Sequencer/Phrase.h"

#include <juce_core/juce_core.h>

using namespace mu_tilate;

namespace
{
std::shared_ptr<const juce::AudioBuffer<float>> loopOf(int n)
{
    auto b = std::make_shared<juce::AudioBuffer<float>>(1, n);
    b->clear();
    return b;
}
}

struct PhraseStateTests : public juce::UnitTest
{
    PhraseStateTests() : juce::UnitTest("Phrase state", "mu-tilate") {}

    void runTest() override
    {
        beginTest("slices, pins, blocks and the loop reference round-trip");
        {
            PhraseState in;
            in.loopPath = "D:/Loops/break.wav"; in.loopName = "break"; in.loopLength = 96000;
            in.slices = SliceList(96000);
            in.slices.applyDetected({ 12000, 30000, 60000 });
            in.slices.setPinned(2, true);
            in.timeline = Timeline(8.0);
            in.timeline.place(2, 0.0, 0.0); in.timeline.place(1, 1.5, 0.0);
            in.timeline.setBlockLength(1, 0.5, 0.0);
            in.timeline.setReverse(1, true);

            juce::ValueTree node("Layer");
            writePhraseState(node, in);
            writePhraseState(node, in);                                   // writing twice replaces, never doubles
            expectEquals(node.getNumChildren(), 1);

            const auto out = readPhraseState(node);
            expectEquals(out.loopPath, in.loopPath);
            expectEquals(out.loopName, in.loopName);
            expectEquals(out.loopLength, 96000);
            expectEquals(out.slices.size(), 4);
            expectEquals(out.slices.marker(1).position, 12000);
            expect(out.slices.marker(2).pinned);
            expect(! out.slices.marker(1).pinned);
            expectEquals(out.timeline.lengthBeats(), 8.0);
            expectEquals(out.timeline.size(), 2);
            expectEquals(out.timeline.blocks()[0].slice, 2);
            expectEquals(out.timeline.blocks()[1].start, 1.5);
            expectEquals(out.timeline.blocks()[1].length, 0.5);
            expect(out.timeline.blocks()[1].reverse);
        }

        beginTest("a node with no Phrase child reads as the default");
        {
            const auto out = readPhraseState(juce::ValueTree("Layer"));
            expect(out.loopPath.isEmpty());
            expectEquals(out.loopLength, 0);
            expectEquals(out.timeline.size(), 0);
            expectEquals(out.timeline.lengthBeats(), 16.0);
        }

        beginTest("a layer saves, and a fresh layer applies the file; the loop arriving later takes the saved edits");
        {
            Phrase a;
            a.installLoop(loopOf(96000), 48000.0, "p.wav", "p");
            auto d = a.getData();
            SliceList sl(96000); sl.applyDetected({ 24000, 48000 });
            Timeline tl(8.0); tl.place(1, 0.0, 0.0); tl.place(2, 2.0, 0.0);
            a.commitEdit(sl, tl);

            juce::ValueTree node("Layer");
            a.writeExtras(node);

            Phrase b;
            b.applyExtras(node);                                          // no loop yet: the edits wait
            expect(b.getData() == nullptr);
            expectEquals(b.loopPath(), juce::String("p.wav"));
            b.installLoop(loopOf(96000), 48000.0, "p.wav", "p");
            expectEquals(b.getData()->slices.size(), 3);
            expectEquals(b.getData()->timeline.size(), 2);
            expectEquals(b.loopBeats(), 8.0);

            Phrase c;
            c.applyExtras(node);
            c.installLoop(loopOf(50000), 48000.0, "other.wav", "other"); // a different loop: the slices do not apply
            expectEquals(c.getData()->slices.size(), 1);
            expectEquals(c.getData()->timeline.size(), 2);                // the timeline is the layer's and stays
        }

        beginTest("applying a file to a layer with a loop keeps the audio and takes the saved edits");
        {
            Phrase a;
            a.installLoop(loopOf(96000), 48000.0, "p.wav", "p");
            SliceList sl(96000); sl.applyDetected({ 48000 });
            Timeline tl(4.0); tl.place(1, 1.0, 0.0);
            a.commitEdit(sl, tl);
            juce::ValueTree node("Layer");
            a.writeExtras(node);

            Phrase b;
            b.installLoop(loopOf(96000), 48000.0, "p.wav", "p");
            b.applyExtras(node);
            expectEquals(b.getData()->slices.size(), 2);
            expect(b.getData()->audio != nullptr);
        }
    }
};

static PhraseStateTests phraseStateTests;
