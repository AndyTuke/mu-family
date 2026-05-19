#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Components/KnobWithLabel.h"
#include "Components/SegmentControl.h"
#include "Components/DropdownSelect.h"
#include "Components/MuClidLookAndFeel.h"
#include "InsertAlgoDefaults.h"   // #435

class PluginProcessor;

// Four-column two-row voice chain panel: Pitch | Filter | Amp | Drive.
// Row 1: config controls.  Row 2: ADSR envelope (Drive has no ADSR — row 2 is blank).
class VoiceSection : public juce::Component
{
public:
    explicit VoiceSection(PluginProcessor& p);

    void setRhythm(int rhythmIndex);

    // Issue #133: refresh the modulated-knob ring indicators based on the current
    // ModulationMatrix assignments for the active rhythm. Cheap; safe to call from
    // RhythmPanel's 30 Hz timer.
    void refreshModulatedIndicators();

    std::function<void(const juce::String& name, const juce::String& value)> onStatusUpdate;
    std::function<void(int driveChar)> onInsertAlgorithmChanged;

    void loadFromRhythm();

    // #353: refresh a single control identified by its APVTS suffix (rhythm-prefixed
    // suffixes like "pitchOct", "fltCut", or send suffixes like "sendEff"). Used by
    // RhythmPanel::parameterChanged to avoid rewriting all 28+ knobs on every change.
    void refreshSuffix(const juce::String& suffix);

    void resized() override;
    void paint(juce::Graphics&) override;

private:
    using Id = MuClidLookAndFeel::ColourIds;

    PluginProcessor& proc;
    int rhythmIndex = -1;

    // ─── PITCH ──────────────────────────────────────────────────────────
    KnobWithLabel pitchOctave  { "Octave",  Id::knobEuclidean };
    KnobWithLabel pitchSemi    { "Semitone", Id::knobEuclidean };
    KnobWithLabel pitchFine    { "Fine",    Id::knobEuclidean };
    KnobWithLabel pitchAtk     { "Attack",  Id::knobEuclidean };
    KnobWithLabel pitchDec     { "Decay",   Id::knobEuclidean };
    KnobWithLabel pitchSus     { "Sustain", Id::knobEuclidean };
    KnobWithLabel pitchRel     { "Release", Id::knobEuclidean };
    KnobWithLabel pitchDepth   { "Depth",   Id::knobEuclidean };

    // ─── FILTER ─────────────────────────────────────────────────────────
    DropdownSelect  filterType;
    KnobWithLabel  filterCutoff { "Cutoff",  Id::knobPostPad  };
    KnobWithLabel  filterRes    { "Resonance", Id::knobPostPad  };
    KnobWithLabel  filterAtk    { "Attack",  Id::knobPostPad  };
    KnobWithLabel  filterDec    { "Decay",   Id::knobPostPad  };
    KnobWithLabel  filterSus    { "Sustain", Id::knobPostPad  };
    KnobWithLabel  filterRel    { "Release", Id::knobPostPad  };
    KnobWithLabel  filterDepth  { "Depth",   Id::knobPostPad  };

    // ─── AMP ────────────────────────────────────────────────────────────
    KnobWithLabel  ampLevel    { "Level",   Id::knobLevel    };
    KnobWithLabel  ampSendEff  { "Effect",  Id::knobFxSend   };
    KnobWithLabel  ampSendDly  { "Delay",   Id::knobFxSend   };
    KnobWithLabel  ampSendRev  { "Reverb",  Id::knobFxSend   };
    KnobWithLabel  ampAccent   { "Accent",  Id::knobLevel    };
    KnobWithLabel  ampAtk      { "Attack",  Id::knobLevel    };
    KnobWithLabel  ampDec      { "Decay",   Id::knobLevel    };
    KnobWithLabel  ampSus      { "Sustain", Id::knobLevel    };
    KnobWithLabel  ampRel      { "Release", Id::knobLevel    };

    // Per-algorithm snapshots for A/B-ing — reset when switching to a new rhythm.
    // #435: struct + default table lifted to shared InsertAlgoDefaults.h (was
    // previously duplicated between VoiceSection.cpp and MixerChannel_Insert.cpp
    // and the two copies had drifted at indices 7 / 8). Kept as a using-alias so
    // existing references to VoiceSection::InsertAlgoSnapshot keep compiling.
    using InsertAlgoSnapshot = InsertAlgoDefaults;
    InsertAlgoSnapshot insertSnapshots[13];
    bool               insertSnapshotValid[11] = {};

    // ─── INSERT ─────────────────────────────────────────────────────────
    DropdownSelect driveChar;
    KnobWithLabel  driveDrive  { "Drive",  Id::knobInsertPad };
    KnobWithLabel  driveOutput { "Output", Id::knobInsertPad };
    KnobWithLabel  driveDither { "Dither", Id::knobInsertPad };
    KnobWithLabel  driveTone   { "LPF",    Id::knobInsertPad };

    void apvtsSet(const char* suffix, float v);
    void wireCallbacks();
    void configureInsertAlgorithm(int charId);  // sets labels/ranges/callbacks/values per algorithm
};
