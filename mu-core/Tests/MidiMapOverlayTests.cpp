#include "TestProcessorBase.h"
#include "UI/MidiMapOverlay.h"

// The MIDI mapping editor (docs/design-ui-family.md section 15): the offered action list, and the rule that learn never outlives the screen.
struct MidiMapOverlayTests : juce::UnitTest
{
    MidiMapOverlayTests() : juce::UnitTest("MIDI mapping editor", "mu-core") {}

    void runTest() override
    {
        using mu_core::ControlActionType;
        using T = ControlActionType;
        const int timeout = MuLookAndFeel::kMidiLearnTimeoutMs;

        beginTest("action list: no refused kind is offered, Parameter is CC only, clip actions need a clip bank");
        {
            const auto noClips = mu_ui::midimap::offeredActions(false, false);
            const auto withClips = mu_ui::midimap::offeredActions(false, true);
            const auto noteList = mu_ui::midimap::offeredActions(true, true);
            for (const auto* list : { &noClips, &withClips, &noteList })
                for (const auto& c : *list)
                    expect(c.type != T::StoreClip && c.type != T::ClearClip && c.type != T::CopyClip && c.type != T::CutClip
                           && c.type != T::PasteClip && c.type != T::PerformanceMode && c.type != T::PerformanceItem
                           && c.type != T::PerformancePrev && c.type != T::PerformanceNext && c.type != T::MuteSelectedLayer
                           && c.type != T::AppPrev && c.type != T::AppNext && c.type != T::PagePrev && c.type != T::PageNext
                           && c.type != T::None, "a refused or empty kind is offered");
            expect(mu_ui::midimap::findChoice(noClips, T::Parameter) != nullptr, "CC offers Parameter");
            expect(mu_ui::midimap::findChoice(noteList, T::Parameter) == nullptr, "a note does not offer Parameter");
            expect(mu_ui::midimap::findChoice(noClips, T::LaunchClip) == nullptr, "no clip bank, no Launch clip");
            expect(mu_ui::midimap::findChoice(withClips, T::LaunchClip) != nullptr, "clip bank offers Launch clip");
            expect(mu_ui::midimap::findChoice(withClips, T::TransportToggle) != nullptr, "Play / Stop is offered");
        }

        beginTest("learn: armed by Add mapping, cancelled by hiding, by Esc, by timeout, and by destroying the screen");
        {
            TestProcessorBase proc(juce::AudioProcessor::wrapperType_Standalone);
            auto& map = proc.getMidiControlMap();
            map.clear();

            auto overlay = std::make_unique<mu_ui::MidiMapOverlay>(proc);
            overlay->setBounds(0, 0, 900, 500);
            overlay->setVisible(true);
            expect(! map.isLearning(), "not learning just by opening");

            overlay->addDraftAndLearn();
            expect(map.isLearning() && overlay->isLearningRow(), "Add mapping arms learn");
            expectEquals(overlay->numRows(), 1);

            overlay->setVisible(false);
            expect(! map.isLearning() && ! overlay->isLearningRow(), "hiding the screen cancels learn");
            expectEquals(overlay->numRows(), 0, "the empty draft row is dropped");

            overlay->setVisible(true);
            overlay->addDraftAndLearn();
            overlay->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
            expect(! map.isLearning() && ! overlay->isLearningRow(), "Esc cancels learn");

            overlay->addDraftAndLearn();
            overlay->tickLearn(juce::Time::getMillisecondCounter() + (juce::uint32) timeout + 50);
            expect(! map.isLearning() && ! overlay->isLearningRow(), "learn times out");
            expectEquals(overlay->numRows(), 0, "the timed-out draft is dropped");

            overlay->addDraftAndLearn();
            overlay.reset();
            expect(! map.isLearning(), "destroying the screen cancels learn");
        }

        beginTest("a learned source plus a chosen action is stored; the same source again replaces it");
        {
            TestProcessorBase proc(juce::AudioProcessor::wrapperType_Standalone);
            auto& map = proc.getMidiControlMap();
            map.clear();

            mu_ui::MidiMapOverlay overlay(proc);
            overlay.setBounds(0, 0, 900, 500);
            overlay.setVisible(true);

            overlay.addDraftAndLearn();
            map.offerLearn(false, 1, 21);
            overlay.tickLearn(juce::Time::getMillisecondCounter());
            expect(! overlay.isLearningRow(), "learning ended when a message arrived");
            expectEquals(overlay.numRows(), 1);
            expectEquals(map.size(), 0, "a source without an action is not stored yet");

            overlay.setRowForTest(0, false, 1, 21, T::TransportPlay);
            expectEquals(map.size(), 1);
            expect(map.get(0).action.type == T::TransportPlay && map.get(0).number == 21 && ! map.get(0).isNote, "stored as learned");

            overlay.addDraftAndLearn();
            map.offerLearn(false, 1, 21);
            overlay.tickLearn(juce::Time::getMillisecondCounter());
            overlay.setRowForTest(overlay.numRows() - 1, false, 1, 21, T::TransportStop);
            expectEquals(map.size(), 1, "the same source replaces, never doubles");
            expect(map.get(0).action.type == T::TransportStop, "the newer action wins");
            expectEquals(overlay.numRows(), 1, "the replaced row is gone from the list");
        }

        beginTest("source text names CCs and notes with their channel");
        {
            mu_core::MidiMapping cc;  cc.isNote = false; cc.channel = 1;  cc.number = 21;
            mu_core::MidiMapping nt;  nt.isNote = true;  nt.channel = 10; nt.number = 60;
            expectEquals(mu_ui::MidiMapOverlay::sourceText(cc), juce::String("CC 21  Ch 1"));
            expectEquals(mu_ui::MidiMapOverlay::sourceText(nt), juce::String("Note C3  Ch 10"));
        }
    }
};

static MidiMapOverlayTests midiMapOverlayTests;
