#pragma once

// Float parameter text in both directions moved to Persistence/ParamText.h: a layer type's parameter table
// uses it, and a layer-library file never includes Plugin/. This forward keeps the products' includes valid.
#include "Persistence/ParamText.h"
