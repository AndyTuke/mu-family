// The slice list built from detected onsets (the detector itself is tested in mu-core, OnsetDetectorTests): the list covers
// the loop from 0 to its end, keeps pinned markers through a re-detect, refuses markers too close, at 0 or past the end, and
// divides evenly.

#include "Sequencer/SliceList.h"

#include <juce_core/juce_core.h>

#include <cmath>

using namespace mu_tilate;

namespace
{
constexpr double kSr = 48000.0;
}

struct SliceDetectionTests : public juce::UnitTest
{
    SliceDetectionTests() : juce::UnitTest("Slice detection", "mu-tilate") {}

    void runTest() override
    {
        beginTest("the slice list covers the loop from 0 to its end");
        {
            SliceList list(48000);
            list.applyDetected({ 12000, 24000, 36000 });
            expectEquals(list.size(), 4);
            expectEquals(list.slice(0).start, 0);
            expectEquals(list.slice(3).end, 48000);
            for (int i = 0; i + 1 < list.size(); ++i) expectEquals(list.slice(i).end, list.slice(i + 1).start);
            expectEquals(list.indexAt(0), 0);
            expectEquals(list.indexAt(23999), 1);
            expectEquals(list.indexAt(24000), 2);
            expectEquals(list.indexAt(48000), -1);
        }

        beginTest("a pinned marker survives a re-detect, an unpinned one does not");
        {
            SliceList list(48000);
            list.applyDetected({ 10000, 30000 });
            list.addMarker(20000);                      // hand-placed, pinned
            list.applyDetected({ 15000 });
            expectEquals(list.size(), 3);               // 0, 15000 (new), 20000 (pinned)
            expectEquals(list.marker(1).position, 15000);
            expectEquals(list.marker(2).position, 20000);
            expect(list.marker(2).pinned);
        }

        beginTest("markers are refused when too close, at 0, or past the end; moves stay between neighbours");
        {
            SliceList list(48000);
            list.applyDetected({ 12000, 24000 });
            expectEquals(list.addMarker(12010), -1);
            expectEquals(list.addMarker(0), -1);
            expectEquals(list.addMarker(48000), -1);
            expect(! list.removeMarker(0));
            expectEquals(list.moveMarker(1, 30000), 1);
            expectEquals(list.marker(1).position, 24000 - 32);
            expect(list.removeMarker(1));
            expectEquals(list.size(), 2);
        }

        beginTest("even division makes equal slices and shortening the loop drops markers beyond it");
        {
            SliceList list(48000);
            list.divideEvenly(8);
            expectEquals(list.size(), 8);
            for (int i = 0; i < 8; ++i) expectEquals(list.slice(i).length(), 6000);
            list.setLoopLength(24000);
            expectEquals(list.size(), 4);
            expectEquals(list.slice(3).end, 24000);
        }
    }
};

static SliceDetectionTests sliceDetectionTests;
