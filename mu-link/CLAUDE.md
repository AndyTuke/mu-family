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

## Source layout

```
mu-link/Source/
├── Ipc/       MuLinkProtocol.h (shared-memory contract), AudioRing.h (lock-free SPSC)   ✅ implemented
├── Clock/     TransportClock.h (sample-accurate master clock)                            ✅ implemented
├── Server/    AudioServer.h (owns HW device, sums clients, publishes transport)          ⏳ skeleton
├── Plugin/    standalone app entry + main window                                          ⏳ TODO
├── UI/        EditorShellBase-style window: clients, levels, master tempo, device picker  ⏳ TODO
└── Tests/     TestMain + IpcTests (AudioRing SPSC + TransportClock)                        ✅ implemented
```

Client-side bus glue (attach / push audio / read transport) will be lifted into **`mu-core/Link/MuLinkClient`** when the first product is wired, so every product shares it. The IPC contract (`MuLinkProtocol.h`) is the single source of truth for both sides — it moves to mu-core at that point.

## Build

```
cmake --build build --config Debug --target mu-link-tests
build/mu-link/mu-link-tests_artefacts/Debug/mu-link-tests.exe
```

The GUI app target is not wired yet (foundation phase). mu-link is **local-only** — no plugin formats, no tester deploy.

## Critical rules

- **The clock is sacred.** It is derived from the hardware audio callback's frame count — never from wall-clock time, a `juce::Timer`, or MIDI input. Any feature that needs musical time reads `TransportClock`.
- **The audio thread never blocks and never allocates.** `AudioRing` is lock-free SPSC by contract: exactly one writer (a client) and one reader (the server) per ring. Never add a second writer/reader.
- **Underruns degrade gracefully** — a late client yields silence for its own stream plus a log line, and must never stall or glitch the summed master output.
- Mirror the family `Source/{...}` topology and conventions per the family-consistency rule.
