#pragma once

#include "UI/Components/KnobWithLabel.h"
#include "Audio/InsertSlotConfig.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <functional>

// Shared UI driver for the 4 generic insert-slot knobs. Closes the #609
// duplication between the per-rhythm InsertSubsection and the master
// MixerChannel_Insert — both call this once per slot to configure label,
// range, skew, display formatter, value, and onValueChanged write-back from
// the same per-algo config table (mu_ui::kInsertAlgoSlots).
//
// Storage contract: the APVTS parameter for each slot holds the NORMALISED
// 0..1 value (same as VoiceParams::insertParam[N]). The knob's slider
// displays the algorithm's ACTUAL range (Hz / dB / Bits / Note name / etc.).
// Conversion is done by mu_ui::normToActual / actualToNorm so the same
// formula sits on both sides; UI and DSP never disagree on what 0.5 means.

namespace mu_ui {

// Map a "low-range" unit suffix to its "high-range" counterpart for
// dynamicUnit knobs that flip at 1000 (Hz↔kHz, ms↔s, etc.).
inline juce::String hiUnitFor(const char* lowUnit) noexcept
{
    const juce::String s (lowUnit);
    if (s == "Hz") return "kHz";
    if (s == "ms") return "s";
    return s;
}

// Format a number for a value-display function. Centralises the "integer for
// IntStep, 1-decimal otherwise, dynamicUnit handling at 1000" rules.
inline juce::String formatSlotValue(double v, const SlotConfig& cfg)
{
    if (cfg.enumNames != nullptr)
    {
        const int idx = juce::jlimit(0,
            (int) std::round(cfg.maxVal - cfg.minVal),
            (int) std::round(v) - cfg.enumOffset);
        return juce::String(cfg.enumNames[juce::jmax(0, idx)]);
    }
    if (cfg.dynamicUnit)
    {
        if (std::abs(v) < 1000.0) return juce::String((int) std::round(v));
        // 2 decimals 1.00..9.99, 1 decimal 10.0..; matches the legacy fmtHzNum
        return v / 1000.0 < 10.0 ? juce::String(v / 1000.0, 2)
                                 : juce::String(v / 1000.0, 1);
    }
    if (cfg.skew == SkewMode::IntStep)
        return juce::String((int) std::round(v));
    return juce::String(v, 1);
}

// Compose the displayed label including unit. dynamicUnit knobs flip Hz↔kHz
// (or ms↔s) at 1000 based on the current value.
inline juce::String composeSlotLabel(const SlotConfig& cfg, double currentActual)
{
    if (cfg.label == nullptr) return {};
    juce::String label (cfg.label);
    if (cfg.unitSuffix == nullptr || cfg.unitSuffix[0] == '\0') return label;
    if (cfg.dynamicUnit)
    {
        const juce::String unit = (std::abs(currentActual) >= 1000.0)
            ? hiUnitFor(cfg.unitSuffix)
            : juce::String(cfg.unitSuffix);
        return label + " (" + unit + ")";
    }
    return label + " (" + cfg.unitSuffix + ")";
}

// Configure one slot knob from the per-algo config table. `onWriteNorm` is
// invoked with the NEW NORMALISED value whenever the user moves the knob,
// so the caller can route to its preferred APVTS write path (apvtsSet for
// per-rhythm, setParam(fullId) for master).
inline void configureKnobFromSlot(KnobWithLabel& k,
                                  int algoIdx,
                                  int slotIdx,
                                  float currentNorm,
                                  std::function<void(float /*newNorm*/)> onWriteNorm)
{
    if (algoIdx < 0 || algoIdx >= kInsertAlgoCount || slotIdx < 0 || slotIdx >= kInsertSlotCount)
    {
        k.setVisible(false);
        k.onValueChanged = nullptr;
        return;
    }

    const SlotConfig& cfg = kInsertAlgoSlots[algoIdx][slotIdx];

    if (cfg.label == nullptr)
    {
        k.setVisible(false);
        k.onValueChanged = nullptr;
        return;
    }
    k.setVisible(true);

    // Range + skew on the underlying slider so drag feel matches storage skew.
    const double step = (cfg.skew == SkewMode::IntStep) ? 1.0
                      : ((cfg.maxVal - cfg.minVal) > 100.0 ? 1.0 : 0.1);
    k.setRange(cfg.minVal, cfg.maxVal, step);
    if (cfg.skew == SkewMode::Log && cfg.minVal > 0.0f && cfg.maxVal > cfg.minVal)
    {
        // setSkewFactorFromMidPoint at the geometric mean gives the canonical
        // log feel — same shape JUCE produces for normalised log parameters.
        k.getSlider().setSkewFactorFromMidPoint(std::sqrt((double) cfg.minVal * (double) cfg.maxVal));
    }
    else
    {
        k.getSlider().setSkewFactor(1.0);
    }

    // Display formatter — single source of truth for unit suffix + enum names.
    k.getSlider().textFromValueFunction = [cfg](double v) -> juce::String {
        return formatSlotValue(v, cfg);
    };
    k.getSlider().valueFromTextFunction = nullptr;

    // Snap the value (actual scale).
    const double actual = (double) normToActual(currentNorm, algoIdx, slotIdx);
    k.setValue(actual, juce::dontSendNotification);
    k.setLabel(composeSlotLabel(cfg, actual));

    // onValueChanged: convert back to normalised + tell caller. Also refreshes
    // the label so dynamicUnit Hz→kHz flips track the slider live.
    k.onValueChanged = [&k, cfg, algoIdx, slotIdx, onWriteNorm](double v)
    {
        onWriteNorm(actualToNorm((float) v, algoIdx, slotIdx));
        k.setLabel(composeSlotLabel(cfg, v));
    };
}

} // namespace mu_ui
