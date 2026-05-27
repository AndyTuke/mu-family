#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/Components/SegmentControl.h"
#include "UI/Components/MuLookAndFeel.h"
#include "ModulatorEditor.h"
#include "ModMatrixPanel.h"
#include "Sequencer/VoiceSlot.h"
#include "MuLimits.h"

// Container panel: tab bar (Mod A–H + Matrix) + content area.
// Owns 8 ModulatorEditor instances (one per ControlSequence) and a ModMatrixPanel.
// Lifted to mu-core so every mu-family product reuses the same modulator UI;
// the product supplies its modulation-destination provider (which controls the
// destination dropdown contents + ID resolution) via setDestProvider().
class ModulatorPanel : public juce::Component
{
public:
    ModulatorPanel();

    // Bind to a voice slot. Pointer ownership stays with the caller.
    void setVoiceSlot(VoiceSlot* slot);
    // For products that switch the per-voice insert algorithm dynamically (mu-clid).
    // Products that don't have algo-switching just leave this at 0.
    void setInsertAlgorithm(int driveChar);
    // Product-supplied destination provider — controls what the destination
    // dropdown shows + how dropdown IDs map back to destination strings.
    void setDestProvider(const ModDestProvider* p);

    // Drive the modulator playhead from the current song beat position.
    void setPlayheadBeat(double beat);

    std::function<void()> onChange;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    VoiceSlot*             voiceSlot     = nullptr;
    const ModDestProvider* destProvider  = nullptr;

    SegmentControl tabBar { {"A","B","C","D","E","F","G","H","Matrix"},
                            SegmentControl::ActiveStyle::General,
                            SegmentControl::DrawStyle::Bar };

    // Must equal VoiceSlot::MaxControlSequences — one tab per ControlSequence.
    static constexpr int kNumMods = mu_limits::kMaxControlSequences;
    ModulatorEditor  editors[kNumMods];
    ModMatrixPanel   matrixPanel;

    int activeTab = 0;

    static constexpr int kTabH = 28;

    static juce::Colour modColour(int index) noexcept;
    void showTab(int idx);
};
