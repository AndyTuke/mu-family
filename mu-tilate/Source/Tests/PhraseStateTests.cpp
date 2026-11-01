// Saving a Phrase: the slices (with pins), the timeline blocks (with lengths and reverse) and the loop reference
// survive a round trip through the layer node, a missing child is the default, and a layer whose loop arrives later
// applies its saved edits only to a loop of the same length.

#include "Sequencer/Phrase.h"

#include <juce_core/juce_core.h>

#include <cmath>
#include <limits>

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
        beginTest("settings stay with their markers through add, move, remove and a re-detect");
        {
            SliceList sl(96000);
            sl.applyDetected({ 24000, 48000 });                           // slices at 0, 24000, 48000
            SliceSettings a; a.reverse = true; a.levelDb = -3.0f;
            SliceSettings b; b.pitchSemi = 5.0f;
            sl.setSettings(1, a); sl.setSettings(2, b);

            sl.moveMarker(1, 26000);                                      // moving a marker keeps its slice's settings
            expect(sl.settings(1).reverse);
            expect(sl.removeMarker(1));                                   // removing slice 1 takes its settings with it
            expectEquals(sl.size(), 2);
            expectEquals(sl.settings(1).pitchSemi, 5.0f);                 // the old slice 2 is slice 1 now
            expect(! sl.settings(1).reverse);

            const int at = sl.addMarker(60000);                           // cut slice 1: the new slice starts like it
            expectEquals(sl.settings(at).pitchSemi, 5.0f);
            sl.applyDetected({ 10000 });                                  // the hand-placed (pinned) marker survives, with its settings
            const int idx = sl.indexAt(60000);
            expectEquals(sl.marker(idx).position, 60000);
            expectEquals(sl.settings(idx).pitchSemi, 5.0f);
        }

        beginTest("saved slices apply only to a loop of the length they were made on");
        {
            PhraseState in;
            in.loopLength = 96000;
            in.slices = SliceList(96000);
            in.slices.applyDetected({ 24000, 48000 });
            SliceSettings own; own.reverse = true;
            in.slices.setSettings(1, own);
            in.timeline = Timeline(4.0);
            juce::ValueTree node("Layer");
            writePhraseState(node, in);

            auto audioOf = [](int n) { auto b = std::make_shared<juce::AudioBuffer<float>>(1, n); b->clear(); return b; };
            {
                Phrase ph;
                ph.applyExtras(node);
                ph.installLoop(audioOf(96000), 48000.0, "a.wav", "a");    // the same length: the slices and their settings come back
                const auto d = ph.getData();
                expectEquals(d->slices.size(), 3);
                expect(d->slices.settings(1).reverse);
            }
            {
                Phrase ph;
                ph.applyExtras(node);
                ph.installLoop(audioOf(48000), 48000.0, "b.wav", "b");    // another length: one slice from 0, nothing carried over
                const auto d = ph.getData();
                expectEquals(d->slices.size(), 1);
                expect(! d->slices.settings(0).reverse);
            }
        }

        beginTest("damaged or out-of-range slice settings are brought into range, never NaN");
        {
            SliceSettings wild;
            wild.cutoffHz = std::numeric_limits<float>::quiet_NaN(); wild.resonance = 7.0f; wild.pitchSemi = 1.0e9f;
            wild.pan = -std::numeric_limits<float>::infinity(); wild.filterType = 9; wild.ampA = -5.0f; wild.chokeGroup = 99;
            SliceList sl(1000);
            sl.setSettings(0, wild);
            const auto& g = sl.settings(0);
            expect(std::isfinite(g.cutoffHz) && g.cutoffHz >= 20.0f && g.cutoffHz <= 20000.0f);
            expectEquals(g.resonance, 1.0f); expectEquals(g.pitchSemi, 24.0f); expectEquals(g.pan, 0.0f);
            expectEquals(g.filterType, 3); expectEquals(g.ampA, 0.0f); expectEquals(g.chokeGroup, 8);

            // The same from a saved file that says "nan".
            juce::ValueTree node("Layer"), p("Phrase"), slices("Slices"), m("M");
            m.setProperty("p", 0, nullptr); m.setProperty("cut", "nan", nullptr); m.setProperty("ft", 2, nullptr);
            slices.appendChild(m, nullptr); p.setProperty("loopLength", 1000, nullptr); p.appendChild(slices, nullptr); node.appendChild(p, nullptr);
            const auto back = readPhraseState(node).slices.settings(0);
            expect(std::isfinite(back.cutoffHz));
        }

        beginTest("an edit keeps the audio's identity and gets a new version, a new loop gets both");
        {
            Phrase ph;
            auto audio = std::make_shared<juce::AudioBuffer<float>>(1, 4800);
            audio->clear();
            ph.installLoop(audio, 48000.0, "a.wav", "a");
            const auto first = ph.getData();
            ph.commitEdit(first->slices, first->timeline);
            const auto edited = ph.getData();
            expect(edited->audioId == first->audioId);                    // the sounding slice plays on through an edit
            expect(edited->version != first->version);                    // but the editor sees it changed
            ph.installLoop(audio, 48000.0, "a.wav", "a");                 // even the same buffer again is a new loop
            const auto again = ph.getData();
            expect(again->audioId != first->audioId);
            expect(again->version != edited->version);
        }

        beginTest("per-slice settings round-trip, and a slice cut from another starts with its settings");
        {
            PhraseState in;
            in.loopLength = 96000;
            in.slices = SliceList(96000);
            in.slices.applyDetected({ 24000, 48000 });
            SliceSettings own;
            own.reverse = true; own.levelDb = -6.0f; own.preFade = true; own.preLengthMs = 200.0f; own.preTone = -0.5f; own.pan = -0.25f; own.pitchSemi = 7.0f; own.fineCents = 12.0f;
            own.filterType = 2; own.cutoffHz = 800.0f; own.resonance = 0.4f; own.ampA = 5.0f; own.fltR = 250.0f;
            own.oneShot = false; own.chokeGroup = 3;
            in.slices.setSettings(1, own);
            expect(in.slices.addMarker(36000) >= 0);                      // cut slice 1: the new slice 2 starts like it
            expect(in.slices.settings(2).reverse && in.slices.settings(2).chokeGroup == 3);
            expect(! in.slices.settings(0).reverse);                      // untouched slices stay neutral

            juce::ValueTree node("Layer");
            writePhraseState(node, in);
            const auto out = readPhraseState(node);
            const auto& g = out.slices.settings(1);
            expect(g.reverse && ! g.oneShot);
            expectEquals(g.levelDb, -6.0f); expect(g.preFade); expectEquals(g.preLengthMs, 200.0f); expectEquals(g.preTone, -0.5f); expectEquals(g.pan, -0.25f); expectEquals(g.pitchSemi, 7.0f); expectEquals(g.fineCents, 12.0f);
            expectEquals(g.filterType, 2); expectEquals(g.cutoffHz, 800.0f); expectEquals(g.resonance, 0.4f);
            expectEquals(g.ampA, 5.0f); expectEquals(g.fltR, 250.0f); expectEquals(g.chokeGroup, 3);
            expect(! out.slices.settings(0).reverse && out.slices.settings(0).levelDb == 0.0f && ! out.slices.settings(0).preFade);
        }

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
