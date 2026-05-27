#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/Components/DropdownSelect.h"
#include "UI/Components/MuLookAndFeel.h"
#include "Sequencer/GatePattern.h"

namespace mu_tant
{

// Per-voice gating designer (placeholder for the eventual interactive gate
// editor). Represents 2 bars of gate pattern as a full-width rectangle; a
// note-length dropdown at the top sets the subdivision-grid spacing
// (1/4, 1/8, 1/16, ...). No gate data is wired yet — this is the visual
// scaffold that the real drawable gate editor will sit inside once the
// sequencer model lands.
class GatingDesigner : public juce::Component
{
public:
    GatingDesigner();

    // Set the subdivision note value. Affects the gridline density.
    // 4 = 1/4 → 8 cells, 8 = 1/8 → 16 cells, 16 = 1/16 → 32, 32 = 1/32 → 64.
    void setSubdivision(int denominator);
    int  getSubdivision() const noexcept { return subdivisionDenom; }

    // Bind to a per-voice GatePattern — clicking the subdivision dropdown
    // writes back to `pattern->subdivision`. Pass nullptr to unbind. Pattern
    // ownership stays with the caller (PluginProcessor).
    void setPattern(GatePattern* pattern);

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Total bars represented by the strip. Fixed at 2 per the design spec.
    static constexpr int kTotalBars = 2;

private:
    juce::Label    subdivLabel;
    DropdownSelect subdivDropdown;

    int subdivisionDenom = 16;   // 1/16 default — 32 cells over 2 bars

    // Bound GatePattern — receives subdivision writes. nullptr when no voice
    // is selected (e.g. just-constructed VoicePanel before setVoice runs).
    GatePattern* boundPattern = nullptr;

    static constexpr int kHeaderH      = 24;   // header band height
    static constexpr int kGridH        = 80;   // gate rectangle height
    static constexpr int kHeaderInset  = 6;
    static constexpr int kDropdownW    = 88;

    int cellCount() const noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GatingDesigner)
};

} // namespace mu_tant
