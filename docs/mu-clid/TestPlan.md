# μ-Clid — Smoke Test Plan

25 atomic tests, ordered so each builds on the state established by the previous one. Run through start-to-finish to validate a build. Each test has a single action and a single expected outcome; if anything deviates, stop and investigate before proceeding.

Reference build: see `build_number.txt`. Run against the Release build (Debug for crash diagnostics only).

---

## A. Startup & basic playback

**1. Launch** — Open the Standalone exe. Window opens within 5 s. Transport bar top-left shows "**μ-Clid**" (Greek mu, not "mu-Clid"). No error dialogs. Status bar empty / idle.

**2. Default rhythm present** — Sidebar shows one rhythm slot named `<unnamed>`. The rhythm circle on the main panel renders three concentric rings; the outer (A) ring shows 16 segments with 4 lit hits.

**3. Play** — Click Play. Within ~250 ms the first hit fires; subsequent hits land on each lit step. Rhythm circle's playhead arc advances smoothly. No audio glitches, no dropouts.

**4. Stop** — Click Stop. Audio silences within one block. Click Play again — playback resumes from step 0 (cold-start, not mid-loop).

---

## B. Pattern editing

**5. Steps A** — While playing, drag Steps A from 16 → 8. Circle's outer ring rebuilds to 8 segments live; audio pulse density doubles. Drag back to 16 — pattern restores.

**6. Hits A** — With Steps A = 8, drag Hits A from 4 → 6. Six segments light up on the A ring. Audio reflects increased density.

**7. Rotate A** — Drag Rot A from 0 → 4. The lit-hits pattern shifts around the ring without changing step count or density.

**8. Logic + Legato row layout** — On the Logic row (between A and B), confirm the LEG toggle sits FIRST (left), separated by a visible sub-panel gap, then the five Logic pills (OR/AND/XOR/A Only/B Only). Click each Logic pill — sound changes accordingly. Click LEG — pill highlights, status bar shows `Pattern Legato : On`.

---

## C. Voice envelopes (recent fixes — #417, #420)

**9. Amp envelope shapes sound** — Set Amp Decay = 4 s and Amp Sustain = 0. Hits should now fade to silence over ~4 s and stay silent until the next hit retriggers them. (If the sound is full-level regardless, #417 has regressed.)

**10. Attack stays at zero** (#420) — Set Amp Attack to its minimum (0). Touch any *other* envelope knob — Decay, Sustain, Release, the filter envelope, etc. The Amp Attack slider must NOT visibly creep upward. (If it bumps to a fractional value, #420 has regressed.)

**11. Release-to-end** (#417) — Set Amp Release to maximum. Play a long pad sample; while it's playing, swap to a different rhythm preset. The OLD sample should continue playing through to its natural end (no fade), while the NEW rhythm starts on top.

---

## D. Filter & insert FX

**12. Filter sweep** — Set Filter Cutoff to ~500 Hz and Resonance to ~0.6. Sound dulls and rings. Sweep cutoff slowly — timbre changes smoothly with no zipper noise.

**13. Insert FX swap** — Change the Insert dropdown to Bitcrusher; drop drvBits to ~6 — audio becomes gritty. Switch to Compressor — GR meter responds. Switch back to None — audio returns to clean. No clicks during switches.

---

## E. Sample handling (recent feature — #418)

**14. Sample browser opens** — Click the rhythm's sample bar / Load button. Browser dialog opens (560 × 470). Top row shows two pills: "**Main Library**" (selected) and "**μ-Clid Content**". File browser fills the rest.

**15. Sample preview** — Single-click any audio file in the browser. The file auditions through master output. Click another — preview switches. Click Cancel — preview stops, dialog closes, no sample loaded into the slot.

**16. Load from Main Library** — Open sample browser. Pick a file from the Main Library default folder. Click Load. Dialog closes; sample bar shows the new file name; pressing Play uses the new sample.

**17. Switch to μ-Clid Content** — Open sample browser again. Click the "μ-Clid Content" pill. Browser root jumps to `<Documents>/TDP/muClid/Samples`. Load any file there — works the same as Main Library.

---

## F. Pattern Legato (recent feature — #419)

**18. Pattern Legato OFF — every hit retriggers** — Disable Pattern Legato (LEG pill off). Set sustain = 0, decay = 4 s, ensure adjacent steps are both hits. Each hit should be short and percussive — envelope retriggers on every step, so the 4 s decay never has time to play.

**19. Pattern Legato ON — tied hits skip retrigger** — Enable Pattern Legato (LEG pill on). Same pattern. Contiguous hits now skip the envelope retrigger: the 4 s decay breathes across the run; only the first hit of a contiguous run triggers the envelope. Tied hits after a rest still retrigger normally. No clicks on tied steps.

---

## G. Presets & hot-swap (Stage 34 — #415, #416)

**20. Save preset** — Click rhythm save. Type a name (e.g. `TEST`), choose a category, Save. Preset appears in the rhythm preset dropdown.

**21. Hot-swap mid-play** — Press Play. Select a different preset in the dropdown. New rhythm commits at the loop boundary (small delay). New preset plays correctly. If the previous preset had a long sustained sample, that sample's tail should audibly continue under the new rhythm's hits (no abrupt cut).

**22. Hot-swap from effect-heavy preset** (#416 regression check) — Load a preset with comb filter + bitcrusher and let it play. Swap to a clean kick preset. The clean kick should sound CLEAN — no comb-ring / bitcrusher overlay bleeding through from the retired engine. (If it sounds distorted, #416 has regressed.)

---

## H. Multi-rhythm, mixer, modulation

**23. Add a second rhythm** — Click the sidebar `+`. New slot appears. Both rhythms play simultaneously. Sidebar shows two playheads animating. Mute one via the sidebar — only that rhythm silences.

**24. Modulator: LFO → cutoff** — Open the modulator panel. Add an LFO targeting Filter Cutoff with substantial depth. Play. Cutoff sweeps in time with the LFO. The cyan modulation indicator ring appears on the Cutoff knob with a live arc.

---

## I. Settings & persistence

**25. Settings round-trip** — Open Settings overlay. Section list (top → bottom): Audio, Hot-swap, MIDI Program Change, Output, **Sample Library**, Content Folder. Browse the Sample Library to a custom folder. Close + reopen Settings — your choice persists. In the host (DAW): save project, close, reopen — every knob, pattern, sample, and mixer state restores exactly.

---

## Common failure signatures

| Symptom | Likely regression |
|---|---|
| First hit late (~250 ms+) | #384 step-0 absorb |
| Filter / effect "overlay" on clean rhythm after preset swap | #416 retired-engine filter not reset |
| Attack slider creeps off zero after touching another control | #420 jmax floor leaked into data layer |
| Sample preset plays at full level despite Sustain=0 / Decay=4s | #417 ampRelToEnd skipping env |
| Pattern Legato off but envelope still continues across hits | #419 tiedMask gating |
| Crash / hang on rapid preset spam | Stage 34 retired-slot back-pressure |
