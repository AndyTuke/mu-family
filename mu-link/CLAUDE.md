# mu-link — CLAUDE.md

Product-specific guidance for working in `mu-link/`. Read alongside the family-wide [/CLAUDE.md](../CLAUDE.md) and the design doc [docs/mu-link/design-mulink.md](../docs/mu-link/design-mulink.md).

## What it is

mu-link is the family's **local audio receiver + master clock**. Multiple mu standalone apps (mu-clid, mu-tant, …) send audio to mu-link over a shared-memory bus; mu-link sums them and renders to one hardware device, and publishes a sample-accurate master transport so every product stays in sync.

It is **not** a system-wide Windows audio driver. Because we own every client, it's a JACK-style local audio server (shared memory + lock-free rings), which avoids a signed kernel-mode driver entirely.

## Locked architecture (see design doc §5)

- **Transport:** shared-memory local bus, mu-products only.
- **Hardware backend:** runtime-selectable WASAPI/ASIO via JUCE `AudioDeviceManager`.
- **Coupling:** async double-buffered rings — underrun fills silence + logs, never clicks the master.
- **Clock:** internal sample-accurate transport (the hardware word clock is the master) + MIDI-clock OUT to outboard gear. **No Ableton Link** (GPL/proprietary — avoided).
- **Clock source (L7):** mu-link is master **or** slaves to **external MIDI clock** (manual GUI toggle). When slaving, a tempo **PLL** (`MidiClockEstimator`) smooths the incoming F8 stream and sets only the transport *tempo* — the frame counter stays the timebase, so the bus never inherits MIDI jitter. The estimator is pure/headless-tested; `MidiClockInput` is the JUCE adapter; never let raw MIDI clock become the timebase.
- **Tempo:** **mu-link is always tempo master** — clients follow the shared `TransportBlock`; a connected client surrenders its own tempo. No client-as-master feedback path.
- **Summing:** **plain sum first** (Stage L2); per-client level/pan/mute/solo via mu-core `MixerEngine` later (Stage L6).
- **First client wired:** **mu-clid** (Stage L5).

## Build stages (see design doc §6)

L1 Win32 shared-memory mapping → L2 `AudioServer` + HW device (plain sum, MIDI-clock-out) → L3 GUI app → L4 lift client glue to `mu-core/Link/MuLinkClient` → L5 wire mu-clid → L6 MixerEngine strips + heartbeat reaping. Each stage ends green before the next.

## Source layout

The IPC **contract + client half live in mu-core** (`mu-core/Link/`) so every product's
standalone shares them; mu-link owns only the **server half** + the app.

```
mu-core/Link/   (shared — used by mu-link AND every product's standalone)
├── MuLinkProtocol.h     contract + seqlock transport read/write                            ✅ implemented
├── AudioRing.h          AudioRingHeader/View/owner — same SPSC logic in-proc or over shm   ✅ implemented
├── MuLinkSharedMemory.h SharedMemoryRegion (Win32 RAII) + MuLinkClientMemory (client)      ✅ implemented
└── MuLinkClient.h       product-facing: auto-detect attach, render-ahead thread, fallback  ✅ implemented

mu-link/Source/
├── Ipc/       MuLinkServerMemory.h — the SERVER composer (creates/owns all regions)        ✅ implemented
├── Clock/     TransportClock.h (sample-accurate master), MidiClockOut.h (F8 out),          ✅ implemented
│              MidiClockEstimator.h (tempo PLL, pure), MidiClockInput.h (MIDI-in adapter)
├── Server/    ServerEngine.h (pure RT core: publish→sum→advance→pulses + peak meters),     ✅ implemented
│              AudioServer.h (owns HW device, delegates each callback to the engine)
├── Plugin/    Main.cpp — JUCEApplication + DocumentWindow owning the AudioServer            ✅ implemented
├── UI/        MuLinkComponent — device picker + transport + client/master meter strip       ✅ implemented
├── Tools/     ToneClientMain.cpp — mu-link-tone reference client (built on MuLinkClient)     ✅ implemented
└── Tests/     TestMain + Ipc + SharedMemory (+child proc) + Server + Client                 ✅ 23/23 green
```

Targets: `mu-link-tests` (juce_core only — the sacred clock/ring/summing/client logic,
tested headless), `mu-link-server` (headless server), `mu-link-tone` (reference tone
client on `MuLinkClient`), `mu-link-harness` (headless audio verification — see below),
and `mu-link` (the GUI app). Every target puts `mu-core` on its include path so
`#include "Link/..."` resolves; none link the mu-core INTERFACE library.

## Verifying audio (no hardware needed)

`mu-link-harness` is a **headless "virtual mu-link"**: it creates the bus + runs the real
`ServerEngine`, but instead of a soundcard it captures the summed master to WAV
(real-time-paced, so clients' render-ahead behaves exactly as in production) and
**self-asserts** with per-frequency Goertzel + RMS analysis (exit 0 = PASS). A real
`mu-link.exe` must NOT be running (it owns the same shared-memory names).

```
pwsh mu-link/scripts/verify-bus.ps1 -Build      # build + run all scenarios, PASS/FAIL
```

Producers: `--internal-tone <hz>` (in-process `MuLinkClient` sine — hermetic, CI-friendly),
`--spawn "<cmd>"` (a real client process — e.g. `mu-link-tone`, or any mu standalone, which
auto-attaches), or none (just capture for N seconds — launch an app by hand against it).
To capture **mu-Clid through the bus**: run `mu-link-harness --seconds 10 --out muclid.wav`,
then launch mu-Clid standalone and load a sound — its audio is summed into the capture.

### Why mu-link does NOT use EditorShellBase (documented deviation)

The family design rule is "share ONE UX via mu-core." mu-link honours the *visual* half —
it reuses `MuLookAndFeel` (palette/sizes) and `VUMeter` from mu-core for family parity.
But it deliberately does **not** derive its window from `EditorShellBase`: that shell is a
`juce::AudioProcessorEditor` bound to a `ProcessorBase` (APVTS, presets, plugin formats),
and mu-link is a **server app, not a plugin** — there is no `AudioProcessor`. Forcing it
through the plugin shell would mean faking a processor. So mu-link composes a bespoke
`DocumentWindow` + `MuLinkComponent`, and the GUI target compiles just the three shared UI
sources it needs (`MuLookAndFeel`/`MuTheme`/`VUMeter`) rather than linking the whole
mu-core INTERFACE library (which would drag in the entire plugin stack).

Client-side bus glue (attach / push audio / read transport / fallback) now lives in **`mu-core/Link/MuLinkClient`** (L4), shared by every product. The IPC contract (`mu-core/Link/MuLinkProtocol.h`) is the single source of truth for both sides. L5 wires the first product (mu-clid **standalone only**) onto `MuLinkClient`.

## Build

```
cmake --build build --config Debug --target mu-link mu-link-tests mu-link-server mu-link-tone mu-link-harness
build/mu-link/mu-link-tests_artefacts/Debug/mu-link-tests.exe     # 23/23 green
build/mu-link/mu-link_artefacts/Debug/mu-link.exe                 # the GUI app
build/mu-link/mu-link-server_artefacts/Debug/mu-link-server.exe   # headless summing server
build/mu-link/mu-link-tone_artefacts/Debug/mu-link-tone.exe 440   # reference tone client
pwsh mu-link/scripts/verify-bus.ps1                              # headless audio PASS/FAIL
```

mu-link is **local-only** — no plugin formats, no tester deploy.

## Critical rules

- **Standalone-only integration — plugins are never touched.** mu-link hooks (route audio to the bus, slave the transport) are wired **only into each product's Standalone build**, gated on `getWrapperType() == juce::AudioProcessor::wrapperType_Standalone`. The **VST3 and CLAP formats keep using the host's transport and host audio I/O, completely unchanged** — the host owns the clock and device; a plugin must never attach to mu-link. A standalone running with no mu-link present behaves exactly as it does today (own device, own transport); it must also revert cleanly to its own device if mu-link quits mid-session.
- **Auto-detection, zero config.** mu-link owns the named shared-memory regions; standalones detect it by `OpenFileMapping` on the known names and self-register in the `ClientRegistry`. Any launch order works; no user setup. When attached, a standalone takes its clock/tempo/transport **and** its audio output from mu-link.
- **The clock is sacred.** It is derived from the hardware audio callback's frame count — never from wall-clock time, a `juce::Timer`, or MIDI input. Any feature that needs musical time reads `TransportClock`.
- **The audio thread never blocks and never allocates.** `AudioRing` is lock-free SPSC by contract: exactly one writer (a client) and one reader (the server) per ring. Never add a second writer/reader.
- **Underruns degrade gracefully** — a late client yields silence for its own stream plus a log line, and must never stall or glitch the summed master output.
- Mirror the family `Source/{...}` topology and conventions per the family-consistency rule.
