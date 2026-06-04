$ErrorActionPreference = "Stop"
$word = New-Object -ComObject Word.Application
$word.Visible = $false
$doc = $word.Documents.Add()
$doc.PageSetup.TopMargin    = $word.InchesToPoints(1.0)
$doc.PageSetup.BottomMargin = $word.InchesToPoints(1.0)
$doc.PageSetup.LeftMargin   = $word.InchesToPoints(1.25)
$doc.PageSetup.RightMargin  = $word.InchesToPoints(1.25)

$sel = $word.Selection

function H1($t)  { $sel.Style = $doc.Styles("Heading 1");  $sel.TypeText($t); $sel.TypeParagraph() }
function H2($t)  { $sel.Style = $doc.Styles("Heading 2");  $sel.TypeText($t); $sel.TypeParagraph() }
function H3($t)  { $sel.Style = $doc.Styles("Heading 3");  $sel.TypeText($t); $sel.TypeParagraph() }
function P($t)   { $sel.Style = $doc.Styles("Normal");      $sel.TypeText($t); $sel.TypeParagraph() }
function Br()    { $sel.InsertBreak(7) }   # wdPageBreak
function Bullet($t) { $sel.Style = $doc.Styles("List Bullet"); $sel.TypeText($t); $sel.TypeParagraph() }

# Screenshot placeholder — italic, centred, distinct so screenshots can be
# located via Find ("[Screenshot:") and replaced by the artwork at release time.
function Pic($caption) {
    $sel.Style = $doc.Styles("Normal")
    $sel.ParagraphFormat.Alignment = 1   # wdAlignParagraphCenter
    $sel.Font.Italic = $true
    $sel.TypeText("[Screenshot: $caption]")
    $sel.Font.Italic = $false
    $sel.ParagraphFormat.Alignment = 0   # wdAlignParagraphLeft
    $sel.TypeParagraph()
}

# ── Title page ──────────────────────────────────────────────────────────────
$sel.Style = $doc.Styles("Title")
$sel.TypeText("mu-Tant")
$sel.TypeParagraph()
$sel.Style = $doc.Styles("Subtitle")
$sel.TypeText("Wavetable Drone Synthesiser")
$sel.TypeParagraph()
$sel.Style = $doc.Styles("Normal")
$sel.TypeText("User Manual")
$sel.TypeParagraph()
$sel.TypeText("Transwarp Development Project")
$sel.TypeParagraph()
Br

# ── 1. Introduction ──────────────────────────────────────────────────────────
H1 "1. Introduction"
P "mu-Tant is a wavetable drone synthesiser from Transwarp Development Project. It runs as a VST3 plug-in, a CLAP plug-in, and a standalone application."
P "mu-Tant is a drone instrument, not a note instrument. Each of up to eight independent voices runs two wavetable oscillators continuously - the oscillators never start or stop. Instead of triggering notes, you sculpt a continuously evolving tone: a drawable gate pattern chops rhythmic bursts out of the drone, dual multi-mode filters shape it, and a per-voice modulator bank warps pitch, timbre, and filter over time."
P "mu-Tant shares its interface shell, modulator engine, mixer, and FX rack with its sibling mu-Clid. The difference is the engine: where mu-Clid triggers samples on Euclidean hits, mu-Tant sustains wavetable drones shaped by a drawn gate."

H2 "Who Is This For?"
P "mu-Tant is for sound designers and producers who work with evolving pads, drones, textures, and generative ambient material. It is equally at home in a DAW session - fully automatable and synchronised to host transport - or as a standalone instrument for live performance and sketching."

Pic "Full plugin window with voice sidebar, voice editor, gate editor, and modulator panel visible."

# ── 2. What is a Drone Synth? ────────────────────────────────────────────────
H1 "2. The Drone Concept"
P "A traditional synthesiser is silent until you press a key; the note starts on key-down and ends on key-up. mu-Tant inverts this. Its oscillators are free-running - they are always producing sound. What you hear is shaped not by note triggers but by three things working together:"
Bullet "The gate - a drawn pattern of attack/decay shapes that opens and closes a volume gate, carving rhythm out of the continuous tone."
Bullet "The modulators - up to eight per voice, slowly (or quickly) warping pitch, wavetable position, filter cutoff, and cross-modulation."
Bullet "The dual filter - two independent multi-mode filters in series or parallel, each able to resonate, comb, or shelve the evolving spectrum."
P "Because nothing ever retriggers from zero, mu-Tant excels at sounds that breathe and shift continuously - the gate adds rhythm without the choppiness of a re-struck note."

# ── 3. Installation ──────────────────────────────────────────────────────────
H1 "3. Installation"
H2 "Plug-in Files"
P "mu-Tant ships in three formats:"
Bullet "VST3  —  copy mu-Tant.vst3 to %ProgramFiles%\Common Files\VST3\"
Bullet "CLAP  —  copy mu-Tant.clap to %ProgramFiles%\Common Files\CLAP\"
Bullet "Standalone  —  run mu-Tant.exe from any location"

H2 "Content Folder"
P "On first launch mu-Tant creates a content folder at %USERPROFILE%\Documents\TDP\muTant\ with two sub-folders: Presets (full preset files, .muTant) and Voices (single-voice preset files, .muPattern)."

# ── 4. Interface Overview ─────────────────────────────────────────────────────
H1 "4. Interface Overview"
P "The mu-Tant window is divided into four regions:"
Bullet "Transport Bar (top) — playback, BPM, presets, mixer, and settings"
Bullet "Voice Sidebar (left) — one item per active voice, plus an Add button"
Bullet "Voice Editor (centre) — oscillators, cross-mod, filters, insert, gate editor, and modulators for the selected voice"
Bullet "Mixer overlay — opened from the transport bar, replacing the editor"
P "The window is resizable and all elements scale proportionally."

Pic "Annotated full-window view showing the voice editor regions."

# ── 5. Transport Bar ──────────────────────────────────────────────────────────
H1 "5. Transport Bar"
P "Controls from left to right:"
Bullet "mu-Tant logo  —  click to open the About panel"
Bullet "Play / Stop  —  in DAW mode follows the host transport and BPM; in standalone drives the internal clock. While stopped, the gate is closed (silent); press Play to hear the gated drone, or use the gate Bypass to audition the raw tone."
Bullet "BPM display  —  read-only in DAW mode; editable in standalone. The clock drives both the gate patterns and the modulators."
Bullet "Position  —  bar and beat readout."
Bullet "Preset dropdown / New / Save  —  load and save full .muTant presets."
Bullet "Mixer button  —  opens the mixer overlay."
Bullet "Settings (gear) icon  —  opens the Settings overlay."

Pic "Transport Bar — full width view."

# ── 6. Voice Sidebar ──────────────────────────────────────────────────────────
H1 "6. Voice Sidebar"
P "The sidebar shows one item per active voice (up to eight). Each item displays a miniature spectrum animation driven by the voice's post-insert audio, the voice colour, and the voice name. Click an item to select that voice for editing."
P "The Add (+ Voice) button at the bottom creates a new voice with default settings and the next unused palette colour. Voices can be reordered by dragging. To remove a voice use the Delete (X) button in the voice header - a confirmation is required, and the last voice cannot be removed."
P "Each voice automatically receives a distinct palette colour that appears on its sidebar item, header bar, and the sub-panel borders inside the editor, and follows the voice through reorder and delete operations."

Pic "Voice Sidebar with three voices, one selected."

# ── 7. Voice Header and Tonal Centre ──────────────────────────────────────────
H1 "7. Voice Header"
P "At the top of the voice editor:"
Bullet "Name — double-click to rename the voice."
Bullet "Reset (circular arrow) / Delete (X) — reset to defaults / remove the voice (both confirm first)."
Bullet "Root and Scale — the shared tonal centre for this voice (see Oscillators)."
Bullet "Voice preset dropdown / Save — load and save a single voice as a .muPattern file in the Voices folder."

# ── 8. Oscillators ────────────────────────────────────────────────────────────
H1 "8. Oscillators (OSC 1 and OSC 2)"
P "Both oscillators run simultaneously and are free-running - their phases never reset. The result is a continuously evolving tone rather than a triggered note."

H2 "Shared Tonal Centre"
P "Both oscillators share the same Root (C to B) and Scale, set in the voice header. The available scales are Major, Minor, Dorian, Phrygian, Lydian, Mixolydian, Locrian, Harmonic Minor, Pentatonic Major, Pentatonic Minor, Blues, and Chromatic. The root and scale define the harmonic space both oscillators move within."

H2 "Per-Oscillator Controls"
P "Each oscillator has a wavetable selector plus five knobs:"
Bullet "Oct (-3 to +3 octaves) — octave offset from the base note"
Bullet "Semi (-12 to +12) — scale-degree offset (integer steps through the scale)"
Bullet "Fine (plus or minus 100 cents) — off-scale detune"
Bullet "Pos (0 to 255) — wavetable frame position; morphs through the table"
Bullet "PEnv (plus or minus 24 semitones) — pitch-envelope depth: how far the gate editor's PITCH layer shifts this oscillator's pitch"
P "Modulating Semi with a stepped modulator snaps cleanly between scale notes (integer jumps). Modulating Semi with a smooth modulator glides through frequency space, passing through off-scale pitches for a glissando effect."

Pic "OSC 1 and OSC 2 panels showing the wavetable selector and the five knobs."

# ── 9. Cross-Modulation and Noise ─────────────────────────────────────────────
H1 "9. Cross-Modulation (X-MOD) and Noise"
P "mu-Tant runs all three cross-modulation types simultaneously, each with its own independent depth knob (0 to 100 percent). Osc 2 is always the modulator; Osc 1 is the carrier."
Bullet "FM — Osc 2's output phase-modulates Osc 1 (classic FM timbre)"
Bullet "AM — Osc 2 amplitude-modulates Osc 1 (adds sidebands, tremolo-like)"
Bullet "Ring — crossfades Osc 1 toward Osc 1 multiplied by Osc 2 (sum and difference frequencies)"
P "Set any combination - a depth of 0 percent skips that type entirely with no CPU cost."

H2 "Sync"
P "The Sync button (below the X-MOD knobs) resets Osc 2's phase each time Osc 1 completes a cycle (hard sync). This thickens the tone and adds a formant-like quality that tracks Osc 1's pitch. Sync is independent of the FM/AM/Ring depths and combines with all of them."

H2 "Noise"
P "A separate noise source (White or Pink) mixes in via the NOISE panel. The noise type dropdown is in the NOISE panel; the noise level knob is in the MIXER panel."

# ── 10. Source Mixer ──────────────────────────────────────────────────────────
H1 "10. Source Mixer"
P "The MIXER panel sets the balance of the three sound sources before they reach the filters:"
Bullet "Osc 1 — level of oscillator 1 (dB)"
Bullet "Osc 2 — level of oscillator 2 (dB)"
Bullet "Noise — level of the noise source (dB; default -60 dB, off)"
P "The voice's overall output level into its mixer channel is set by the channel fader in the mixer overlay (and is available as the Level modulation destination)."

# ── 11. Dual Filter ───────────────────────────────────────────────────────────
H1 "11. Dual Filter"
P "mu-Tant has two independent filters per voice - Filter 1 (top row) and Filter 2 (bottom row). Each is the full mu-core multi-mode filter with identical controls:"
Bullet "Type — sixteen algorithms: LP 6 / 12 / 24, HP 6 / 12 / 24, BP 12 / 24, Notch, Notch 24, AP 12, Comb +, Comb -, Peak, Lo Shelf, Hi Shelf"
Bullet "Drive (0 to 100 percent) — pre-filter valve saturation (asymmetric tanh) for harmonic warmth before the filter"
Bullet "Cutoff (20 Hz to 20 kHz) — log-scaled; clamped safely below Nyquist so the filter is stable at any sample rate"
Bullet "Resonance (0 to 100 percent) — Q / feedback"
Bullet "FEnv (plus or minus 1) — filter-envelope depth: how far the gate editor's FILT layer sweeps this filter's cutoff"
Bullet "Low Cut (20 Hz to 20 kHz) — post-filter 4-pole high-pass; clears subsonic build-up at high resonance"

H2 "Series / Parallel Routing"
P "The round purple button between the two filter rows sets how the filters combine. Click it to toggle; the icon animates a 90-degree rotation between two states:"
Bullet "Two horizontal lines — Parallel: both filters process the input and their outputs are mixed (two signal paths running side by side)"
Bullet "Two vertical lines — Series: the signal passes through Filter 1, then into Filter 2 (one chain)"
P "A freshly added second filter defaults to an 8 kHz cutoff so it is audible immediately. To hear Filter 2's effect over a dark Filter 1 tone, set its cutoff below Filter 1's content."

Pic "Filter section showing the two filter rows and the series/parallel routing toggle."

# ── 12. Insert Effect ─────────────────────────────────────────────────────────
H1 "12. Insert Effect"
P "The INSERT panel is the per-voice insert effect, placed after the gate (oscillators, then filters, then gate, then insert, then mixer). Select an algorithm from the dropdown; its four parameter knobs relabel to match. Available algorithms: None, Soft Clip, Hard Clip, Fold, Bitcrusher, Clipper, 3-Band EQ, Compressor, Limiter, Ring Mod, Tape Saturation, Karplus, Vocoder, and Vocoder Stereo. Insert parameters P1 to P4 are available as modulation destinations."

# ── 13. Gate Editor ───────────────────────────────────────────────────────────
H1 "13. Gate Editor"
P "The gate editor is the heart of mu-Tant's rhythmic character. It occupies the full-width band below the voice panel and chops rhythmic bursts out of the otherwise-continuous drone."

H2 "Layers"
P "Three layers, toggled by the GATE, FILT, and PITCH buttons:"
Bullet "GATE — shapes amplitude. Envelope value 0 is silent, 1 is full level."
Bullet "FILT — shapes filter cutoff. 0 is 20 Hz (closed), 1 is the base cutoff (open). Depth set by each filter's FEnv knob."
Bullet "PITCH — shifts oscillator pitch. 0 is no shift, 1 is the full PEnv depth above the base Semi. Depth set per oscillator by the PEnv knobs."
P "The inactive layers are drawn as ghosts at 20 percent opacity so you can align layers visually."

H2 "Grid Controls"
Bullet "Bypass — passes the raw drone through without gating, for auditioning the oscillator/filter sound while designing a patch."
Bullet "Bars (1 to 16) — the pattern length in bars. The view window is always two bars wide; a scrollbar below the grid scrolls smoothly through longer patterns. All three layers share the same length."
Bullet "Grid — the cell subdivision: 1/4, 1/8, 1/16 (default), or 1/32."
Bullet "Gap — forces a silence tail at the end of every envelope region. At 0 percent the envelope fills the full region; at 50 percent it completes in the first half and the second half is silent."

H2 "Toolbox"
P "Three tools in the header:"
Bullet "Pencil — click an empty cell to draw a one-cell envelope; drag the start/end grab handles (bottom corners) to extend a region"
Bullet "Eraser — click an envelope to delete it"
Bullet "Reverse — click an envelope to flip its attack and decay"

H2 "Envelope Shape"
P "Each envelope covers a contiguous region of cells and has an attack/decay shape:"
Bullet "Top handle (the peak point) — drag horizontally to move where within the region the peak falls. Far left is instant attack (pure decay); far right is slow attack with no decay. Snaps to the grid; hold ALT for fine placement."
Bullet "Attack bend handle (mid-point on the rising line) — drag vertically to bow the attack."
Bullet "Decay bend handle (mid-point on the falling line) — drag vertically to bow the decay."
P "A new envelope defaults to instant attack with a linear decay - the classic gate sound."

H2 "Audio Behaviour"
Bullet "Stopped — silent (gate closed)"
Bullet "Playing, empty pattern — silent (nothing drawn passes)"
Bullet "Playing, has envelopes — per-sample envelope evaluation"
Bullet "Bypass on — raw drone passes regardless of play state"

Pic "Gate editor showing the GATE layer with several attack/decay envelopes and the FILT ghost behind."

# ── 14. Modulators ────────────────────────────────────────────────────────────
H1 "14. Modulators"
P "Each voice has eight independent modulators (Mod A through Mod H) plus a Matrix tab - the same shared engine as mu-Clid. Each voice has its own modulator bank."
P "Each modulator has a Mode toggle (Stepped bar-graph or Smooth drawable curve), a Polarity toggle (Unipolar 0 to +1, or Bipolar -1 to +1), and a loop length (a note-value dropdown times an integer multiplier, independent of any pattern length). A playhead line shows the current loop position. In Smooth mode, click to add a node, drag to move, ALT-click a segment to bend it, right-click to remove. In Stepped mode, drag bars up or down. A dice button randomises the current modulator's values without changing its shape."
P "Add destinations with the Add Target button; each assignment has a bipolar depth (-100 to +100 percent) and a source-side curve. mu-Tant modulation destinations include: Osc 1/2 Octave, Semi, Fine, and Position; X-Mod FM, AM, and Ring depths; Osc 1/2 and Noise levels; Filter Cutoff and Resonance; the voice Level; and Insert P1 to P4. When a knob is a destination, an arc around it shows the live modulated value."
P "The Matrix tab lists every assignment across all eight modulators in one table."

Pic "Modulator panel with the Stepped editor and several destination targets."

# ── 15. Mixer ─────────────────────────────────────────────────────────────────
H1 "15. Mixer"
P "Click the Mixer button in the transport bar to open the mixer overlay. Each voice occupies one channel strip; to the right are the Effect, Delay, and Reverb return channels and the Master strip. This is the same shared mixer and FX rack as mu-Clid."
P "Each voice strip has a fader with VU metering, pan, mute and solo, sidechain ducking (source, amount, attack, release), and Effect / Delay / Reverb sends. The FX return strips carry intra-FX routing (Effect to Delay, Effect to Reverb, Delay to Reverb), and the master strip carries a master insert. The three FX unit rows (Effect / Delay / Reverb) sit below the strips with algorithm selectors and inline parameters."

Pic "Mixer overlay with voice channels, FX returns, and master."

# ── 16. Presets ───────────────────────────────────────────────────────────────
H1 "16. Presets"
H2 "Full Presets (.muTant)"
P "Full presets save the entire plugin state - all voices, modulators, gate patterns, mixer, and FX - to Documents\TDP\muTant\Presets. Load from the transport dropdown; save with the Save button (name, description, and category)."
H2 "Per-Voice Presets (.muPattern)"
P "Each voice can be saved and loaded independently to Documents\TDP\muTant\Voices, including its oscillator, filter, insert, and level settings, all three gate layers (GATE / FILT / PITCH), and modulator assignments. Use the voice header dropdown and Save button."

# ── 17. Settings ──────────────────────────────────────────────────────────────
H1 "17. Settings"
P "Click the gear icon to open the Settings overlay:"
Bullet "Master Volume — overall output level"
Bullet "UI Size — Medium or Large window"
Bullet "Tempo (BPM) — internal clock, also editable from the transport BPM field"

# ── 18. Signal Flow ───────────────────────────────────────────────────────────
H1 "18. Signal Flow"
P "Osc 1 and Osc 2 are combined with FM / AM / Ring cross-modulation and optional hard Sync, summed with the Noise source, then passed through the dual filter (series: Filter 1 into Filter 2; parallel: Filter 1 plus Filter 2 mixed). The result passes through the gate (GATE layer amplitude, FILT layer cutoff sweep, PITCH layer pitch shift), then the per-voice insert, then into the voice's mixer channel. The mixer routes channel strips through the Effect / Delay / Reverb sends to the master and out."

# ── 19. Tips and Workflow ─────────────────────────────────────────────────────
H1 "19. Tips and Workflow"
Bullet "Starting from silence: press Play, then draw at least one cell on the GATE layer - an empty gate produces no sound."
Bullet "Auditioning the raw drone: press Bypass in the gate editor to hear the uncut oscillator/filter tone."
Bullet "Dual-filter character: set Filter 1 to Comb + and Filter 2 to LP 24 in Series for a tuned resonator rolled off by a smooth low-pass; or use Parallel with a low-pass on one side and a high-pass on the other to carve a band."
Bullet "Layered pads: add several voices with different Semi offsets (e.g. 0, 2, 4 for a triad in the scale) and staggered gate patterns."
Bullet "Filter morphing: draw a slow attack / fast decay on the FILT layer and set FEnv to 1.0 - the filter opens slowly then snaps closed on each gate event."
Bullet "FM drones: raise the X-MOD FM knob to 60 to 80 percent and modulate Osc 2 Semi with a stepped sequencer - each step adds a new FM timbre as Osc 2's ratio changes."
Bullet "Long evolving patterns: set Bars to 8 or 16 and scroll through the pattern, combining sparse GATE hits with a slowly drifting Smooth modulator on Filter Cutoff."

# ── 20. Technical Specifications ──────────────────────────────────────────────
H1 "20. Technical Specifications"
Bullet "Formats: VST3, CLAP, Standalone (Windows)"
Bullet "Voices: up to 8 simultaneous, each with two wavetable oscillators plus a noise source"
Bullet "Cross-modulation: FM, AM, and Ring simultaneously, plus hard sync"
Bullet "Filters: two per voice (series or parallel), 16 algorithms each"
Bullet "Filter types: LP / HP at 6, 12, 24 dB per octave; BP 12 / 24; Notch 12 / 24; AP 12; Comb + / Comb -; Peak; Lo Shelf; Hi Shelf"
Bullet "Insert effects: 13 plus None (Soft Clip, Hard Clip, Fold, Bitcrusher, Clipper, 3-Band EQ, Compressor, Limiter, Ring Mod, Tape Sat, Karplus, Vocoder, Vocoder Stereo)"
Bullet "Gate editor: 1 to 16 bar patterns, three layers (GATE / FILT / PITCH), subdivisions 1/4 to 1/32"
Bullet "Modulators per voice: 8 (smooth LFO or stepped), plus a matrix view"
Bullet "Full preset format: .muTant (XML). Per-voice preset format: .muPattern (XML)"
Bullet "Third-party libraries: JUCE, Signalsmith Reverb (MIT), Monocypher (BSD-2-Clause), clap-juce-extensions (MIT)"

# ── 21. Not in mu-Tant ────────────────────────────────────────────────────────
H1 "21. Design Decisions (Not in mu-Tant)"
Bullet "No amplitude envelope - the gate stage does this job."
Bullet "No note-on / note-off - oscillators run continuously; MIDI is not used for played notes."
Bullet "No user samples - the built-in wavetable bank only."
Bullet "No granular - pure wavetable synthesis."

# ── Save ──────────────────────────────────────────────────────────────────────
$outPath = "d:\Dev\mu\docs\mu-tant\mu-Tant User Manual.docx"
$doc.SaveAs([ref]$outPath, [ref]16)
$doc.Close()
$word.Quit()
[System.Runtime.InteropServices.Marshal]::ReleaseComObject($word) | Out-Null
Write-Host "Saved: $outPath"
