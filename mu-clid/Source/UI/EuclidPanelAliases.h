#pragma once

// The Euclid panel and ring live in mu-core (shared with mu-Clid MIDI); this brings their names into
// mu_clid for the editor files. Kept apart from GeneratorAliases.h because they are GUI classes.
#include "UI/EuclideanPanel.h"
#include "UI/RhythmCircle.h"

namespace mu_clid {

using mu_ui::EuclideanPanel;
using mu_ui::RhythmCircle;

} // namespace mu_clid
