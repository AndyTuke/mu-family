#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Audio/AlgorithmNames.h"   // mu_audio::kInsertAlgorithmCount

// Shared global FX + return + master APVTS parameter layout for the family
// mixer. The shared MixerOverlay / FXRow / DelayRow bind to these exact IDs, so
// any product that wants the mixer FX rack declares them via this helper (no
// duplication). Per-channel strip params (`ch{i}_*`) + product-specific globals
// (e.g. mu-clid's `mstrLoop`) are declared by the product, since their count /
// display names differ. Ranges + defaults mirror mu-clid's reference layout.
//
// The matching engine sync is ProcessorBase::syncGlobalFxParam().
namespace mu_mixfx {

inline void addGlobalFxParams(juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    auto addF = [&](const juce::String& id, const juce::String& name, float mn, float mx, float def)
    { layout.add(std::make_unique<juce::AudioParameterFloat>(id, name, juce::NormalisableRange<float>(mn, mx), def)); };
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
        addF(q+"lvl",   nm+"Level",    0.0f,    1.0f,    0.75f);
        addF(q+"pan",   nm+"Pan",     -1.0f,    1.0f,    0.0f);
        addB(q+"mute",  nm+"Mute",    false);
        addB(q+"solo",  nm+"Solo",    false);
        addI(q+"scSrc", nm+"SC Src",  0,        9,       0);  // 0=off, 1-8=ch0-ch7, 9=ext DAW bus
        addF(q+"scAmt", nm+"SC Amt",  0.0f,     1.0f,    0.0f);
        addF(q+"scAtk", nm+"SC Atk",  1.0f,   500.0f,    5.0f);
        addF(q+"scRel", nm+"SC Rel",  10.0f, 2000.0f,  100.0f);
    }

    // ── Master + master inserts ───────────────────────────────────────────────
    addF("mstr_lvl",    "Master Level",     0.0f,    1.0f,     1.0f);
    addF("mstr_pan",    "Master Pan",      -1.0f,    1.0f,     0.0f);
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
        || id == "mstr_lvl" || id == "mstr_pan" || id.startsWith("mst_ins");
}

} // namespace mu_mixfx
