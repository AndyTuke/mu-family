#include "SettingsOverlay.h"
#include "Plugin/PluginProcessor.h"

namespace mu_tant
{

SettingsOverlay::SettingsOverlay(PluginProcessor& p)
    : StandardSettingsOverlay(p), product(p)
{
    // ── Hot-swap timing — when a staged preset / program-change swap commits ───
    makeFieldLabel(swapModeLabel, "Timing");
    swapModeDropdown.addItem("On master loop", 1);
    swapModeDropdown.addItem("On voice loop",  2);
    swapModeDropdown.setSelectedId((int) product.getSwapMode() + 1, false);
    swapModeDropdown.onChange = [this](int id)
    {
        product.setSwapMode(id == 2 ? PluginProcessor::SwapMode::OnVoiceLoop
                                    : PluginProcessor::SwapMode::OnMasterLoop);
    };
    addAndMakeVisible(swapModeDropdown);
    addSection(Where::MidiBeforeClock, { "Hot-swap", kRowH, [this](const Rows& r) {
        swapModeLabel   .setBounds(r.labelX, r.area.getY(), r.labelW, r.rowH);
        swapModeDropdown.setBounds(r.ctrlX,  r.area.getY(), r.ctrlW,  r.rowH); } });

    // ── Note mode (Free / Note) — gate + pitch-track the drone from notes ──────
    makeFieldLabel(noteModeLabel, "Mode");
    noteModeDropdown.addItem("Free", 1);
    noteModeDropdown.addItem("Note", 2);
    noteModeDropdown.setSelectedId(product.getMidiNoteMode() + 1, false);
    noteModeDropdown.onChange = [this](int id) { product.setMidiNoteMode(id - 1); };
    addAndMakeVisible(noteModeDropdown);
    addSection(Where::MidiAfterClock, { "Note Mode", kRowH, [this](const Rows& r) {
        noteModeLabel   .setBounds(r.labelX, r.area.getY(), r.labelW, r.rowH);
        noteModeDropdown.setBounds(r.ctrlX,  r.area.getY(), r.ctrlW,  r.rowH); } });

    // ── Program change (Ch 1-8 → voice presets, Ch 9 → full presets) ───────────
    midiPresetsBtn.onClick = [this] { if (onMidiPresetsClicked) onMidiPresetsClicked(); };
    fullPresetsBtn.onClick = [this] { if (onFullPresetsClicked) onFullPresetsClicked(); };
    addAndMakeVisible(midiPresetsBtn);
    addAndMakeVisible(fullPresetsBtn);
    addSection(Where::MidiAfterClock, { "MIDI Program Change", kRowH, [this](const Rows& r) {
        const int btnW = mu_ui::s(180), gap = mu_ui::s(8);
        midiPresetsBtn.setBounds(r.labelX,              r.area.getY(), btnW, r.rowH);
        fullPresetsBtn.setBounds(r.labelX + btnW + gap, r.area.getY(), btnW, r.rowH); } });
}

} // namespace mu_tant
