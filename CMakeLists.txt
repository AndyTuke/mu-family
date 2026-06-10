cmake_minimum_required(VERSION 3.22)

# ── macOS universal binary (arm64 + x86_64) ─────────────────────────────────
# Without this the build only targets the configuring machine's native arch —
# and the CI Mac runner (macos-latest) is Apple Silicon, so the artefacts came out
# arm64-only and Intel Macs couldn't load the plugins at all (no Gatekeeper trick
# helps a missing slice). Build both arches so the Mac binaries run everywhere.
# Set BEFORE project() so the architectures are honoured during toolchain setup.
# Guarded by NOT DEFINED so a local dev build can override for speed, e.g.
#     cmake -B build -DCMAKE_OSX_ARCHITECTURES=arm64
# No-op off Apple. (Signing/notarization to clear Gatekeeper is separate — #99.)
if(APPLE AND NOT DEFINED CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS universal build (arm64 + x86_64)")
endif()

project(mu-family VERSION 1.0.0)

# ── mu-family monorepo umbrella ─────────────────────────────────────────────
# This repo hosts the shared `mu-core` library plus one or more plugins built
# on top of it. Today: mu-clid + mu-clid-lite. Future siblings (mu-tant,
# mu-toni) are scaffolded under their own subdirectories with placeholder
# CMakeLists.txt but are NOT yet wired into the build — uncomment their
# add_subdirectory line below once their Source/ trees are real.
#
# Layout:
#   mu-core/             shared audio/FX/modulation/UI source set + ProcessorBase
#   mu-clid/             Euclidean rhythm sequencer plugin (full + Lite variants)
#   mu-tant/             scaffolding only — Source/ tree empty
#   mu-toni/             scaffolding only — Source/ tree empty
#   tools/               shared dev tools (license keygen etc.)
#   tests/               cross-plugin listening-test pipeline (analyse.py, runners)
#   cmake/               shared cmake helpers (build-number increment, installer)
#   docs/                family-shared design docs; product-specific docs under docs/<product>/
#   ThirdParty/          vendored sources (clap-juce, signalsmith, monocypher)

# Embed the VC++ runtime so plugins run without the Redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# JUCE and CLAP plugin extensions are shared across every plugin in the family.
add_subdirectory($ENV{JUCE_PATH} JUCE)
add_subdirectory(ThirdParty/clap-juce-extensions)

# Tester-deploy kill-switch. OFF by default so NO build (Debug or Release, and
# regardless of any runner that forces a Release pass) can push artefacts to the
# OneDrive tester share. Flip ON only for an intentional tester release:
#     cmake -B build -DMUFAMILY_DEPLOY_TESTERS=ON
# The per-plugin OneDrive POST_BUILD deploy steps are guarded by this option.
option(MUFAMILY_DEPLOY_TESTERS "Deploy Release artefacts to the OneDrive tester share" OFF)

# ── Family-wide version metadata ────────────────────────────────────────────
# Read the current build number at configure time so PLUGIN_VERSION in every
# juce_add_plugin call reflects the real release number rather than the cmake
# project fallback (1.0.0.0). Note: the build step increments build_number.txt
# *after* configure, so the version embedded in a first-session build is N (the
# previous session's number); subsequent builds within the same session are exact.
# This is accurate enough for tester identification and far better than 1.0.0.0.
file(READ "${CMAKE_SOURCE_DIR}/build_number.txt" MUFAMILY_BUILD_NUMBER)
string(STRIP "${MUFAMILY_BUILD_NUMBER}" MUFAMILY_BUILD_NUMBER)
string(TIMESTAMP MUFAMILY_YEAR "%Y")
set(MUFAMILY_PLUGIN_VERSION "1.0.${MUFAMILY_BUILD_NUMBER}")
set(MUFAMILY_COPYRIGHT "Copyright (c) ${MUFAMILY_YEAR} Transwarp Development Project")
# Windows FILEVERSION uses comma-separated quads (1,0,736,0).
string(REPLACE "." "," MUFAMILY_FILEVERSION "${MUFAMILY_PLUGIN_VERSION}")
set(MUFAMILY_FILEVERSION "${MUFAMILY_FILEVERSION},0")

message(STATUS "mu-family version: ${MUFAMILY_PLUGIN_VERSION}")

# Distribution paths the plugin subdirs reference for installer + deploy hooks.
# Cached so cmake/IncrementBuildNumber.cmake and per-plugin installers can find them.
set(MUFAMILY_WIN_DIST "D:/Andy Tuke/Music - Music Software/TDP/Windows"
    CACHE PATH "OneDrive tester drop folder for built plugins")
set(MUFAMILY_INSTALLER_DIST "${MUFAMILY_WIN_DIST}/Installer"
    CACHE PATH "OneDrive drop folder for Inno Setup installers")

# Locate Inno Setup once at the family level — every plugin's installer target reuses it.
find_program(ISCC_EXE ISCC
    PATHS "C:/Program Files (x86)/Inno Setup 6"
    NO_DEFAULT_PATH)
if(NOT ISCC_EXE)
    message(STATUS "Inno Setup not found — installer targets disabled across the family")
endif()

# MSVC defaults to the system codepage (1252) for source + command-line defines,
# which mangles the UTF-8 µ (U+03BC) in the product display name
# (JucePlugin_Name="µ-Clid" → "?-Clid" in the binary) and warns C4566 on µ string
# literals. /utf-8 makes MSVC treat both source and execution charset as UTF-8 so
# the µ branding survives into the VST3/CLAP/Standalone display name. Added after
# JUCE/clap-juce-extensions so it only affects mu-core + the plugins.
if(MSVC)
    add_compile_options(/utf-8)
endif()

# ── Family plugin formats ───────────────────────────────────────────────────
# AU (Audio Unit v2) is macOS-only — the JUCE wrapper needs Apple's AudioUnit /
# CoreAudio SDK, so it can only be built on a Mac. Expose it as one variable every
# product appends to its FORMATS list; empty off-Apple so an unquoted ${MUFAMILY_AU_FORMAT}
# expands to nothing and the Windows/Linux configure is unchanged. FAMILY RULE: every
# product's juce_add_plugin FORMATS must include ${MUFAMILY_AU_FORMAT} so new siblings get
# AU automatically (see CLAUDE.md "Plugin formats"). VST3 + Standalone are cross-platform;
# CLAP is added separately via clap_juce_extensions_plugin.
if(APPLE)
    set(MUFAMILY_AU_FORMAT AU)
else()
    set(MUFAMILY_AU_FORMAT "")
endif()

# ── Subdirectories ──────────────────────────────────────────────────────────
add_subdirectory(mu-core)
add_subdirectory(mu-clid)
add_subdirectory(mu-tant)     # wavetable drone synth (Standalone + VST3 + CLAP)
add_subdirectory(mu-toni)     # scaffold: shared shell + mixer wired, engine TBD (Standalone + VST3 + CLAP)
add_subdirectory(mu-on)       # 909-style groove sequencer: Kick/Bass/Hat/Snare (Standalone + VST3 + CLAP)
add_subdirectory(mu-link)     # local audio receiver + master clock (foundation: IPC tests only)
