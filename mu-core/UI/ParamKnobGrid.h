#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/Components/KnobWithLabel.h"
#include "UI/Components/MuLookAndFeel.h"
#include "Modulation/ModulationMatrix.h"

#include <functional>
#include <memory>
#include <vector>

// Spec-driven wrapping grid of APVTS-bound controls — one rotary knob per float param,
// one combo per choice param, each under a centred name label. A product supplies a list
// of {paramId, label} specs and the grid builds the widgets, attaches them to APVTS, and
// lays them out in a grid that fits as many columns as the width allows, then wraps.
//
// Lifted from mu-on's engine panel (now TrackContent) so any product can build its engine / parameter surface
// from one shared widget (the family-reuse rule). Plugin-agnostic — the product owns the
// {id,label} list; this owns the build + attachment + layout.
namespace mu_ui {

class ParamKnobGrid : public juce::Component
{
public:
    // `modDestId` (with `modMatrix` and `modLive`) is optional: a float param that is a modulation
    // target shows its live modulation arc, bound through KnobWithLabel::bindModulation.
    struct Spec
    {
        juce::String            id;
        juce::String            label;
        const char*             modDestId = nullptr;
        const ModulationMatrix* modMatrix = nullptr;
        std::function<float()>  modLive;
        // Optional one-line hint shown on hover over the knob (empty = none). It says what the knob does, never its
        // name or value (the status bar carries those); a product whose knob changes meaning rebuilds the specs.
        juce::String            tooltip;
        // Start a new row of the grid at this control (a group, such as an envelope, kept together on its own row).
        bool                    newRow = false;
        // Start a group (an oscillator, a filter, an envelope): a row that has groups shares its spare width equally as
        // the space before the first group, between groups and after the last, so its sections sit spread across the panel.
        bool                    groupStart = false;
        // The name the status bar shows for the control (empty: the label), for a label that is abbreviated on the knob.
        juce::String            statusName;
        // A smaller knob (the family's Size 4) in the same cell, for a secondary row such as an envelope under a filter.
        bool                    small = false;
        // The host's id for this knob as a step lock target (-1: the knob cannot be locked). See setLockMode.
        int                     lockKey = -1;
    };

    explicit ParamKnobGrid(juce::AudioProcessorValueTreeState& state);

    // Rebuild the controls from `specs` (clears existing + re-attaches). Safe to call
    // repeatedly, e.g. on lane / voice select. Knobs are drawn at the family's standard
    // Size-2 dimensions and wrap to fit the width.
    void setSpecs(const std::vector<Spec>& specs);

    // Drop every knob's modulation binding (matrix pointer + live-value callback), so no timer tick
    // can reach a layer's data while a structural edit moves or frees it. setSpecs rebinds.
    void clearModBindings();

    // The standard knob size the controls fit at in the grid's current height, as an index: 0 = Size 2, 1 = Size 3, 2 = Size 4.
    int  fitIndex() const;
    // Never draw bigger than this size index: two grids side by side take the larger of their fitIndex so their knobs match.
    void setMinSizeIndex(int index) { if (index != minSizeIndex) { minSizeIndex = index; resized(); } }

    // Where control `index` (in spec order) sits, in the grid's own coordinates; empty if there is none. For a decoration drawn
    // beside the controls (a line joining two knobs).
    juce::Rectangle<int> getControlBounds(int index) const
    {
        if (index < 0 || index >= (int) controls.size()) return {};
        const auto& c = *controls[(size_t) index];
        return c.knob != nullptr ? c.knob->getBounds() : c.combo != nullptr ? c.combo->getBounds() : juce::Rectangle<int>();
    }

    // ── Parameter locks (a step's own values for chosen knobs) ──────────────────────────────────────────────────────
    // The host says what the shown step has locked (lockOf: the lock's proportion 0..1 on a key) and takes edits (onLockEdit,
    // onLockRemove). A knob with a lock shows the locked value, a red LED in its centre and a ghost line at the track's own value;
    // while lock mode is on, turning a lockable knob writes a lock instead of changing the track's parameter. Such a knob is
    // detached from its parameter until the lock is gone and the mode is off. Call refreshLocks when the step, its locks or the
    // mode change (it is cheap; the host's timer may call it).
    std::function<bool(int key, float& proportion)> lockOf;
    std::function<bool(int key, float proportion)>  onLockEdit;   // false: refused (the step is full)
    std::function<void(int key)>                    onLockRemove;
    void setLockMode(bool on) { lockMode = on; refreshLocks(); }
    bool isLockMode() const noexcept { return lockMode; }
    void refreshLocks();

    // The width (pixels) the controls need on a single row — a selector takes two cells.
    int getPreferredWidth() const;

    void resized() override;
    void mouseEnter(const juce::MouseEvent&) override;   // a control under the mouse reports its value

    // Fired with the control's label + current value text when a control is hovered or changed.
    std::function<void(const juce::String& name, const juce::String& value)> onStatusUpdate;

    // One row of cells (unscaled): name label over a Size-2 knob, plus a small gap.
    static constexpr int kLabelH = 13;
    static constexpr int kCellH  = kLabelH + MuLookAndFeel::kKnobSize2H + 4;

private:
    struct Control
    {
        juce::String                    label;
        bool                            newRow = false;   // starts a new row of the grid
        bool                            groupStart = false;   // starts a group (see Spec)
        juce::String                    statusName;
        bool                            small = false;   // a Size 4 knob (see Spec)
        int                             lockKey = -1;    // see Spec
        juce::String                    paramId;
        bool                            detached = false;   // not attached to the parameter: it shows (and edits) a step's lock
        std::unique_ptr<KnobWithLabel>  knob;
        std::unique_ptr<juce::ComboBox> combo;
        juce::Label                     name;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   knobAtt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAtt;
    };

    void reportStatus(const Control&) const;
    void lockEdited(Control&);
    bool lockMode = false;
    bool refreshingLocks = false;
    int  heightAt(int sizeIndex, int areaWidth) const;   // the height the controls need at a size index, wrapped at the width
    int  minSizeIndex = 0;

    juce::AudioProcessorValueTreeState&   apvts;
    std::vector<std::unique_ptr<Control>> controls;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParamKnobGrid)
};

} // namespace mu_ui
