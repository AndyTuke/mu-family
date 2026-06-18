#include "Persistence/PresetMigrations.h"
#include "Audio/AlgorithmNames.h"    // mu_audio::indexFromName, kInsertAlgorithmNames
#include "Audio/InsertSlotConfig.h"  // mu_ui::actualToNorm

namespace mu_pp_migrate
{

void migrateLegacyHostState(juce::ValueTree& state)
{
    const int version = (int)state.getProperty("formatVersion", 0);
    if (version >= kCurrentStateFormatVersion) return;

    auto isAdsrTimeSuffix = [](const juce::String& suffix) -> bool {
        return suffix == "aEnvAtk" || suffix == "aEnvDec" || suffix == "aEnvRel"
            || suffix == "fEnvAtk" || suffix == "fEnvDec" || suffix == "fEnvRel"
            || suffix == "pEnvAtk" || suffix == "pEnvDec" || suffix == "pEnvRel";
    };

    const juce::Identifier paramType   ("PARAM");
    const juce::Identifier idProperty  ("id");
    const juce::Identifier valProperty ("value");

    for (int i = 0; i < state.getNumChildren(); ++i)
    {
        auto child = state.getChild(i);
        if (child.getType() != paramType) continue;

        const juce::String id = child.getProperty(idProperty).toString();
        // Match r{0-7}_<suffix>
        if (id.length() < 4 || id[0] != 'r' || id[1] < '0' || id[1] > '7' || id[2] != '_')
            continue;

        const juce::String suffix = id.substring(3);
        if (!isAdsrTimeSuffix(suffix)) continue;

        const float oldVal = (float)child.getProperty(valProperty, 0.0);
        float newVal = juce::jlimit(0.0f, 10.0f, oldVal * 0.03f);

        // aEnvRel End-mode sentinel: old max (100) → new max (10).
        if (suffix == "aEnvRel" && oldVal >= 100.0f) newVal = 10.0f;

        child.setProperty(valProperty, newVal, nullptr);
    }

    state.setProperty("formatVersion", kCurrentStateFormatVersion, nullptr);
}

// Stage 36 (v3): collapse the per-rhythm insert from 9 named fields
// (drvDrv / drvOut / drvDit / drvTon / drvBits / drvRate / eqLowGain /
// eqMidGain / eqHighGain) to 4 generic normalised Param slots
// (insP1 / insP2 / insP3 / insP4). The translation depends on the saved
// algorithm — each algo's old field set maps to the new slots through
// mu_ui::kInsertAlgoSlots' per-algo ranges. Older EQ presets where Low /
// High were packed into drvDrv / drvDit as 0..100 are also handled via the
// formula `dB = old0to100 / 100 * 36 - 18`.
//
// Pre-condition: the old `eqLowGain` / `eqHighGain` migration (which this
// supersedes) is no longer needed — those fields now feed straight into the
// per-algo slot mapping for EQ.
//
// `srcPrefix` is "r0_" for rhythm presets / hot-swap loads, or "" for the
// MuClidPreset per-rhythm Rhythm child tree.
void migrateInsertSlotsV3(juce::ValueTree& tree, const juce::String& srcPrefix)
{
    const juce::String charProp = srcPrefix + "drvChar";
    if (! tree.hasProperty(charProp)) return;

    // Skip if already v3 (any one of the new slot props present).
    if (tree.hasProperty(srcPrefix + "insP1")) return;

    const juce::String charName = tree.getProperty(charProp).toString();
    const int algoIndex = mu_audio::indexFromName(mu_audio::kInsertAlgorithmNames, charName);
    if (algoIndex < 0) return;

    auto readD = [&tree, &srcPrefix](const juce::String& name, double dflt) -> float {
        return tree.hasProperty(srcPrefix + name)
            ? (float)(double) tree.getProperty(srcPrefix + name)
            : (float) dflt;
    };

    const float oldDrv = readD("drvDrv", 0.0);
    const float oldOut = readD("drvOut", 0.0);
    const float oldDit = readD("drvDit", 0.0);
    const float oldTon = readD("drvTon", 20000.0);
    const float oldBit = readD("drvBits", 16.0);
    const float oldRte = readD("drvRate", 48000.0);
    const float oldEqM = readD("eqMidGain", 0.0);
    const float oldEqL = readD("eqLowGain", 0.0);
    const float oldEqH = readD("eqHighGain", 0.0);

    float actual[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    switch (algoIndex)
    {
        case 1: case 2: case 3: case 5: case 10:  // SoftClip/Hard/Fold/Clipper/TapeSat
            actual[0] = oldDrv;
            actual[1] = oldOut;
            actual[3] = oldTon;
            break;
        case 4:  // Bitcrusher
            actual[0] = oldBit;
            actual[1] = oldRte;
            actual[2] = oldDit;
            actual[3] = oldTon;
            break;
        case 6:  // EQ — prefer dedicated fields if present, else legacy packed
            actual[0] = tree.hasProperty(srcPrefix + "eqLowGain")  ? oldEqL
                                                                   : (oldDrv / 100.0f * 36.0f - 18.0f);
            actual[1] = oldEqM;
            actual[2] = oldTon;   // Mid Hz lives in tone
            actual[3] = tree.hasProperty(srcPrefix + "eqHighGain") ? oldEqH
                                                                   : (oldDit / 100.0f * 36.0f - 18.0f);
            break;
        case 7: case 8:  // Compressor / Limiter
            actual[0] = oldDrv;
            actual[1] = oldOut;
            actual[2] = oldDit;
            actual[3] = oldTon;
            break;
        case 9:  // RingMod
            actual[0] = oldDrv;
            actual[3] = oldTon;
            break;
        case 11:  // Karplus
            actual[0] = oldDrv;
            actual[1] = oldBit;
            actual[2] = oldDit;
            actual[3] = oldTon;
            break;
        case 12: case 13:  // Vocoder / VocoderSt — Unison decode from -24..0 → 0..6
            actual[0] = oldDrv;
            actual[1] = (oldOut + 24.0f) * 0.25f;
            actual[2] = oldDit;
            actual[3] = oldBit;
            break;
        default: break;  // 0 None — leave zeros
    }

    for (int s = 0; s < 4; ++s)
    {
        const float norm = mu_ui::actualToNorm(actual[s], algoIndex, s);
        tree.setProperty(srcPrefix + "insP" + juce::String(s + 1), (double) norm, nullptr);
    }
}

// Same for master insert slots {1, 2} in a GlobalState child. Mirrors
// migrateInsertSlotsV3 but consumes `mst_ins*` / `mst_ins2*` property names.
void migrateMasterInsertSlotsV3(juce::ValueTree& tree, int slot)
{
    const juce::String pfx = slot == 1 ? juce::String("mst_ins")
                                       : juce::String("mst_ins2");
    const juce::String charProp = pfx + "Char";
    if (! tree.hasProperty(charProp)) return;
    if (tree.hasProperty(pfx + "P1")) return;

    const juce::String charName = tree.getProperty(charProp).toString();
    const int algoIndex = mu_audio::indexFromName(mu_audio::kInsertAlgorithmNames, charName);
    if (algoIndex < 0) return;

    auto readD = [&tree, &pfx](const juce::String& name, double dflt) -> float {
        return tree.hasProperty(pfx + name)
            ? (float)(double) tree.getProperty(pfx + name)
            : (float) dflt;
    };

    const float oldDrv = readD("Drv", 0.0);
    const float oldOut = readD("Out", 0.0);
    const float oldDit = readD("Dit", 0.0);
    const float oldTon = readD("Ton", 20000.0);
    const float oldBit = readD("Bits", 16.0);
    const float oldRte = readD("Rate", 48000.0);
    const float oldEqM = readD("Mid", 0.0);
    const float oldEqL = readD("Low", 0.0);
    const float oldEqH = readD("High", 0.0);

    float actual[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    switch (algoIndex)
    {
        case 1: case 2: case 3: case 5: case 10:
            actual[0] = oldDrv; actual[1] = oldOut; actual[3] = oldTon; break;
        case 4:
            actual[0] = oldBit; actual[1] = oldRte; actual[2] = oldDit; actual[3] = oldTon; break;
        case 6:
            actual[0] = tree.hasProperty(pfx + "Low")  ? oldEqL : (oldDrv / 100.0f * 36.0f - 18.0f);
            actual[1] = oldEqM;
            actual[2] = oldTon;
            actual[3] = tree.hasProperty(pfx + "High") ? oldEqH : (oldDit / 100.0f * 36.0f - 18.0f);
            break;
        case 7: case 8:
            actual[0] = oldDrv; actual[1] = oldOut; actual[2] = oldDit; actual[3] = oldTon; break;
        case 9:
            actual[0] = oldDrv; actual[3] = oldTon; break;
        case 11:
            actual[0] = oldDrv; actual[1] = oldBit; actual[2] = oldDit; actual[3] = oldTon; break;
        case 12: case 13:
            actual[0] = oldDrv; actual[1] = (oldOut + 24.0f) * 0.25f; actual[2] = oldDit; actual[3] = oldBit; break;
        default: break;
    }

    for (int s = 0; s < 4; ++s)
    {
        const float norm = mu_ui::actualToNorm(actual[s], algoIndex, s);
        tree.setProperty(pfx + "P" + juce::String(s + 1), (double) norm, nullptr);
    }
}

// Modulator assignment migration: old destinations like "insert.drive" /
// "ks.note" / "voc.octave" remap per-algo to the new "insert.p1".."insert.p4".
// Caller supplies the rhythm's `Modulators` child + the resolved algoIndex.
void migrateModAssignmentsV3(juce::ValueTree& modsTree, int algoIndex)
{
    if (! modsTree.isValid() || algoIndex < 0) return;

    // For each algo, which OLD destination ID maps to which NEW insert.pN slot.
    // Empty target = drop the assignment (no slot in the new layout).
    auto translate = [algoIndex](const juce::String& oldDest) -> juce::String {
        // Generic insert names + algorithm-specific names. The mapping is the
        // same table as migrateInsertSlotsV3 above, expressed as which OLD
        // identifier should land on which NEW slot.
        struct M { const char* oldId; int slot; };
        static const M genericOnly[] = {
            { "insert.drive",  0 }, { "insert.output", 1 },
            { "insert.dither", 2 }, { "insert.lpf",    3 },
        };
        switch (algoIndex)
        {
            case 1: case 2: case 3: case 5: case 10:
                for (auto& m : genericOnly) if (oldDest == m.oldId) return "insert.p" + juce::String(m.slot + 1);
                break;
            case 4:  // Bitcrusher
                if (oldDest == "insert.bits")   return "insert.p1";
                if (oldDest == "insert.rate")   return "insert.p2";
                if (oldDest == "insert.dither") return "insert.p3";
                if (oldDest == "insert.lpf")    return "insert.p4";
                break;
            case 6:  // EQ
                if (oldDest == "insert.drive")  return "insert.p1";   // Low
                if (oldDest == "insert.lpf")    return "insert.p3";   // Mid Hz (was tone in v2)
                if (oldDest == "insert.dither") return "insert.p4";   // High
                break;
            case 7: case 8:
                for (auto& m : genericOnly) if (oldDest == m.oldId) return "insert.p" + juce::String(m.slot + 1);
                break;
            case 9:
                if (oldDest == "insert.drive") return "insert.p1";
                if (oldDest == "insert.lpf")   return "insert.p4";
                break;
            case 11:  // Karplus
                if (oldDest == "ks.note")       return "insert.p1";
                if (oldDest == "ks.octave")     return "insert.p2";
                if (oldDest == "insert.dither") return "insert.p3";
                if (oldDest == "insert.lpf")    return "insert.p4";
                break;
            case 12: case 13:  // Vocoder
                if (oldDest == "insert.drive")  return "insert.p1";   // Wave
                if (oldDest == "voc.unison")    return "insert.p2";
                if (oldDest == "voc.octave")    return "insert.p3";
                if (oldDest == "voc.note")      return "insert.p4";
                break;
            default: break;
        }
        return {};
    };

    for (int i = 0; i < modsTree.getNumChildren(); ++i)
    {
        auto child = modsTree.getChild(i);
        if (child.getType() != juce::Identifier("Asgn")) continue;
        if (! child.hasProperty("dest")) continue;
        const juce::String oldDest = child.getProperty("dest").toString();
        const juce::String newDest = translate(oldDest);
        if (newDest.isNotEmpty())
            child.setProperty("dest", newDest, nullptr);
    }
}

} // namespace mu_pp_migrate
