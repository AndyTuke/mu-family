cmake_minimum_required(VERSION 3.22)
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

# ── Subdirectories ──────────────────────────────────────────────────────────
add_subdirectory(mu-core)
add_subdirectory(mu-clid)
add_subdirectory(mu-tant)     # wavetable drone synth (Standalone + VST3 + CLAP)
add_subdirectory(mu-toni)     # scaffold: shared shell + mixer wired, engine TBD (Standalone + VST3 + CLAP)
