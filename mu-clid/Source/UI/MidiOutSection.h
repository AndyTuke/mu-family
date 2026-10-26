#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/Components/DropdownSelect.h"
#include "UI/Components/KnobWithLabel.h"
#include "UI/Components/MuLookAndFeel.h"
#include "UI/Voice/VoiceBand.h"

namespace mu_clid {

class RhythmHost;

// What replaces the 144 px voice band while a rhythm is in MIDI output mode: one raised "MIDI Out"
// section holding Note, Channel, Velocity and Accent, bound to the selected rhythm's
// r{N}_midiNote / midiChan / midiVel / midiAccent parameters.
class MidiOutSection : public juce::Component
{
public:
    // Geometry, unscaled px. The box starts where the voice band's Pitch box does and is as tall as
    // the band's boxes, so swapping the two keeps the plate and box in the same place.
    static constexpr int kCols     = MuLookAndFeel::kVoiceUnitW;   // one knob column (54)
    static constexpr int kDropCols = 2;                            // Note and Channel dropdowns
    static constexpr int kBoxX     = VoiceBand::kLeftGap;
    static constexpr int kBoxY     = MuLookAndFeel::kVoiceLabelH;
    static constexpr int kBoxW     = (2 * kDropCols + 2) * kCols;  // Note | Channel | Velocity | Accent
    static constexpr int kBoxH     = MuLookAndFeel::kVoiceSubH;
    static constexpr int kCaptionH = 14;                           // engraved caption above a dropdown
    static constexpr int kDropH    = MuLookAndFeel::kKnobSize2H / 2;
    static constexpr int kRowH     = MuLookAndFeel::kKnobSize2H;   // a control row is one Size-2 knob high

    explicit MidiOutSection(RhythmHost& host);

    void setRhythm(int ri);
    void loadFromRhythm();
    void refreshSuffix(const juce::String& suffix);

    // The section box in the component's own scaled coordinates.
    juce::Rectangle<int> boxBounds() const;

    void resized() override;
    void paint(juce::Graphics&) override;

    std::function<void(const juce::String& name, const juce::String& value)> onStatusUpdate;

private:
    using Id = MuLookAndFeel::ColourIds;

    RhythmHost& host;
    int rhythmIndex = -1;

    DropdownSelect noteSelect;
    DropdownSelect chanSelect;
    KnobWithLabel  velKnob    { "Velocity", Id::knobLevel };
    KnobWithLabel  accentKnob { "Accent",   Id::knobLevel };

    void apvtsSet(const char* suffix, float v);
    float apvtsGet(const char* suffix, float fallback) const;
    juce::Rectangle<int> cell(int index) const;   // the control row's index-th cell (scaled)

    static_assert(kBoxW <= VoiceBand::kEffectsX + VoiceBand::kEffectsW - VoiceBand::kLeftGap,
                  "the MIDI Out box must fit in the width the voice band's boxes use");
    static_assert(kCaptionH + 2 + kDropH <= kRowH && kRowH <= kBoxH,
                  "the Note / Channel cells must fit the control row inside the box");
};

} // namespace mu_clid
