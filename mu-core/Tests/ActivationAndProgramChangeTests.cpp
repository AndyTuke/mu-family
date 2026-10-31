#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include "Plugin/ActivationWorker.h"
#include "Plugin/ProgramChangeRouter.h"

#include <atomic>

// ActivationWorker (one online-activation call on an owned thread) and ProgramChangeRouter (audio-thread scan of
// program changes into a FIFO, message-thread drain into the preset loaders), the two pieces cut out of ProcessorBase.
struct ActivationAndProgramChangeTests : juce::UnitTest
{
    ActivationAndProgramChangeTests() : juce::UnitTest("ActivationWorker and ProgramChangeRouter", "mu-core") {}

    void runTest() override
    {
        using mu_core::ActivationWorker;
        using mu_core::OnlineActivationOutcome;

        // Create the message manager on this thread first: whichever thread asks first becomes the message thread,
        // and the worker's callAsync must not be that thread.
        juce::MessageManager::getInstance();

        beginTest("an empty activation function starts nothing");
        {
            ActivationWorker w;
            expect(! w.start({}, "key", [](const OnlineActivationOutcome&) {}));
        }

        beginTest("the outcome reaches `done` on the message thread, then a new request may start");
        {
            ActivationWorker w;
            std::atomic<int> calls { 0 };
            bool onMessageThread = false;
            juce::String seenKey;
            auto fn = [&](const juce::String& key) { seenKey = key; OnlineActivationOutcome o; o.ok = true; return o; };
            expect(w.start(fn, "ABC-123", [&](const OnlineActivationOutcome& o)
            {
                onMessageThread = juce::MessageManager::getInstance()->isThisTheMessageThread();
                expect(o.ok);
                ++calls;
            }));
            for (int i = 0; i < 100 && calls.load() == 0; ++i)
                juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
            expectEquals(calls.load(), 1);
            expect(onMessageThread, "done runs on the message thread");
            expectEquals(seenKey, juce::String("ABC-123"));
            expect(w.start(fn, "again", [](const OnlineActivationOutcome&) {}), "a finished request does not block the next");
            w.stop();
        }

        beginTest("a second request while one is running is refused");
        {
            ActivationWorker w;
            juce::WaitableEvent release;
            auto slow = [&](const juce::String&) { release.wait(3000); return OnlineActivationOutcome {}; };
            expect(w.start(slow, "k", [](const OnlineActivationOutcome&) {}));
            expect(! w.start(slow, "k2", [](const OnlineActivationOutcome&) {}), "one request at a time");
            release.signal();
            w.stop();
        }

        beginTest("stop() drops a reply that is already queued: `done` never runs");
        {
            ActivationWorker w;
            std::atomic<int> calls { 0 };
            auto fn = [](const juce::String&) { OnlineActivationOutcome o; o.ok = true; return o; };
            expect(w.start(fn, "k", [&](const OnlineActivationOutcome&) { ++calls; }));
            juce::Thread::sleep(100);   // the worker has produced its outcome and queued the reply
            w.stop();
            for (int i = 0; i < 10; ++i)
                juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
            expectEquals(calls.load(), 0);
        }

        beginTest("ProgramChangeRouter: slot and full-preset program changes are enqueued by channel mask, then drained to the loaders");
        {
            const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("mu-pc-router-test");
            dir.createDirectory();
            const auto slotFile = dir.getChildFile("slot.muTest");
            const auto fullFile = dir.getChildFile("full.muTest");
            slotFile.replaceWithText("x");
            fullFile.replaceWithText("x");

            MidiPresetMap presetMap;
            MidiFullPresetMap fullMap;
            presetMap.setChannelMask(0x03);                 // channels 1 and 2
            presetMap.setPresetPath(5, slotFile);
            fullMap.setEnabled(true);
            fullMap.setPresetPath(7, fullFile);

            mu_core::ProgramChangeRouter router;
            juce::MidiBuffer midi;
            const uint8_t pcCh2[]  = { 0xc1, 5 };           // program change, channel 2, program 5 -> slot 1
            const uint8_t pcCh3[]  = { 0xc2, 5 };           // channel 3 is not in the mask: ignored
            const uint8_t pcFull[] = { 0xc8, 7 };           // channel 9 -> full preset 7
            midi.addEvent(pcCh2, 2, 0);
            midi.addEvent(pcCh3, 2, 1);
            midi.addEvent(pcFull, 2, 2);
            expect(router.scan(midi, presetMap, fullMap), "something was enqueued");

            int slotCalls = 0, fullCalls = 0, lastSlot = -1;
            router.drain(4, presetMap, fullMap,
                         [&](const juce::File& f) { ++fullCalls; expect(f == fullFile); },
                         [&](int slot, const juce::File& f) { ++slotCalls; lastSlot = slot; expect(f == slotFile); });
            expectEquals(slotCalls, 1);
            expectEquals(lastSlot, 1);
            expectEquals(fullCalls, 1);

            router.drain(4, presetMap, fullMap, [&](const juce::File&) { ++fullCalls; }, [&](int, const juce::File&) { ++slotCalls; });
            expectEquals(slotCalls + fullCalls, 2, "a drained queue is empty");

            presetMap.setChannelMask(0);
            fullMap.setEnabled(false);
            expect(! router.scan(midi, presetMap, fullMap), "nothing mapped: the scan returns at once");

            dir.deleteRecursively();
        }

        beginTest("ProgramChangeRouter: a slot beyond the live layers is dropped");
        {
            const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("mu-pc-router-test2");
            dir.createDirectory();
            const auto f = dir.getChildFile("p.muTest");
            f.replaceWithText("x");
            MidiPresetMap presetMap;
            MidiFullPresetMap fullMap;
            presetMap.setChannelMask(0x08);                 // channel 4 -> slot 3
            presetMap.setPresetPath(1, f);
            mu_core::ProgramChangeRouter router;
            juce::MidiBuffer midi;
            const uint8_t pc[] = { 0xc3, 1 };
            midi.addEvent(pc, 2, 0);
            expect(router.scan(midi, presetMap, fullMap));
            int calls = 0;
            router.drain(3, presetMap, fullMap, [&](const juce::File&) { ++calls; }, [&](int, const juce::File&) { ++calls; });
            expectEquals(calls, 0, "only slots below the live layer count load");
            dir.deleteRecursively();
        }
    }
};

static ActivationAndProgramChangeTests activationAndProgramChangeTests;
