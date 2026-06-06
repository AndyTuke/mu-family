# mu-link — design (v0.3)

**Status:** architecture decided (see [Decisions](#5-decisions-locked)) **and the build is staged** (see [§6 Build stages](#6-build-stages-l1l6)). Foundation (IPC ring + transport clock + tests) scaffolded and unit-tested. Win32 shared-memory mapping, audio server, GUI, and client-side mu-core glue are the staged increments.

## 1. Purpose

mu-link is a sibling product in the mu family with two jobs:

1. **Audio receiver / summing bus** — multiple mu standalone apps (mu-clid, mu-tant, future siblings) send their audio to mu-link, which sums them and renders to **one** hardware output device.
2. **Master clock** — mu-link is the family's tempo + transport source, so every connected product plays in tight, drift-free sync.

Two hard requirements drive every decision below:

- **The clock must be rock solid.** Sample-accurate, no drift, no jitter.
- **The audio must be low-latency with no clicks or pops.**

## 2. The key realisation — don't build a system audio driver

The obvious-sounding approach is "make a virtual sound card that any Windows app can select." That means a **kernel-mode audio endpoint driver** (WDM / PortCls / AVStream, à la VB-Audio Cable). On Windows 10/11 that path requires the WDK, kernel debugging, an EV code-signing certificate, and **Microsoft attestation/WHQL submission** for the driver to load. It's a large, high-risk, slow effort — and it's overkill, because **we own every client.** No third-party app needs to route into mu-link.

When you own the clients, the proven model is a **local audio server with a shared-memory bus** — this is exactly what [JACK](https://jackaudio.org/api/) does. Clients are normal user-mode processes; audio is shared through shared memory with single-reader/single-writer ring buffers that need no locks ([JACK ringbuffer](https://jackaudio.org/api/ringbuffer_8h.html)). That gives us low latency, zero/minimal-copy summing, and — critically — a single clock domain.

## 3. Proposed architecture

```
┌──────────── mu-link process (the server) ─────────────┐
│  Hardware audio callback  (owns ONE output device)    │
│   every block:                                        │
│     1. update TransportBlock {samplePos, tempo, play} │  ← shared memory, written once/block
│     2. for each client: read its audio ring           │
│     3. sum (optional per-client level/pan/mute)        │
│     4. write to hardware buffer → soundcard            │
└───────────────────────────────────────────────────────┘
        ▲ audio rings (1 per client)     ▲ TransportBlock (read by all)
        │ shared memory                  │ shared memory
┌───────┴────────┐   ┌────────┴────────┐
│ mu-clid (app)  │   │ mu-tant (app)   │   …
│ renders ahead  │   │ renders ahead   │
│ into its ring, │   │ into its ring,  │
│ slaved to the  │   │ slaved to the   │
│ TransportBlock │   │ TransportBlock  │
└────────────────┘   └─────────────────┘
```

### 3.1 The clock (the critical part)

- mu-link owns the hardware device. **The audio device's word clock is the master clock.** Everything is referenced to it, so there is no second timebase to drift against.
- Each block, mu-link writes a **`TransportBlock`** to shared memory: absolute sample position, current tempo (BPM), bar/beat phase, and play/stop state.
- Clients **read** that block and render exactly the sample range mu-link is about to consume. Because the position comes from the master and the clients are pulled in lockstep with it, sync is **sample-accurate with ±0 drift**.
- We deliberately **do not** use MIDI clock internally. MIDI clock (24 ppqn) is jitter-prone — it shares a serial wire with other data and its timing is tied to buffer boundaries, giving milliseconds of jitter ([E-RM jitter report](https://www.e-rm.de/data/E-RM_report_Jitter_02_14_EN.pdf)). A sample-accurate audio-derived clock is the known fix (±1 sample).
- **Outward bridges (optional, later):** mu-link *can* emit real MIDI-clock OUT to sync outboard hardware, and/or be an [Ableton Link](https://ableton.github.io/link/) peer so third-party apps can sync to us. These are *outputs* derived from the master clock, never the internal mechanism. (Ableton Link is GPLv2+/proprietary — a closed product needs the paid licence, so it's opt-in only.)

### 3.2 The audio path (low latency, no clicks)

Two ways to couple clients to the server callback:

- **Synchronous / lockstep (JACK-style):** mu-link signals all clients, waits for them to render this block, sums, outputs. Lowest possible latency, but fragile across process boundaries — one late client underruns the whole master. Windows thread scheduling makes this risky.
- **Asynchronous double-buffered (recommended):** each client renders *ahead* on its own high-priority thread into its ring buffer, woken by an event from mu-link. mu-link reads whatever is ready. Costs ~1 extra buffer of latency but is robust: a momentarily slow client can't glitch the master. An underrun fills with silence and logs — **never a click on the output**, only (at worst) a brief dropout from the offending client.

Backend candidates for the hardware device:

| Backend | Latency | Coexists w/ Windows sounds | Extra SDK |
|---|---|---|---|
| **WASAPI exclusive** | very low (near-ASIO) | no (owns the device) | none |
| **ASIO** | lowest | no | Steinberg ASIO SDK (licence agreement, no redistribution) |
| **WASAPI shared** | higher (engine mix/resample) | yes | none |

JUCE's `juce_audio_devices` already wraps all three, so the backend is mostly a configuration choice rather than new low-level code.

### 3.3 Client integration (mu-core)

> **CRITICAL — standalone-only.** mu-link integration is wired **only into the Standalone build** of each product. The **VST3 and CLAP plugin formats are never touched** — a plugin always uses the host's transport and the host's audio I/O, exactly as today, because *the host owns the clock and the audio device*; a plugin attaching to mu-link would fight its host. Every mu-link hook is gated on `getWrapperType() == juce::AudioProcessor::wrapperType_Standalone` (equivalently the standalone app wrapper), so the plugin code path is provably unchanged.

- A new `mu-core` module, **`MuLinkClient`**, encapsulates: discover/attach to a running mu-link, allocate this client's audio ring, and expose the `TransportBlock`.
- **Auto-detection (no config).** mu-link is the hub: it creates and owns the named shared-memory regions (`kTransportMapName` / `kRegistryMapName`). Each standalone, at startup and periodically, tries to `OpenFileMapping` those names — success means "mu-link is running" and the standalone registers a `ClientSlot` and attaches its ring; failure means "run alone." So launching mu-link, mu-clid, mu-tant in any order, they discover each other automatically through the registry with zero user setup. mu-link's UI lists every attached client.
- When mu-link is present, a **standalone** routes its render into the bus instead of opening its own `AudioDeviceManager` output, and **slaves its transport** (clock + tempo + play/stop) to the shared `TransportBlock`. mu-core already has [`HostTransport`](../../mu-core/Plugin/HostTransport.h) — the natural seam to feed from mu-link instead of the internal standalone transport.
- When mu-link is **absent**, the standalone behaves exactly as today (own audio device, own internal transport). mu-link is additive, never a hard dependency — and a standalone that was attached must cleanly revert to its own device the moment mu-link quits.

### 3.4 Summing / mixing

mu-core already has a `MixerEngine` (used by mu-tant/mu-clid). mu-link could reuse it for a per-client strip (level/pan/mute/solo + master), or start as a plain sum and grow into a mixer. Open question below.

## 4. Proposed component structure (server path)

> Mirrors the family `Source/{...}` topology where it applies. Finalised once the architecture forks are decided.

```
mu-link/
├── CLAUDE.md
├── CMakeLists.txt
└── Source/
    ├── Plugin/        (App entry + main window; standalone only — no plugin formats)
    ├── Server/        AudioServer (HW callback, summing), ClientRegistry, TransportClock
    ├── Ipc/           SharedMemoryBus, AudioRing (SPSC), TransportBlock, handshake/discovery
    ├── Clock/         MidiClockOut bridge, (optional) AbletonLink peer
    ├── UI/            EditorShellBase-based window: connected clients, levels, master tempo, device picker
    └── Tests/         ring-buffer SPSC tests, transport-advance tests, underrun handling
```
Client-side bus glue lives in **`mu-core/Link/MuLinkClient.*`** (shared by all products), not here.

## 5. Decisions (locked)

1. **Audio transport model** → **Shared-memory local bus** (mu-products only). No kernel driver.
2. **Hardware backend** → **Support several, pick at runtime** — WASAPI (exclusive/shared) + ASIO via JUCE's `AudioDeviceManager` device picker. (ASIO only if the user installs the ASIO SDK; otherwise WASAPI exclusive is the low-latency default.)
3. **Client↔server coupling** → **Async double-buffered rings** (click-resistant). Underrun → silence + log, never a pop on the master.
4. **Clock outputs** → internal sample-accurate transport bus **+ MIDI-clock OUT** to outboard gear. **No Ableton Link** (avoids the GPL/proprietary licence).

### Secondary decisions (resolved)
- **Tempo ownership** → **mu-link is always tempo master.** All clients follow the shared `TransportBlock`; a connected client surrenders its own tempo control. (Simplest path to the "rock-solid clock" goal; no feedback path into the master clock.)
- **Mixing depth** → **plain sum first** (L2), grow into per-client level/pan/mute/solo + master via mu-core `MixerEngine` in **L6**.
- **First client wired** → **mu-clid** (L5) — the most mature product; rhythmic transients make sync tightness audible.
- **Channel layout** → stereo per client summed to a stereo master (`kMaxChannels = 2`). Multi-channel / per-client returns deferred.
- **Max clients** → 8 (`kMaxClients = 8`), fixes ring / shared-memory sizing.
- **Sample-rate / buffer authority** → **mu-link dictates SR + block size**; clients conform (a client whose device can't is refused and falls back to its own output).

### Still open (design detail, not blocking)
- **Discovery/handshake:** auto-attach when mu-link is running (named shared memory + registration); how to present "mu-link connected" in each product's UI. Settled during L4/L5.

## 6. Build stages (L1–L6)

Numbered like mu-clid's v1 roadmap. Each stage ends green (tests + build) before the next starts. Stages share the family build-number stream.

| Stage | Scope | Done when |
|---|---|---|
| **L1** ✅ | **Win32 shared-memory mapping.** `SharedMemoryBus` wrapper over `CreateFileMapping`/`MapViewOfFile` for the `TransportBlock`, the `ClientRegistry`, and one ring region per client. `AudioRing` storage + indices relocate into the mapping unchanged (the header already notes this is pure relocation). | **Done (v1.0.779, #893).** Server publishes a `TransportBlock`; a spawned child process attaches by name, version-checks, reads it back, and produces a ring the parent consumes in order. 15/15 tests green. |
| **L2** ✅ | **`AudioServer` + hardware device.** JUCE `AudioDeviceManager` (runtime WASAPI/ASIO), one callback → `clock.advance`, publish transport, **plain-sum** active client rings (zero-fill underrun + log), write to device. MIDI-clock-out bridge from `pulsesElapsed` delta. | **Done (v1.0.779, #895).** Pure `ServerEngine` (publish→sum→advance→pulse-count) unit-tested headless: two clients sum sample-accurately, underrun tail is silent (never clicks), 24-ppqn pulses track the clock. Device-owning `AudioServer` + runnable `mu-link-server` app build clean. 20/20 tests green. |
| **L3** ✅ | **GUI app target.** `juce_add_gui_app`; window with device picker, connected-client list + level meters, master tempo + transport. | **Done (v1.0.779, #897).** `mu-link.exe` runs: `AudioDeviceSelectorComponent` picker (+ MIDI-clock-out port), play/stop + tempo (always master), and a mixer-style strip of 8 client meters + master, live-polled from the registry. **Reuses the shared design system (`MuLookAndFeel` + `VUMeter`) but NOT the plugin `EditorShellBase`** — mu-link is a server app, not an `AudioProcessor`, so it composes a bespoke `DocumentWindow` instead (documented deviation). 20/20 tests green. |
| **L4** ✅ | **`mu-core/Link/MuLinkClient`.** Lift the client glue (auto-detect via `OpenFileMapping`, register a `ClientSlot`, allocate this client's ring, expose the `TransportBlock`) into mu-core; `MuLinkProtocol.h` moves to mu-core as the shared contract. Absent/quit → fallback. | **Done (v1.0.779, #898).** `mu-core/Link/` now holds `MuLinkProtocol.h` + `AudioRing.h` + `MuLinkSharedMemory.h` (`SharedMemoryRegion` + `MuLinkClientMemory`) + `MuLinkClient.h` (auto-detect attach, high-priority render-ahead thread, `onRender` callback, transport read, heartbeat, clean detach). mu-link keeps only the server half (`MuLinkServerMemory`). `mu-link-tone` rebuilt on `MuLinkClient`. Headless `MuLinkClient` test: attach → render-ahead → server sums → transport read-back → detach → re-claim. Core-boundary check passes; 23/23 tests green. |
| **L5** ✅ | **Wire mu-clid — STANDALONE ONLY.** Gate every hook on `wrapperType_Standalone`; the VST3/CLAP path is untouched. The standalone routes its render into the bus instead of its own output device and slaves transport to the shared block; "mu-link connected" indicator; clean fallback when mu-link isn't running or quits mid-session. | **Done (v1.0.780, #899).** New standalone-only `MuLinkBridge` (compiled only into `mu-clid_Standalone`) polls for mu-link; on attach it detaches the processor from the local `AudioProcessorPlayer`, hands it a `MuLinkPlayHead` (so `deriveTransport`'s existing `getPlayHead()` path slaves to mu-link with **zero processBlock changes**), re-prepares at mu-link's SR, and lets `MuLinkClient`'s render-ahead thread drive it. Quit-detect via frozen transport generation → reattach to local device. Title shows "mu-link connected". VST3/CLAP built byte-identical; mu-clid 140/140 + mu-link 23/23 green. |
| **L6** ✅ | **Mixing + robustness.** Per-client level/pan/mute/solo + master; heartbeat-based client reaping (reap a client that died without detaching); MIDI-clock-out to outboard gear validated. | **Done (v1.0.780, #901).** Per-client **gain / pan (linear balance) / mute / solo + master gain** implemented **directly in `ServerEngine`** (lightweight, not the plugin-coupled `MixerEngine` — documented deviation) and exposed as GUI knob + M/S toggles per strip. **Heartbeat reaping:** the engine reaps a slot whose heartbeat freezes for >2 s (client crashed without detaching) so its zombie ring/meter clears and the slot frees. 27/27 tests (gain scales, mute silences, solo isolates, zombie reaped, live client untouched). MIDI-clock-out to outboard gear is wired (L3 picker → `MidiClockOut`) but final validation needs hardware (manual). Wiring **mu-tant standalone** onto the bus is a separate follow-up (#902) — its standalone transport must be taught to consult the playhead, touching its hot-swap boundary logic, so it gets its own careful pass. |

## 7. Licensing notes

- **ASIO SDK** — free but under a Steinberg licence agreement; the SDK cannot be redistributed. Only relevant if we pick the ASIO backend.
- **Ableton Link** — dual GPLv2+ / proprietary. A closed-source product needs the paid licence. Avoided unless we choose to be a Link peer; the internal clock is custom and licence-free.

## Sources
- JACK architecture / lock-free ring buffer — <https://jackaudio.org/api/> , <https://jackaudio.org/api/ringbuffer_8h.html>
- WASAPI vs ASIO routing — <https://github.com/dechamps/FlexASIO> , <https://markheath.net/post/what-up-with-wasapi>
- Ableton Link — <https://ableton.github.io/link/> , <https://github.com/Ableton/link>
- MIDI-clock jitter vs sample-accurate sync — <https://www.e-rm.de/data/E-RM_report_Jitter_02_14_EN.pdf>
