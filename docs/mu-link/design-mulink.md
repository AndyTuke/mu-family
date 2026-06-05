# mu-link — design (v0.2)

**Status:** architecture decided (see [Decisions](#5-decisions-locked)). Foundation (IPC ring + transport clock + tests) scaffolded. Audio server, shared-memory mapping, GUI, and client-side mu-core glue are the next increments.

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

- A new `mu-core` module, **`MuLinkClient`**, encapsulates: discover/attach to a running mu-link, allocate this client's audio ring, and expose the `TransportBlock`.
- When mu-link is present, a mu product routes its render into the bus instead of opening its own `AudioDeviceManager` output, and **slaves its transport** to the shared block. mu-core already has [`HostTransport`](../../mu-core/Plugin/HostTransport.h) — the natural seam to feed from mu-link instead of the plugin host / internal transport.
- When mu-link is **absent**, the product behaves exactly as today (own audio device, own internal transport). mu-link is additive, never a hard dependency.

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

### Secondary questions (design detail, not blocking)
- **Tempo ownership:** is mu-link always tempo master, or can a client (e.g. mu-clid) be master and mu-link follow?
- **Channel layout:** stereo per client summed to a stereo master, or multi-channel / per-client returns?
- **Mixing depth:** plain sum, or reuse `MixerEngine` for per-client level/pan/mute/solo + master?
- **Max clients** expected (fixes ring/shared-memory sizing).
- **Discovery/handshake:** auto-attach when mu-link is running (named shared memory + registration); how to present "mu-link connected" in each product's UI.
- **Sample-rate / buffer authority:** mu-link dictates SR + block size; clients must conform (confirm).

## 6. Licensing notes

- **ASIO SDK** — free but under a Steinberg licence agreement; the SDK cannot be redistributed. Only relevant if we pick the ASIO backend.
- **Ableton Link** — dual GPLv2+ / proprietary. A closed-source product needs the paid licence. Avoided unless we choose to be a Link peer; the internal clock is custom and licence-free.

## Sources
- JACK architecture / lock-free ring buffer — <https://jackaudio.org/api/> , <https://jackaudio.org/api/ringbuffer_8h.html>
- WASAPI vs ASIO routing — <https://github.com/dechamps/FlexASIO> , <https://markheath.net/post/what-up-with-wasapi>
- Ableton Link — <https://ableton.github.io/link/> , <https://github.com/Ableton/link>
- MIDI-clock jitter vs sample-accurate sync — <https://www.e-rm.de/data/E-RM_report_Jitter_02_14_EN.pdf>
