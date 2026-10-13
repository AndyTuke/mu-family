#include "SettingsOverlay.h"
#include "Plugin/PluginProcessor.h"

namespace mu_tant
{

SettingsOverlay::SettingsOverlay(PluginProcessor& p)
    : StandardSettingsOverlay(p), product(p)
{
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
    addProgramChangeSection("Voice Presets", "Full Presets");
}

} // namespace mu_tant
