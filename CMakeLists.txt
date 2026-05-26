cmake_minimum_required(VERSION 3.22)
project(mu-family VERSION 1.0.0)

# ── mu-family monorepo umbrella ─────────────────────────────────────────────
# This repo hosts the shared `mu-core` library plus one or more plugins built
# on top of it. Today: mu-clid + mu-clid-lite. Future siblings (mu-tant,
# mu-toni) drop in as additional `add_subdirectory` lines below.
#
# Layout:
#   mu-core/             shared audio/FX/modulation/UI source set + ProcessorBase
#   mu-clid/             Euclidean rhythm sequencer plugin
#   tools/               shared dev tools (license keygen etc.)
#   tests/               cross-plugin listening-test pipeline (analyse.py, runners)
#   cmake/               shared cmake helpers (build-number increment, installer)
#   docs/                shared design docs
#   ThirdParty/          vendored sources (clap-juce, signalsmith, monocypher)

# Embed the VC++ runtime so plugins run without the Redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# JUCE and CLAP plugin extensions are shared across every plugin in the family.
add_subdirectory($ENV{JUCE_PATH} JUCE)
add_subdirectory(ThirdParty/clap-juce-extensions)

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

# ── Subdirectories ──────────────────────────────────────────────────────────
add_subdirectory(mu-core)
add_subdirectory(mu-clid)
