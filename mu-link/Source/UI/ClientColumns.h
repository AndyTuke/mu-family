#pragma once

#include "UI/Components/MuLookAndFeel.h"

// ClientColumns — where the eight client columns, the divider and the master column sit across a
// panel's width. The Mixer panel and the Scenes panel both place their parts from this one
// calculation, so each scene cell lines up under its strip at every display size.
namespace mu_link
{

struct ClientColumns
{
    int x0 = 0;          // left edge of the first client column (panel pixels)
    int stripW = 0;      // width of one client column
    int gap = 0;         // gap between two client columns
    int masterX = 0;     // left edge of the master column (the strip, then the insert panel)
    int dividerX = 0;    // left edge of the divider groove between the strips and the master
    int contentRight = 0;

    static constexpr int kClients = 8;

    // `panelW` is the panel's width in pixels (already display-scaled).
    static ClientColumns forWidth(int panelW)
    {
        using LF = MuLookAndFeel;
        using mu_ui::s;
        ClientColumns c;
        c.x0 = s(LF::kScrewedPanelInset);
        c.contentRight = panelW - s(LF::kScrewedPanelInset);
        c.gap = s(LF::kMixerChanGap);
        const int contentW = c.contentRight - c.x0;
        c.stripW = (contentW - s(LF::kMixerMasterTotalW) - s(LF::kMixerDivW) - (kClients - 1) * c.gap) / kClients;
        c.masterX = c.contentRight - s(LF::kMixerMasterTotalW);
        const int stripsRight = c.x0 + kClients * c.stripW + (kClients - 1) * c.gap;
        c.dividerX = stripsRight + (c.masterX - stripsRight - s(LF::kMixerDivW)) / 2;
        return c;
    }

    int stripX(int i) const { return x0 + i * (stripW + gap); }
};

} // namespace mu_link
