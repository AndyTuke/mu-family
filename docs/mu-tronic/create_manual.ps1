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

# Screenshot: when an image file is given and exists in the images folder next to this script, it is
# inserted (full text width) with its caption underneath. Otherwise an italic, centred placeholder
# is written, distinct so it can be located via Find ("[Screenshot:") and replaced by artwork later.
function Pic($caption, $image = "") {
    $path = ""
    if ($image) { $path = Join-Path $PSScriptRoot ("images\" + $image) }
    $sel.Style = $doc.Styles("Normal")
    $sel.ParagraphFormat.Alignment = 1   # wdAlignParagraphCenter
    if ($path -and (Test-Path $path)) {
        $shape = $sel.InlineShapes.AddPicture($path, $false, $true)
        $shape.LockAspectRatio = -1
        if ($shape.Width -gt 432) { $shape.Width = 432 }     # text width in points (6 inches)
        if ($shape.Height -gt 340) { $shape.Height = 340 }   # keep tall crops on the page
        $sel.EndKey(5) | Out-Null   # wdLine: move past the picture
        $sel.TypeParagraph()
        $sel.Font.Italic = $true
        $sel.TypeText($caption)
        $sel.Font.Italic = $false
    } else {
        $sel.Font.Italic = $true
        $sel.TypeText("[Screenshot: $caption]")
        $sel.Font.Italic = $false
    }
    $sel.ParagraphFormat.Alignment = 0   # wdAlignParagraphLeft
    $sel.TypeParagraph()
}

# ── Title page ──────────────────────────────────────────────────────────────
$sel.Style = $doc.Styles("Title")
$sel.TypeText("mu-Tronic")
$sel.TypeParagraph()
$sel.Style = $doc.Styles("Subtitle")
$sel.TypeText("Performance Unit")
$sel.TypeParagraph()
$sel.Style = $doc.Styles("Normal")
$sel.TypeText("User Manual")
$sel.TypeParagraph()
$sel.TypeText("Version 1.1 (pre-release)")
$sel.TypeParagraph()
$sel.TypeText("Transwarp Development Project")
$sel.TypeParagraph()
Br

# ── 1. Introduction ──────────────────────────────────────────────────────────
H1 "1. Introduction"
P "mu-Tronic is a performance unit from Transwarp Development Project. It is a standalone application: there is no VST3 or CLAP version. It is free."
P "mu-Tronic starts empty. It has sixteen layer slots, shown eight at a time on two pages. Into each slot you put one layer, chosen from the instruments of the other mu apps: a Rhythm from mu-Clid, a Pattern from mu-Tant, an Arp from mu-Toni, or one of mu-On's five engines (Kick, Bass, Hat, Snare and Rumble). A layer in mu-Tronic is the same instrument you know from its own app, with the same controls, modulators and presets. Mix and match as you like: sixteen layers, any combination."
P "Around the layers is the family you may know from the other apps: the same mixer, effects, modulators, presets and transport bar. mu-Tronic adds three buses to the mixer, so you can group layers and process the group, and scenes, so you can change the clip on every layer with one press."
P "Because mu-Tronic hosts several instruments at once, it can stand in for mu-link as a performance tool: one window, one tempo, one mix."

H2 "The Layer Types"
Bullet "Rhythm (from mu-Clid)  -  a Euclidean sample rhythm. It can also send MIDI notes instead of sound."
Bullet "Pattern (from mu-Tant)  -  a wavetable drone with a gate pattern."
Bullet "Arp (from mu-Toni)  -  a generative arpeggiator."
Bullet "Kick, Bass, Hat and Snare (from mu-On)  -  a 909-style drum or bass sound with its own row of sixteen steps."
Bullet "Rumble (from mu-On)  -  a long, dark tail made from another layer's sound, usually a Kick."
P "Each of these is described in the manual of the app it comes from. This manual covers what is different in mu-Tronic."

Pic "The mu-Tronic window: transport bar, layer sidebar with scene pads and page switch, a layer in the main area." "main-window.png"

# ── 2. Installation ──────────────────────────────────────────────────────────
H1 "2. Installation"
P "mu-Tronic is a standalone application. Run mu-Tronic.exe from any location. It needs no licence key and shows no Support window."
P "On first launch mu-Tronic creates a content folder at %USERPROFILE%\Documents\TDP\muTronic\. Full mu-Tronic presets (.muTronic) are kept there. Layer presets are not: mu-Tronic reads and writes the preset folders of the other apps (see Presets), so install the apps whose layers you want to use, or at least keep their folders."
P "In the standalone application an Options button at the top left of the window opens the audio and MIDI device settings."

# ── 3. Interface Overview ─────────────────────────────────────────────────────
H1 "3. Interface Overview"
P "The mu-Tronic window is divided into these regions:"
Bullet "Transport Bar (top)  -  playback, BPM, presets, settings, performance mode and mixer"
Bullet "Layer Sidebar (left)  -  eight slots at a time, the scene pads and the page switch"
Bullet "Layer Editor (centre)  -  the header, the controls and the modulators of the selected layer"
Bullet "Mixer overlay  -  opened from the transport bar, replacing the editor"
Bullet "Status Bar (bottom)  -  the name and value of the control you last touched, and the CPU readout at the right-hand end"
P "The window has two sizes, Medium and Large, chosen in Settings (Display, Size). Everything scales with it."
P "The main area takes the colour of the selected layer's type: purple for Rhythm, green for Pattern, blue for Arp and yellow for the five mu-On layers. The transport bar, sidebar, mixer and settings keep mu-Tronic's own ice-white and steel."

H2 "CPU Readout"
P "At the right-hand end of the Status Bar, mu-Tronic shows CPU followed by a percentage. It is how hard the audio engine is working, with all sixteen layers, the buses and the effects counted. The figure is the worst moment of the last second, because it is the brief spike that causes clicks. The number turns amber from 70 percent and red from 90 percent. If it goes red, remove a layer or a heavy effect. The readout stays visible in Performance mode."

# ── 4. Transport Bar ──────────────────────────────────────────────────────────
H1 "4. Transport Bar"
P "The transport bar is the same as in the other mu apps. From left to right:"
Bullet "mu-Tronic logo  -  click to open the About panel."
Bullet "Play / Stop  -  starts and stops every layer together. Locked while an external MIDI clock controls play."
Bullet "BPM display  -  tempo from 20.0 to 300.0 BPM with one decimal. Click the arrows to step one whole BPM (Shift+click steps a tenth). Double-click to type a value. Locked while an external MIDI clock sets the tempo."
Bullet "Clock lamp or Tap button  -  the lamp shows the state of an incoming MIDI clock (dim amber waiting, green locked, red lost); Tap sets the tempo from your taps. You see one or the other, never both."
Bullet "Nudge buttons (- and +)  -  hold to run the groove 4 percent slower or faster to line up by ear; the set tempo returns when you let go."
Bullet "Position  -  bar, beat and sixteenth readout."
Bullet "Preset dropdown / New / Save  -  load and save full .muTronic presets. The dropdown also holds the Import and Performances entries (see Presets)."
Bullet "Settings (gear) icon  -  opens Settings."
Bullet "Perf button  -  turns on Performance mode (also Ctrl+P). See Performance Mode."
Bullet "Mixer button  -  opens the mixer. The label changes to Sequencer while the mixer is open."
Pic "Transport Bar - full width view." "transport-bar.png"

# ── 5. Slots and the Layer Sidebar ───────────────────────────────────────────
H1 "5. Slots and the Layer Sidebar"
P "mu-Tronic has sixteen slots, numbered 1 to 16. The sidebar shows eight of them at a time. Each item shows the slot number, a small picture of the layer's type with a letter on it, and the layer's colour. A new mu-Tronic holds no layers at all: every slot is empty, and the first thing you see is the layer chooser (see Adding a Layer)."
Bullet "Select  -  click an item to select that slot. The selected slot is outlined and shown in the main area."
Bullet "Empty slots  -  shown with a + sign. They stay in the list, so the slots never move or renumber."
Bullet "Colour  -  each layer has its own colour. Within one page no two layers share a colour; slot 1 and slot 9 may both be red."
Bullet "Reorder  -  drag an item onto another slot on the same page to swap the two. Dragging onto an empty slot moves the layer there. The layer keeps its mixer strip, clips, modulators and colour. Moving a layer to the other page is not yet possible."
Bullet "Delete  -  the Delete (x) button in the layer header empties the slot, after a confirmation. No other slot moves. The layer's mixer strip, clips and modulators go with it."
Bullet "Names  -  every layer has a name that you can change: click the name in the layer header and type. The default is the type and the slot number, for example Kick 3 or Rhythm 12. Delete the text to return to the default. Names are saved, and they appear in the mixer and in the sidechain lists."
Pic "Layer Sidebar with a mix of layer types, one selected, empty slots shown with a plus sign." "sidebar.png"

H2 "Pages"
P "The page switch sits at the foot of the sidebar, under the engraved word Page. Click 1 for slots 1 to 8 or 2 for slots 9 to 16. You can also press Page Up and Page Down to move between pages; they stop at the first and last page. The keys work when no text box is being edited. While the mixer is open they change the mixer's page instead."
P "Switching page selects the slot at the same position on the new page, so slot 3 goes to slot 11. Selecting a slot on the other page by any other route also switches the page to follow it."

H2 "Scene Pads"
P "Above the page switch is the Scene block: eight numbered pads in two columns. Each pad launches clip n on every layer that stores one, across both pages. See Scenes and Clips."

# ── 6. Adding a Layer ────────────────────────────────────────────────────────
H1 "6. Adding a Layer"
P "Select an empty slot and the main area shows the layer chooser, titled Add a layer to slot n. It is a set of cards, one for each layer type:"
Bullet "mu-Clid  -  Rhythm: Euclidean sample rhythm."
Bullet "mu-Tant  -  Pattern: wavetable drone, gate pattern."
Bullet "mu-Toni  -  Arp: generative arpeggiator."
Bullet "mu-On  -  a framed group of five cards: Kick (909 kick drum), Bass (bass line), Hat (hi-hat), Snare (snare) and Rumble (rumble tail from a kick)."
P "Click a card, or its Add button, to put that layer in the slot. The new layer starts from the default for its type (see Presets). The status bar confirms with Added followed by the layer's name."
P "You can also load a layer preset into an empty slot: open the preset dropdown in the layer header and choose any layer preset. The slot takes the type of the preset you chose."
P "Rumble works on the sound of another layer, called its Source. A new Rumble listens to the first Kick layer if there is one. If there is no Kick yet, the Rumble is added with no Source and the status bar says Source needed: add a Kick layer. Choose a Source in the Rumble's own Source dropdown. A Rumble can listen to any other layer that is not a Rumble."
Pic "The layer chooser for an empty slot: three product cards and the mu-On group of five engine cards." "chooser.png"

# ── 7. The Layers ────────────────────────────────────────────────────────────
H1 "7. The Layers"
P "A layer in mu-Tronic has the controls of its type, in the same place as in the app it comes from. See that app's manual for the details of each control. This chapter lists what is different in mu-Tronic."

H2 "The Layer Header"
P "The strip across the top of the layer editor holds:"
Bullet "Layer colour dot and a small type tag: Clid, Tant, Toni or On."
Bullet "Layer name  -  click to edit."
Bullet "Layer preset dropdown  -  lists the layer presets for this layer's type only (see Presets). An empty slot lists every type."
Bullet "Notes In  -  for Arp and Pattern layers only. See below."
Bullet "Clip strip  -  eight numbered pads. See Scenes and Clips. A Rumble layer has no clip strip."
Bullet "Save  -  saves the layer as a layer preset of its type."
Bullet "Reset (circular arrow) / Delete (x)  -  reset the layer to its defaults, or empty the slot. Both ask first. Reset also clears the layer's clips."

H2 "Notes In (MIDI channel per layer)"
P "Arp and Pattern layers can be played from a MIDI keyboard. Each of these layers has its own Notes In setting in the header, so different layers can listen to different channels. Choose Omni (every channel, the default) or Channel 1 to Channel 16. The setting is saved with the layer and with the full preset. Loading a layer preset or a clip never changes it, because it describes your MIDI setup rather than the sound. A Pattern layer shows the control in Free mode too; it takes effect when you switch that layer to Note mode. Rhythm, Kick, Bass, Hat, Snare and Rumble layers do not read notes and show no Notes In control."

H2 "Rhythm"
Bullet "The Save button opens the same Save Rhythm Preset card as in mu-Clid: Name, Description, Category and Embed sample in file. Tick Embed sample in file if you will open the preset on another computer."
Bullet "A new Rhythm layer starts with no sample if its default sample cannot be found, and shows the normal empty sample bar (drop a sample here or click to browse). A Missing warning appears only for a sample that a loaded preset or saved session names and that has gone."
Bullet "A Rhythm set to send MIDI instead of sound makes no audio. Its mixer strip is drawn dimmed. The notes go out through the MIDI output device chosen in the standalone's settings."

H2 "Pattern"
P "On the Root and Scale row, at the right, is a MIDI switch with two choices, Free and Note. In Free, the drone plays on its own. In Note, it sounds while a key is held, at the key's pitch. Each Pattern layer has its own setting; it is saved with the layer but is not part of a layer preset. Free is the default."

H2 "Kick, Bass, Hat and Snare"
P "These are mu-On's engines, each with its own row of sixteen steps. Because the layer is a Kick, Bass, Hat or Snare, there is no Engine selector: to change the sound family, empty the slot and add a different layer. The Groove box holds Swing and Accent for this layer alone. The step row shows 16 steps; click a step to turn it on or off, right-click for an accent."

H2 "Rumble"
P "Rumble has an envelope and a Source dropdown instead of a step row. The envelope curve shows a baseline, level lines at 25, 50 and 75 percent, and beat lines that follow its loop length."

H2 "Effect Send Names"
P "The mixer's Effect return is named after its algorithm, for example Chorus or Echo. The Effect send knob in each layer, and the modulation target for it, carry the same name."

H2 "Modulators"
P "Every layer has its own modulators exactly as in its own app. Modulators can only move the controls of their own layer. See the manual of the layer's app."

# ── 8. Scenes and Clips ──────────────────────────────────────────────────────
H1 "8. Scenes and Clips"
H2 "Clips"
P "A clip is a stored copy of one layer's current state, ready to recall while the music plays. Every layer, except Rumble, has up to eight clips in the Clip strip in its header: the word Clip followed by pads numbered 1 to 8. A clip is saved in the layer's own type, so it carries that type with it."
P "Each pad is lit in the layer's colour:"
Bullet "Bright and steady  -  the clip that is playing."
Bullet "Dim  -  a clip is stored here."
Bullet "Unlit, with just its number  -  the pad is empty."
Bullet "Pulsing  -  the clip is queued and will start at the next bar."
Bullet "Flashing on and off  -  the playing clip belongs to a muted layer."
P "How to use the pads:"
Bullet "Click a stored pad to launch it. While playing it waits for the next bar and the pad pulses; when stopped it starts at once."
Bullet "Click the playing pad to mute the layer; click again to unmute."
Bullet "Click an empty pad to store the layer's current state there."
Bullet "Shift+click any pad to store the current state in it. If the pad holds a different clip from the one playing, mu-Tronic asks you to confirm with Replace."
Bullet "Right-click a pad (or Ctrl+click) for a menu with Store here and Clear."
P "Turning a knob changes only the playing layer, not the stored clip. To keep a tweak, store it again with Shift+click. Resetting a layer clears its clips; deleting a layer removes them with it. Clips are saved with the full preset and your session, but not in layer presets. A clip of a different layer type is never launched: the status bar says that the clip is for a different layer type."

H2 "Scenes"
P "A scene launches clip n on every layer at once. It replaces the Scene buttons of mu-link. The eight Scene pads sit in the sidebar, under the layers and above the page switch, and they always act on all sixteen slots, whichever page you are looking at."
Bullet "Click pad n to launch clip n on every layer that has a clip n. Layers that have no clip n, empty slots, and layers whose clip n belongs to a different type keep playing as they are."
Bullet "All the layers switch together, at the next bar line."
Bullet "A pad is unlit when no layer stores that clip, dim when at least one does, bright when every layer that stores it is playing it, and pulsing while any layer is waiting to start it."
Bullet "The status bar reads Clip n on N layers when a scene launches, or Clip n is empty when no layer has that clip."
P "In Performance mode the pads cannot be clicked, but they keep moving so that you can see what the control surface is doing."
Pic "The Scene pads and page switch at the foot of the sidebar, with scene 1 playing and scene 3 queued." "scene-pads.png"
Pic "Clip strip at the right of the layer header: pad 3 is the playing clip, pad 1 is queued, pad 2 holds a stored clip, the others are empty." "clip-strip.png"

# ── 9. Modulators ────────────────────────────────────────────────────────────
H1 "9. Modulators"
P "Each layer has eight independent modulators (Mod A to Mod H) plus a Matrix tab, the same shared engine as the other mu apps, in the lower part of the layer editor. Each has a Stepped or Smooth mode, a polarity, a loop length, and its own targets with a depth from -100 to +100 percent. Which controls can be targets depends on the layer's type; see that app's manual."
Pic "Modulator panel with the Stepped editor and the target list." "modulators.png"

# ── 10. Mixer ────────────────────────────────────────────────────────────────
H1 "10. Mixer"
P "Click the Mixer button in the transport bar to open the mixer. It is the same shared mixer and effects rack as the other mu apps, with room for sixteen channels and three buses. The mixer has its own page selector at the top right: 1-8, 9-16 and Buses. When you open the mixer it shows the page that the sidebar is on; after that the two pages are independent, so you can adjust a bus while the editor stays on a layer. Page Up and Page Down move between the channel pages while the mixer is open; click Buses to reach the bus page."

H2 "Channel Strips"
P "Each slot has a channel strip with a fader and meter, pan, mute and solo, sidechain ducking (source, amount, attack and release) and Effect, Delay and Reverb sends. The strip shows the layer's name in its colour. An empty slot's strip is dimmed but can still be set. A Rhythm layer set to send MIDI has a dimmed strip, because it makes no sound."
P "The sidechain source list holds all the other layers by slot and name, the External input, and the buses. A layer can duck from any other layer."

H2 "Out: Main or a Bus"
P "At the foot of each channel strip, the Out selector chooses where the channel goes: Main (the master output, the default), Bus 1, Bus 2 or Bus 3. A channel set to a bus shows the bus name lit. The channel's sends to the Effect, Delay and Reverb returns do not depend on its Out setting. A bus always goes to Main. A bus cannot be routed to another bus."

H2 "The Buses Page"
P "Click Buses in the mixer's page selector. The page shows three bus units, then the Master. Each bus unit is a strip with its own insert column beside it, built like the Master:"
Bullet "Name  -  Bus 1, Bus 2 or Bus 3."
Bullet "Sidechain  -  the same source, amount, attack and release as a channel. A bus can duck from any layer and from any earlier bus (Bus 2 can use Bus 1; Bus 3 can use Bus 1 and Bus 2), never from itself or a later bus."
Bullet "Sends  -  Effect, Delay and Reverb sends, as on a channel, taken after the bus fader."
Bullet "Pan, fader with meter, level readout, mute and solo."
Bullet "Insert 1 and Insert 2  -  two insert effects, in that order, with the same list of algorithms and four knobs each as the Master inserts."
P "The three FX returns appear on both channel pages and are unaffected by which page you view. The Effect, Delay and Reverb rows below the strips are the same on every page."
Pic "The Buses page: three bus units each with two inserts, and the Master." "mixer-buses.png"
Pic "Mixer overlay, channel page 1-8, with the page selector at the top right and the Out selector at the foot of each strip." "mixer.png"

H2 "Insert Effects"
P "Every layer, every bus and the Master has insert effects. The algorithm dropdown lists None, 3-Band EQ, Bitcrusher, Clipper, Compressor, Fold, Hard Clip, Karplus, Limiter, Overdrive, Ring Mod, Soft Clip, Tape Sat, Vocoder and Vocoder St. Four knobs appear and are labelled to match the algorithm."
H3 "Overdrive"
P "Overdrive is a warm, tube-style saturation that also adds a little even-harmonic body. Its four knobs:"
Bullet "Drive (0 to 100, default 0)  -  how hard the signal is pushed. At 0 it passes unchanged."
Bullet "Output (-24 to 0 dB, default 0 dB)  -  the level after the saturation, to match the loudness before and after."
Bullet "Bias (0 to 100 percent, default 25)  -  how asymmetrically the signal is clipped. More Bias adds more even harmonics and a thicker sound."
Bullet "Tone (200 Hz to 20 kHz, default 20 kHz)  -  a low-pass filter on the result. Turn it down to soften the top end."

H2 "Effects and Master"
Bullet "Effect  -  Chorus, Flanger, Phaser or Echo. A pure send, with no Mix knob."
Bullet "Delay  -  a Time dropdown (Free, 1/32, 1/16, 1/8 or 1/4), Feedback, Spread and Dirt."
Bullet "Reverb  -  Room, Hall, Plate or Spring, with Size, Decay, Pre-delay, Diffusion, Damp, Mod and Dirt. A pure send."
Bullet "Master  -  the master strip has two inserts, Insert 1 and Insert 2, that the summed output passes through in that order."
P "A row of buttons at the top right (Peak, VU, K-12, K-14) chooses the metering standard."

# ── 11. Presets ──────────────────────────────────────────────────────────────
H1 "11. Presets"
H2 "Full Presets (.muTronic)"
P "A full preset saves the whole of mu-Tronic: every slot's layer, its settings, clips and name, the mixer, the buses and the effects. Presets are kept in Documents\TDP\muTronic\Presets. Load from the transport bar's dropdown. Save with the Save button: enter a name, an optional description and a category, and tick Save as Default to make it the preset that loads at startup and when you press New. Loading a preset shows page 1 with slot 1 selected. While the transport is playing, a loaded preset waits and takes over at the next bar."

H2 "Layer Presets and Where They Live"
P "mu-Tronic does not keep a separate set of layer presets. Each layer type reads and writes the preset folder of the app it comes from, in the shared Documents\TDP folder. A layer you save in mu-Tronic appears in the original app, and the other way round."
Bullet "Rhythm  -  Documents\TDP\muClid\Rhythms, files ending .muRhythm."
Bullet "Pattern  -  Documents\TDP\muTant\Voices, files ending .muPattern."
Bullet "Arp  -  Documents\TDP\muToni\Arps, files ending .muArp."
Bullet "Kick, Bass, Hat, Snare and Rumble  -  Documents\TDP\muOn\Tracks, files ending .muTrack. A .muTrack file knows which engine it is for, so a Kick preset shows only in Kick slots."
P "In a slot that holds a layer, the layer preset dropdown lists only presets of that layer's type (for example only .muArp files in an Arp slot). To use a preset of another type, choose it in an empty slot. Presets from the other apps' factory and user folders both appear."

H2 "Defaults for New Layers"
P "A layer added from the chooser starts from the default of its own app: the file named _default in that app's layer preset folder (for example _default.muArp in the Arps folder), which the app writes when you tick Save as Default in a layer preset. If there is no such file the layer starts from the built-in defaults. So a default you set in mu-Toni is the Arp you get in mu-Tronic."

H2 "Import Product Preset"
P "You can bring a whole preset from another mu app into mu-Tronic. Open the transport bar's preset dropdown and choose Import product preset... in the Import section. A file chooser opens, filtered to .muClid, .muTant, .muToni and .muOn files. Choose one."
Bullet "The layers of the preset are laid into the free slots, in order, starting from the first empty slot. Each layer keeps its settings, name, mixer strip (level, pan, mute, sends and sidechain) and, for a Pattern layer, the tonal centre if you have no Pattern yet."
Bullet "Slots that already hold a layer are never touched. If the preset has more layers than there are free slots, nothing is imported and the status bar reads Needs n free slots, m free."
Bullet "Solo is never imported, so it cannot silence the layers already in mu-Tronic. A sidechain or Rumble Source that pointed at another layer of the preset follows that layer to its new slot."
Bullet "The preset's global effects, master settings, clips and layer colours are ignored. mu-Tronic keeps its own."
Bullet "On success the status bar reads Imported n layers from followed by the preset's name. If the file cannot be read, nothing is imported and the status bar says so."
P "Presets saved by an old version of mu-On that predates its variable layer count cannot be imported."

H2 "Performance Mode"
P "Performance mode turns mu-Tronic into a locked gig view. The screen shows what is playing, but you cannot change anything with the mouse or keyboard. A connected control surface switches presets and layers, and the screen follows it. A performance is a saved list of up to eight full presets, stored as a .muPerformance file in the Performances folder in your content folder."
H3 "The Performance Chooser"
P "Open the chooser from the Performances entry at the bottom of the transport bar's preset dropdown, or by clicking the Perf button (or pressing Ctrl+P) when no performance has been chosen yet. On the left is the list of saved performances; on the right are the eight places. Each place has a + Preset button (empty) or Replace, Up, Down and Remove buttons (filled). A preset whose file is missing shows a red lamp. New starts an empty performance; Load opens a .muPerformance file; Save stores it under a name; Rename and Delete do what they say. Use makes this performance the one that Performance mode plays."
H3 "Entering and Leaving"
P "Click the Perf button or press Ctrl+P. The button lights and reads LIVE, and the preset dropdown and New and Save are replaced by a step indicator with a row of eight lamps. Everything is locked, including the sidebar's page switch, the scene pads, the mixer and Settings, but they keep moving so that you can see what the surface is doing. To leave, press and hold the Perf button for one second. A plain click does nothing, so you cannot leave by accident."
Pic "Transport Bar in Performance mode: the Perf button lit and reading LIVE, the step indicator and its row of eight lamps." "performance-transport.png"

# ── 12. Settings ─────────────────────────────────────────────────────────────
H1 "12. Settings"
P "Click the gear icon to open Settings. Click Close to return."
P "General:"
Bullet "Audio, Master Vol  -  overall output level."
Bullet "Display, Size  -  Medium or Large window."
Bullet "Transport, Tempo and Time signature  -  the internal clock's tempo, and the time signature that the Position readout counts in."
Bullet "Updates, Check for updates  -  a tick box. When ticked, mu-Tronic asks the transwarp.me website once a day for the latest version number and shows an Update available link in the status bar when there is a newer one. It sends nothing about you. Untick to switch the check off."
P "MIDI:"
Bullet "MIDI Clock, Source  -  Internal, or MIDI In to follow an external MIDI clock. A Messages choice (Clock only, Transport only, Clock + Transport) and a Sync offset appear. They work as in the other mu apps."
Bullet "MIDI Program Change  -  buttons that open the tables mapping incoming program-change messages to presets."
Bullet "Controller  -  connects a Launchpad (see Launchpad)."
P "Notes In is not in Settings: each Arp and Pattern layer has its own, in the layer header."

# ── 13. Launchpad ────────────────────────────────────────────────────────────
H1 "13. Launchpad"
P "mu-Tronic is designed to be played from a Novation Launchpad X as well as from the screen. The Launchpad's controller support is still being finished for mu-Tronic and has not yet been confirmed on real hardware, so treat the following as planned rather than promised."
Bullet "Planned: hold the Launchpad's logo button and press any pad in a column to launch that scene, the same as clicking a Scene pad. While the logo is held, the pads show the scenes as white columns."
P "Connect and disconnect the Launchpad from the Controller line in Settings."

# ── 14. Connecting to mu-link ────────────────────────────────────────────────
H1 "14. Connecting to mu-link"
P "mu-link is a companion application from Transwarp Development Project: a local audio hub and master clock for the mu family. When mu-link is running, mu-Tronic connects to it automatically. Its audio is summed into mu-link's mix and its transport locks to mu-link's master clock."
H2 "How to Connect"
Bullet "Launch mu-link, then launch mu-Tronic, in either order. Within about a second the mu-Tronic title bar shows mu-link connected."
Bullet "mu-Tronic's audio plays through mu-link's output device, summed with any other connected mu apps."
Bullet "mu-link is the tempo master. Use mu-link's Play/Stop and tempo while connected."
Bullet "Quit mu-link and mu-Tronic reverts to its own audio device and internal transport, with no restart."
P "Because mu-Tronic holds up to sixteen layers itself, you often do not need mu-link at all. Use mu-link when you want to combine mu-Tronic with other apps or with outboard gear. See the mu-link User Manual for the full picture."

# ── 15. Tips and Workflow ────────────────────────────────────────────────────
H1 "15. Tips and Workflow"
Bullet "Starting out: add a Kick to slot 1 and a Bass to slot 2, press Play, and turn on a few steps."
Bullet "Rumble: add a Kick, then a Rumble, which listens to the Kick by default."
Bullet "Group the drums: send the Kick, Hat and Snare to Bus 1 and put a Compressor in Bus 1's Insert 1 to glue them."
Bullet "Pump the bass: in the Bass layer's channel strip, set the sidechain source to your Kick."
Bullet "Prepare variations: store clip 1 on every layer as your first section, change things, and store clip 2. Then use the Scene pads to switch the whole set at the next bar."
Bullet "Bring in an old idea: use Import product preset to pull a whole mu-Clid or mu-On preset into the free slots."
Bullet "Keep an eye on the CPU readout as you add layers."

# ── 16. Technical Specifications ─────────────────────────────────────────────
H1 "16. Technical Specifications"
Bullet "Format: Standalone (Windows). No VST3 or CLAP. Free; no licence key."
Bullet "Layers: 16 slots on two pages of 8, starting empty"
Bullet "Layer types: Rhythm, Pattern, Arp, Kick, Bass, Hat, Snare, Rumble"
Bullet "Mixer: 16 channel strips, 3 buses with 2 inserts each, Effect / Delay / Reverb returns, master with 2 inserts"
Bullet "Insert effects: 14 plus None, on every layer, bus and the master"
Bullet "Clips: 8 per layer (none for Rumble); scenes 1 to 8"
Bullet "Modulators per layer: 8, plus a matrix view"
Bullet "Full preset format: .muTronic. Layer presets: .muRhythm, .muPattern, .muArp, .muTrack (shared with the other apps)"
Bullet "Import: .muClid, .muTant, .muToni, .muOn full presets"
Bullet "Third-party libraries: JUCE, Signalsmith Reverb (MIT)"

# ── Save ──────────────────────────────────────────────────────────────────────
$outPath = "d:\Dev\mu\docs\mu-tronic\mu-Tronic User Manual.docx"
$doc.SaveAs([ref]$outPath, [ref]16)
$doc.Close()
$word.Quit()
[System.Runtime.InteropServices.Marshal]::ReleaseComObject($word) | Out-Null
Write-Host "Saved: $outPath"
