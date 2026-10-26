#pragma once

#include "Sequencer/Layer.h"
#include "Sequencer/LayerTypeInfo.h"

namespace mu_tronic
{

// The layer of a slot that holds nothing. The block driver skips an Empty layer (its slot's scratch is cleared
// instead) and a slot's mixer strip, clips and modulators exist for it as for any layer. Its name is empty,
// which means "the default": the processor shows "Slot N" until the user types one.
struct EmptyLayer : Layer
{
    EmptyLayer() { name.clear(); }
    int typeId() const override { return (int) mu_core::LayerTypeId::Empty; }
};

} // namespace mu_tronic
