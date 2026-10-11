#pragma once

// The Euclidean pattern generators live in mu-core (shared with mu-Clid MIDI); this brings
// their names into mu_clid so existing code reads unchanged.
#include "Sequencer/EuclideanGenerator.h"
#include "Sequencer/HitGenerator.h"

namespace mu_clid {

using mu_core::EuclideanGenerator;
using mu_core::HitGenerator;
using mu_core::EuclidGenOverrides;
using mu_core::InsertMode;
using mu_core::StepType;

} // namespace mu_clid
