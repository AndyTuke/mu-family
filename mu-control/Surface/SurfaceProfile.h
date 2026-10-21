#pragma once

#include "Control/LiveView.h"
#include "Surface/SurfaceDescription.h"
#include "Surface/SurfaceEvent.h"
#include "Surface/SurfaceMapping.h"
#include <optional>

// A product's way of using a surface beyond the family default mapping: small, named differences held as data, so the
// driver and the surface description stay generic and a product never forks them. The profile is chosen from what the
// LiveView says the product offers, never from a product name.
//
// Two differences today. Layer pages: top-row 5 and 6 turn the page instead of stepping clips, and the rows mean the layers of the
// shown page. And mu-Tronic's scene picker. While a hold control is down, the grid stops meaning "launch
// clip" and shows the scenes as whole columns; pressing a lit column launches that scene (clip n on every layer that stores
// one). The hold is a state of the surface model, not a ControlAction, so it cannot be mapped, saved or sent from MIDI.
namespace mu_control
{

struct SurfaceProfile
{
    // The control held to open the scene picker; none = the product has no picker (the family default).
    std::optional<SurfaceControlId> sceneHold;

    // The product pages its layers on the surface: the clip arrows become PagePrev / PageNext, and the grid rows and the side
    // column are the layers of the shown page. Chosen from what the product says it offers, never from its name.
    bool layerPages = false;

    // The mu-Tronic profile for a surface: hold the logo and press a pad in column n. The press of the logo (CC 99,
    // value 127 / 0 on a Launchpad X) is still to be confirmed on the hardware. If it does not report, the ruled fallback is
    // to hold top-row button 7 instead (SurfaceControlId { Top, 6, 0 }), with its tap-to-mute firing on release when no pad
    // was pressed during the hold; nothing else about the picker changes. A surface without a logo has no picker.
    static SurfaceProfile forView(const mu_core::LiveView& view, const SurfaceDescription& description)
    {
        SurfaceProfile p;
        const SurfaceControlId logo { ControlKind::Special, 0, 0 };
        if (view.hasSceneControls() && hasControl(description, logo)) p.sceneHold = logo;
        p.layerPages = view.hasLayerPages();
        return p;
    }
};

} // namespace mu_control
