#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// The Arp layer type's parameter table (docs/mu-tronic/design.md 2.3): the per-slot rows under the
// slot's a{N}_ prefix, the suffix table a layer binds its parameter pointers from, and mu-Toni's one
// global parameter that is not a mixer or FX row (the MIDI-in channel). Saved data keys on these ids
// and their order, so rows are only ever appended.
namespace mu_toni
{

// ── Per-layer arp/voice/env parameter slots ──────────────────────────────────
// One entry per row addLayerParams adds (the set must match; the order here is the binding order, not
// the layout order). A layer reads its slot's values through pointers cached once per slot in this
// order (never by id on the audio thread).
namespace vpi
{
    enum
    {
        scale, root, roct, chord, inv, octs, dir, rate, gate, leg, porta, snap, trig,     // arp (13)
        o1wt, o1o, o1f, o1pos, o1l, o2wt, o2o, o2s, o2f, o2pos, o2l,                            // oscillators (11)
        xmPh, xmIdx, sync, xmFb, xmAm, xmDep, xmSsb,                                              // cross-mod (7)
        noise, ntype, ft, cut, res, drv, locut,                                                  // noise + filter (7)
        aeA, aeD, aeS, aeR, aeL, feA, feD, feS, feR, feDep, peA, peD, peS, peR, peDep, ptgt,      // envs (16)
        drvChar, insP1, insP2, insP3, insP4,                                                     // insert (5)
        acc, accLen, accPat, stepMask,                                                           // accent + step pattern (4)
        COUNT
    };
    inline constexpr const char* const suffix[COUNT] = {
        "scale","root","roct","chord","inv","octs","dir","rate","gate","leg","porta","snap","trig",
        "o1_wt","o1o","o1f","o1_pos","o1l","o2_wt","o2o","o2s","o2f","o2_pos","o2l",
        "xmod_phaseMode","xmod_index","sync","xmod_fdbk","xmod_ampMode","xmod_depth","xmod_ssb",
        "noise","ntype","ft","cut","res","drv","locut",
        "aeA","aeD","aeS","aeR","aeL","feA","feD","feS","feR","feDep","peA","peD","peS","peR","peDep","ptgt",
        "drvChar","insP1","insP2","insP3","insP4",
        "acc","accLen","accPat","stepMask",
    };
}

// Rows per layer slot.
inline constexpr int kNumVoiceParams = vpi::COUNT;
static_assert(kNumVoiceParams == 63, "a new row goes into vpi, suffix[] and addLayerParams together");

// The id of mu-Toni's MIDI-in channel parameter (0 = Omni, 1..16 = that channel).
inline constexpr const char* kMidiInChannelParamId = "midiInCh";

// The root tag of a layer-preset file (.muArp), shared by mu-Toni's own presets and the slot hosts that load them.
inline constexpr const char* kArpPresetTag = "MuToniLayer";

// The Arp type's rows for one layer slot (a{slot}_*), in vpi order.
void addLayerParams(juce::AudioProcessorValueTreeState::ParameterLayout& layout, int slot);

// mu-Toni's non-mixer global rows: which MIDI channel's notes drive the arps (Omni = any).
void addArpGlobalParams(juce::AudioProcessorValueTreeState::ParameterLayout& layout);

} // namespace mu_toni
