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

# ── Title page ──────────────────────────────────────────────────────────────
$sel.Style = $doc.Styles("Title")
$sel.TypeText("mu-Clid")
$sel.TypeParagraph()
$sel.Style = $doc.Styles("Subtitle")
$sel.TypeText("Euclidean Rhythm Sequencer")
$sel.TypeParagraph()
$sel.Style = $doc.Styles("Normal")
$sel.TypeText("User Manual")
$sel.TypeParagraph()
$sel.TypeText("Transwarp Development Project")
$sel.TypeParagraph()
Br

# ── 1. Introduction ──────────────────────────────────────────────────────────
H1 "1. Introduction"
P "mu-Clid is a polyrhythmic Euclidean rhythm sequencer and sample trigger plugin by Transwarp Development Project, available as a VST3 plug-in, CLAP plug-in, and standalone application for Windows."
P "mu-Clid lets you build up to eight simultaneous rhythm tracks. Each track is driven by up to three independent Euclidean generators (Ring A, Ring B, and Ring C). Each rhythm triggers a sample through a voice chain including pitch control, a resonant filter, amplitude envelope, and a per-voice drive insert. Eight LFO/step modulators per rhythm allow deep modulation of any voice parameter. A shared FX chain provides a modulation or time effect, a BPM-synchronised delay, and a reverb, all mixed through a full channel-strip mixer."

H2 "Who Is This For?"
P "mu-Clid is designed for producers and sound designers who work with rhythmic textures, polyrhythmic patterns, and generative percussion. It is equally at home in a full DAW session or as a standalone performance instrument."

# ── 2. Installation ──────────────────────────────────────────────────────────
H1 "2. Installation"
H2 "Plug-in Files"
P "mu-Clid ships with three formats:"
Bullet "VST3  —  copy mu-Clid.vst3 to %ProgramFiles%\Common Files\VST3\"
Bullet "CLAP  —  copy mu-Clid.clap to %ProgramFiles%\Common Files\CLAP\"
Bullet "Standalone  —  run mu-Clid.exe from any location"

H2 "Content Folder"
P "On first launch mu-Clid creates a content folder at:"
P "    %USERPROFILE%\Documents\TDP\muClid\"
P "Inside this folder there are three sub-folders:"
Bullet "Presets\   —  full preset files (.muclid)"
Bullet "Rhythms\   —  single-rhythm preset files (.muRhyth)"
Bullet "Samples\   —  reference location for sample files"
P "You can change the content folder path in the Settings overlay at any time."

H2 "Default Presets"
P "If a file named _default.muclid exists in your Presets folder it is loaded silently on startup. If _default.muRhyth exists in your Rhythms folder it is applied whenever you add a new rhythm. Save over either file to create a personal default."

# ── 3. Interface Overview ─────────────────────────────────────────────────────
H1 "3. Interface Overview"
P "The mu-Clid window is divided into four regions:"
Bullet "Transport Bar  (top, 36 px)  —  playback, BPM, presets, and global controls"
Bullet "Rhythm Sidebar  (left, 82 px)  —  rhythm slots and add/remove controls"
Bullet "Main Panel  (remaining area)  —  the selected rhythm or the Mixer Overlay"
Bullet "Status Bar  (bottom, 20 px)  —  live readout of the last touched control"
P "The default window size is 1170 x 870 pixels. It can be resized between 780 x 580 (minimum) and 2400 x 1600 (maximum). All elements scale proportionally."

# ── 4. Transport Bar ──────────────────────────────────────────────────────────
H1 "4. Transport Bar"
P "Controls from left to right:"
Bullet "mu-CLID logo  —  click to open the About panel"
Bullet "Play / Stop  —  in DAW mode reflects the host transport; in standalone drives the internal clock"
Bullet "BPM display  —  read-only in DAW mode; drag or double-click to edit in standalone mode; tap the label rapidly to tap-tempo"
Bullet "Position  —  bar and beat (e.g. 3.2). Read-only in DAW mode; resets on stop in standalone."
Bullet "Sync pill  —  indicates whether playback is locked to the host timeline or free-running"
Bullet "Rhythm count  —  active rhythms out of maximum (e.g. 3/8)"
Bullet "Master Loop dropdown  —  sets how many steps the entire pattern runs before looping. Choices range from 16 steps to 512 steps, or infinity (free-running, shown as an infinity symbol)."
Bullet "Preset dropdown  —  select a saved preset to load instantly"
Bullet "Save button  —  opens the Save Preset dialog"
Bullet "Mixer button  —  toggles the Mixer Overlay. Button label changes to 'Sequencer' when the mixer is active."
Bullet "Settings (gear) icon  —  opens the Settings overlay"

# ── 5. Rhythm Sidebar ─────────────────────────────────────────────────────────
H1 "5. Rhythm Sidebar"
P "The sidebar shows one item per active rhythm. Each item displays a miniature Rhythm Circle, the rhythm name, and its colour dot. Click an item to select it and display it in the main panel."
P "The selected item shows a vertical accent bar in the rhythm colour on its right edge, connecting it visually to the main panel."
P "When a rhythm fires a hit, its sidebar item pulses with the rhythm colour."
P "The dashed Add button at the bottom of the sidebar creates a new rhythm slot (maximum 8). The button is disabled when 8 rhythms are active."
P "To delete a rhythm, use the Delete button (X) in the Rhythm Panel header. A confirmation popup appears before the rhythm is removed."

# ── 6. Rhythm Panel ───────────────────────────────────────────────────────────
H1 "6. Rhythm Panel"
P "The Rhythm Panel is the main editing surface for the selected rhythm, divided into five sections from top to bottom."

H2 "Header Bar"
P "Shows a coloured accent strip, the rhythm name (double-click to rename), and action buttons (mute, solo, delete). A colour dot on the left shows the assigned rhythm colour."

H2 "Sample Bar"
P "Drag a sample file onto this bar or click it to browse. Supported formats: WAV, AIFF, MP3, FLAC. Once loaded the filename is shown. A missing-sample warning appears in amber. Click the '...' icon to locate a replacement."

H2 "Rhythm Circle and Euclidean Panel"
P "The upper section splits width between the circular step display (left) and the Euclidean parameter panel (right). See Chapters 7 and 8."

H2 "Voice Section"
P "A fixed-height strip with four columns: Pitch, Filter, Amp, and Insert. See Chapter 9."

H2 "Modulator Panel"
P "Fills the remaining height. Contains tabs for up to eight modulators and a Matrix tab. See Chapter 10."

# ── 7. Rhythm Circle ──────────────────────────────────────────────────────────
H1 "7. Rhythm Circle"
P "The Rhythm Circle shows concentric rings, one per generator or modulator. Rings are drawn outside-in in this order:"
Bullet "Ring A (outermost)  —  purple. Primary Euclidean generator."
Bullet "Ring B  —  coral. Secondary Euclidean generator."
Bullet "Ring C  —  amber dashed outline. Accent layer."
Bullet "Modulator rings  —  colour-coded by assignment."
P "During playback the rings rotate so the current step is always at the 12 o'clock position. Step 1 is at the top when the transport is at bar 1, beat 1."

H2 "Step Colours"
Bullet "Filled in ring colour  —  a hit step"
Bullet "Dim outline  —  an empty step"
Bullet "Cyan-teal fill  —  a pre-padded step (forced silence before the pattern)"
Bullet "Teal fill  —  a post-padded step (forced silence after the pattern)"
Bullet "Pink fill  —  an insert-padded step (silence within the pattern)"
P "On each hit a radial pulse expands from the step arc position and the centre hub briefly flashes in the rhythm colour."

# ── 8. Euclidean Panel ────────────────────────────────────────────────────────
H1 "8. Euclidean Panel"
P "The Euclidean Panel sits to the right of the Rhythm Circle with three rows of knobs (A, B, C) and a Logic bar between A and B."

H2 "Euclid A and Euclid B"
P "Both rows share identical controls:"
Bullet "Steps (1-64)  —  total number of steps including all padding"
Bullet "Hits (0-Steps)  —  hits distributed using the Euclidean (Bjorklund) algorithm"
Bullet "Rotate  —  shifts the pattern left or right"
Bullet "Pre Pad (0-12)  —  empty steps forced before the pattern"
Bullet "Post Pad (0-12)  —  empty steps forced after the pattern"
Bullet "Insert Start  —  start position of the insert zone within the pattern"
Bullet "Insert Length (0-8)  —  number of steps in the insert zone"
Bullet "Insert Mode  —  Pad (steps excluded from hit distribution) or Mute (hits distributed but silenced)"
P "Ranges update dynamically: when Steps changes, the Hits, Rotate, and Insert Start ranges clamp to match."

H2 "Logic Bar"
P "Five-pill selector between rows A and B. Sets how the two patterns combine:"
Bullet "OR  —  fires if either A or B fires"
Bullet "AND  —  fires only when both A and B fire simultaneously"
Bullet "XOR  —  fires when exactly one of A or B fires"
Bullet "A Only  —  only A fires; B is ignored"
Bullet "B Only  —  only B fires; A is ignored"

H2 "Euclid C — Accent Layer"
P "Three knobs: Steps, Hits, Rotate. No logic relationship with A or B. When Ring C fires a hit on the same step as a combined A+B hit, that step is accented. The accent gain boost (in dB) is set by the Accent knob in the Amp column of the Voice Section."

# ── 9. Voice Section ──────────────────────────────────────────────────────────
H1 "9. Voice Section"
P "A fixed-height strip below the Rhythm Circle with four columns: Pitch, Filter, Amp, and Insert. Each column has a configuration row (top) and an envelope row (bottom)."

H2 "Pitch (purple)"
P "Config row: Octave (-4 to +4), Semitone (-12 to +12), Fine (-100 to +100 cents)."
P "Envelope row: Attack, Decay, Sustain, Release, and Depth (0-24 semitones). The envelope sweeps the pitch upward from the base pitch by the Depth amount."

H2 "Filter (teal)"
P "Config row: Type (Low Pass / High Pass / Band Pass), Cutoff (20 Hz to 20 kHz), Resonance (0-0.99)."
P "Envelope row: Attack, Decay, Sustain, Release, and Depth (0-48 semitones of cutoff sweep)."

H2 "Amp (amber)"
P "Config row: Level (0-2 linear), Accent (0 to +12 dB extra gain on accented steps), and send knobs (Effect, Delay, Reverb)."
P "Envelope row: Attack, Decay, Sustain, Release. Shapes the output volume of every hit."

H2 "Insert (pink)"
P "A per-voice character insert placed after the filter and before the amp envelope."
P "Character dropdown:"
Bullet "None  —  bypass with no CPU cost"
Bullet "Soft  —  tanh waveshaping (warm saturation)"
Bullet "Hard  —  hard clipping (aggressive distortion)"
Bullet "Fold  —  triangular foldback (metallic harmonics)"
Bullet "Bitcrusher  —  sample-rate and bit-depth reduction"
P "Additional controls: Drive (0-100%), Output (-24 to 0 dB level trim), LPF (one-pole low-pass on driven signal)."
P "In Bitcrusher mode the second knob shows Bits (1-16 bit depth) and the third shows Rate (100 Hz to 48 kHz target sample rate)."
P "Setting Drive to 0% passes audio through unchanged regardless of Character."

H2 "Knob Interaction"
P "Drag up to increase, drag down to decrease. Double-click to type an exact value. Hover over any knob to see its name and value in the Status Bar."

# ── 10. Modulator Panel ───────────────────────────────────────────────────────
H1 "10. Modulator Panel"
P "The Modulator Panel fills the lower portion of the Rhythm Panel. Each rhythm has eight independent modulators (Mod A through Mod H) and a Matrix tab."

H2 "Modulator Tabs"
P "Each modulator has:"
Bullet "Smooth / Stepped mode toggle"
Bullet "Loop Length — note value and multiplier (e.g. 1/4 x 4 = one bar)"
Bullet "Curve editor (smooth) or bar graph (stepped)"
Bullet "A scrolling playhead line showing current loop position"
Bullet "Target list — one row per assignment with a destination dropdown and bipolar depth bar"
Bullet "Add Destination button"
P "In smooth mode: click on the curve to add nodes, drag to move, right-click to remove, ALT-click a segment to add a bezier handle."
P "In stepped mode: drag bars up or down. Step Length sets the duration of each bar."

H2 "Matrix Tab"
P "Shows all active modulation assignments for the current rhythm. Each row shows the source modulator, the destination parameter, and a bipolar depth bar. Remove or add assignments here."
P "Modulation targets are per-rhythm only. Global FX parameters cannot be modulated."

# ── 11. Mixer ─────────────────────────────────────────────────────────────────
H1 "11. Mixer"
P "Click the Mixer button in the Transport Bar to open the Mixer Overlay. The sidebar remains visible."

H2 "Channel Strips"
P "The mixer always shows 8 rhythm channels, even if fewer rhythms are active. Inactive channels are dimmed. Channel order left to right:"
Bullet "Rhythm 1-8"
Bullet "Divider"
Bullet "Effect Return, Delay Return, Reverb Return"
Bullet "Divider"
Bullet "Master (slightly wider)"
P "Each strip contains (top to bottom): 3 px colour bar, channel name, send rotaries, pan knob, vertical fader + VU meter, level readout, Mute and Solo buttons."

H2 "FX Send Behaviour"
P "For Effect and Delay sends on rhythm channels:"
Bullet "0-50%: dry stays at 100%; wet blends from 0 to 100%"
Bullet "50-100%: wet stays at 100%; dry fades from 100% to 0%"
P "Reverb is always a pure send: increasing the Reverb send does not reduce the dry signal."

H2 "FX Rows"
P "Three bordered panels below the channel strips for Effect, Delay, and Reverb. Each has an on/off toggle, an algorithm dropdown, and parameter knobs."

# ── 12. Effects ───────────────────────────────────────────────────────────────
H1 "12. Effects"
H2 "Effect Slot"
P "Hosts one of four algorithms:"
Bullet "Chorus  —  Rate, Depth, Voices (2-4), Spread, Mix"
Bullet "Flanger  —  Rate, Depth, Feedback (bipolar), Mix"
Bullet "Phaser  —  Rate, Depth, Stages (up to 12), Feedback, Mix"
Bullet "Echo  —  Time, Feedback, Spread, Mix  (no BPM sync; use Delay for sync)"

H2 "Delay Slot"
P "Tempo-synchronised stereo delay:"
Bullet "Time  —  note value selector or milliseconds (free mode)"
Bullet "Feedback  —  number of repeats"
Bullet "Spread  —  slightly longer right channel delay for stereo widening"
Bullet "Dirt  —  soft saturation on the feedback path for tape-degraded character"

H2 "Reverb Slot"
P "Algorithmic reverb with four characters: Room, Hall, Plate, Spring. Controls: Size, Pre-delay, Diffusion, Damp, Mod, Dirt. Reverb is always a pure send effect — there is no Mix knob."

# ── 13. Presets ───────────────────────────────────────────────────────────────
H1 "13. Presets"
H2 "Saving a Preset"
P "Click Save in the Transport Bar to open the save dialog. Enter a name, optional description, and category. Tick 'Embed samples' to include all loaded sample audio as base64 data inside the preset file, making it fully self-contained. Click Save to write a .muclid file to your Presets folder."

H2 "Loading a Preset"
P "Select a preset from the dropdown in the Transport Bar. All rhythms, voice parameters, and sample references are restored immediately. If a sample cannot be found at its original path, mu-Clid searches the Samples folder in your content directory as a fallback."

H2 "Preset File Format"
P ".muclid files are XML files containing a MuClidPreset root element with one Rhythm child element per active rhythm. Each Rhythm element stores all voice parameters, Euclidean settings, name, colour index, and sample path. Samples embedded at save time are stored as base64 within their Rhythm element."

H2 "Rhythm Presets"
P ".muRhyth files are single-rhythm presets stored in the Rhythms folder. They follow the same internal structure as a single rhythm within a .muclid file. Save a rhythm preset to reuse a particular pattern and voice combination across multiple full presets."

# ── 14. Settings ──────────────────────────────────────────────────────────────
H1 "14. Settings"
P "Click the gear icon in the Transport Bar to open the Settings overlay. Sections:"
Bullet "Visual  —  hit pulse style, ring expansion size, sidebar and centre hub pulse, step dot size"
Bullet "Sequencer  —  host sync behaviour, display of milliseconds alongside musical divisions"
Bullet "Performance  —  interpolation quality (Lo-fi / Linear / Clean), oversampling quality"
Bullet "Voice  —  overlap fade length for voice transitions (1-10 ms, default 2 ms)"
Bullet "Gain  —  default fader level and default master volume"
Bullet "Presets  —  default preset path, restore factory presets, change content folder"
Bullet "Standalone  —  audio device selection"

# ── 15. Tips ──────────────────────────────────────────────────────────────────
H1 "15. Tips and Workflow"
Bullet "Polyrhythm: Set Ring A to 16 steps / 4 hits and Ring B to 12 steps / 3 hits with Logic set to OR for a 16-against-12 polyrhythm that locks every 48 steps."
Bullet "Accents with Ring C: Give Ring C a different step count to A/B and set a few hits. The Accent knob in the Amp column adds a dB boost whenever Ring C coincides with an A+B hit."
Bullet "Groove with Pre Pad: Use Pre Pad on Ring A to push the whole pattern slightly later in the bar, similar to adding swing."
Bullet "Self-contained sharing: Tick 'Embed samples' when saving a preset to share with collaborators who may not have your sample library."
Bullet "Status Bar: Hover over any knob to see its name and current value without clicking. The Status Bar never clears automatically."
Bullet "Default content: Save a preset as _default.muclid to have it load automatically every session."

# ── 16. Technical Specifications ──────────────────────────────────────────────
H1 "16. Technical Specifications"
Bullet "Formats: VST3, CLAP, Standalone (Windows)"
Bullet "Rhythms: up to 8 simultaneous slots"
Bullet "Steps per generator: 1-64"
Bullet "Voices per rhythm: 4-voice polyphonic pool with round-robin steal"
Bullet "Modulators per rhythm: 8 (smooth LFO or stepped)"
Bullet "Modulation assignments: up to 64 per rhythm"
Bullet "Supported sample formats: WAV, AIFF, MP3, FLAC"
Bullet "Preset format: .muclid (XML)"
Bullet "Rhythm preset format: .muRhyth (XML)"
Bullet "Third-party libraries: JUCE, SoundTouch (LGPL, DLL), Signalsmith Reverb (MIT)"

# ── Save ──────────────────────────────────────────────────────────────────────
$outPath = "d:\Dev\mu-clid\docs\mu-Clid User Manual.docx"
$doc.SaveAs([ref]$outPath, [ref]16)
$doc.Close()
$word.Quit()
[System.Runtime.InteropServices.Marshal]::ReleaseComObject($word) | Out-Null
Write-Host "Saved: $outPath"
