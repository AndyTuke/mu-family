#pragma once

#include "Persistence/ParamText.h"
#include "Plugin/HotSwap.h"   // mu_hotswap::kSwapTimingParamId
#include <juce_audio_processors/juce_audio_processors.h>
#include "Audio/AlgorithmNames.h"   // mu_audio::kInsertAlgorithmCount
#include "Audio/VoiceParams.h"      // VoiceParams::kInsertSlotCount
#include "MuLimits.h"             // mu_limits::kMaxLayers / kMaxBuses: the sidechain source range

// Shared global FX + return + master APVTS parameter layout for the family
// mixer. The shared MixerOverlay / FXRow / DelayRow bind to these exact IDs, so
// any product that wants the mixer FX rack declares them via this helper (no
// duplication). Per-channel strip params (`ch{i}_*`) + product-specific globals
// (e.g. mu-clid's `mstrLoop`) are declared by the product, since their count /
// display names differ. Ranges + defaults mirror mu-clid's reference layout.
//
// The matching engine sync is ProcessorBase::syncGlobalFxParam().
namespace mu_mixfx {

// The largest sidechain source parameter value: 0 = off, 1..kMaxLayers = channels, kMaxLayers + 1 = the
// external input, then one per group bus (equal to kMaxLayers + 1 where a product has no buses).
inline constexpr int kScSrcMax = mu_limits::kMaxLayers + 1 + mu_limits::kMaxBuses;

// `swapTimingDefault` is the product's hot-swap timing (mu_hotswap::SwapTiming as an int): OnMasterLoop (0)
// for the looping products, OnBar (2) for those with no loop of their own.
inline void addGlobalFxParams(juce::AudioProcessorValueTreeState::ParameterLayout& layout, int swapTimingDefault = 0)
{
    auto addF = [&](const juce::String& id, const juce::String& name, float mn, float mx, float def)
    { layout.add(std::make_unique<juce::AudioParameterFloat>(id, name, juce::NormalisableRange<float>(mn, mx), def)); };
    // A 0..1 or -1..1 amount shown to three decimals, so the host's text round-trips exactly.
    auto addF3 = [&](const juce::String& id, const juce::String& name, float mn, float mx, float def)
    {
        const juce::NormalisableRange<float> r(mn, mx);
        layout.add(std::make_unique<juce::AudioParameterFloat>(id, name, r, def,
                       mu_fmt::floatText(r, [](float v, int) { return juce::String(v, 3); })));
    };
    auto addB = [&](const juce::String& id, const juce::String& name, bool def)
    { layout.add(std::make_unique<juce::AudioParameterBool>(id, name, def)); };
    auto addI = [&](const juce::String& id, const juce::String& name, int mn, int mx, int def)
    { layout.add(std::make_unique<juce::AudioParameterInt>(id, name, mn, mx, def)); };

    // ── Effect slot ──────────────────────────────────────────────────────────
    addI("eff_algo", "Effect Algorithm", 0, 7, 0);
    addB("eff_en",   "Effect Enable", true);
    addF("eff_p0", "Effect P0", 0.0f, 1.0f, 0.5f);
    addF("eff_p1", "Effect P1", 0.0f, 1.0f, 0.5f);
    addF("eff_p2", "Effect P2", 0.0f, 1.0f, 0.5f);
    addF("eff_p3", "Effect P3", 0.0f, 1.0f, 0.5f);
    addF("eff_p4", "Effect P4", 0.0f, 1.0f, 0.5f);

    // ── Delay slot ───────────────────────────────────────────────────────────
    addB("dly_en",        "Delay Enable",    true);
    addB("dly_mode",      "Delay Sync",      true);
    addF("dly_ms",        "Delay Time (ms)", 1.0f, 4000.0f, 250.0f);
    addI("dly_syncDenom", "Delay Denom",     0, 3, 2);
    addB("dly_syncDot",   "Delay Dotted",    true);
    addB("dly_syncTrip",  "Delay Triplet",   false);
    addI("dly_count",     "Delay Count",     1, 8, 1);
    addF("dly_fb",        "Delay Feedback",  0.0f,  0.98f, 0.30f);
    addF("dly_spread",    "Delay Spread",    0.0f,  1.0f,  0.0f);
    addF("dly_dirt",      "Delay Dirt",      0.0f,  1.0f,  0.0f);

    // ── Reverb slot ──────────────────────────────────────────────────────────
    addI("rev_algo", "Reverb Algorithm",  0, 3,  1);          // 1 = Hall
    addB("rev_en",   "Reverb Enable",     true);
    addF("rev_lvl",  "Reverb Level",  0.0f,   1.0f,  1.0f);
    addF("rev_size", "Reverb Size",   0.0f,   1.0f,  0.75f);
    addF("rev_decay", "Reverb Decay", 0.1f,  10.0f,  2.5f);    // RT60 in seconds; 2.5 is the Hall preset
    addF("rev_pre",  "Reverb Pre-Delay", 0.0f, 100.0f, 25.0f);
    addF("rev_diff", "Reverb Diffusion", 0.0f,   1.0f,  0.80f);
    addF("rev_damp", "Reverb Damp",   0.0f,   1.0f,  0.30f);
    addF("rev_mod",  "Reverb Mod",    0.0f,   1.0f,  0.15f);
    addF("rev_dirt", "Reverb Dirt",   0.0f,   1.0f,  0.0f);

    // ── Intra-FX routing ──────────────────────────────────────────────────────
    addF("eff2dly", juce::String::fromUTF8(u8"Effect→Delay"),  0.0f, 1.0f, 0.0f);
    addF("eff2rev", juce::String::fromUTF8(u8"Effect→Reverb"), 0.0f, 1.0f, 0.0f);
    addF("dly2rev", juce::String::fromUTF8(u8"Delay→Reverb"),  0.0f, 1.0f, 0.0f);

    // ── Echo (embedded in the EFX slot when algo=Echo) ────────────────────────
    addB("echo_en",        "Echo Enable",      true);
    addF("echo_mode",      "Echo Mode",        0.0f, 1.0f, 0.0f);
    addF("echo_ms",        "Echo Time Ms",     1.0f, 4000.0f, 250.0f);
    addI("echo_syncDenom", "Echo Sync Denom",  0, 3, 2);
    addB("echo_syncDot",   "Echo Dotted",      false);
    addB("echo_syncTrip",  "Echo Triplet",     false);
    addI("echo_count",     "Echo Count",       1, 8, 1);
    addF("echo_fb",        "Echo Feedback",    0.0f, 1.0f, 0.45f);
    addF("echo_spread",    "Echo Spread",      0.0f, 1.0f, 0.0f);
    addF("echo_dirt",      "Echo Dirt",        0.0f, 1.0f, 0.0f);

    // ── Return channel strips (eff / dly / rev) ───────────────────────────────
    for (const char* ret : { "eff", "dly", "rev" })
    {
        const juce::String q  = juce::String("ret_") + ret + "_";
        const juce::String nm = juce::String("Ret ") + ret + " ";
        addF3(q+"lvl",  nm+"Level",    0.0f,    1.0f,    0.75f);
        addF3(q+"pan",  nm+"Pan",     -1.0f,    1.0f,    0.0f);
        addB(q+"mute",  nm+"Mute",    false);
        addB(q+"solo",  nm+"Solo",    false);
        addI(q+"scSrc", nm+"SC Src",  0,        kScSrcMax, 0);  // 0=off, 1..kMaxLayers=channels, kMaxLayers+1=external, then the buses
        addF(q+"scAmt", nm+"SC Amt",  0.0f,     1.0f,    0.0f);
        addF(q+"scAtk", nm+"SC Atk",  1.0f,   500.0f,    5.0f);
        addF(q+"scRel", nm+"SC Rel",  10.0f, 2000.0f,  100.0f);
    }

    // ── Master + master inserts ───────────────────────────────────────────────
    addF("mstr_lvl",    "Master Level",     0.0f,    1.0f,     1.0f);
    addF3("mstr_pan",   "Master Pan",      -1.0f,    1.0f,     0.0f);
    // Where a staged preset swap lands (mu_hotswap::SwapTiming; the order is saved data, append only).
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { mu_hotswap::kSwapTimingParamId, 1 }, "Swap Timing",
                   juce::StringArray { "On master loop", "On layer loop", "On bar" }, swapTimingDefault));
    addI("mst_insChar", "Mst Insert Algo", 0, mu_audio::kInsertAlgorithmCount - 1, 0);
    addF("mst_insP1",   "Mst Insert P1",   0.0f, 1.0f, 0.0f);
    addF("mst_insP2",   "Mst Insert P2",   0.0f, 1.0f, 0.0f);
    addF("mst_insP3",   "Mst Insert P3",   0.0f, 1.0f, 0.0f);
    addF("mst_insP4",   "Mst Insert P4",   0.0f, 1.0f, 0.0f);
    addI("mst_ins2Char","Mst Insert2 Algo",0, mu_audio::kInsertAlgorithmCount - 1, 0);
    addF("mst_ins2P1",  "Mst Insert2 P1",  0.0f, 1.0f, 0.0f);
    addF("mst_ins2P2",  "Mst Insert2 P2",  0.0f, 1.0f, 0.0f);
    addF("mst_ins2P3",  "Mst Insert2 P3",  0.0f, 1.0f, 0.0f);
    addF("mst_ins2P4",  "Mst Insert2 P4",  0.0f, 1.0f, 0.0f);
}

// IDs this helper declares — for products that want to register parameter
// listeners / run an initial engine sync over exactly this set. Channel-strip
// (`ch{i}_*`) IDs are NOT here (product declares those).
inline bool isGlobalFxParamId(const juce::String& id)
{
    return id.startsWith("eff_") || id.startsWith("eff2")
        || id.startsWith("dly_") || id.startsWith("dly2")   // dly2rev: delay→reverb intra-FX send
        || id.startsWith("rev_")
        || id.startsWith("echo_") || id.startsWith("ret_")
        || id == "mstr_lvl" || id == "mstr_pan" || id.startsWith("mst_ins")
        || id.startsWith("bus");   // bus{B}_*: the group buses (channel-strip "bus" is under ch{N}_)
}

// Per-strip defaults and ID style for addChannelStripParams.
struct ChannelStripOptions
{
    int   versionHint = 1;        // parameter version hint (mu-Clid's strips predate hints: 0)
    bool  continuous  = false;    // ranges without a snapping interval (mu-Clid's reference layout)
    int   scSrc       = 0;        // sidechain source default: 0 = off, 1..kMaxLayers = channels, kMaxLayers + 1 = external
    float scAmt       = 0.0f;     // sidechain amount default
    float scRel       = 100.0f;   // sidechain release default (ms)
    bool  busAssign   = true;     // declare "bus" (the group bus this strip feeds); only where buses exist, never on a bus itself
    bool  midiIn      = false;    // declare "midiIn" (the MIDI channel this strip's layer hears: 0 Omni, 1-16); only where each strip has its own layer
};

// One mixer channel strip's parameters — level / pan / mute / solo, the three FX sends,
// sidechain and output bus — under `prefix` (e.g. "ch3_"), named "<namePrefix>Level" etc. Where the
// product has group buses (mu_limits::kMaxBuses > 0) a channel strip also gets "bus" (0 = master, 1..kMaxBuses).
// The shared MixerChannel binds to these ids; ProcessorBase::syncGlobalFxParam syncs them.
inline void addChannelStripParams(juce::AudioProcessorValueTreeState::ParameterLayout& layout,
                                  const juce::String& prefix, const juce::String& namePrefix,
                                  const ChannelStripOptions& o = {})
{
    auto range = [&o](float lo, float hi, float step)
    { return juce::NormalisableRange<float>(lo, hi, o.continuous ? 0.0f : step); };
    auto addF = [&](const char* id, const char* name, float lo, float hi, float step, float def)
    {
        // Three decimals to match the 0.001 steps, so the host's text round-trips exactly.
        const auto r = range(lo, hi, step);
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { prefix + id, o.versionHint },
                                                               namePrefix + name, r, def,
                                                               mu_fmt::floatText(r, [](float v, int) { return juce::String(v, 3); })));
    };
    auto addB = [&](const char* id, const char* name)
    { layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { prefix + id, o.versionHint },
                                                            namePrefix + name, false)); };
    auto addI = [&](const char* id, const char* name, int lo, int hi, int def)
    { layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID { prefix + id, o.versionHint },
                                                           namePrefix + name, lo, hi, def)); };

    addF("lvl",     "Level",      0.0f,    1.0f, 0.001f, 1.0f);   // 0 dB default
    addF("pan",     "Pan",       -1.0f,    1.0f, 0.001f, 0.0f);
    addB("mute",    "Mute");
    addB("solo",    "Solo");
    addF("sendEff", "Send Eff",   0.0f,    1.0f, 0.001f, 0.0f);
    addF("sendDly", "Send Dly",   0.0f,    1.0f, 0.001f, 0.0f);
    addF("sendRev", "Send Rev",   0.0f,    1.0f, 0.001f, 0.0f);
    addI("scSrc",   "SC Src",     0, kScSrcMax, o.scSrc);   // 0 off, 1..kMaxLayers channels, the external input, then the buses
    addF("scAmt",   "SC Amount",  0.0f,    1.0f, 0.001f, o.scAmt);
    addF("scAtk",   "SC Attack",  1.0f,  500.0f, 0.1f,   5.0f);
    addF("scRel",   "SC Release", 10.0f, 2000.0f, 1.0f,  o.scRel);
    addI("outBus",  "Output Bus", 0, 8, 0);                     // 0 = master mix, 1..8 = direct out
    if constexpr (mu_limits::kMaxBuses > 0)
        if (o.busAssign)
            addI("bus", "Bus", 0, mu_limits::kMaxBuses, 0);         // 0 = master, 1..kMaxBuses = group bus
    if (o.midiIn)
        addI("midiIn", "MIDI In", 0, 16, 0);                        // 0 = Omni, 1..16 = that channel only
}

// One insert's parameters under `prefix` (e.g. "bus0_i1_"): the algorithm and the four generic slot
// knobs. The suffixes and ranges are the layer insert's (drvChar, insP1..insP4, stored normalised 0..1),
// so the shared insert UI and the engine read a bus insert exactly as they read a layer's.
inline void addInsertParams(juce::AudioProcessorValueTreeState::ParameterLayout& layout,
                            const juce::String& prefix, const juce::String& namePrefix = {}, int versionHint = 1)
{
    layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID { prefix + "drvChar", versionHint },
                   namePrefix + "Insert Algo", 0, mu_audio::kInsertAlgorithmCount - 1, 0));
    for (int p = 1; p <= VoiceParams::kInsertSlotCount; ++p)
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { prefix + "insP" + juce::String(p), versionHint },
                       namePrefix + "Insert P" + juce::String(p), juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
}

// The group buses' parameters: bus{B}_ with the channel strip's suffixes (a bus has the same fader, pan,
// mute / solo, sends and sidechain; "outBus" is declared but unused), and bus{B}_i{I}_ per insert. Nothing is
// declared where mu_limits::kMaxBuses is 0. Buses have no modulators.
inline void addBusParams(juce::AudioProcessorValueTreeState::ParameterLayout& layout, ChannelStripOptions o = {})
{
    o.busAssign = false;
    for (int b = 0; b < mu_limits::kMaxBuses; ++b)
    {
        const juce::String prefix = "bus" + juce::String(b) + "_";
        const juce::String name   = "Bus " + juce::String(b + 1) + " ";
        addChannelStripParams(layout, prefix, name, o);
        for (int i = 0; i < mu_limits::kBusInserts; ++i)
            addInsertParams(layout, prefix + "i" + juce::String(i) + "_", name + "I" + juce::String(i + 1) + " ", o.versionHint);
    }
}

} // namespace mu_mixfx
