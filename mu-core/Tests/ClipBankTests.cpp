#include "Sequencer/Layer.h"

#include <juce_core/juce_core.h>

namespace
{
    juce::ValueTree makePayload(const juce::String& tag, int value)
    {
        juce::ValueTree t(tag);
        t.setProperty("v", value, nullptr);
        juce::ValueTree child("Inner");
        child.setProperty("w", value * 2, nullptr);
        t.appendChild(child, nullptr);
        return t;
    }
}

struct ClipBankTests : juce::UnitTest
{
    ClipBankTests() : juce::UnitTest("ClipBank", "mu-core") {}

    void runTest() override
    {
        using mu_core::ClipBank;

        beginTest("a new bank is empty and the count is the family limit");
        {
            ClipBank b;
            expectEquals(ClipBank::kCount, mu_limits::kClipsPerLayer);
            expectEquals(ClipBank::kCount, 8);
            expect(b.isEmpty());
            expectEquals((int) b.mask(), 0);
            for (int i = 0; i < ClipBank::kCount; ++i)
                expect(! b.isOccupied(i) && ! b.get(i).isValid());
        }

        beginTest("store, name, clear and clearAll");
        {
            ClipBank b;
            expect(b.store(2, makePayload("P", 5), "Verse"));
            expect(b.isOccupied(2));
            expectEquals(b.getName(2), juce::String("Verse"));
            expectEquals((int) b.get(2).getProperty("v"), 5);
            expect(b.store(7, makePayload("P", 1)));
            expectEquals((int) b.mask(), (1 << 2) | (1 << 7));
            expect(b.clear(2));
            expect(! b.isOccupied(2));
            expectEquals(b.getName(2), juce::String());
            b.clearAll();
            expect(b.isEmpty());
        }

        beginTest("out-of-range indices are refused and harmless");
        {
            ClipBank b;
            expect(! b.store(-1, makePayload("P", 1)));
            expect(! b.store(ClipBank::kCount, makePayload("P", 1)));
            expect(! b.clear(8));
            expect(! b.isOccupied(-1) && ! b.isOccupied(99));
            expect(! b.get(99).isValid());
            expect(b.isEmpty());
        }

        beginTest("storing an invalid tree empties the clip");
        {
            ClipBank b;
            b.store(1, makePayload("P", 1), "x");
            expect(b.store(1, juce::ValueTree()));
            expect(! b.isOccupied(1));
            expectEquals(b.getName(1), juce::String());
        }

        beginTest("store and get deep-copy: no aliasing with the caller or between reads");
        {
            ClipBank b;
            auto src = makePayload("P", 3);
            b.store(0, src);
            src.setProperty("v", 99, nullptr);                 // caller edits after store
            expectEquals((int) b.get(0).getProperty("v"), 3);

            auto out = b.get(0);
            out.setProperty("v", 77, nullptr);                 // caller edits a read-out
            out.getChild(0).setProperty("w", 77, nullptr);
            expectEquals((int) b.get(0).getProperty("v"), 3);
            expectEquals((int) b.get(0).getChild(0).getProperty("w"), 6);
        }

        beginTest("copying a bank deep-copies it");
        {
            ClipBank a;
            a.store(4, makePayload("P", 8), "n");
            ClipBank c(a);
            ClipBank d;
            d = a;
            a.clear(4);
            expect(c.isOccupied(4) && d.isOccupied(4));
            expectEquals(c.getName(4), juce::String("n"));
            expectEquals((int) d.get(4).getProperty("v"), 8);
        }

        beginTest("writeTo / readFrom round trip, including names and the empty-name case");
        {
            ClipBank a;
            a.store(0, makePayload("MuTantVoice", 1), "Intro");
            a.store(5, makePayload("MuClidRhythm", 2));
            juce::ValueTree node("Slot");
            node.setProperty("keep", 1, nullptr);
            a.writeTo(node);
            expect(node.getChildWithName("Clips").isValid());
            expectEquals((int) node.getProperty("keep"), 1);

            ClipBank b;
            b.store(3, makePayload("Old", 0));                 // readFrom replaces the whole bank
            b.readFrom(node);
            expectEquals((int) b.mask(), (1 << 0) | (1 << 5));
            expectEquals(b.getName(0), juce::String("Intro"));
            expectEquals(b.getName(5), juce::String());
            expect(b.get(0).hasType("MuTantVoice"));
            expect(b.get(5).hasType("MuClidRhythm"));
            expectEquals((int) b.get(5).getProperty("v"), 2);
            expectEquals((int) b.get(5).getChild(0).getProperty("w"), 4);
        }

        beginTest("writeTo replaces an old child and writes nothing for an empty bank");
        {
            ClipBank a;
            a.store(1, makePayload("P", 1));
            juce::ValueTree node("Slot");
            a.writeTo(node);
            a.writeTo(node);
            expectEquals(node.getNumChildren(), 1);
            a.clearAll();
            a.writeTo(node);
            expectEquals(node.getNumChildren(), 0);
        }

        beginTest("readFrom is tolerant of missing, invalid and malformed input");
        {
            ClipBank b;
            b.store(0, makePayload("P", 1));
            b.readFrom(juce::ValueTree("Slot"));               // no <Clips>: empty bank
            expect(b.isEmpty());

            b.store(0, makePayload("P", 1));
            b.readFrom(juce::ValueTree());                     // invalid node: empty bank
            expect(b.isEmpty());

            juce::ValueTree node("Slot");
            juce::ValueTree list("Clips");

            juce::ValueTree good("Clip");
            good.setProperty("idx", 3, nullptr);
            good.appendChild(makePayload("P", 9), nullptr);
            list.appendChild(good, nullptr);

            juce::ValueTree badIdx("Clip");
            badIdx.setProperty("idx", 8, nullptr);
            badIdx.appendChild(makePayload("P", 1), nullptr);
            list.appendChild(badIdx, nullptr);

            juce::ValueTree negIdx("Clip");
            negIdx.setProperty("idx", -1, nullptr);
            negIdx.appendChild(makePayload("P", 1), nullptr);
            list.appendChild(negIdx, nullptr);

            juce::ValueTree noIdx("Clip");
            noIdx.appendChild(makePayload("P", 1), nullptr);
            list.appendChild(noIdx, nullptr);

            juce::ValueTree noPayload("Clip");
            noPayload.setProperty("idx", 6, nullptr);
            list.appendChild(noPayload, nullptr);

            list.appendChild(juce::ValueTree("Stranger"), nullptr);
            node.appendChild(list, nullptr);

            b.readFrom(node);
            expectEquals((int) b.mask(), 1 << 3);
            expectEquals((int) b.get(3).getProperty("v"), 9);
        }

        beginTest("readFrom deep-copies out of the source tree");
        {
            ClipBank a;
            a.store(0, makePayload("P", 4));
            juce::ValueTree node("Slot");
            a.writeTo(node);
            ClipBank b;
            b.readFrom(node);
            node.getChildWithName("Clips").getChild(0).getChild(0).setProperty("v", 50, nullptr);
            expectEquals((int) b.get(0).getProperty("v"), 4);
        }

        beginTest("a Layer's bank survives assignment, copy and adoptDefaults");
        {
            Layer a, b;
            a.clips->store(1, makePayload("P", 1), "keep");
            b.clips->store(2, makePayload("P", 2));

            a = b;                                             // layer-preset load / installRhythm style
            expect(a.clips->isOccupied(1) && ! a.clips->isOccupied(2));
            expectEquals(a.clips->getName(1), juce::String("keep"));

            Layer fresh;
            fresh.clips->store(6, makePayload("P", 6));
            a.adoptDefaults(fresh);
            expectEquals((int) a.clips->mask(), 1 << 1);

            Layer c(a);                                        // a copy starts with an empty bank
            expect(c.clips->isEmpty());
        }
    }
};

static ClipBankTests clipBankTests;
