cmake_minimum_required(VERSION 3.22)

# ── macOS universal binary (arm64 + x86_64) ─────────────────────────────────
# Without this the build only targets the configuring machine's native arch —
# and the CI Mac runner (macos-latest) is Apple Silicon, so the artefacts came out
# arm64-only and Intel Macs couldn't load the plugins at all (no Gatekeeper trick
# helps a missing slice). Build both arches so the Mac binaries run everywhere.
# Set BEFORE project() so the architectures are honoured during toolchain setup.
# Guarded by NOT DEFINED so a local dev build can override for speed, e.g.
#     cmake -B build -DCMAKE_OSX_ARCHITECTURES=arm64
# No-op off Apple. (Signing/notarization to clear Gatekeeper is a separate step.)
if(APPLE AND NOT DEFINED CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS universal build (arm64 + x86_64)")
endif()

# Family major.minor lives in version.txt (single source — the build-number script,
# installers, release packaging, CI tag and build-report hook all read it too).
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/version.txt" MUFAMILY_VERSION_BASE)
string(STRIP "${MUFAMILY_VERSION_BASE}" MUFAMILY_VERSION_BASE)

project(mu-family VERSION ${MUFAMILY_VERSION_BASE}.0)

# ── mu-family monorepo umbrella ─────────────────────────────────────────────
# This repo hosts the shared `mu-core` library plus the plugins built on top of it
# (one add_subdirectory each, below).
#
# Layout:
#   mu-core/             shared audio/FX/modulation/UI source set + ProcessorBase
#   mu-clid/             Euclidean rhythm sequencer + sample trigger plugin
#   mu-clid-midi/        mu-Clid MIDI: the single-rhythm Euclidean MIDI effect (the former mu-Clid Lite)
#   mu-tant/             wavetable drone synth
#   mu-toni/             generative arpeggiator mono-synth
#   mu-on/               909-style groove sequencer
#   mu-tronic/           performance unit of 16 layer slots (Standalone only, pay what you want)
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

# JUCE version guard: the checkout at JUCE_PATH must match the single pin in juce-version.txt
# (the CI workflows check out that same pin), so a local build can never drift from CI.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/juce-version.txt" MUFAMILY_JUCE_PIN)
string(STRIP "${MUFAMILY_JUCE_PIN}" MUFAMILY_JUCE_PIN)
get_directory_property(MUFAMILY_JUCE_FOUND DIRECTORY "$ENV{JUCE_PATH}" DEFINITION JUCE_VERSION)
if(NOT MUFAMILY_JUCE_FOUND VERSION_EQUAL MUFAMILY_JUCE_PIN)
    message(FATAL_ERROR "JUCE at $ENV{JUCE_PATH} is ${MUFAMILY_JUCE_FOUND} but juce-version.txt pins ${MUFAMILY_JUCE_PIN}. "
                        "Check out the pinned JUCE tag, or bump the pin (see CLAUDE.md, 'JUCE is kept up to date').")
endif()
message(STATUS "JUCE ${MUFAMILY_JUCE_FOUND} (matches juce-version.txt)")

# Tester-deploy kill-switch. OFF by default so NO build (Debug or Release, and
# regardless of any runner that forces a Release pass) can push artefacts to the
# OneDrive tester share. Flip ON only for an intentional tester release:
#     cmake -B build -DMUFAMILY_DEPLOY_TESTERS=ON
# The per-plugin OneDrive POST_BUILD deploy steps are guarded by this option.
option(MUFAMILY_DEPLOY_TESTERS "Deploy Release artefacts to the OneDrive tester share" OFF)

# Focused builds: while working on one or two apps, configure with -DMUFAMILY_FOCUSED_BUILD=ON and build
# just those targets; the build number is read but not incremented. The full Debug build of every app at
# a stage boundary uses the default OFF, which increments the number by exactly 1.
option(MUFAMILY_FOCUSED_BUILD "Build without incrementing the build number (focused work on 1-2 apps)" OFF)

# Controller support (mu-control). The library and its tests are always built; this only decides whether the
# standalone apps link it (via mu_control_attach). OFF until the Launchpad X is accepted on the hardware, so the
# release build and CI stay without it. For hardware testing configure the dev build with it ON:
#   cmake -B build -DMUFAMILY_WITH_CONTROL=ON      (add -DMUFAMILY_FOCUSED_BUILD=ON to keep the build number)
option(MUFAMILY_WITH_CONTROL "Link mu-control (hardware controller support) into the standalone apps" OFF)

# ASIO audio devices in the standalone apps (Windows). Off by default: using ASIO means accepting
# Steinberg's ASIO licence (the headers JUCE bundles are covered by it); turn it on once that is done.
#   cmake -B build -DMUFAMILY_WITH_ASIO=ON
option(MUFAMILY_WITH_ASIO "Offer ASIO audio devices in the standalone apps (Windows; needs the Steinberg ASIO licence)" OFF)
function(mu_asio_attach target)
    if(MUFAMILY_WITH_ASIO AND WIN32)
        target_compile_definitions(${target} PRIVATE JUCE_ASIO=1)
    endif()
endfunction()

# ── Family-wide version metadata ────────────────────────────────────────────
# Read the current build number at configure time so PLUGIN_VERSION in every
# juce_add_plugin call reflects the real release number rather than the cmake
# project fallback (<major>.<minor>.0.0). Note: the build step increments build_number.txt
# *after* configure, so the version embedded in a first-session build is N (the
# previous session's number); subsequent builds within the same session are exact.
# This is accurate enough for tester identification and far better than x.y.0.0.
file(READ "${CMAKE_SOURCE_DIR}/build_number.txt" MUFAMILY_BUILD_NUMBER)
string(STRIP "${MUFAMILY_BUILD_NUMBER}" MUFAMILY_BUILD_NUMBER)
string(TIMESTAMP MUFAMILY_YEAR "%Y")
set(MUFAMILY_PLUGIN_VERSION "${MUFAMILY_VERSION_BASE}.0.${MUFAMILY_BUILD_NUMBER}")
set(MUFAMILY_COPYRIGHT "Copyright (c) ${MUFAMILY_YEAR} Transwarp Development Project")
# Windows FILEVERSION uses comma-separated quads (1,1,0,972). The version
# string is already 4-part so a straight replace gives the right quad.
string(REPLACE "." "," MUFAMILY_FILEVERSION "${MUFAMILY_PLUGIN_VERSION}")

# Pre-release products (mu-On, mu-Toni) version as <version-pre.txt>.0.<shared build number>.
file(READ "${CMAKE_SOURCE_DIR}/version-pre.txt" MUFAMILY_PRE_VERSION_BASE)
string(STRIP "${MUFAMILY_PRE_VERSION_BASE}" MUFAMILY_PRE_VERSION_BASE)
set(MUFAMILY_PRE_PLUGIN_VERSION "${MUFAMILY_PRE_VERSION_BASE}.0.${MUFAMILY_BUILD_NUMBER}")

message(STATUS "mu-family version: ${MUFAMILY_PLUGIN_VERSION} (pre-release products: ${MUFAMILY_PRE_PLUGIN_VERSION})")

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

# JUCE builds each VST3 manifest helper as its own sub-project at build time, so add_compile_options
# above never reaches it and the helper compiles the µ in JucePlugin_Name under code page 1252 (C4566).
# MSBuild imports a Directory.Build.props found above a project, so one placed over the helper build
# directories gives only those projects /utf-8.
if(MSVC AND CMAKE_GENERATOR MATCHES "Visual Studio")
    file(WRITE "${CMAKE_BINARY_DIR}/vst3_helpers/Directory.Build.props"
"<Project>
  <ItemDefinitionGroup>
    <ClCompile>
      <AdditionalOptions>/utf-8 %(AdditionalOptions)</AdditionalOptions>
    </ClCompile>
  </ItemDefinitionGroup>
</Project>
")
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
add_subdirectory(mu-control)   # controller drivers, mapping and surface model (INTERFACE library + tests); depends on mu-core only
add_subdirectory(mu-clid)
add_subdirectory(mu-clid-midi) # mu-Clid MIDI: Euclidean MIDI effect (VST3 + CLAP); needs mu-clid's increment_build_number target
add_subdirectory(mu-tant)     # wavetable drone synth (Standalone + VST3 + CLAP)
add_subdirectory(mu-toni)     # generative arpeggiator mono-synth, freeware (Standalone + VST3 + CLAP)
add_subdirectory(mu-on)       # 909-style groove sequencer: Kick/Bass/Hat/Snare (Standalone + VST3 + CLAP)
add_subdirectory(mu-tronic)   # performance unit: 16 layer slots (Standalone only, pay what you want); after the products whose layers it will host
add_subdirectory(tests/layer-link)    # mu-layer-link-test: the four layer libraries in one link (after the products define them)
add_subdirectory(mu-link)     # local audio receiver + master clock (foundation: IPC tests only)
add_subdirectory(ControlDesign)       # knob-style design sandbox (dev tool, not shipped)
add_subdirectory(tools/license-tool)  # owner-side license keygen + signer (dev tool, not shipped)
