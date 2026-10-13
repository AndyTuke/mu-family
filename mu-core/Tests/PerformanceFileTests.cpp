// The Performance file: a list of up to eight full-preset pointers (docs/design-launchpad.md section 3.2c).
// Guards the saved shape (round trip, relative and absolute paths), the "shown, never dropped" rule for missing
// and foreign items, and the refusals (newer format, junk, too many items).

#include "Persistence/PerformanceFile.h"

class PerformanceFileTests : public juce::UnitTest
{
public:
    PerformanceFileTests() : juce::UnitTest("Performance file", "PerformanceFile") {}

    void runTest() override
    {
        using mu_pp::PerformanceFile;
        const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                              .getChildFile("mu_perf_test_" + juce::String(juce::Time::currentTimeMillis()));
        const auto presets = root.getChildFile("Presets");
        presets.createDirectory();
        const auto inside   = presets.getChildFile("Sub").getChildFile("Drone A.muTant");
        const auto outside  = root.getChildFile("Elsewhere").getChildFile("Other.muTant");
        inside.getParentDirectory().createDirectory();
        outside.getParentDirectory().createDirectory();
        expect(inside.replaceWithText("x") && outside.replaceWithText("x"));

        beginTest("a performance round-trips: name, description, items, product and cached names");
        {
            PerformanceFile p;
            p.name = "Friday set";
            p.description = "Gig 1";
            expect(p.setItem(0, "MuTantState", inside,  presets, "Drone A"));
            expect(p.setItem(7, "MuClidState", outside, presets, "Other"));
            const auto file = root.getChildFile("Performances").getChildFile("Friday.muPerformance");
            expect(p.save(file), "the save created the folder and wrote the file");

            PerformanceFile q;
            expect(PerformanceFile::load(file, q) == PerformanceFile::LoadResult::Ok);
            expectEquals(q.name, juce::String("Friday set"));
            expectEquals(q.description, juce::String("Gig 1"));
            expectEquals(q.count(), 2);
            expect(q.items[0].used && ! q.items[1].used && q.items[7].used, "an absent idx is an empty item");
            expectEquals(q.items[0].product, juce::String("MuTantState"));
            expectEquals(q.items[7].product, juce::String("MuClidState"));
            expectEquals(q.items[0].name, juce::String("Drone A"));
        }

        beginTest("a preset under the preset folder is stored relative, one outside absolute; both resolve back");
        {
            PerformanceFile p;
            p.setItem(0, "MuTantState", inside,  presets, "A");
            p.setItem(1, "MuTantState", outside, presets, "B");
            expectEquals(p.items[0].path, juce::String("Sub/Drone A.muTant"));
            expect(juce::File::isAbsolutePath(p.items[1].path), "outside the folder: absolute");
            expect(p.resolve(0, presets) == inside,  "relative path resolves against the preset folder");
            expect(p.resolve(1, presets) == outside, "absolute path resolves to itself");
            expect(p.resolve(2, presets) == juce::File(), "an empty item resolves to nothing");
        }

        beginTest("status: ok, missing, other product, empty; a missing item stays in the list");
        {
            PerformanceFile p;
            p.setItem(0, "MuTantState", inside, presets, "A");
            p.setItem(1, "MuTantState", presets.getChildFile("Gone.muTant"), presets, "Gone");
            p.setItem(2, "MuClidState", inside, presets, "C");
            expect(p.status(0, presets, "MuTantState") == PerformanceFile::ItemStatus::Ok);
            expect(p.status(1, presets, "MuTantState") == PerformanceFile::ItemStatus::Missing);
            expect(p.status(2, presets, "MuTantState") == PerformanceFile::ItemStatus::OtherProduct);
            expect(p.status(3, presets, "MuTantState") == PerformanceFile::ItemStatus::Empty);
            expect(p.status(9, presets, "MuTantState") == PerformanceFile::ItemStatus::Empty, "out of range reads as empty");
            expectEquals(p.count(), 3);   // the missing and foreign items still count: never dropped

            const auto file = root.getChildFile("keep.muPerformance");
            p.save(file);
            PerformanceFile q;
            PerformanceFile::load(file, q);
            expectEquals(q.count(), 3);   // and they survive a save / load unchanged
        }

        beginTest("more than eight items: the extras and any bad or repeated idx are counted, not loaded");
        {
            juce::String xml = "<MuPerformance format=\"1\" name=\"n\">";
            for (int i = 0; i < 10; ++i)
                xml += "<Item idx=\"" + juce::String(i) + "\" product=\"MuTantState\" path=\"p" + juce::String(i) + "\"/>";
            xml += "<Item idx=\"3\" product=\"MuTantState\" path=\"dup\"/><Item product=\"MuTantState\" path=\"noidx\"/></MuPerformance>";
            PerformanceFile p;
            expect(PerformanceFile::parse(xml, p) == PerformanceFile::LoadResult::Ok);
            expectEquals(p.count(), PerformanceFile::kMaxItems);
            expectEquals(p.ignoredItems, 4);   // idx 8 and 9, the repeated 3, the one with no idx
            expectEquals(p.items[3].path, juce::String("p3"));   // the first one wins
        }

        beginTest("an empty list, a missing file, junk and a newer format");
        {
            PerformanceFile p;
            expect(PerformanceFile::parse("<MuPerformance format=\"1\"/>", p) == PerformanceFile::LoadResult::Ok);
            expectEquals(p.count(), 0);
            expect(PerformanceFile::load(root.getChildFile("nope.muPerformance"), p) == PerformanceFile::LoadResult::NotFound);
            expect(PerformanceFile::parse("not xml", p) == PerformanceFile::LoadResult::Invalid);
            expect(PerformanceFile::parse("<Other format=\"1\"/>", p) == PerformanceFile::LoadResult::Invalid);
            p.name = "kept";
            expect(PerformanceFile::parse("<MuPerformance format=\"99\" name=\"new\"/>", p) == PerformanceFile::LoadResult::UnknownFormat);
            expectEquals(p.name, juce::String("kept"), "a refused file leaves the target untouched");
        }

        beginTest("an item index out of range is refused, and clearing empties the item");
        {
            PerformanceFile p;
            expect(! p.setItem(-1, "MuTantState", inside, presets, "x"));
            expect(! p.setItem(PerformanceFile::kMaxItems, "MuTantState", inside, presets, "x"));
            p.setItem(4, "MuTantState", inside, presets, "x");
            p.clearItem(4);
            expectEquals(p.count(), 0);
        }

        root.deleteRecursively();
    }
};

static PerformanceFileTests performanceFileTests;
