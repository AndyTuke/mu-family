#pragma once

#include "UI/ChannelSidebar.h"

namespace mu_tant
{

class PluginProcessor;

// mu-tant's left sidebar IS the shared mu-core ChannelSidebar — same select /
// add / delete / reorder UX as mu-clid. The only product-specific part is the
// per-voice mini-graphic injected via createMiniVisual (mu-clid uses a
// RhythmCircle; mu-tant a voice glyph). No bespoke sidebar code.
class VoiceSidebar : public ChannelSidebar
{
public:
    explicit VoiceSidebar(PluginProcessor& proc);
};

} // namespace mu_tant
