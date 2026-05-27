// C4 — MidiFullPresetMap JSON round-trip (#655 ch-9 full-preset map).
//
// Guards the persistence of the channel-9 program→full-preset map: paths set
// per program number, the enabled flag, and clear, must survive a save→reload
// cycle. Catches a serialiser/parser drift that would silently drop a user's
// MIDI program-change bindings.

#include <juce_data_structures/juce_data_structures.h>
#include "Persistence/MidiFullPresetMap.h"

class MidiFullPresetMapTest : public juce::UnitTest
{
public:
    MidiFullPresetMapTest() : juce::UnitTest ("MIDI full-preset map", "MidiFullPresetMap") {}

    void runTest() override
    {
        auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("muClid_midiFullPresets_test_" + juce::String (juce::Time::currentTimeMillis()) + ".json");
        const auto presetA = juce::File::getCurrentWorkingDirectory().getChildFile ("Aaa.muClid");
        const auto presetB = juce::File::getCurrentWorkingDirectory().getChildFile ("Bbb.muClid");

        beginTest ("C4: paths + enabled flag survive save → reload");
        {
            {
                MidiFullPresetMap m;
                m.setStorageFile (tmp);
                m.setPresetPath (0,  presetA);   // setPresetPath auto-saves
                m.setPresetPath (64, presetB);
                m.setEnabled (false);
            }

            MidiFullPresetMap m2;
            m2.setStorageFile (tmp);
            m2.load();

            expect (m2.hasPreset (0),    "slot 0 present after reload");
            expect (m2.hasPreset (64),   "slot 64 present after reload");
            expect (! m2.hasPreset (1),  "an unset slot stays empty");
            expect (m2.getPresetPath (0).endsWith ("Aaa.muClid"),  "slot 0 path round-trips");
            expect (m2.getPresetPath (64).endsWith ("Bbb.muClid"), "slot 64 path round-trips");
            expect (! m2.isEnabled(),    "enabled flag round-trips (was set false)");
        }

        beginTest ("C4: clearPreset removes an entry");
        {
            MidiFullPresetMap m;
            m.setStorageFile (tmp);
            m.setPresetPath (5, presetA);
            expect (m.hasPreset (5), "slot 5 set");
            m.clearPreset (5);
            expect (! m.hasPreset (5), "slot 5 cleared");
        }

        beginTest ("C4: out-of-range indices are rejected, not crashed");
        {
            MidiFullPresetMap m;
            m.setStorageFile (tmp);
            m.setPresetPath (-1,  presetA);   // ignored
            m.setPresetPath (128, presetA);   // ignored (NumSlots == 128, valid 0..127)
            expect (! m.hasPreset (-1),  "negative index has no preset");
            expect (! m.hasPreset (128), "index == NumSlots has no preset");
            expect (m.getPresetPath (-1).isEmpty(), "negative index returns empty path");
        }

        tmp.deleteFile();
    }
};

static MidiFullPresetMapTest midiFullPresetMapTest;
