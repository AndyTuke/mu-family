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
$sel.TypeText("mu-link")
$sel.TypeParagraph()
$sel.Style = $doc.Styles("Subtitle")
$sel.TypeText("Audio Hub and Master Clock for the mu Family")
$sel.TypeParagraph()
$sel.Style = $doc.Styles("Normal")
$sel.TypeText("User Manual")
$sel.TypeParagraph()
$sel.TypeText("Transwarp Development Project")
$sel.TypeParagraph()
Br

# ── 1. Introduction ──────────────────────────────────────────────────────────
H1 "1. Introduction"
P "mu-link is a companion application from Transwarp Development Project. It does two jobs for the mu family of instruments. First, it is a local audio hub: several mu standalone applications — mu-Clid, mu-Tant, and future siblings — send their audio to mu-link, which sums them and plays the mix through one hardware output. Second, it is the family's master clock: mu-link publishes a rock-solid, sample-accurate transport that every connected app locks to, so they all play in perfect sync."
P "mu-link is a standalone application only — there is no plug-in version. It is the thing your mu instruments connect to, not a thing you load into a DAW."

H2 "Who Is This For?"
P "mu-link is for performers and producers who want to run more than one mu instrument at once — for example a rhythmic mu-Clid alongside an evolving mu-Tant drone — locked tightly together and mixed to a single output, without a DAW. It is built for the stage: it can also slave the whole rig to an external MIDI clock from a drum machine, groovebox, or DAW, and send MIDI clock out to outboard gear."

Pic "mu-link window: device picker on the left, transport and clock controls, and the client meter strip."

# ── 2. The Concept ───────────────────────────────────────────────────────────
H1 "2. How It Works"
P "Running two standalone audio apps at once normally means two separate connections to your soundcard, each with its own clock — which drift apart over time and cannot share one output cleanly. mu-link solves this the way professional audio systems do: it owns the one hardware output, and the apps send their audio to it through a fast shared-memory connection on the same machine."
P "Because mu-link owns the device, the soundcard's own clock becomes the single master clock for everything. Every connected app renders exactly in step with it, so there is no drift and no jitter — the sync is sample-accurate. The apps each render slightly ahead into a buffer, so a momentary hiccup in one app can never click or glitch the others."
P "mu-link is NOT a system-wide virtual sound card or audio driver. It only connects mu-family apps, by design — that keeps it simple, reliable, and driver-free."

# ── 3. Installation ──────────────────────────────────────────────────────────
H1 "3. Installation"
P "mu-link ships as a single executable — mu-link.exe. There is no installer: copy it anywhere and run it. It is a standalone application with no plug-in formats."
P "Run only ONE audio server at a time. mu-link and its companion testing tools all share the same connection points, so two running at once will conflict. Launch a single mu-link.exe."

# ── 4. Interface Overview ─────────────────────────────────────────────────────
H1 "4. Interface Overview"
P "The mu-link window has two areas:"
Bullet "Audio Setup (left) — the device picker: output device, sample rate, buffer size, and MIDI input/output selection."
Bullet "Bus (right) — the master transport, the clock-source toggle, the master tempo, and a mixer-style strip with one meter per connected app plus the summed Master."
P "The window is resizable."

Pic "Annotated window showing the device picker, transport row, and meter strip."

# ── 5. Choosing an Audio Device ───────────────────────────────────────────────
H1 "5. Choosing an Audio Device"
P "Use the Audio Setup panel on the left to pick the output device mu-link plays through. This is the device the whole rig is summed to."
Bullet "Output — choose your soundcard or audio interface. For the lowest latency choose a low-latency driver (WASAPI Exclusive, or ASIO if your interface provides it)."
Bullet "Sample rate and buffer size — mu-link dictates these to every connected app, so the whole rig runs at one sample rate. A smaller buffer gives lower latency at the cost of more CPU."
Bullet "MIDI input — enable the port carrying MIDI clock if you want mu-link to slave to external gear (see External MIDI Clock Sync)."
Bullet "MIDI output — choose a port to send MIDI clock out to outboard gear (see MIDI Clock Out)."
P "If no device opens at launch (for example the default is in use), just pick a working one here — mu-link starts driving the bus as soon as a device opens; no restart is needed."

Pic "Audio Setup panel with an output device and a MIDI input selected."

# ── 6. Connecting Your mu Apps ────────────────────────────────────────────────
H1 "6. Connecting Your mu Apps"
P "Connecting is automatic — there is nothing to configure."
Bullet "Launch mu-link, then launch any mu standalone app (mu-Clid, mu-Tant, …) — in either order."
Bullet "Within about a second the app detects mu-link and attaches. The app's title bar shows 'mu-link connected', and it appears as a named strip in mu-link's meter section."
Bullet "From then on the app's audio is summed into mu-link's output, and its transport follows mu-link's master clock."
Bullet "Quit mu-link and every app instantly reverts to its own audio device and internal clock — no restart needed."
P "Only the STANDALONE version of each app connects. The VST3 and CLAP plug-in versions are never affected — inside a DAW the host already owns the clock and audio device, so a plug-in never attaches to mu-link."
P "Up to eight apps can be connected at once. If an app is closed or crashes, mu-link automatically frees its strip after a moment."

Pic "Two connected apps (mu-Clid and mu-Tant) shown as named strips with live meters."

# ── 7. The Mixer ──────────────────────────────────────────────────────────────
H1 "7. The Mixer"
P "Each connected app gets a channel strip in the meter section, plus a Master strip on the right. From top to bottom a strip has a level meter, the app's name, a gain knob, and Mute (M) and Solo (S) buttons."
Bullet "Gain — sets the app's level in the mix."
Bullet "Mute (M) — silences that app in the mix."
Bullet "Solo (S) — hears only the soloed app(s); everything else is muted while any Solo is active."
Bullet "Master — a gain knob and meter for the summed output."
P "The master output is protected by a safety limiter, so the summed mix can never harshly clip the soundcard no matter how hot the connected apps run. Normal levels pass through untouched."

Pic "Meter strip with several app channels (gain, M, S) and the Master strip."

# ── 8. The Master Clock ───────────────────────────────────────────────────────
H1 "8. The Master Clock"
P "mu-link is the tempo master for everything connected to it. Its Play / Stop button and tempo drive every connected app's sequencer, gate, and modulators in lockstep."
Bullet "Play / Stop — starts and stops the whole rig together."
Bullet "Tempo — the master BPM. While mu-link is the clock source (Internal), set it here; every connected app follows."
P "Because the clock is derived from the audio device, it is sample-accurate — connected apps stay perfectly aligned with no drift, however long you play."

# ── 9. External MIDI Clock Sync ───────────────────────────────────────────────
H1 "9. External MIDI Clock Sync"
P "mu-link can hand the tempo reins to an external device — a drum machine, groovebox, or DAW — by following its MIDI clock. This is essential for fitting mu-link into a larger live rig."
H2 "Setting It Up"
Bullet "In Audio Setup, enable the MIDI input port that carries the external clock."
Bullet "Set the clock-source toggle in the transport row to Ext MIDI (it reads 'Clock: Internal' by default; click to switch to 'Clock: Ext MIDI')."
Bullet "Start your external device. mu-link follows its tempo, and its Start / Stop / Continue messages drive mu-link's transport."
P "While slaved, the tempo field shows the detected BPM and is read-only, and the local Play / Stop is driven by the incoming clock."
H2 "Why It Stays Rock Solid"
P "Raw MIDI clock is slightly jittery, so mu-link never uses it directly as its timebase. Instead it measures the incoming clock and smooths it into a stable tempo estimate, then drives its own sample-accurate audio clock at that tempo. The connected apps see the same clean, drift-free transport they always do — they never inherit MIDI jitter. The result is tight external sync without sacrificing timing stability."

Pic "Transport row with the clock-source toggle set to Ext MIDI and a detected BPM shown."

# ── 10. MIDI Clock Out ────────────────────────────────────────────────────────
H1 "10. MIDI Clock Out"
P "mu-link can also send MIDI clock OUT, so outboard hardware (a delay pedal, a sequencer, a drum machine) locks to mu-link's tempo. Choose a MIDI output port in Audio Setup; mu-link emits standard 24-ppqn MIDI clock derived from its master clock. This works whether mu-link is the master (Internal) or itself slaved to an external clock (Ext MIDI) — in either case the outbound clock follows mu-link's transport."

# ── 11. Tips and Workflow ─────────────────────────────────────────────────────
H1 "11. Tips and Workflow"
Bullet "Two-instrument rig: launch mu-link, then mu-Clid and mu-Tant standalones. Press Play in mu-link — both lock to its clock and sum to one output. Balance them with the gain knobs."
Bullet "Lowest latency: choose a WASAPI Exclusive or ASIO output and a small buffer size in Audio Setup."
Bullet "Slave to your groovebox: enable its MIDI port, switch the clock source to Ext MIDI, and hit play on the groovebox — the whole mu rig follows."
Bullet "Drive a pedal in time: select a MIDI output port to clock an outboard tempo-synced effect."
Bullet "Solo to audition: use a strip's S button to hear one instrument alone while the others keep running silently."

# ── 12. Troubleshooting ───────────────────────────────────────────────────────
H1 "12. Troubleshooting"
Bullet "An app does not connect — make sure mu-link is running and has an open audio device (pick one in Audio Setup). Apps only attach once mu-link has a working output. Also confirm you are running the STANDALONE app, not the plug-in."
Bullet "No sound — check the output device in Audio Setup, the Master gain, and that the connected app is actually producing sound (and is not muted in its strip)."
Bullet "External clock not followed — confirm the correct MIDI input is enabled, the clock-source toggle is set to Ext MIDI, and the external device is sending clock (start it playing)."
Bullet "Nothing happens / conflict — make sure only one audio server is running. Two at once share the same connection points and will conflict."

# ── 13. Technical Specifications ──────────────────────────────────────────────
H1 "13. Technical Specifications"
Bullet "Format: standalone application (Windows); no plug-in formats."
Bullet "Connected apps: up to 8 simultaneously."
Bullet "Audio: stereo per app, summed to one stereo hardware output; one shared sample rate set by mu-link."
Bullet "Clock: sample-accurate master derived from the audio device; Internal (master) or External MIDI (slave) source."
Bullet "MIDI: clock input (slave) with tempo smoothing; 24-ppqn clock output to outboard gear."
Bullet "Mixer: per-app gain, mute, solo; master gain with a safety limiter; live metering per app and on the master."
Bullet "Connection: local shared-memory bus (same machine); no virtual audio driver, no network."
Bullet "Third-party libraries: JUCE."

# ── Save ──────────────────────────────────────────────────────────────────────
$outPath = "d:\Dev\mu\docs\mu-link\mu-link User Manual.docx"
$doc.SaveAs([ref]$outPath, [ref]16)
$doc.Close()
$word.Quit()
[System.Runtime.InteropServices.Marshal]::ReleaseComObject($word) | Out-Null
Write-Host "Saved: $outPath"
