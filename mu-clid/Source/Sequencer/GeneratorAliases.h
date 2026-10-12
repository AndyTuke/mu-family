#pragma once

// The Euclidean pattern generators live in mu-core (shared with mu-Clid MIDI); this brings
// their names into mu_clid so existing code reads unchanged.
#include "Sequencer/EuclideanGenerator.h"
#include "Sequencer/HitGenerator.h"
#include "Sequencer/Rhythm.h"
#include "Sequencer/RhythmPlayState.h"
#include "Sequencer/RhythmSequencer.h"
#include "Persistence/RhythmParamTable.h"
#include "Persistence/RhythmPresetHelpers.h"

namespace mu_clid {

using mu_core::EuclideanGenerator;
using mu_core::HitGenerator;
using mu_core::EuclidGenOverrides;
using mu_core::InsertMode;
using mu_core::StepType;
using mu_core::Rhythm;
using mu_core::Logic;
using mu_core::EuclidOverrides;
using mu_core::RhythmPlayState;
using mu_core::RhythmSequencer;
using mu_core::BlockResult;
using mu_core::ParamKind;
using mu_core::RhythmParamDef;
using mu_core::kRhythmParamDefs;
using mu_core::kRhythmParamCount;
using mu_core::findRhythmParamDef;
using mu_core::writeKindedProperty;
using mu_core::readKindedPropertyAsActualV2;

} // namespace mu_clid
