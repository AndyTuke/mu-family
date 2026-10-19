#pragma once
// The one place the mirror is written.
struct ProcessorCore { void storeLayerCount(int n) { numLayers.store(n); } };
