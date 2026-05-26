#pragma once

// First-visit defaults for send-FX (effect row) algorithm params.
// Values are ACTUAL (not normalised 0..1) to remain readable.
// Applied when an effect algorithm is visited for the first time in a session.
//
// Four entries indexed by eff_algo: 0=Chorus, 1=Flanger, 2=Phaser, 3=Echo.
// Derived from FXAlgorithmDef::effectAlgorithms()[i].params[j].defaultVal.
// When adding a new send-FX algorithm, append a matching entry here.
//
// Note: reverb algorithms (room/hall/plate/spring) all share the same six APVTS
// param IDs and ranges (rev_size/pre/diff/damp/mod/dirt) — no aliasing on switch,
// so per-algorithm reverb defaults are not needed.

struct EffectAlgoDefaults {
    float p0 = 0.5f, p1 = 0.5f, p2 = 0.5f, p3 = 0.5f, p4 = 0.5f;
};

namespace mu_ui {

//   p0         p1      p2      p3      p4
inline const EffectAlgoDefaults kEffectAlgoDefaults[] = {
    { 1.0f,   50.0f,  2.0f,  50.0f, 50.0f },  // 0 Chorus:  rate=1 Hz, depth=50%, voices=2, spread=50%, mix=50%
    { 0.5f,   50.0f,  0.0f,  50.0f,  0.5f },  // 1 Flanger: rate=0.5 Hz, depth=50%, feedback=0%, mix=50%, p4 unused
    { 0.5f,   50.0f,  6.0f,  50.0f, 50.0f },  // 2 Phaser:  rate=0.5 Hz, depth=50%, stages=6, feedback=50%, mix=50%
    { 250.0f, 30.0f,  0.0f,  50.0f,  0.5f },  // 3 Echo:    time=250 ms, feedback=30%, spread=0%, mix=50%, p4 unused
};

} // namespace mu_ui
