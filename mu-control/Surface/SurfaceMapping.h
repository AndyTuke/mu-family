#pragma once

#include "Control/ControlAction.h"
#include "Surface/SurfaceDescription.h"
#include "Surface/SurfaceEvent.h"
#include <juce_data_structures/juce_data_structures.h>
#include <map>
#include <optional>
#include <vector>

// Which surface control means which ControlAction. The default is generated from the SurfaceDescription alone
// (top row = performance items, side column = layer select, grid = clip launch, layer 1 on the top row);
// only the overrides a user made are saved, as a ValueTree / XML keyed by the description id. No device bytes
// and no product: actions are saved by their stable names.
namespace mu_control
{

// The one place the layer/row convention lives: layer N (0-based) is surface row N counted from the top, on the
// grid and on the side column alike, matching the on-screen layer order. Lights and presses both go through the
// mapping, so they cannot disagree. Change the order here only.
constexpr int layerToRow(int layer) noexcept { return layer; }
constexpr int rowToLayer(int row) noexcept   { return row; }

// Every control the description has, in a fixed order: pads row by row from the top, then Top, Side, Special.
std::vector<SurfaceControlId> controlsOf(const SurfaceDescription& d);

// True when the description has this control.
bool hasControl(const SurfaceDescription& d, const SurfaceControlId& id) noexcept;

class SurfaceMapping
{
public:
    static constexpr int kVersion = 1;   // the saved form; a newer file is ignored, not guessed at

    struct Binding
    {
        SurfaceControlId        id;
        mu_core::ControlAction  action;
    };

    explicit SurfaceMapping(const SurfaceDescription& d);

    // The action a press of `id` performs; empty when the control is unbound or not on this surface.
    std::optional<mu_core::ControlAction> actionFor(const SurfaceControlId& id) const;

    // Every bound control, in controlsOf order (the model walks this to decide the lights).
    std::vector<Binding> bindings() const;

    // A user override. An action of type None unbinds the control; an id not on this surface is refused.
    bool setOverride(const SurfaceControlId& id, const mu_core::ControlAction& action);
    void clearOverride(const SurfaceControlId& id);
    bool hasOverrides() const noexcept { return ! overrides.empty(); }

    // Overrides only. restoreFrom replaces the overrides and ignores what does not fit: another description
    // id, an unknown or newer version, an unknown action name, a control this surface does not have.
    // Returns the number of overrides applied.
    juce::ValueTree toValueTree() const;
    juce::String    toXml() const;
    int             restoreFrom(const juce::ValueTree& tree);
    int             restoreFromXml(const juce::String& xml);

    const SurfaceDescription& getDescription() const noexcept { return description; }

private:
    std::optional<mu_core::ControlAction> defaultAction(const SurfaceControlId& id) const;

    SurfaceDescription                                   description;
    std::map<SurfaceControlId, mu_core::ControlAction>   overrides;   // type None = explicitly unbound
};

} // namespace mu_control
