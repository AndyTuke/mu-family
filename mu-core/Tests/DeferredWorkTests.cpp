// The audio thread hands MIDI program changes and control-map CCs to the message thread through
// the deferred-work mailbox (an atomic flag), never an allocating async-update post. These tests
// drive that path headless: queueMidiProgramChanges raises the flag, flushPendingAsyncUpdates
// drains it, and the work lands.

#include "TestProcessorBase.h"
#include <type_traits>

// The allocation counter lives with the global operator new in MidiControlTests.cpp.
namespace mu_test_alloc { extern thread_local bool counting; extern thread_local std::size_t count; }

// An AsyncUpdater base would bring back an audio-thread-callable triggerAsyncUpdate().
static_assert(! std::is_base_of_v<juce::AsyncUpdater, ProcessorBase>,
              "ProcessorBase must not be an AsyncUpdater: the audio thread uses the deferred-work mailbox");

namespace
{
    // Records which preset files the program changes asked for; one float parameter "p" for the CC.
    struct DeferredProcessor : TestProcessorBase
    {
        static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
        {
            juce::AudioProcessorValueTreeState::ParameterLayout l;
            l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { "p", 1 }, "p",
                                                              juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
            return l;
        }
        DeferredProcessor() : TestProcessorBase(WrapperType::wrapperType_Undefined, makeLayout()) {}

        int getNumChannels() const override { return 2; }
        void applyFullMidiPreset(const juce::File& f) override { fullLoads.add(f.getFileName()); }
        void applyMidiPresetLayer(int slot, const juce::File& f) override
        {
            layerLoads.add(juce::String(slot) + ":" + f.getFileName());
        }

        bool mailboxPending() const { return swapMailbox.isPending(); }

        juce::StringArray fullLoads, layerLoads;
    };
}

struct DeferredWorkTests : juce::UnitTest
{
    DeferredWorkTests() : juce::UnitTest("Deferred work mailbox", "mu-core") {}

    void runTest() override
    {
        juce::TemporaryFile layerFile("layer"), fullFile("full");
        layerFile.getFile().replaceWithText("x");
        fullFile.getFile().replaceWithText("x");

        beginTest("a program change waits in the mailbox until the drain, then loads");
        {
            DeferredProcessor p;
            p.midiPresetMap.setChannelMask(0x01);                    // channel 1 = slot 0
            p.midiPresetMap.setPresetPath(5, layerFile.getFile());
            p.midiFullPresetMap.setEnabled(true);
            p.midiFullPresetMap.setPresetPath(3, fullFile.getFile());

            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::programChange(1, 5), 0);
            midi.addEvent(juce::MidiMessage::programChange(9, 3), 4);
            p.queueMidiProgramChanges(midi);

            expect(p.mailboxPending(), "the audio thread should have raised the flag");
            expectEquals(p.layerLoads.size(), 0);                    // nothing ran on the audio side
            expectEquals(p.fullLoads.size(), 0);

            p.flushPendingAsyncUpdates();
            expect(! p.mailboxPending(), "the drain should clear the flag");
            expectEquals(p.layerLoads.size(), 1);
            expectEquals(p.fullLoads.size(), 1);
            if (p.layerLoads.size() == 1)
                expectEquals(p.layerLoads[0].upToFirstOccurrenceOf(":", false, false), juce::String("0"));

            p.flushPendingAsyncUpdates();                            // a second drain finds nothing new
            expectEquals(p.layerLoads.size(), 1);
            expectEquals(p.fullLoads.size(), 1);
        }

        beginTest("a mapped CC reaches its parameter through the same mailbox");
        {
            DeferredProcessor p;
            mu_core::MidiMapping m;
            m.isNote = false; m.channel = 1; m.number = 21;
            m.action.type = mu_core::ControlActionType::Parameter;
            m.action.paramId = "p";
            p.getMidiControlMap().add(m);

            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::controllerEvent(1, 21, 127), 0);
            p.queueMidiProgramChanges(midi);

            expect(p.mailboxPending(), "a claimed CC should raise the flag");
            expectEquals(midi.getNumEvents(), 0);                    // the router took the CC out of the block
            expectWithinAbsoluteError(p.apvts.getParameter("p")->getValue(), 0.0f, 0.0001f);

            p.flushPendingAsyncUpdates();
            expectWithinAbsoluteError(p.apvts.getParameter("p")->getValue(), 1.0f, 0.001f);
        }

        beginTest("a block with no program change and no mapped control raises nothing");
        {
            DeferredProcessor p;
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0);
            p.queueMidiProgramChanges(midi);
            expect(! p.mailboxPending());
            expectEquals(midi.getNumEvents(), 1);
        }

        beginTest("a long SysEx is skipped by the program change scan without allocating");
        {
            DeferredProcessor p;
            p.midiPresetMap.setChannelMask(0x01);
            p.midiFullPresetMap.setEnabled(true);
            p.getMidiControlMap().add([]
            {
                mu_core::MidiMapping m;
                m.isNote = false; m.channel = 1; m.number = 21;
                m.action.type = mu_core::ControlActionType::Parameter;
                m.action.paramId = "p";
                return m;
            }());

            std::vector<juce::uint8> payload(20, 0x22);
            juce::MidiBuffer midi;
            midi.ensureSize(2048);
            midi.addEvent(juce::MidiMessage::createSysExMessage(payload.data(), (int) payload.size()), 0);
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 1);

            mu_test_alloc::count = 0; mu_test_alloc::counting = true;
            p.queueMidiProgramChanges(midi);
            mu_test_alloc::counting = false;

            expectEquals((int) mu_test_alloc::count, 0);
            expect(! p.mailboxPending());
            expectEquals(midi.getNumEvents(), 2);
        }
    }
};

static DeferredWorkTests deferredWorkTests;

// Teardown: the swap poll must be stopped before a product's members go, or a tick could run the
// product's virtual overrides on a half-destroyed object (a pure virtual call in the worst case).
namespace
{
    // Stands in for a product: its commitDeferredWork override counts into `*commits`, and its
    // destructor asks for deferred work and lets the message loop run, as a slow teardown would.
    struct TeardownProcessor : TestProcessorBase
    {
        TeardownProcessor(int* commitCounter, bool stopFirst)
            : TestProcessorBase(WrapperType::wrapperType_Undefined), commits(commitCounter), stopPollFirst(stopFirst) {}

        ~TeardownProcessor() override
        {
            if (stopPollFirst) prepareForDestruction();
            requestDeferredWork();
            juce::MessageManager::getInstance()->runDispatchLoopUntil(150);   // timers would fire here
            commits = nullptr;   // from here on the override must not be reached
        }

        using ProcessorBase::prepareForDestruction;
        void requestDeferredWorkForTest() { requestDeferredWork(); }

        void commitDeferredWork() override { if (commits != nullptr) ++*commits; else ++lateCalls; }

        int* commits;
        bool stopPollFirst;
        static inline int lateCalls = 0;
    };
}

struct ProcessorTeardownTests : juce::UnitTest
{
    ProcessorTeardownTests() : juce::UnitTest("ProcessorBase teardown stops the swap poll", "mu-core") {}

    void runTest() override
    {
        beginTest("control: while the processor lives, the poll runs the deferred work");
        {
            int commits = 0;
            TeardownProcessor p(&commits, false);
            p.requestDeferredWorkForTest();
            juce::MessageManager::getInstance()->runDispatchLoopUntil(150);
            expect(commits > 0, "the swap poll ran commitDeferredWork");
        }

        beginTest("prepareForDestruction first: no tick reaches the override while the destructor runs");
        {
            int commits = 0;
            TeardownProcessor::lateCalls = 0;
            { TeardownProcessor p(&commits, true); }
            expectEquals(commits, 0);
            expectEquals(TeardownProcessor::lateCalls, 0);
        }

        beginTest("prepareForDestruction is safe to call twice");
        {
            int commits = 0;
            TeardownProcessor p(&commits, true);
            p.prepareForDestruction();
            p.prepareForDestruction();
        }
    }
};

static ProcessorTeardownTests processorTeardownTests;
