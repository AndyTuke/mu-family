# μ Family — Ableton Link Plan

**Status: planned, not started (owner, 2026-10-09).** Gated on a licence decision (§3). Facts below were
checked on 2026-10-09 against Ableton's README, documentation and GitHub release list (Link 4.1,
published 2026-09-23). Nothing here has been built or tried.

## 1. Why

Link is how laptops, phones and apps keep time together on a jam: every participant shares **tempo**, **beat
phase** and **start / stop** over the local network with no master and no setup. For playing live with
other musicians it is the sync most of them already have (Live, many DJ and iOS apps). MIDI clock only
carries tempo, with jitter (see the MIDI sync items in the backlog). Link complements it, it does not
replace it: MIDI clock stays for outboard hardware.

## 2. What Link gives us (and does not)

- **Shares:** tempo, a beat timeline aligned to a *quantum* (in beats; four beats = a bar), and start / stop
  (since Link 3). Each app keeps its own timeline; Link keeps them in step.
- **Phase:** two apps with the same quantum stay bar-aligned. Pressing play should start at the **next
  quantum boundary**, so everyone lands on the same downbeat.
- **Start / stop** comes only from a user action; joining does not change the session's transport.
- **Does not share:** a song position beyond phase, time signatures, presets, or audio. (Link 4.x also adds
  *Link Audio*, which shares audio channels between peers; it must not be used at the same time as plain
  Link. Not planned here; noted in design-future.md.)
- **Threads:** the audio thread has a real-time-safe capture / commit pair (audio thread only); other threads
  use a separate, blocking-allowed capture for the UI.
- **Time mapping:** Link needs the *output time* of each audio buffer, so the device's output latency must be
  added to the system time before it is passed in. The same latency number is what the sync-offset item
  (backlog #1253) needs, so they share one source.

## 3. The gating decision — licence (owner)

Link is **dual-licensed: GPLv2 or later, or a proprietary licence** from Ableton (contact
`link-devs@ableton.com`). Options:

| Route | What it means | Cost to us |
|---|---|---|
| **A. Proprietary licence** | Ask Ableton for terms to embed Link in closed-source apps. | Unknown fee / terms; owner-only conversation. |
| **B. GPL** | Ship the apps (or the part that contains Link) under GPL-compatible terms with source. | The mu-family repo has **no LICENSE file** today and the apps were sold with licence keys; #1164 (free apps) may make open source acceptable. JUCE has its own licence choice (AGPLv3 or its commercial tiers) that must be compatible — the tier in use should be written down. |
| **C. Keep Link out of the apps** | A separate GPL helper process hosts Link and talks to mu-link over IPC, so no GPL code is linked into the apps. | Extra process; whether this is enough to keep the apps separate is a legal question — **get advice before relying on it**. |

Until this is decided the plan is **build-time optional**: Link sits behind a CMake option
`MUFAMILY_ENABLE_LINK` (default **OFF**), so licence-free builds stay possible and the code path costs nothing
when it is off. Private experiments on the build PC are fine under route B; distributing a binary is what
the licence governs.

## 4. Where it fits in the family

Checked against the one-framework direction in [design-future.md](design-future.md): Link is added **once, in
`mu-core` and `mu-link`**, never per product.

- **mu-link is the Link peer.** `mu-link` gains a third clock source (`ClockSource::Link`, beside Internal and
  ExternalMidi). Every app attached to the bus then follows through the existing playhead path, so one
  Link peer per machine covers the whole family. Its MIDI clock out (once fixed) turns it into a
  **Link-to-hardware bridge**: Link in, MIDI clock / Start / Stop out.
- **Standalone apps without mu-link** can join Link themselves: `mu-core`'s `TransportResolver` gets a `Link`
  source between the host / mu-link position and MIDI clock. Plugins do not use Link: the DAW owns the clock
  there, and a DAW that has Link hands the plugin its position. (This is my reading; Ableton's docs do not
  address plug-ins.)
- **One wrapper:** `mu_core::LinkSession` hides Ableton's API (capture / commit, enable, quantum, start / stop,
  peer count) behind a small interface that is easy to stub in tests. `mu-core` still knows no product.
- **Build:** Link is header-only, needs C++17 (we have it) and MSVC 2022 or later, and brings its own
  asio. Vendor it under `ThirdParty/` as the other libraries are, behind the CMake option. Windows builds
  need `_WIN32_WINNT` set consistently (not set by our CMake today — check).
- **Source selection:** one of Internal / MIDI clock / Link at a time, chosen in mu-link and in each
  standalone's Settings. Tempo is fractional (Link tempo is a double), so the whole-number BPM fields must
  change first (backlog #1253).
- **UI:** a Link toggle, peer count and quantum in mu-link and in Settings; a Link indicator beside the
  clock-locked indicator in the TransportBar.
- **Live behaviour to design:** a remote peer can change tempo mid-song; allow the user to ignore remote
  tempo changes if that proves needed on stage. First-run Windows Firewall prompts (Link uses the local
  network) need a note in the manual.

## 5. Staged plan (backlog items; all On Hold behind the licence decision)

| Stage | Work | Needs |
|---|---|---|
| L0 | Decide the licence route (§3). | Owner. |
| L1 | Vendor Link behind `MUFAMILY_ENABLE_LINK`; `LinkSession` wrapper; a small console test that joins a session and prints tempo / phase, tried against Link's own `LinkHut` example as a second peer. | L0; build PC. |
| L2 | mu-link: `ClockSource::Link`, UI, device-latency time mapping, MIDI clock out bridge. | L1, plus the sync fixes (below). |
| L3 | Standalone apps: resolver `Link` source, Settings, TransportBar indicators, saved settings. | L2. |
| L4 | Quantised start, quantum choice (follows the time-signature work), manual test plan MS2 against Ableton Live and a second laptop. | L3. |

**Do the sync groundwork first.** Link will expose every timing flaw in the current clock path, so the order is:
the MIDI sync fixes (#1261, #1260, #1259), sync offset / tap tempo / fractional BPM (#1253), mu-link latency
(#1256) and clock out (#1254, #1258), time signature (#1251) — then Link.

## 6. Keeping Link current

Link follows the same rule as JUCE (CLAUDE.md): track the latest stable release (4.1 today), pinned in one
place, bumped deliberately after reading its changelog; method names have changed between releases, so check
the version's header rather than copying old examples.

## 7. Open questions

- Which licence route (§3), and which JUCE licence tier are the apps under?
- Should a standalone be allowed to join Link directly, or only through mu-link?
- Do we want Link Audio later (stream layers into another Link app)?
