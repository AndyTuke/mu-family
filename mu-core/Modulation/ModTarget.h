#pragma once

#include <cstddef>
#include <string_view>

// The family-standard modulation target (docs/design-plugin-family.md "Modulation targets").
//
// Rule: modulation depth is a percentage of the target knob's range. Every product seeds
// each target into the ModulationMatrix as its knob's 0..1 proportion, the matrix adds
// source% x depth% (so depth 100% from a full-scale source moves the knob its whole range),
// and the product converts back through the knob's range (mu_mod::resolveLane does this for
// targets read straight from parameters). There are no per-target scale factors.
//
// Each product keeps ONE table of these — the single place its targets are defined:
//   id      — the stable name saved in presets (never change it once shipped)
//   label   — the destination-dropdown text
//   section — the dropdown heading it sits under (nullptr = no heading)
//   param   — the parameter it drives: its id without the product's per-channel prefix
//             (nullptr for a reserved / retired slot kept so table indices don't shift).
//             Mixer-strip targets (pan, FX sends) name the mixer channel's parameter
//             (ch{N}_pan, ch{N}_sendEff, ...) by its suffix; the product applies them through
//             the MixerEngine channel's per-block modulation overrides.
// New targets are appended as one row.
namespace mu_mod
{

struct ModTarget
{
    const char* id;
    const char* label;
    const char* section;
    const char* param;
};

// The row whose id matches, or nullptr.
template <std::size_t N>
constexpr const ModTarget* findTarget(const ModTarget (&table)[N], std::string_view id) noexcept
{
    for (const auto& t : table)
        if (t.param != nullptr && id == t.id) return &t;
    return nullptr;
}

// True if `id` names a live (non-reserved) target in the table.
template <std::size_t N>
constexpr bool isValidTarget(const ModTarget (&table)[N], std::string_view id) noexcept
{
    return findTarget(table, id) != nullptr;
}

} // namespace mu_mod
