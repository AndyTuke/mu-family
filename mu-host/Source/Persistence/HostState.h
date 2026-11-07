#pragma once

// mu-Host's saved names. It is the mu-Tronic shell with one slot, but a product of its own: its state, its full-preset format and its
// settings / content folder are its own, so nothing it saves is ever read as a mu-Tronic file or the other way round.
namespace mu_host
{

inline constexpr const char* kStateType           = "MuHostState";
inline constexpr const char* kFullPresetTag       = "MuHostPreset";
inline constexpr const char* kFullPresetExtension = "muHost";
inline constexpr const char* kAppName             = "muHost";   // the settings file and content folder

} // namespace mu_host
