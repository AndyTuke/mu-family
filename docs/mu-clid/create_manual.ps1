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
$sel.TypeText("mu-Clid")
$sel.TypeParagraph()
$sel.Style = $doc.Styles("Subtitle")
$sel.TypeText("Polyrhythmic Euclidean Sequencer")
$sel.TypeParagraph()
$sel.Style = $doc.Styles("Normal")
$sel.TypeText("User Manual")
$sel.TypeParagraph()
$sel.TypeText("Transwarp Development Project")
$sel.TypeParagraph()
Br

# ── 1. Introduction ──────────────────────────────────────────────────────────
H1 "1. Introduction"
P "mu-Clid is a polyrhythmic Euclidean rhythm sequencer and sample trigger plugin from Transwarp Development Project. It runs as a VST3 plug-in, a CLAP plug-in, and a standalone application."
P "mu-Clid lets you build up to eight simultaneous rhythm tracks. Each track is driven by up to three independent Euclidean generators (Ring A, Ring B, and Ring C) and triggers a sample through a complete voice chain — pitch control, multi-mode resonant filter, amplitude envelope, and a per-voice insert effect. Each rhythm has eight LFO or step modulators that can drive any voice parameter. A shared FX chain provides a modulation effect, a tempo-synchronised delay, and a reverb, all routed through an eight-channel mixer with sidechain ducking and per-channel master inserts."

H2 "Who Is This For?"
P "mu-Clid is for producers and sound designers who work with rhythmic textures, polyrhythmic patterns, and generative percussion. It is equally at home in a full DAW session — fully automatable, multi-bus capable, and synchronised to host transport — or as a standalone instrument for live performance and sketching, with optional external MIDI clock sync."

Pic "Full plugin window with sidebar, rhythm panel, transport bar, and status bar visible."

# ── 2. What is Euclidean Sequencing? ─────────────────────────────────────────
H1 "2. What is Euclidean Sequencing?"
P "Euclidean rhythms come from a 2004 paper by Godfried Toussaint, who noticed that an algorithm Euclid wrote down 2,300 years ago for finding the greatest common divisor of two numbers also produces patterns that look strikingly like the rhythms of the world's traditional music."
P "The idea is simple: given a number of steps and a smaller number of hits, distribute the hits as evenly as possible across the steps. With 16 steps and 4 hits you get a hit every fourth step — a classic four-on-the-floor pattern. With 8 steps and 3 hits the hits cannot divide evenly, so the algorithm spaces them as nearly evenly as it can: hit, rest, rest, hit, rest, rest, hit, rest. That is the Cuban tresillo."
P "Many traditional rhythms turn out to be Euclidean. The Cuban son clave, the Brazilian bossa nova, West African bell patterns, Bulgarian folk meters, and Persian aksak rhythms can all be described by a (steps, hits) pair. This is why even random Euclidean patterns tend to feel musical — the algorithm produces the same kind of even-but-not-uniform spacing that human musicians have arrived at across centuries of tradition."
P "mu-Clid pushes the idea further by giving each rhythm three independent Euclidean generators and combining them with logical operators — OR for layered patterns, AND for sparse coincidences, XOR for interlocking rhythms. Add to this rotation (shifting the same pattern to start on a different step), padding (forcing silence at the beginning, end, or middle of the bar), and an accent layer (Ring C), and a few simple controls give you access to a vast range of grooves."

Pic "Diagram: 16 steps with 5 hits distributed evenly, illustrating Bjorklund's algorithm."

# ── 3. Installation ──────────────────────────────────────────────────────────
H1 "3. Installation"
H2 "Plug-in Files"
P "mu-Clid ships in three formats:"
Bullet "VST3  —  copy mu-Clid.vst3 to %ProgramFiles%\Common Files\VST3\"
Bullet "CLAP  —  copy mu-Clid.clap to %ProgramFiles%\Common Files\CLAP\"
Bullet "Standalone  —  run mu-Clid.exe from any location"

H2 "Content Folder"
P "On first launch mu-Clid creates a content folder at %USERPROFILE%\Documents\TDP\muClid\ with three sub-folders: Presets (full preset files, .muClid), Rhythms (single-rhythm preset files, .muRhythm), and Samples (default browse location for samples). You can change the content folder path in the Settings overlay at any time."

H2 "Default Presets"
P "If a file named _default.muClid exists in your Presets folder it is loaded silently on startup. If _default.muRhythm exists in your Rhythms folder it is applied whenever you add a new rhythm. Save over either file to create a personal default."

# ── 4. Interface Overview ─────────────────────────────────────────────────────
H1 "4. Interface Overview"
P "The mu-Clid window is divided into four regions:"
Bullet "Transport Bar (top) — playback, BPM, presets, and global controls"
Bullet "Rhythm Sidebar (left) — one item per active rhythm, plus an Add button"
Bullet "Main Panel (centre) — the selected rhythm, or the Mixer when toggled"
Bullet "Status Bar (bottom) — live readout of the last touched control"
P "The window is resizable and all elements scale proportionally."

Pic "Annotated full-window view showing the four main regions."

# ── 5. Transport Bar ──────────────────────────────────────────────────────────
H1 "5. Transport Bar"
P "Controls from left to right:"
Bullet "mu-CLID logo  —  click to open the About panel"
Bullet "Play / Stop  —  in DAW mode reflects the host transport; in standalone drives the internal clock. Green when stopped, red when playing."
Bullet "BPM display  —  read-only in DAW mode; drag or click to edit in standalone mode."
Bullet "Position  —  bar and beat readout (e.g. 3.2)."
Bullet "Master Loop dropdown  —  how many steps the entire pattern runs before looping. Choices range from 16 steps to 512 steps, or infinity (free-running)."
Bullet "Preset dropdown  —  shows the current preset name; click to load another."
Bullet "Save button  —  opens the Save Preset dialog."
Bullet "Mixer button  —  toggles the Mixer view. The label changes to Sequencer when the mixer is active."
Bullet "Settings (gear) icon  —  opens the Settings overlay."

Pic "Transport Bar — full width view showing all controls."

# ── 6. Rhythm Sidebar ─────────────────────────────────────────────────────────
H1 "6. Rhythm Sidebar"
P "The sidebar shows one item per active rhythm. Each item displays a miniature Rhythm Circle, the rhythm name, and its colour dot. Click an item to select it and display it in the main panel."
P "The selected item shows a vertical accent bar in the rhythm colour on its right edge, connecting it visually to the main panel. When a rhythm fires a hit, its sidebar item briefly pulses with the rhythm colour."
P "The Add button at the bottom of the sidebar creates a new rhythm slot (maximum eight). The button is disabled when eight rhythms are active. To delete a rhythm, use the Delete button in the Rhythm Panel header — a confirmation popup appears before the rhythm is removed."
P "Rhythms can be reordered by dragging items up or down within the sidebar. A pending hot-swap is shown by an orange SWP badge on the sidebar item; click the badge to cancel the swap."

Pic "Rhythm Sidebar with three rhythms loaded, one selected."

# ── 7. Rhythm Panel ───────────────────────────────────────────────────────────
H1 "7. Rhythm Panel"
P "The Rhythm Panel is the main editing surface for the selected rhythm. It is divided into five sections from top to bottom: Header, Sample Bar, Rhythm Circle and Euclidean Panel, Voice Section, and Modulator Panel."

H2 "Header"
P "Shows the rhythm name (double-click to rename), Mute and Solo buttons, a Delete button, and a rhythm-preset dropdown that lists .muRhythm files from your Rhythms folder. Selecting a preset loads it into the current rhythm slot immediately (or stages a hot-swap during playback). A coloured accent strip on the left identifies the rhythm."

H2 "Sample Bar"
P "Drag a sample file onto this bar or click it to browse. Supported formats: WAV, AIFF, MP3, FLAC. Once loaded the filename is shown. A missing-sample warning appears in amber; click the locator icon to find a replacement."

Pic "Rhythm Panel — full layout from header down to modulator tabs."

# ── 8. Rhythm Circle ──────────────────────────────────────────────────────────
H1 "8. Rhythm Circle"
P "The Rhythm Circle shows concentric rings, one per generator. Rings are drawn outside-in:"
Bullet "Ring A (outermost) — purple. Primary Euclidean generator."
Bullet "Ring B — coral. Secondary Euclidean generator."
Bullet "Ring C — amber dashed outline. Accent layer."
P "During playback the rings rotate so the current step is always at the 12 o'clock position. Step 1 is at the top when the transport is at bar 1, beat 1."

H2 "Step Colours"
Bullet "Filled in ring colour — a hit step"
Bullet "Dim outline — an empty step"
Bullet "Cyan-teal fill — a pre-padded step (forced silence before the pattern)"
Bullet "Teal fill — a post-padded step (forced silence after the pattern)"
Bullet "Pink fill — an insert-padded step (silence within the pattern)"
P "On each hit a radial pulse expands from the step arc position and the centre hub briefly flashes in the rhythm colour."

Pic "Rhythm Circle showing all three rings with a mix of hit, empty, and padded steps."

# ── 9. Euclidean Panel ────────────────────────────────────────────────────────
H1 "9. Euclidean Panel"
P "The Euclidean Panel sits to the right of the Rhythm Circle. It has three rows of knobs (A, B, C) and a Logic bar between rows A and B."

H2 "Euclid A and Euclid B"
P "Both rows share identical controls:"
Bullet "Steps (1 to 64) — total number of steps including all padding"
Bullet "Hits (0 to Steps) — hits distributed using Bjorklund's algorithm"
Bullet "Rotate — shifts the pattern left or right"
Bullet "Pre Pad (0 to 12) — empty steps forced at the start of the pattern"
Bullet "Post Pad (0 to 12) — empty steps forced at the end of the pattern"
Bullet "Insert Start — start position of the insert zone within the pattern"
Bullet "Insert Length (0 to 8) — number of steps in the insert zone"
Bullet "Insert Mode — Pad (steps excluded from hit distribution) or Mute (hits distributed but silenced in the insert zone)"
P "Ranges update dynamically: when Steps changes, Hits, Rotate, and Insert Start clamp to fit."

H2 "Logic Bar"
P "Pill selector between rows A and B. The LEG toggle on the left controls Pattern Legato (see below). To its right, five logic pills set how the A and B patterns combine:"
Bullet "OR — fires if either A or B fires"
Bullet "AND — fires only when both A and B fire on the same step"
Bullet "XOR — fires when exactly one of A or B fires"
Bullet "A Only — only A fires; B is ignored"
Bullet "B Only — only B fires; A is ignored"

H2 "Pattern Legato (LEG)"
P "The LEG pill on the Logic Bar toggles Pattern Legato for the whole rhythm. When OFF (default) every hit retriggers the pitch, filter, and amp envelopes from zero — the right behaviour for percussion. When ON, hits that follow immediately after another hit (no silent step between them) skip the envelope retrigger — the envelopes ride through the tied notes. This produces the smooth legato attack you expect from sustained pad or synth material, and is the recommended mode whenever a pattern has runs of consecutive hits and the sample is long enough that the retrigger would feel choppy."

H2 "Euclid C — Accent Layer"
P "Three knobs: Steps, Hits, Rotate. C has no logic relationship with A or B. When Ring C fires a hit on the same step as a combined A+B hit, that step is accented. The accent gain (in dB) is set by the Accent knob in the Amp column of the Voice Section."

Pic "Euclidean Panel with all three rows visible and the Logic bar set to OR."

# ── 10. Voice Section ─────────────────────────────────────────────────────────
H1 "10. Voice Section"
P "The Voice Section is a strip below the Rhythm Circle with four columns: Pitch, Filter, Amp, and Insert. Each column has a configuration row at the top and an envelope row below."

H2 "Pitch (purple)"
P "Configuration: Octave (-4 to +4), Semitone (-12 to +12), Fine (-100 to +100 cents)."
P "Envelope: Attack, Decay, Sustain, Release, and Depth (up to 24 semitones). The envelope adds to the base pitch by the Depth amount."

H2 "Filter (teal)"
P "Configuration: Type, Drive (pre-filter valve saturation, 0 to 100 percent), Cutoff (20 Hz to 20 kHz), Resonance (0 to 0.99), and Low Cut (a post-filter high-pass that clears subsonic build-up from high resonance settings). Cutoff is clamped safely below the Nyquist frequency, so the filter stays stable at any sample rate."
P "Available filter types:"
Bullet "LP 6 / LP 12 / LP 24 — low-pass at 6, 12, or 24 dB per octave"
Bullet "HP 6 / HP 12 / HP 24 — high-pass at 6, 12, or 24 dB per octave"
Bullet "BP 12 / BP 24 — band-pass at 12 or 24 dB per octave"
Bullet "Notch / Notch 24 — band-rejection (the 24 dB version is sharper)"
Bullet "AP 12 — second-order all-pass (phase shift, no level change)"
Bullet "Comb + — positive-feedback comb with resonant peaks at multiples of cutoff"
Bullet "Comb − — negative-feedback comb; peaks at odd multiples, Karplus-Strong plucked-string character"
Bullet "Peak / Lo Shf / Hi Shf — parametric EQ-style boosts (+12 dB)"
P "Envelope: Attack, Decay, Sustain, Release, and Depth (up to 48 semitones of cutoff sweep)."

H2 "Amp (amber)"
P "Configuration: Level (-60 to +6 dB; -inf at minimum), Accent (0 to +12 dB extra gain on accented steps), and three FX send knobs (Effect, Delay, Reverb)."
P "Envelope: Attack, Decay, Sustain, Release. Shapes the output volume of every hit."

H2 "How the envelopes respond to triggers"
P "A rhythm hit is a trigger, not a held key, so the Attack-Decay-Sustain-Release envelopes (pitch, filter, and amp) behave a little differently from a keyboard synthesiser:"
Bullet "Attack and Decay run on every untied hit - the envelope retriggers from zero. For percussion this is usually all you hear: a fast pattern re-fires the attack before the envelope clears the decay stage, so it behaves like a classic attack-decay drum envelope."
Bullet "Sustain is the level the envelope holds once decay finishes, and you only reach it during Pattern Legato runs (see the Logic Bar). Tied hits skip the retrigger, so the envelope rides through the contiguous notes and settles at the Sustain level. On non-legato percussion the sustain level has little effect because the next hit retriggers first."
Bullet "Release is not a key-up fade. A triggered hit never sends a note-off, so the release stage is never entered during normal playback. Instead it sets how long a voice tails out when a rhythm or full-preset hot-swap retires it (see Hot-Swapping) - the retired voice fades over its release time so swaps stay smooth. For the same reason, Release is the one envelope stage that is not offered as a modulation target."
P "So a legato phrase holds at the Sustain level and plays the sample out (or is retriggered by the next hit) rather than fading on a key-up - there is no note-off to start a release. To make a sound die away between hits, shape it with Decay and Sustain rather than Release."

H2 "Insert (pink)"
P "A per-voice character insert placed in the voice chain after the amp envelope, so feedback-based algorithms (Karplus, Vocoder) can ring on past the envelope's release from their own internal state — a sustained Karplus pluck will continue after a short envelope decay. Available algorithms (alphabetical in the dropdown after None):"
Bullet "None — bypass with no CPU cost"
Bullet "3-Band EQ — low shelf, mid peak with adjustable centre frequency, and high shelf"
Bullet "Bitcrusher — sample-rate and bit-depth reduction with anti-aliasing and dither"
Bullet "Clipper — soft or hard clipping at adjustable threshold"
Bullet "Compressor — feed-forward dynamics (4:1 ratio)"
Bullet "Fold — triangular foldback (metallic harmonics)"
Bullet "Hard Clip — hard clipping (aggressive distortion)"
Bullet "Karplus — physical-modelling plucked-string synthesiser; samples excite the K-S delay line"
Bullet "Limiter — brick-wall limiting (100:1 ratio)"
Bullet "Ring Mod — sine-wave ring modulation"
Bullet "Soft Clip — tanh waveshaping (warm saturation)"
Bullet "Tape Sat — tanh saturation with DC block and tone shaping"
Bullet "Vocoder — voice/carrier vocoder with internal pitched carrier; 20 analysis bands"
Bullet "Vocoder St — stereo Vocoder; independent analysis on left/right channels for preserved width"
P "Each algorithm exposes four slot knobs that take on algorithm-specific meanings. For example in Bitcrusher mode the slots are Bits, Rate, Dither, and LPF; in 3-Band EQ mode they are Low, Mid, Mid Hz, and High; in Karplus mode they are Note, Octave, Feedback, and LPF. The knob labels update automatically when you switch algorithm. A Gain Reduction arc sweeps anti-clockwise over the Output knob when Compressor or Limiter is active."

H3 "Karplus"
P "Karplus excites an internal delay-line resonator with the incoming sample audio, producing the characteristic decaying pluck of a string. Note and Octave set the pitch (C1 to B4); Feedback sets the decay time (above 95 percent the string rings for several seconds; at 100 percent it is effectively infinite); LPF darkens the feedback path to produce a softer, faster-decaying tone. Because the resonator's feedback is independent of the amp envelope, the pluck rings naturally past short decays — pair a 300 ms amp decay with 80 percent feedback for percussive plucks that bloom into sustained tones."

H3 "Vocoder and Vocoder Stereo"
P "Vocoder analyses the incoming sample as a modulator signal and shapes a pitched internal carrier with the resulting spectral envelope across 20 bands. The Wave knob picks the carrier waveform (Sine, Saw, White, Pink); for Sine and Saw the Note and Octave knobs set the pitch and Unison stacks up to seven detuned voices for width. The two noise carriers (White, Pink) grey out Note, Octave, and Unison since they have no fundamental pitch. Vocoder Stereo uses the same algorithm but with independent analysis on left and right channels — preserves stereo width from the source sample at extra CPU cost."

H2 "Knob Interaction"
P "Drag up to increase, drag down to decrease. Double-click to type an exact value. Hover over any knob to see its name and current value in the Status Bar. A cyan ring around a knob indicates it is being modulated; an animated arc shows the live modulated value."

Pic "Voice Section showing all four columns with envelope rows visible."

# ── 11. Modulator Panel ───────────────────────────────────────────────────────
H1 "11. Modulator Panel"
P "The Modulator Panel fills the lower portion of the Rhythm Panel. Each rhythm has eight independent modulators (Mod A through Mod H) plus a Matrix tab."

H2 "Modulator Tabs"
P "Each modulator has:"
Bullet "Mode toggle: Smooth (continuous LFO curve) or Stepped (bar graph)"
Bullet "Polarity toggle: Unipolar (0 to +1) or Bipolar (-1 to +1)"
Bullet "Loop length: a note-value dropdown plus an integer multiplier (e.g. 1/4 x 4 = one bar)"
Bullet "Curve editor (smooth) or step editor (stepped)"
Bullet "A scrolling playhead line showing the current loop position"
Bullet "A target list with one row per assignment — each row shows a row number, a destination dropdown, a bipolar depth bar, and a source-side curve knob (drag to shape the modulation response: positive values are exponential, negative are logarithmic)"
Bullet "Pager arrows and a counter appear above the Add Target button when there are more assignments than fit on screen"
Bullet "Add Destination button"
P "In Smooth mode: click on the curve to add a node, drag to move, right-click to remove, ALT-click a segment to add a bezier handle. In Stepped mode: drag bars up or down. Step Length sets the duration of each bar."

H2 "Matrix Tab"
P "Lists every active modulation assignment for the current rhythm. Each row shows the source modulator, the destination parameter (grouped by Voice section), and a bipolar depth bar. Add or remove assignments here for an overview view."
P "Modulation targets are per-rhythm only — a modulator in one rhythm cannot affect another rhythm. Global FX parameters cannot be modulated."

Pic "Modulator Panel with the Smooth-mode curve editor visible and three destination targets assigned."

# ── 12. Mixer ─────────────────────────────────────────────────────────────────
H1 "12. Mixer"
P "Click the Mixer button in the Transport Bar to open the Mixer view. The sidebar remains visible."

H2 "Meter Mode"
P "A SegmentControl at the top of the mixer selects the metering standard applied to every channel, return, and master meter:"
Bullet "Peak — instant attack with peak hold and clip LED. Useful for catching transients."
Bullet "VU — 300 ms RMS ballistics matching the classic VU convention. 0 VU is marked at −18 dBFS."
Bullet "K-12 — K-system reference at −12 dBFS. Colour zones shift: green below 0 K, yellow to +4 K, red above."
Bullet "K-14 — K-system reference at −14 dBFS. Headroom reduced; suited to dynamics-heavy material."

H2 "Channel Strips"
P "The mixer always shows eight rhythm channels, even when fewer rhythms are active — inactive channels are dimmed. Channel order from left to right:"
Bullet "Rhythm 1 to Rhythm 8"
Bullet "Effect Return, Delay Return, Reverb Return"
Bullet "Master (slightly wider, with a Master Insert panel attached)"
P "Each rhythm strip contains, from top to bottom: a coloured header, the channel name, a sidechain section (source dropdown, Amount, Attack, Release), three FX send rotaries (Effect, Delay, Reverb), an output bus selector, a pan knob, a vertical fader with VU meter and gain-reduction meter, a level readout, and Mute and Solo buttons."

H2 "Sidechain Ducking"
P "Each rhythm channel can duck from any other channel. The source dropdown selects the trigger channel, Amount sets the depth (0 to 100 percent), and Attack and Release shape the envelope follower. When a channel is being ducked, a downward-filling gain-reduction meter appears next to its main VU meter."

H2 "FX Send Behaviour"
P "All three FX sends (Effect, Delay, Reverb) on rhythm channels are standard parallel sends. The dry signal always passes to the master bus at the channel fader level, and the send knob adds a copy of the channel signal — scaled by the send amount — into the FX return bus. Turning a send up never reduces the dry signal."

H2 "Master Insert"
P "A compact insert panel attached to the Master strip provides the same algorithm set as the per-rhythm voice insert (None plus the thirteen effects, including Karplus and the Vocoders). The Master Insert is placed post-master-fader and runs once on the summed output."

H2 "FX Rows"
P "Three bordered panels below the channel strips for Effect, Delay, and Reverb. Each has an on/off toggle, an algorithm dropdown, and parameter knobs. See Chapter 13."

H2 "Multi-Bus Output (DAW)"
P "When running as a VST3 or CLAP plugin, mu-Clid exposes up to ten stereo output buses: Master, eight Direct Outs, and FX Returns. Each rhythm channel has an output bus selector — channels routed to a Direct Out bypass the master mix and FX sends, going straight to their bus post-fader. The multi-bus feature can be toggled in the Settings overlay; turning it off forces the host to a single stereo bus on next reload."

Pic "Mixer view with all eight rhythm channels, three FX returns, and the master strip visible."

# ── 13. Effects ───────────────────────────────────────────────────────────────
H1 "13. Effects"

H2 "Effect Slot"
P "Hosts one of four algorithms:"
Bullet "Chorus — Rate, Depth, Voices (2 to 4), Spread, Mix. Catmull-Rom Hermite-interpolated multi-voice chorus with per-voice rate detuning."
Bullet "Flanger — Rate, Depth, Feedback (bipolar), Mix. Through-zero flanger so the sweep can pass through unity."
Bullet "Phaser — Rate, Depth, Stages (up to 12), Feedback, Mix. Notches sweep logarithmically from 200 Hz to 4 kHz."
Bullet "Echo — Time, Feedback, Spread, Mix. Free-time delay (use the Delay slot for tempo sync)."

H2 "Delay Slot"
P "Tempo-synchronised stereo delay:"
Bullet "Time — note value selector (1/32 through 1/4) with Straight, Dotted, or Triplet modifier; or free-mode in milliseconds"
Bullet "Feedback — number of repeats"
Bullet "Spread — slightly different left and right channel times for stereo widening"
Bullet "Dirt — soft saturation on the feedback path for tape-degraded character"
P "Hermite cubic interpolation on the read pointer prevents zipper noise when the delay time changes (BPM nudges, LFO automation)."

H2 "Reverb Slot"
P "A Hadamard FDN reverb (Signalsmith-DSP) with four characters:"
Bullet "Room — short, intimate"
Bullet "Hall — long, diffuse"
Bullet "Plate — bright, dense"
Bullet "Spring — bouncy, modulated"
P "Controls: Size, Pre-delay, Diffusion, Damp, Mod, Dirt. Reverb is always a pure send — there is no Mix knob."

Pic "Effect, Delay, and Reverb FX rows side by side in the Mixer view."

# ── 14. Presets ───────────────────────────────────────────────────────────────
H1 "14. Presets"

H2 "Saving a Preset"
P "Click Save in the Transport Bar to open the save dialog. Enter a name, optional description, and category. Tick Embed Samples to include all loaded sample audio as base64 data inside the preset file, making it fully self-contained. Click Save to write a .muClid file to your Presets folder."

H2 "Loading a Preset"
P "Select a preset from the dropdown in the Transport Bar. All rhythms, voice parameters, and sample references are restored immediately. If a sample cannot be found at its original path, mu-Clid searches the Samples folder in your content directory as a fallback. The preset dropdown shows <unnamed preset> when no preset is currently loaded."

H2 "Hot-swap During Playback"
P "Loading a single-rhythm .muRhythm file while playback is running stages the swap until the next loop boundary, so the change happens in time. The Settings overlay lets you choose between On Master Loop and On Rhythm Loop timing. A pending swap is shown by an orange SWP badge on the affected rhythm in the sidebar — click the badge to cancel."
P "FX tails survive the swap: when the new rhythm takes over, the old rhythm's sample continues playing through its amp envelope's release, and any filter resonance, comb feedback, or Karplus / vocoder feedback from its insert effect rings out naturally. The retired rhythm fades over its envelope's release time plus a short drain budget for feedback-based inserts (roughly two seconds), so transitions feel musical even when the old sample has a long tail or the insert effect was ringing at the moment of swap."

H2 "Rhythm Presets"
P ".muRhythm files are single-rhythm presets stored in the Rhythms folder. They can be saved with embedded samples and loaded into any rhythm slot. Save a rhythm preset to reuse a particular pattern and voice combination across multiple full presets."
P "To load a rhythm preset, use the dropdown in the Rhythm Panel header (labelled 'rhythm preset…'). The dropdown lists all .muRhythm files in your Rhythms folder alphabetically. Selecting one loads it immediately, or stages a hot-swap if playback is running. Right-clicking an item offers Load and Delete options."

H2 "Preset File Format"
P ".muClid and .muRhythm files are XML, with a MuClidPreset (or single Rhythm) root element. They can be inspected and edited with any text editor. Embedded sample audio is stored as base64 within the relevant Rhythm element."

# ── 15. MIDI ──────────────────────────────────────────────────────────────────
H1 "15. MIDI Integration"

H2 "MIDI Clock Sync (Standalone)"
P "When running as a standalone application, mu-Clid can lock to an external MIDI clock source. Open Settings and set Clock Source to MIDI In, choose which messages to honour (Clock only, Start/Stop/Continue, or Both), and select the MIDI input device. With external sync active the BPM display becomes read-only and shows the estimated tempo derived from incoming clock ticks."

H2 "MIDI Program Change to Preset (DAW)"
P "When running as a VST3 or CLAP plugin, incoming program change messages on MIDI channels 1 to 8 can load .muRhythm presets into rhythm slots 1 to 8 (channel N maps to slot N). The preset list is configured in the MIDI Presets panel — accessed from Settings — and stored as a JSON file alongside the plugin state. Hot-swap timing follows the swap-mode setting."

# ── 16. Settings ──────────────────────────────────────────────────────────────
H1 "16. Settings"
P "Click the gear icon in the Transport Bar to open the Settings overlay. Sections include:"
Bullet "Visual — hit pulse style, ring expansion size, sidebar and centre hub pulse, step dot size"
Bullet "Sequencer — host sync behaviour, hot-swap timing"
Bullet "Performance — interpolation quality (Lo-fi / Linear / Clean), oversampling quality"
Bullet "Voice — overlap fade length for voice transitions"
Bullet "Gain — default channel and master fader levels"
Bullet "Presets — content folder location, default preset"
Bullet "MIDI — clock source and message filtering (standalone), MIDI Presets panel (DAW)"
Bullet "Multi-bus — enable or disable multi-bus output (DAW); host rescan required after toggling"
Bullet "Standalone — audio device selection"

Pic "Settings overlay open over the main window."

# ── 17. Tips and Workflow ─────────────────────────────────────────────────────
H1 "17. Tips and Workflow"
Bullet "Polyrhythm: Set Ring A to 16 steps / 4 hits and Ring B to 12 steps / 3 hits with Logic set to OR — a 16-against-12 polyrhythm that locks every 48 steps."
Bullet "Accents with Ring C: Give Ring C a different step count to A and B and set a few hits. The Accent knob in the Amp column adds a dB boost whenever Ring C coincides with an A+B hit."
Bullet "Groove via Pre Pad: Use Pre Pad on Ring A to push the whole pattern slightly later in the bar, similar to adding swing."
Bullet "Sidechain pumping: Set rhythm 2's sidechain source to rhythm 1, dial Amount to 60-80%, and use a fast attack with a slow release for classic kick-ducks-bass."
Bullet "Self-contained sharing: Tick Embed Samples when saving to share a preset with collaborators who may not have your sample library."
Bullet "Default content: Save a preset as _default.muClid to have it load every session. Save a rhythm as _default.muRhythm to use it as the template for every new rhythm slot."
Bullet "Status Bar: Hover over any knob to see its name and current value without clicking — the Status Bar never clears automatically."
Bullet "DAW automation: Every per-rhythm parameter is exposed to the host with human-readable names. Recorded automation moves the on-screen knobs in real time."

# ── 18. Connecting to mu-link ─────────────────────────────────────────────────
H1 "18. Connecting to mu-link"
P "mu-link is a companion application from Transwarp Development Project: a local audio hub and master clock for the mu family. When mu-link is running, the mu-Clid standalone application connects to it automatically — mu-Clid's audio is summed into mu-link's mix and its transport locks to mu-link's master clock. The plug-in versions are unaffected; only the standalone connects, because in a DAW the host already owns the clock and audio device."
H2 "How to Connect"
Bullet "Launch mu-link, then launch mu-Clid standalone — in either order. Within about a second the mu-Clid title bar shows 'mu-link connected'."
Bullet "mu-Clid's audio now plays through mu-link's output device, summed with any other connected mu apps. mu-Clid appears as a channel in mu-link's client strip with its own level, pan, mute, and solo."
Bullet "mu-Clid's transport follows mu-link: mu-link is the tempo master, and its Play/Stop and tempo drive mu-Clid. Use mu-link's transport (not mu-Clid's) while connected."
Bullet "Quit mu-link and mu-Clid instantly reverts to its own audio device and internal transport — no restart needed."
P "This lets you run mu-Clid, mu-Tant, and other mu standalones together, perfectly in sync and mixed to a single output — and lets mu-link slave the whole rig to an external MIDI clock from a drum machine or DAW. See the mu-link User Manual for the full picture."

# ── 19. Technical Specifications ──────────────────────────────────────────────
H1 "19. Technical Specifications"
Bullet "Formats: VST3, CLAP, Standalone (Windows)"
Bullet "Rhythms: up to 8 simultaneous slots"
Bullet "Steps per generator: 1 to 64"
Bullet "Voices per rhythm: 4-voice polyphonic pool with round-robin steal"
Bullet "Filter types: 16 (LP / HP / BP at 6, 12, and 24 dB per octave; Notch 12/24; AP 12; Comb + / Comb −; Peak; Lo Shelf; Hi Shelf)"
Bullet "Insert effects: 13 plus None (Soft Clip, Hard Clip, Fold, Bitcrusher, Clipper, 3-Band EQ, Compressor, Limiter, Ring Mod, Tape Sat, Karplus, Vocoder, Vocoder Stereo) — on both the per-rhythm voice insert and the master insert"
Bullet "Modulators per rhythm: 8 (smooth LFO or stepped)"
Bullet "Modulation assignments: up to 64 per rhythm"
Bullet "Output buses (DAW): up to 10 stereo (Master + 8 Direct Outs + FX Returns)"
Bullet "Supported sample formats: WAV, AIFF, MP3, FLAC"
Bullet "Preset format: .muClid (XML)"
Bullet "Rhythm preset format: .muRhythm (XML)"
Bullet "Third-party libraries: JUCE, Signalsmith Reverb (MIT), Monocypher (BSD-2-Clause), clap-juce-extensions (MIT)"

# ── Save ──────────────────────────────────────────────────────────────────────
$outPath = "d:\Dev\mu\docs\mu-clid\mu-Clid User Manual.docx"
$doc.SaveAs([ref]$outPath, [ref]16)
$doc.Close()
$word.Quit()
[System.Runtime.InteropServices.Marshal]::ReleaseComObject($word) | Out-Null
Write-Host "Saved: $outPath"
