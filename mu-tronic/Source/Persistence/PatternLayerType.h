#pragma once

#include "Sequencer/LayerTypeInfo.h"

class ProcessorBase;

namespace mu_tronic
{

// The Pattern layer type as mu-Tronic offers it: mu-Tant's description of the type (its parameter table and tonal
// centre, its layer) plus what a slot host adds: the layer services' host adapter (the shared wavetable bank, the
// slots' engines), the layer's own Note mode, the Pattern content and sidebar picture built over that adapter, the
// type's paint colour (mu-Tant's green) and its header tag ("Tant"). The one place mu-Tronic names the Pattern's
// layer library. The content and picture factories exist in the app target only (MU_TRONIC_WITH_UI), as for the Arp.
mu_core::LayerTypeInfo patternLayerType();

// Note mode of the Pattern in slot `slot`: 0 Free (the drone runs), 1 Note (it sounds while a key is held and
// follows the held key). Per layer and saved in the layer's own node (mu-Tant keeps it as an app setting). The setter
// is message thread; it returns false for a slot that does not hold a Pattern. The getter answers 0 for such a slot.
bool setPatternNoteMode(ProcessorBase& host, int slot, int mode);
int  getPatternNoteMode(const ProcessorBase& host, int slot);

} // namespace mu_tronic
