// mu_ui::ParamRefreshQueue: the hand-off that lets an editor's parameter listener run on the audio thread (host
// automation) without allocating or posting to the message queue. post() copies the id into a fixed slot; the editor's
// timer drains. The listeners themselves need an editor and cannot be called headless, so the queue is tested directly.

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>

#include <thread>
#include <vector>

#include "UI/ParamRefreshQueue.h"
// The counting operators are carried by MidiControlTests.cpp (one translation unit per executable).
#include "AllocationProbe.h"

namespace
{
    std::vector<juce::String> drainAll(mu_ui::ParamRefreshQueue& q)
    {
        std::vector<juce::String> got;
        q.drain([&got](const juce::String& id) { got.push_back(id); });
        return got;
    }
}

struct ParamRefreshQueueTests : juce::UnitTest
{
    ParamRefreshQueueTests() : juce::UnitTest("ParamRefreshQueue", "mu-core") {}

    void runTest() override
    {
        beginTest("post from another thread allocates nothing, and drain returns the ids in order");
        {
            mu_ui::ParamRefreshQueue q;
            std::vector<juce::String> ids;   // built here, before the probe is armed on the posting thread
            for (int i = 0; i < 100; ++i) ids.push_back("r" + juce::String(i) + "_hitsA");

            int allocs = -1, frees = -1;
            std::thread poster([&]
            {
                mu_alloc::Armed region;   // counts this thread only
                for (const auto& id : ids) q.post(id);
                region.stop();
                allocs = region.allocs();
                frees = region.frees();
            });
            poster.join();

            expectEquals(allocs, 0, "post() allocated on the posting thread");
            expectEquals(frees, 0, "post() freed on the posting thread");

            const auto got = drainAll(q);
            expectEquals((int) got.size(), 100, "every posted id is drained");
            bool inOrder = got.size() == ids.size();
            for (size_t i = 0; inOrder && i < got.size(); ++i) inOrder = (got[i] == ids[i]);
            expect(inOrder, "the ids come out oldest first, unchanged");
            expect(drainAll(q).empty(), "a second drain finds nothing");
        }

        beginTest("an id longer than 47 bytes is dropped, 47 bytes is kept, an empty id is ignored");
        {
            mu_ui::ParamRefreshQueue q;
            const auto longest = juce::String::repeatedString("a", 47);
            q.post(longest);
            q.post(juce::String::repeatedString("b", 48));   // one byte too long
            q.post(juce::String());
            q.post("ok");
            const auto got = drainAll(q);
            expectEquals((int) got.size(), 2, "only the 47-byte id and \"ok\" are queued");
            if (got.size() == 2)
            {
                expect(got[0] == longest, "the 47-byte id is intact");
                expect(got[1] == "ok", "and the next id follows it");
            }
        }

        beginTest("ids round-trip as UTF-8, and the length limit counts bytes, not characters");
        {
            mu_ui::ParamRefreshQueue q;
            // a-umlaut (2 bytes) and the euro sign (3 bytes), built from code points so the source encoding does not matter.
            const juce::String accented = "par" + juce::String::charToString((juce::juce_wchar) 0x00E4) + "m_"
                                          + juce::String::charToString((juce::juce_wchar) 0x20AC);
            q.post(accented);
            juce::String twentyEuros;   // 20 characters, 60 bytes: over the limit
            for (int i = 0; i < 20; ++i) twentyEuros += juce::String::charToString((juce::juce_wchar) 0x20AC);
            q.post(twentyEuros);
            const auto got = drainAll(q);
            expectEquals((int) got.size(), 1, "the 60-byte id is dropped although it is only 20 characters");
            if (! got.empty())
                expect(got[0] == accented, "the non-ASCII id came back identical");
        }

        beginTest("a full queue drops new ids without blocking, keeps the oldest, and accepts again once drained");
        {
            mu_ui::ParamRefreshQueue q;
            // Far more than the queue holds, with no drain in between: post() must return each time.
            for (int i = 0; i < 1000; ++i) q.post("p" + juce::String(i));

            const auto got = drainAll(q);
            const int n = (int) got.size();
            logMessage("a full queue held " + juce::String(n) + " of 1000 ids");
            expect(n > 100 && n < 1000, "the queue holds a bounded number of ids (" + juce::String(n) + ")");
            // What was kept is the oldest run, unbroken: p0, p1, p2 ...
            bool oldestKept = true;
            for (int i = 0; i < n; ++i) oldestKept = oldestKept && (got[(size_t) i] == "p" + juce::String(i));
            expect(oldestKept, "the ids that were kept are the first ones posted, in order");

            q.post("after");
            const auto again = drainAll(q);
            expectEquals((int) again.size(), 1, "the drained queue accepts an id again");
            if (! again.empty()) expect(again[0] == "after");
        }
    }
};

static ParamRefreshQueueTests paramRefreshQueueTests;
