**μ-CLID**

*Euclidean Rhythm Sequencer & Sample Trigger*

*by Transwarp Development Project*

**Full Project & Business Guide --- v6.0**

28 April 2026

VST3 / CLAP Plugin --- JUCE / C++ / CMake

**1. Plugin Identity**

  -------------------------- --------------------------------------------
  **Property**               **Value**

  Plugin name                μ-Clid

  Full name                  μ-Clid Euclidean Rhythm Sequencer

  Company                    Transwarp Development Project

  Short name                 TDP

  JUCE                       TDP1
  PLUGIN_MANUFACTURER_CODE   

  JUCE PLUGIN_CODE           MCld

  Formats                    VST3 + CLAP + Standalone

  UI display                 μ-CLID / EUCLIDEAN RHYTHM SEQUENCER · TDP

  Logo click                 Opens About panel

  Gear icon click            Opens Settings overlay
  -------------------------- --------------------------------------------

**3. Plugin Architecture**

**3.1 Folder Structure**

mu-clid/

├── CMakeLists.txt

├── Tests/

│ ├── EuclideanGeneratorTests.cpp

│ ├── HitGeneratorTests.cpp

│ ├── ControlSequenceTests.cpp

│ └── ModulationMatrixTests.cpp

└── Source/

├── PluginProcessor.h/.cpp

├── PluginEditor.h/.cpp

├── Sequencer/

│ ├── EuclideanGenerator.h/.cpp

│ ├── HitGenerator.h/.cpp

│ ├── ControlSequence.h/.cpp

│ ├── Rhythm.h/.cpp

│ └── SequencerEngine.h/.cpp

├── Audio/

│ ├── SamplePlayer.h/.cpp

│ ├── TimeStretcherBase.h

│ ├── SoundTouchStretcher.h/.cpp

│ ├── ResonantFilter.h/.cpp

│ ├── VoiceEngine.h/.cpp

│ └── MixerEngine.h/.cpp

├── FX/

│ ├── FXSlotBase.h

│ ├── FXAlgorithm.h/.cpp

│ ├── EffectFX.h/.cpp

│ ├── DelayFX.h/.cpp

│ ├── ReverbFX.h/.cpp

│ └── FXChain.h/.cpp

├── Modulation/

│ ├── ModulationMatrix.h/.cpp

│ └── ModulationAssignment.h

└── UI/

├── Components/

│ ├── MuClidLookAndFeel.h/.cpp

│ ├── KnobWithLabel.h/.cpp

│ ├── SegmentControl.h/.cpp

│ ├── NudgeInput.h/.cpp

│ ├── TimeSelector.h/.cpp

│ ├── DropdownSelect.h/.cpp

│ ├── PresetBrowser.h/.cpp

│ ├── SaveDialog.h/.cpp

│ ├── PopupMenu.h/.cpp

│ ├── StepEditor.h/.cpp

│ ├── LFOEditor.h/.cpp

│ ├── AddButton.h/.cpp

│ └── StatusBar.h/.cpp

├── RhythmCircle.h/.cpp

├── RhythmPanel.h/.cpp

├── EuclideanPanel.h/.cpp

├── ModulatorPanel.h/.cpp

├── ModMatrixPanel.h/.cpp

├── FXPanel.h/.cpp

├── MixerPanel.h/.cpp

├── TransportBar.h/.cpp

├── SettingsOverlay.h/.cpp

└── AboutPanel.h/.cpp

**3.2 Critical Architectural Rules**

- Everything in AudioProcessorValueTreeState --- if not in the value
  tree it will not save

- Each rhythm in its own subtree --- enables per-rhythm preset save/load
  and hot-swap

- FXSlotBase interface for all FX --- enables VST hosting in future
  without refactoring

- Audio thread never allocates memory --- all allocation in
  prepareToPlay

- ControlSequence objects are fully independent --- never couple lengths
  or rates to rhythm step counts

- ModulationMatrix sits between APVTS and audio engine --- audio engine
  reads from ModulationMatrix only

- All UI uses the shared component library --- never build a one-off
  version of any standard control

- No hardcoded colours or sizes anywhere except MuClidLookAndFeel

- Hot-swap rhythm slots via atomic pointer from day one --- required for
  v2 live swap feature

- TimeStretcherBase interface wraps SoundTouch --- enables Rubber Band
  swap in v2 without refactoring

- RhythmSidebar item order must support variable ordering from day one
  --- required for v2 drag-to-reorder feature

- Modulator output values and modulator depth values are named string
  IDs --- enables meta-modulation in v2

- ModulationMatrix processes assignments in dependency order --- detect
  and reject circular dependencies

- Rhythms are fully self-contained --- control sequences may only target
  parameters within their own rhythm

**3.3 Key Interfaces**

**FXSlotBase**

class FXSlotBase {

public:

virtual void prepare(double sampleRate, int blockSize) = 0;

virtual void process(juce::AudioBuffer\<float\>& buffer) = 0;

virtual juce::String getName() = 0;

virtual juce::String getCategory() = 0;

virtual juce::Component\* createEditor() = 0;

virtual void getStateInformation(juce::MemoryBlock&) = 0;

virtual void setStateInformation(const void\*, int) = 0;

};

**TimeStretcherBase**

class TimeStretcherBase {

public:

virtual void prepare(double sampleRate, int blockSize) = 0;

virtual void setTimeRatio(float ratio) = 0;

virtual void setPitchRatio(float ratio) = 0;

virtual void process(juce::AudioBuffer\<float\>& buffer) = 0;

};

**4. Class Inventory**

**4.1 JUCE Classes Used --- DSP**

  -------------------------------------------- ----------------------------------------
  **JUCE Class**                               **Purpose**

  juce::AudioProcessor                         Base for PluginProcessor

  juce::AudioProcessorEditor                   Base for PluginEditor

  juce::AudioProcessorValueTreeState           All parameter storage, save/load,
                                               automation

  juce::AudioPlayHead                          Reads DAW transport position and BPM

  juce::AudioPlayHead::CurrentPositionInfo     Struct: BPM, bar, beat, playing state

  juce::AudioBuffer\<float\>                   Audio buffer in processBlock

  juce::dsp::ProcessSpec                       Holds sample rate, block size, channels

  juce::dsp::StateVariableTPTFilter\<float\>   Resonant filter --- per-rhythm voice

  juce::dsp::IIR::Filter\<float\>              General biquad filter --- FX tone
                                               sections

  juce::dsp::LadderFilter\<float\>             Moog-style filter --- additional Effect
                                               algorithm

  juce::dsp::WaveShaper\<float\>               Waveshaping --- distortion algorithms

  juce::dsp::DelayLine\<float\>                Delay buffer --- delay FX and reverb

  juce::dsp::Oscillator\<float\>               Waveform generation --- used internally
                                               by ControlSequence smooth mode

  juce::dsp::Oversampling\<float\>             2x/4x oversampling for waveshaper
                                               algorithms

  juce::ADSR                                   Amplitude and filter envelopes

  juce::SmoothedValue\<float\>                 Smooths parameter changes to avoid
                                               zipper noise

  juce::ScopedNoDenormals                      Prevents CPU spikes in processBlock

  juce::AudioFormatManager                     Registers audio format readers

  juce::AudioFormatReader                      Reads audio file from disk

  juce::AudioSampleBuffer                      Stores decoded audio in memory
  -------------------------------------------- ----------------------------------------

**4.2 JUCE Classes Used --- State & Threading**

  ------------------------------ ----------------------------------------
  **JUCE Class**                 **Purpose**

  juce::ValueTree                Tree data structure for all plugin state

  juce::ApplicationProperties    Stores plugin preferences on disk

  juce::AsyncUpdater             Posts work from audio thread to message
                                 thread safely

  juce::AbstractFifo             Lock-free FIFO for audio/message thread
                                 communication

  juce::CriticalSection          Mutex for thread-safe data access

  juce::SpinLock                 Lightweight lock for audio thread
  ------------------------------ ----------------------------------------

**4.3 Classes We Create --- Sequencer**

  -------------------- ------------------------- ---------------------------- -------------------
  **Class**            **Base**                  **Purpose**                  **Extensibility**

  EuclideanGenerator   ---                       Pure static algorithm.       Excellent --- add
                                                 steps/hits/padding → bool    new algorithms as
                                                 array                        static methods

  HitGenerator         ---                       One euclidean layer. Steps,  Good --- use vector
                                                 hits, rotate,                for future multiple
                                                 pre/post/insert pad, accent  insert pads
                                                 hits, accent level, mute,    
                                                 logic                        
                                                 (OR/AND/XOR/A-only/B-only)   

  ControlSequence      ---                       Modulation source.           Excellent --- add
                                                 Serum/Vital style drawable   new generator types
                                                 LFO. Smooth or stepped mode. as enum variants
                                                 Loop length as note value ×  
                                                 multiplier. Up to 8 per      
                                                 rhythm. Freely assignable to 
                                                 any per-rhythm parameter via 
                                                 ModulationMatrix. Unipolar   
                                                 or bipolar output. Value     
                                                 range -100 to +100.          

  Rhythm               ---                       One rhythm slot. Two         Good --- use
                                                 HitGenerators (A and B),     std::vector for
                                                 vector of ControlSequences   ControlSequences
                                                 (max 8), name, colour,       
                                                 resetSteps                   
                                                 (std::optional<int> ---      
                                                 nullopt = INF).              
                                                 Self-contained --- no        
                                                 cross-rhythm dependencies.   

  SequencerEngine      juce::ChangeBroadcaster   Manages all rhythms. Reads   Moderate --- use
                                                 DAW position. Calculates     atomic pointer for
                                                 combined hits per block      hot-swap from day
                                                                              one
  -------------------- ------------------------- ---------------------------- -------------------

**4.4 Classes We Create --- Audio**

  ------------------- ------------- ---------------------- -----------------------
  **Class**           **Base**      **Purpose**            **Extensibility**

  SamplePlayer        ---           Loads sample. Tracks   Good --- compile flag
                                    read position. One     for
                                    shot and loop modes.   SoundTouch/RubberBand
                                    Uses                   swap
                                    TimeStretcherBase.     
                                    Absolute file path     
                                    storage.               

  VoiceEngine         ---           Per-rhythm audio.      Moderate --- wrap
                                    SamplePlayer +         filter in VoiceFilter
                                    amplitude ADSR +       class for future chain
                                    filter + filter ADSR.  
                                    Handles triggers.      
                                    Short overlap fade on  
                                    voice cut.             

  MixerEngine         ---           Combines all           Good --- use vector of
                                    VoiceEngine outputs.   sidechain assignments
                                    Faders, sends, pan,    not single master
                                    sidechain              

  SidechainEnvelope   ---           Gain reduction from    Good
                                    trigger events with    
                                    attack/release/depth   
  ------------------- ------------- ---------------------- -----------------------

**4.5 Classes We Create --- FX**

  ---------------- ------------- ----------------------------------------
  **Class**        **Base**      **Purpose**

  FXSlotBase       ---           Pure virtual interface. All FX inherit
                                 this. Enables VST hosting in v3.

  FXAlgorithmDef   ---           Data class. Algorithm id, name,
                                 description, category, rows, params,
                                 visibleWhen conditions. Includes
                                 oversampling flag per algorithm.

  EffectFX         FXSlotBase    Effect slot DSP. Loads algorithm
                                 definition. Routes to selected
                                 algorithm. Applies oversampling wrapper
                                 where required.

  DelayFX          FXSlotBase    Ping pong delay. DelayLine + feedback +
                                 spread + dirt waveshaper. Intra-FX send
                                 knob to Reverb on FX strip.

  ReverbFX         FXSlotBase    Reverb DSP. Signalsmith library. Room,
                                 hall, plate, spring algorithms.

  FXChain          ---           Holds Effect, Delay, Reverb. Manages
                                 routing, send crossfade, and global
                                 intra-FX sends between units.
  ---------------- ------------- ----------------------------------------

**4.6 Classes We Create --- Modulation**

  ---------------------- ------------- ------------------------ --------------------
  **Class**              **Base**      **Purpose**              **Extensibility**

  ModulationAssignment   ---           One assignment: source   Excellent --- string
                                       ID (string), destination IDs make new sources
                                       ID (string), depth (-100 and destinations
                                       to +100). Source can be  automatic
                                       a ControlSequence output 
                                       or another assignment\'s 
                                       depth value (for         
                                       meta-modulation in v2).  

  ModulationMatrix       ---           List of                  Excellent --- add
                                       ModulationAssignments    new
                                       per rhythm. Processes in source/destination
                                       dependency order.        types without
                                       Detects and rejects      refactoring
                                       circular dependencies.   
                                       Applies offsets each     
                                       block. Audio engine      
                                       reads final values from  
                                       here.                    
  ---------------------- ------------- ------------------------ --------------------

**4.7 Classes We Create --- UI Components**

  --------------------- ---------------------- ------------------------------
  **Class**             **Base**               **Purpose**

  MuClidLookAndFeel     juce::LookAndFeel_V4   Global look and feel. All
                                               colours via enum. No
                                               hardcoding elsewhere.

  KnobWithLabel         juce::Component        Rotary slider + category
                                               colour + label + value. Status
                                               bar callback on change.

  SegmentControl        juce::Component        2-5 option toggle bar.
                                               Configurable active state
                                               colour.

  NudgeInput            juce::Component        Number display + up/down
                                               arrows + step size buttons.

  TimeSelector          juce::Component        Note grid
                                               (1,1/2,1/4,1/8,1/16,1/32) +
                                               triplet/dotted + display. Used
                                               in delay, loop length, and
                                               modulator length settings.

  DropdownSelect        juce::Component        Styled wrapper around
                                               juce::ComboBox.

  PresetBrowser         juce::Component        Search + category filter +
                                               scrollable list. Shared by
                                               patch and rhythm browsers.

  SaveDialog            juce::Component        Name + description +
                                               category + cancel/save.

  PopupMenu             juce::Component        Custom styled popup with
                                               items, dividers, disabled
                                               states, arrow pointer.

  StepEditor            juce::Component        Bipolar bar graph step editor.
                                               Bars draggable up/down. Centre
                                               line = zero. All bars use the
                                               modulator's assigned colour
                                               regardless of value sign. Step
                                               count derived from loop length
                                               / step length. Truncates on
                                               resize.

  LFOEditor             juce::Component        Serum/Vital style drawable
                                               curve editor. Draggable
                                               control points. Curved or
                                               straight segments. Used for
                                               smooth modulator mode.

  AddButton             juce::TextButton       Dashed border + button. Opens
                                               PopupMenu on click.

  StatusBar             juce::Component        Rhythm colour tag + param
                                               name + value. Single source of
                                               parameter value feedback.

  ColourPickerPopup     juce::Component        Fixed 30-colour palette
                                               picker. Appears above rhythm
                                               colour dot.
  --------------------- ---------------------- ------------------------------

**4.8 Classes We Create --- UI Panels**

  --------------------- ----------------------------- ------------------------------
  **Class**             **Base**                      **Purpose**

  RhythmCircle          juce::Component, juce::Timer  Concentric ring display.
                                                      Euclid A outermost, Euclid B
                                                      second ring. No Mod rings.
                                                      Each ring rotates
                                                      independently
                                                      counter-clockwise, active step
                                                      at top. Pulse rings on hits.
                                                      Global visual pause button
                                                      support. Used in both
                                                      RhythmPanel (large) and
                                                      SidebarItem (small).

  RhythmSidebar         juce::Component               Left sidebar. Fixed width
                                                      \~82px. Contains one
                                                      SidebarItem per active rhythm
                                                      in order. Add rhythm button at
                                                      bottom. Scrollable if needed.

  SidebarItem           juce::Component, juce::Timer  One sidebar entry. Small
                                                      RhythmCircle (Euclid rings
                                                      only). Colour dot. Rhythm
                                                      name. Tab line on right edge
                                                      when selected connecting to
                                                      main panel. Pulse animation on
                                                      hit.

  EuclideanPanel        juce::Component               Always-visible euclidean
                                                      controls. Euclid A section,
                                                      logic row, Euclid B section,
                                                      Euclid C section stacked
                                                      vertically. Each section is
                                                      compact single row of knobs.

  ModulatorPanel        juce::Component               Modulator editing panel. Tabs:
                                                      Mod A through Mod H plus
                                                      Matrix. LFO/step editor with
                                                      playhead line showing current
                                                      position. Loop length
                                                      controls. Target list with
                                                      depth bars. CC/internal toggle
                                                      per modulator.

  ModMatrixPanel        juce::Component               Full modulation matrix table.
                                                      Source, destination, depth
                                                      columns. Meta-modulation
                                                      destination support.

  RhythmPanel           juce::Component,              Full rhythm editor. Compact
                        juce::FileDragAndDropTarget   layout: header bar + sample
                                                      bar + \[RhythmCircle top-left,
                                                      EuclideanPanel top-right\] +
                                                      VoiceSection + ModulatorPanel
                                                      stacked vertically.

  VoiceSection          juce::Component               Compact voice controls. Four
                                                      groups: amp envelope, filter,
                                                      filter envelope, output mode.
                                                      All in a single compact row.
                                                      Output mode toggle
                                                      (sample/MIDI) with note
                                                      selector.

  MixerOverlay          juce::Component               Replaces RhythmPanel when
                                                      mixer button active. Contains
                                                      channel strips for all active
                                                      rhythms + 3 FX returns +
                                                      master. Plus 3 FX rows below.
                                                      Horizontally scrollable.
                                                      Sidebar remains visible.

  MixerChannel          juce::Component               Single mixer channel strip.
                                                      Colour dot + name. Send
                                                      rotaries (where applicable).
                                                      Pan rotary. Fader + VUMeter
                                                      side by side. Mute + Solo
                                                      buttons. FX return channels
                                                      have intra-FX sends instead of
                                                      rhythm sends.

  VUMeter               juce::Component, juce::Timer  Peak VU meter. Green/amber/red
                                                      zones. Peak hold line.
                                                      Slow-falling ballistics. Reads
                                                      atomic float from MixerEngine
                                                      at \~30hz.

  FXRow                 juce::Component               Single FX unit row. On/off
                                                      toggle, name, algorithm
                                                      dropdown, parameter knobs
                                                      inline. Horizontally
                                                      scrollable if algorithm has
                                                      many params.

  TransportBar          juce::Component, juce::Timer  Logo (click=about), play/stop,
                                                      BPM, position, sync pill,
                                                      rhythm count, preset selector,
                                                      save, mixer button, gear
                                                      icon, + rhythm button.

  SettingsOverlay       juce::Component               Full settings page. Scrollable
                                                      single page with sections.

  AboutPanel            juce::Component               Version, company, links,
                                                      credits. Opened by clicking
                                                      logo.

  PluginEditor          juce::AudioProcessorEditor    Root editor. TransportBar +
                                                      \[RhythmSidebar \| RhythmPanel
                                                      or MixerOverlay\] + StatusBar.
  --------------------- ----------------------------- ------------------------------

**5. UI Component Library & Design System**

**5.1 Knob Colour Coding**

  ------------------ ------------------ ---------------------------------
  **Category**       **Colour**         **Used For**

  Euclidean          Purple #7F77DD     Steps, hits, rotate

  Padding            Teal #1D9E75       Pre pad, post pad

  Insert pad         Pink #D4537E       Insert start, insert length

  Filter             Teal #1D9E75       Cutoff, resonance, filter env
                                        depth

  Level / amplitude  Amber #EF9F27      Level, amplitude envelope

  FX sends           Coral #D85A30      Effect send, delay send, reverb
                                        send

  Intra-FX routing   Coral #D85A30      Effect→Delay, Effect→Reverb,
                                        Delay→Reverb sends

  Pitch              Purple #7F77DD     Pitch modulation

  Pan                Grey #888780       Pan

  Euclid C accent    Amber #EF9F27      Euclid C steps, hits, rotate,
                                        accent level, velocity

  Accent             Amber #EF9F27      Accent level knob

  VU meter           Green #1D9E75 /    Low/mid/clip zones on VU meter
                     Amber #EF9F27 /    
                     Red #E24B4A        

  Delay parameters   Teal #1D9E75       Feedback, spread, dirt

  Reverb parameters  Blue #378ADD       Size, diffusion, damp, pre-delay

  Modulation         Pink #D4537E       Modulator controls
  ------------------ ------------------ ---------------------------------

**5.2 Ring Colour Coding**

Each concentric ring on the RhythmCircle uses a fixed colour based on
its type. These are consistent across all rhythms regardless of the
rhythm\'s own colour.

*NOTE: MuClidLookAndFeel colour enum names are TBD --- to be defined at
the start of Stage 5 (UI component library). All colour references use
hex values and descriptive names only. The implementer must define the
full ColourIds enum in MuClidLookAndFeel.h before writing any component
drawing code.*

  ------------------ ------------------ ---------------------------------
  **Ring Type**      **Colour**         **Notes**

  Euclid A           Purple #7F77DD     Always outermost ring

  Euclid B           Coral #D85A30      Always second ring from outside

  Euclid C           Amber #EF9F27      Third ring (accent layer) shown
                                        as dashed outline ring

  Mod rings          Not shown on       Mod rings removed from circle
                     RhythmCircle       display. Mod visualisation via
                                        LFO editor playhead only.

  Mod B              Amber #EF9F27      Second control sequence ring

  Mod C              Pink #D4537E       Third control sequence ring

  Mod D              Blue #378ADD       Fourth control sequence ring

  Mod E--H           Cycling through    Additional rings if needed
                     remaining palette  
  ------------------ ------------------ ---------------------------------

**5.3 Segment Control Active State Colours**

  ------------------ ---------------------- -----------------------------
  **State**          **Style**              **Used For**

  Active --- general Purple bg #3C3489 /    Most toggle active states
                     border #7F77DD         

  Active ---         Teal bg #085041 /      Sync on, loop on, smooth mode
  positive           border #1D9E75         

  Active --- warning Amber bg #854F0B /     Mute active
                     border #EF9F27         

  Inactive           #2a2a2a bg / #444      All off states
                     border / #888 text     
  ------------------ ---------------------- -----------------------------

**5.4 Standard Control Behaviours**

- Knobs: drag up to increase, drag down to decrease, double click to
  type value

- All knob interactions report to status bar immediately --- no tooltips
  anywhere

- TimeSelector: note buttons in two rows (bars / beats) + triplet/dotted
  toggles, mutually exclusive

- NudgeInput: up/down arrows + step size buttons (1, 5, 10) + direct
  text entry

- StepEditor: drag bar up/down to set value. All bars same teal colour
  regardless of sign. Centre line = zero. +100 = top, -100 = bottom.

- LFOEditor: click to add point, drag to move, right-click to remove.
  Segments have curve handles.

- PresetBrowser: search covers name and description, category filters
  single select + all

- AddButton: dashed border, + prefix, click opens PopupMenu with
  already-added items greyed

- StatusBar: shows last moved control, never clears automatically,
  rhythm colour tag for rhythm params

- Delete rhythm: shows confirmation popup with rhythm name and
  red-tinted delete button

**5.5 Sample Bar States**

  --------------------- -------------------------- -----------------------
  **State**             **Display**                **Colour**

  No sample loaded      drop sample here or click  Muted #444
                        folder (italic)            

  Sample loaded         filename.wav               #999 normal

  Sample missing        sample missing --- click   Amber #EF9F27 warning
                        to locate                  
  --------------------- -------------------------- -----------------------

**6. Signal Flow**

**6.1 Audio Chain Per Rhythm**

**Sample mode (default):**

Sample file

-\> SamplePlayer (one shot / loop, SoundTouch stretching)

-\> Amplitude ADSR (retrigger on each hit --- see retrigger mode)

-\> ResonantFilter (StateVariableTPTFilter)

-\> Filter ADSR (depth knob controls sweep above base cutoff, retrigger)

-\> Sidechain gain reduction (if enabled, post-filter pre-fader)

-\> Channel fader (post-ADSR, post-filter gain. Default -6dB.
User-configurable default in Settings)

-\> FX sends (effect, delay, reverb) \[Internal routing only\]

-\> Internal mixer sum -\> Master output \[Internal routing\]

-\> OR: Direct DAW output bus per rhythm \[External routing\]

**MIDI mode (per rhythm toggle):**

Hit event -\> MidiOutputEngine -\> MIDI note on/off (user-definable
note, fixed velocity or from Euclid C)

NOTE: Voice chain bypassed entirely in MIDI mode

*NOTE: Control sequences modulate parameters via the ModulationMatrix
before DSP processing. In MIDI mode, ControlSequences assigned to CC
destinations output via MidiOutputEngine instead.*

**6.2 FX Send Knob Crossfade Behaviour**

  ------------- ------------------ ------------------ -------------------
  **Knob        **Effect send**    **Delay send**     **Reverb send**
  range**                                             

  0-50%         Dry 100%, send     Dry 100%, send     Dry unaffected,
                scales 0→100%      scales 0→100%      reverb scales
                                                      0→100%

  50-100%       Send stays 100%,   Send stays 100%,   Dry unaffected,
                dry scales 100→0%  dry scales 100→0%  reverb stays 100%
  ------------- ------------------ ------------------ -------------------

*NOTE: Reverb is always a pure send --- it never reduces the dry
signal.*

**6.3 Modulation Signal Flow**

Base parameter value (from APVTS)

\+ ControlSequence output × assignment depth (from ModulationMatrix)

= Final value passed to audio engine

ModulationMatrix processing order:

1\. Evaluate all ControlSequence outputs for current song position

2\. Sort ModulationAssignments by dependency (assignments targeting
other assignment depths come after their source)

3\. Apply each assignment: destination += source_output × depth

4\. Clamp all destination values to valid parameter ranges

*NOTE: Circular dependencies are detected at assignment creation time
and rejected. Meta-modulation (targeting another assignment\'s depth) is
architecturally supported but the UI for setting it is a v2 feature.*

**7. Sequencer Design**

**7.1 Euclidean Layer Parameters**

  --------------------- --------------- ----------------------------------
  **Parameter**         **Range**       **Description**

  Steps                 1-32            Total step count including all
                                        padding

  Hits                  0-steps         Hits distributed by euclidean
                                        algorithm. Default 0 on new layer.

  Rotate                0-steps-1       Rotation offset applied after
                                        distribution

  Pre pad               0-12            Empty steps forced at start of
                                        pattern

  Post pad              0-12            Empty steps forced at end of
                                        pattern

  Insert start          0-steps-1       Start position of insert pad zone

  Insert length         0-8             Length of insert pad zone

  Insert mode           Mute / Pad      Mute: hits distributed through gap
                                        but silenced. Pad: gap excluded
                                        from distribution.

  Logic                 OR / AND / XOR  How Euclid A and B combine to
                        / A only / B    produce the resultant pattern. OR
                        only            = either fires. AND = both must
                                        fire. XOR = one but not both. A
                                        only = A fires but B does not. B
                                        only = B fires but A does not.
                                        Applied globally to both layers,
                                        shown between the two Euclid
                                        panels.

  Mute                  On/Off          Exclude this layer from combined
                                        result. Per layer.

  Reset steps           1-256 / INF     Per-rhythm cycle length. After this
                                        many steps both generators (A and B)
                                        reset to step 0, pulling the
                                        polyrhythm back into alignment with
                                        the song. INF (std::nullopt) means
                                        free-running --- generators never
                                        reset. Display cycle uses LCM of A
                                        and B step counts when INF (capped
                                        at 256 for display only).
  --------------------- --------------- ----------------------------------

**7.1b Euclid C --- Accent Layer Parameters**

Euclid C is a third independent euclidean layer dedicated to accents. It
has no logic relationship with A and B. When Euclid C fires on the same
step that A+B produces a hit, that hit is accented. Euclid C firing on a
step with no A+B hit has no effect.

  --------------------- --------------- ----------------------------------
  **Parameter**         **Range**       **Description**

  Steps                 1-32            Step count for Euclid C pattern

  Hits                  0-steps         Hits distributed by euclidean
                                        algorithm

  Rotate                0-steps-1       Rotation offset

  Advance mode          Steps / Hits    Steps: Euclid C advances its read
                                        position on every step regardless
                                        of A+B. Hits: Euclid C only
                                        advances when A+B fires a hit.
                                        Same toggle concept as
                                        ControlSequence advance.

  Accent level          0 to +12dB      Level boost applied to hits that
                                        coincide with A+B

  Accent velocity       0-127           MIDI velocity for accented hits
                                        when in MIDI mode

  Mute                  On/Off          Disable accent layer without
                                        deleting it
  --------------------- --------------- ----------------------------------

**7.2 DAW Position Sync**

- Step position calculated from absolute DAW host position every block

- Formula: (host position in beats / step length in beats) % step count

- Uses global song timeline (beat position from 0.0.0.0 counting up) ---
  not clip-relative position

- Tempo changes handled automatically --- formula recalculates from
  absolute position every block, no internal counter

- BPM recalculated from DAW playhead every block to handle tempo
  automation correctly

- DAW loop points and clip boundaries have no special handling ---
  sequencer follows absolute timeline

- Scrubbing: step position updates silently, no samples triggered during
  scrub

- Ring rotation: animates only during active playback, static when
  stopped or scrubbing

- Ring rotation: updates immediately on manual playhead repositioning,
  polled at \~10-15hz when stopped

- Playback start mid-step: wait for next step boundary before firing
  first hit

- All rings start with step 1 / loop start at top (12 o\'clock) at song
  position 0.0.0.0

- Global setting: Sync to host position OR Reset on play (start from
  step 1)

- Per-rhythm override of global sync setting available

**7.3 Control Sequence Parameters**

  --------------------- ---------------- ---------------------------------
  **Parameter**         **Range**        **Description**

  Mode                  Smooth / Stepped Smooth: Serum/Vital style
                                         drawable LFO curve. Stepped: bar
                                         graph step sequencer.

  Polarity              Unipolar /       Unipolar: output range 0 to +100.
                        Bipolar          Bipolar: output range -100 to
                                         +100. Switching to unipolar
                                         preserves negative step values in
                                         memory but treats them as zero
                                         until switched back.

  Loop length --- note  1/1, 1/2, 1/4,   Base note value for the loop
  value                 1/8, 1/16,       length. Uses same TimeSelector
                        1/32 +           component as delay and loop
                        triplet/dotted   length.

  Loop length ---       1--16 (integer)  Multiplied by note value. e.g.
  multiplier                             1/4 × 3 = 3 quarter notes = 3/4
                                         bar loop.

  Step length --- note  1/1, 1/2, 1/4,   Stepped mode only. Duration of
  value                 1/8, 1/16,       each step.
                        1/32 +           
                        triplet/dotted   

  Step length ---       1--16 (integer)  Stepped mode only. Step count =
  multiplier                             loop length / step length.
                                         Automatically calculated.

  Step values           Array of floats, Stepped mode only. Stored in
                        -100 to +100     ValueTree. Truncated on step
                                         count decrease.

  Curve points          Array of (x,y)   Smooth mode only. Stored in
                        nodes with       ValueTree. Unlimited number of
                        optional bezier  nodes. X axis: normalised loop
                        handles per      position 0.0--1.0. Y axis:
                        segment          normalised output -1.0 to +1.0
                                         (mapped to -100 to +100 output).
                                         Default: two nodes at (0,0) and
                                         (1,0) joined by a straight line.
                                         Segments are straight lines by
                                         default. ALT-click on a segment
                                         creates a bezier handle at the
                                         click point which can be dragged
                                         to bend the segment. Bezier
                                         handle stored as offset from
                                         segment midpoint. Right-click a
                                         node to remove it. Click on the
                                         line (not a node or handle) to
                                         add a new node.
  --------------------- ---------------- ---------------------------------

**8. Voice Engine**

**8.1 Per-Rhythm Voice Chain**

  ---------------- ----------------------- -------------------------------
  **Stage**        **Parameters**          **Notes**

  Sample playback  Mode (one shot/loop),   Monophonic --- new hit cuts
                   quality                 previous with short overlap
                   (lo-fi/linear/clean),   fade. Always plays from
                   stretch mode            beginning.

  Voice cut        Overlap fade length     Outgoing voice fades out over
  behaviour        (1-10ms, default 2ms,   overlap duration while incoming
                   user configurable in    voice starts immediately.
                   Settings)               Prevents clicks. Independent of
                                           ADSR.

  Loop settings    Tempo (musical/free),   Only in loop mode. Musical uses
                   length (note selector + TimeSelector. Free uses ms.
                   triplet/dotted), fit    
                   (pitch/stretch)         

  Amplitude ADSR   Attack, decay, sustain, Reset: retriggers from zero on
                   release, retrigger mode each hit. Legato: retriggers
                   (Reset/Legato)          from current level. Default
                                           Reset.

  Resonant filter  Type (LP/HP/BP/notch),  StateVariableTPTFilter
                   cutoff, resonance       

  Filter ADSR      Attack, decay, sustain, Depth controls sweep above base
                   release, depth,         cutoff. Default Reset.
                   retrigger mode          
                   (Reset/Legato)          
  ---------------- ----------------------- -------------------------------

**8.2 Interpolation Quality**

  ---------------- ---------------- ----------- --------------------------
  **Setting**      **Algorithm**    **CPU**     **Character**

  Lo-fi (default)  Nearest          Very low    Aliasing and grit on
                   neighbour                    transposed notes. Lo-fi
                                                techno character.

  Linear           Linear           Low         Slight smoothing, still
                   interpolation                characterful

  Clean            Cubic            Medium      Smooth, professional
                   interpolation                quality
  ---------------- ---------------- ----------- --------------------------

**8.3 Sample File Handling**

- Sample file paths stored as absolute paths in the ValueTree

- On DAW project load: all samples reloaded immediately --- plugin is
  fully ready to play on open

- Missing sample detected immediately on load --- sample bar shows
  warning state without waiting for user interaction

- Auto-relocate missing samples: v2 feature (recursive folder search)

- No relative path fallback in v1 --- handled by locate button in sample
  bar

**9. FX Design**

**9.1 FX Unit Overview**

  ---------- ----------- -------------- ---------------------------------------
  **Slot**   **Name**    **Type**       **Send behaviour**

  1          Effect      Insert-style   0-50% blends in wet. 50-100% fades out
                                        dry.

  2          Delay       Insert-style   0-50% blends in wet. 50-100% fades out
                                        dry.

  3          Reverb      Send-style     Pure send. Dry signal unaffected.
  ---------- ----------- -------------- ---------------------------------------

*NOTE: Global FX parameters are not valid modulation destinations for
rhythm control sequences. FX are global --- modulating them from a
rhythm would break rhythm independence.*

**9.2 Intra-FX Routing**

- Three global send knobs route signal between FX return channels in the
  mixer

- Routing type: sends --- effected signal continues to its mixer channel
  normally, a copy is sent to the target

- Effect return channel: two routing knobs --- Effect→Delay send and
  Effect→Reverb send

- Delay return channel: one routing knob --- Delay→Reverb send

- Reverb return channel: no outgoing routing knobs --- end of chain

- UI placement: routing knobs sit in the FX return channel strip in the
  mixer, above pan

**9.3 Effect Unit Algorithms (v1)**

  --------------- -------------- ------------------------- ------------------ ------------------
  **Algorithm**   **Category**   **DSP Approach**          **Key Parameters** **Oversampling**

  Soft clip       Distortion     WaveShaper + tanh         Drive, output,     Optional
                                                           tone               
                                                           (LP/HP/BP/peak +   
                                                           freq/res/mix)      

  Hard clip       Distortion     WaveShaper + clamp        Drive, threshold,  Required --- 4x
                                                           output, tone       

  Foldback        Distortion     WaveShaper + fold math    Drive, folds,      Required --- 4x
                                                           output, tone       

  Bitcrush        Distortion     Custom quantise function  Bits, rate,        Required --- 2x
                                                           output, tone       

  Ladder filter   Filter         juce::dsp::LadderFilter   Cutoff, resonance, Drive path --- 2x
                                                           drive, mode        
                                                           (LP/HP/BP)         

  Chorus          Modulation     DelayLine + LFO mod       Rate, depth,       None
                                                           voices, spread,    
                                                           mix                

  Phaser          Modulation     Allpass chain + LFO       Rate, depth,       None
                                                           stages, feedback,  
                                                           mix                

  Comb filter     Filter         DelayLine + feedback      Freq, feedback,    None
                                                           output, mix        
  --------------- -------------- ------------------------- ------------------ ------------------

**9.4 Delay Parameters**

  ------------------ ----------------------------------------------------
  **Section**        **Parameters**

  Time --- sync mode Note selector (1,1/2,1/4,1/8,1/16,1/32) in
                     bars/beats rows + triplet/dotted toggles + ms
                     display at current BPM

  Time --- free mode ms input with nudge up/down arrows + step size
                     buttons (1, 5, 10ms)

  Repeats            Feedback, spread, dirt (saturation on feedback path)

  Intra-FX routing   Delay→Reverb send knob on Delay return channel in
                     mixer
  ------------------ ----------------------------------------------------

**9.5 Reverb Algorithms (v1)**

  --------------- --------------- --------------- ---------------------------
  **Algorithm**   **Library**     **Character**   **Parameters**

  Room            Signalsmith     Tight natural   Size, pre-delay, diffusion,
                                  space           damp, mod, dirt

  Hall            Signalsmith     Long lush decay Size, pre-delay, diffusion,
                                                  damp, mod, dirt

  Plate           Signalsmith /   Dense metallic  Size, pre-delay, diffusion,
                  FVerb                           damp, mod, dirt

  Spring          Custom          Lo-fi           Size, pre-delay, diffusion,
                                  mechanical      damp, drip, mod, dirt
  --------------- --------------- --------------- ---------------------------

*NOTE: Reverb has no mix knob. Shimmer removed from v1 --- requires
pitch shifting in feedback loop, added to v2.*

**10. UI Layout**

**10.1 Transport Bar**

  ---------------------- ---------------------- -------------------------
  **Element**            **Plugin mode**        **Standalone mode**

  μ-CLID logo (click)    Opens About panel      Opens About panel

  Play/stop button       Reflects DAW transport Drives internal clock

  BPM                    Read only from host    Editable with nudge + tap
                                                tempo

  Position (bar.beat)    Read only from host    Resets on stop

  Host sync pill         Pulses when synced     Shows internal clock
                                                status

  Rhythm count (n/8)     Display only           Display only

  Preset selector        Dropdown browser       Dropdown browser

  Save button            Opens save dialog      Opens save dialog

  Mixer button           Opens mixer overlay    Opens mixer overlay.
                         (replaces rhythm       
                         panel). Active state   
                         shows mixer is open.   

  \+ Rhythm button       Adds a new rhythm      Adds a new rhythm slot.
                         slot. Disabled when 8  Disabled when 8 rhythms
                         rhythms are active.    are active. Always
                         Always visible.        visible.

  Gear icon (click)      Opens Settings overlay Opens Settings overlay (+
                                                audio device)
  ---------------------- ---------------------- -------------------------

**10.2 Rhythm Panel --- Centre (Updated Design)**

The rhythm panel is the primary editing surface for the active rhythm.
It has a fixed vertical layout stacking from top to bottom:

**Header bar**

Contains: rhythm colour dot (click opens 30-colour palette picker),
rhythm name (double-click to rename), save rhythm button, presets
button, M (mute), S (solo), X (delete with confirmation).

**Sample bar**

Contains: folder icon, filename display (or \'drop sample here\'
placeholder in italic, or \'sample missing --- click to locate\' in
amber). Drag and drop target. Clear button on right.

**RhythmCircle**

Concentric ring display. Dark background. No clock hand. Each ring
rotates counter-clockwise independently. At song position 0.0.0.0 all
rings start with step 1 at top. Rings from outside to inside: Euclid A
(purple), Euclid B (coral), then Mod rings in assigned colours. Euclid A
and B always shown. Mod rings shown only for active modulators, hidden
from Mod A inward when window is too small. Global visual pause button
(TBD) freezes rotation without affecting playback.

Euclid rings show filled circles for hits and outline circles for empty
steps, evenly distributed. On a hit firing the expanding pulse ring
animation plays from that step position.

ControlSequence rings show the variable width band visualisation ---
ring width varies around its circumference to show the modulation shape.
Wider = higher positive value, narrower = lower or negative value. Up to
a threshold step count the arc indicator is visible. Above threshold
only the width shape is shown.

**EuclideanPanel**

Always visible. Contains Euclid A controls, the logic selector, and
Euclid B controls stacked vertically.

Euclid A section: label \'Euclid A\' in purple, mute toggle on right.
Two rows of 4 knobs each. Row 1: Steps, Hits, Rotate, Pre Pad (purple
knobs). Row 2: Post Pad (purple), Insert Start (pink), Insert Length
(pink), Insert Mode toggle (Mute/Pad, pink).

Logic selector: centred between the two euclid sections. Five pill
buttons: OR, AND, XOR, A only, B only. One active at a time.

Euclid B section: same layout as A but with coral colour border. Has its
own mute toggle.

**Voice section**

Compact four-group row below the circle and EuclideanPanel. Groups: (1)
Amp envelope --- A, D, S, R knobs plus Reset/Legato toggle. (2) Filter
--- cutoff, resonance, depth knobs plus LP/HP/BP/N type selector. (3)
Filter envelope --- A, D, S, R plus Reset/Legato toggle. (4) Output mode
--- sample/MIDI toggle. When sample: interpolation quality selector
(lo-fi/linear/clean). When MIDI: note selector showing user-definable
note (e.g. C2). FX send knobs (effect, delay, reverb) also in this
section.

**ModulatorPanel**

Tabbed panel below the EuclideanPanel. Tabs: Mod A, Mod B, Mod C, Mod D,
Mod E, Mod F, Mod G, Mod H, Matrix. Inactive mod tabs that have no
content are shown dimmed. A mod tab is active/enabled once it has at
least one target assignment. Tabs beyond the currently active set are
visible but dimmed to encourage use.

Mod tab contents (when a Mod tab is selected): Header row shows
modulator name, colour dot, smooth/stepped toggle, and internal/CC
toggle. The LFO curve editor (smooth mode) or step bar graph editor
(stepped mode) fills the central area. A vertical playhead line moves
across the graph to show current loop position. Below the editor: loop
length row with TimeSelector + multiplier nudge + result display. In
stepped mode: additional step length row + step count display. Target
list below: each assignment shows destination dropdown (parameter name
for internal, CC number for CC mode) and bipolar depth bar. Add target
button at bottom.

Matrix tab contents (when Matrix is selected): Hides all modulator
config. Shows a full table of all active modulation assignments across
all modulators. Columns: source (modulator name + colour dot),
destination (parameter name), depth (bipolar bar + value). Each row has
a remove button. Meta-modulation destinations (e.g. \'Mod A depth →
filter cutoff\') are shown with the full path. Add assignment button at
bottom.

**10.3 Settings Overlay --- Sections**

  --------------- -------------------------------------------------------
  **Section**     **Settings**

  Visual          Hit pulse style (off/flash/ring/both), ring expansion
                  size, sidebar item pulse on/off, centre hub pulse
                  on/off, step dot size, visual pause button position

  Sequencer       Sync behaviour (sync to host / reset on play), show ms
                  values alongside musical divisions

  Performance     Default interpolation quality (lo-fi/linear/clean),
                  oversampling quality (2x/4x per algorithm category)

  Voice           Default overlap fade length (1-10ms, default 2ms)

  Gain            Default fader position for new rhythms (-12dB to 0dB
                  range, default -6dB). Default master volume (-12dB to
                  0dB range, default -6dB).

  Presets         Default preset display + change + clear, restore
                  factory presets button

  Standalone      Audio device configure button, default BPM nudge input
  --------------- -------------------------------------------------------

**10.4 About Panel**

- Opened by clicking μ-CLID logo in transport bar

- Contains: large μ-CLID logo, version badge, company name,
  website/manual/changelog links

- Credits: JUCE, SoundTouch, Signalsmith Reverb, Bjorklund algorithm
  attribution

- Easter egg: frickin lasers --- v4 roadmap

**10.5 Window Size**

- Minimum: 780 × 580px --- sidebar, rhythm panel and status bar always
  fully visible

- Maximum: 2400 × 1600px

setResizeLimits(780, 580, 2400, 1600);

- All UI elements scale proportionally --- no hardcoded pixel values
  except in MuClidLookAndFeel

**10.6 Rhythm Sidebar**

- Fixed left sidebar, \~82px wide. Always visible including when mixer
  overlay is open.

- One SidebarItem per active rhythm. Order matches rhythm creation
  order. Reorder is v2 feature.

- SidebarItem shows: small RhythmCircle (Euclid A and B rings only,
  rotating during playback), rhythm colour dot below the circle, rhythm
  name below the dot.

- Selected rhythm: right edge shows a vertical tab line in the rhythm
  colour connecting the sidebar item to the main panel, giving a tab
  visual cue.

- Hit pulse: when a hit fires the sidebar item\'s circle pulses with the
  rhythm colour.

- Add rhythm button at the bottom of the sidebar. Dashed border, +
  symbol, rhythm label. Always visible, disabled when 8 rhythms active.

- When a rhythm is deleted the sidebar immediately removes its item. No
  placeholders for inactive slots.

- Sidebar is scrollable if more rhythms than fit at minimum window
  height (unlikely with max 8 rhythms).

- Drag-to-reorder sidebar items: v2 feature. RhythmSidebar must support
  variable item ordering from day one.

**10.7 Mixer Overlay**

The mixer overlay replaces the rhythm panel when the mixer button in the
transport bar is active. The sidebar remains visible. Clicking any
sidebar item while the mixer is open switches back to the rhythm panel
for that rhythm.

**Channel strip layout (top to bottom per strip):**

- Colour dot (rhythm channels) or coloured indicator (FX return
  channels)

- Channel name

- Send rotaries where applicable (rhythm channels: eff, dly, rev. Effect
  return: →dly, →rev. Delay return: →rev. Reverb return: none.)

- Pan rotary (all channels)

- Fader (vertical) + VUMeter side by side, touching with no gap

- Level readout in dB below fader

- Mute and Solo buttons side by side at the bottom

**Channel order (left to right):**

- Rhythm channels --- one per active rhythm, matching sidebar order

- Divider line

- Effect return channel (purple border)

- Delay return channel (teal border)

- Reverb return channel (blue border)

- Divider line

- Master channel (slightly wider, includes Internal/External routing
  toggle above the fader area)

**FX rows (below channel strips):**

- Three fixed rows: Effect, Delay, Reverb

- Each row: on/off toggle, name label, algorithm dropdown, then
  parameter knobs inline

- Horizontally scrollable per row if algorithm has more params than fit

- Effect row has its algorithm dropdown since it supports multiple
  algorithms

- EQ row: reserved space, shows \'EQ --- coming in v2\' placeholder

**Empty rhythm slots:**

- Rhythm channel strips only shown for active rhythms --- no placeholder
  columns for inactive slots

- Maximum 12 columns (8 rhythms + 3 FX returns + master) fits within
  planned window size without scrolling

**11. Preset & State System**

**11.1 Plugin State**

- Full state in APVTS ValueTree --- serialised by JUCE
  getStateInformation/setStateInformation

- DAW project save/restore is automatic --- full state restored on
  project open, samples reloaded immediately

- Preset name shows asterisk (\*) when state differs from last saved
  preset

- Fresh instance loads user default preset if set, otherwise factory
  demo patch

- Factory demo patch: 2-3 rhythms with euclidean patterns, no samples
  loaded, sequencer runs but silent

- New rhythm default state: Euclid A with 0 hits and OR logic, no
  sample, fader at -6dB, no modulators active

**11.2 Preset Storage**

- Patch presets: JUCE ValueTree serialised to XML files in user
  documents folder

- Rhythm presets: same format, single rhythm subtree

- Default preset setting: stored in ApplicationProperties /
  PropertiesFile (global, not per-project)

- Factory presets: shipped with plugin, stored in plugin resources,
  restorable from settings

**11.3 Preset Browser**

- Search covers name and description

- Category filters: techno, perc, ambient, experimental + all

- Right click preset → context menu: Load, Set as default (★ indicator),
  Delete

- Save dialog: name input + description input + category selector +
  cancel/save

- Rhythm preset preview: circle showing hit pattern + sample filename

**12. Third Party Libraries**

  ------------- ---------------- ---------------- ----------- --------------------
  **Library**   **Purpose**      **Licence**      **Cost**    **Notes**

  JUCE          Core framework   Indie commercial \$50/year   Required from day
                --- DSP, UI,                                  one
                plugin format                                 

  SoundTouch    Time stretching  LGPL             Free        Ship as DLL for LGPL
                v1                                            compliance. Grittier
                                                              character suits
                                                              lo-fi aesthetic.

  Signalsmith   Room, hall,      MIT              Free        Header-only, clean
  Reverb        plate reverb                                  integration

  FVerb         Plate reverb     Open source      Free        Simple header-only
                (alternative)                                 C++

  Rubber Band   Time stretching  GPL/Commercial   \~£499      Upgrade from
  Library       v2                                one-time    SoundTouch in v2.
                                                              Wrapped behind
                                                              TimeStretcherBase
                                                              interface --- no
                                                              refactor needed.
  ------------- ---------------- ---------------- ----------- --------------------

**12.1 LGPL Compliance for SoundTouch**

- Ship SoundTouch as a separate DLL rather than statically linking

- This satisfies LGPL requirement to allow users to relink against
  modified SoundTouch

- Document the SoundTouch version used in the About panel credits

**13. Build Stages**

  ----------- ---------------------- ------------------------------------------
  **Stage**   **What**               **Key Files**

  1           Euclidean logic ---    EuclideanGenerator, HitGenerator, Rhythm
              algorithm, padding,    
              accent                 

  2           DAW sync and step      SequencerEngine, PluginProcessor
              triggering             

  3           Sample playback engine SamplePlayer, TimeStretcherBase,
                                     SoundTouchStretcher, VoiceEngine

  4           Control sequences and  ControlSequence, ModulationMatrix,
              modulation             ModulationAssignment

  5           UI component library   All UI/Components/ files,
                                     MuClidLookAndFeel, StepEditor, LFOEditor

  6           Sidebar and rhythm     RhythmSidebar, SidebarItem, RhythmPanel,
              panel                  RhythmCircle, EuclideanPanel,
                                     VoiceSection, AccentHitGenerator

  7           Modulator panel and    ModulatorPanel, ModMatrixPanel,
              matrix                 MidiOutputEngine

  8           FX chain               All FX/ files, FXChain, FXRow,
                                     oversampling wrappers

  9           Mixer overlay and VU   MixerEngine, MixerOverlay, MixerChannel,
              meters                 VUMeter

  10          Transport bar,         TransportBar, PresetBrowser, SaveDialog,
              presets, settings,     SettingsOverlay, AboutPanel
              about                  

  11          Polish --- animations, StatusBar, all UI refinements, ring arc
              resize, status bar     animations, concentric ring pulse
                                     animations
  ----------- ---------------------- ------------------------------------------

**14. Feature List**

**v1 --- Core Release**

**Sequencer**

- Euclidean pattern generation per rhythm

- Up to 8 simultaneous polyrhythmic rhythms

- Two euclidean hit generators per rhythm (A and B) with combined logic:
  OR, AND, XOR, A only, B only

- Euclidean accent distribution --- accent hits distributed across
  active hit positions

- Accent level knob per euclidean layer

- Pre pad, post pad per euclidean layer

- Insert pad with start, length, Mute/Pad mode per euclidean layer

- Euclid C accent layer --- independent euclidean pattern for accents.
  Advance mode: steps or hits. Accent level and velocity per accented
  hit.

- MIDI output mode per rhythm --- toggle between sample playback and
  MIDI note trigger. User-definable note per rhythm.

- MIDI CC output from ControlSequences --- each modulator can target
  MIDI CC instead of internal parameter

- External routing mode --- global toggle bypasses internal mixer,
  routes each rhythm to its own DAW output bus

- DAW host sync with absolute position calculation --- follows global
  song timeline

- Sync to host position or reset on play (global + per rhythm override)

**Modulation**

- Up to 8 control sequences per rhythm (Mod A through Mod H)

- Each control sequence is a Serum/Vital style drawable LFO --- familiar
  to most producers

- Smooth mode: free-form drawable curve with control points and variable
  tension segments

- Stepped mode: bar graph step sequencer. Step values -100 to +100. Drag
  bars up/down.

- Smooth/stepped toggle per modulator

- Unipolar (0 to +100) or bipolar (-100 to +100) toggle per modulator

- Switching to unipolar preserves negative step values in memory ---
  non-destructive

- Loop length: note value × multiplier (1/1 to 1/32, ×1 to ×16). Any
  musical length expressible.

- Stepped mode step length: same note value × multiplier. Step count
  automatically derived.

- Step count change on resize: truncates from end. New steps at zero
  when expanding.

- Each modulator freely assignable to any per-rhythm parameter via mod
  matrix

- Multiple targets per modulator, each with independent depth -100 to
  +100

- Negative depth inverts the modulation direction

- Modulation matrix tab shows all assignments across all modulators in
  one view

- Rhythms are self-contained --- modulators cannot target parameters in
  other rhythms

- Global FX parameters are not valid modulation destinations

- Architecture supports meta-modulation (modulating another modulator\'s
  depth) in v2

**Sample Engine**

- WAV, AIFF, MP3, OGG support

- Drag and drop + file browser loading

- Absolute path storage --- sample missing warning on load if file not
  found

- One shot and loop playback modes

- Loop: musical length (note selector + triplet/dotted) or free

- Loop fit: pitch mode (playback rate) or stretch mode (SoundTouch)

- Interpolation quality: lo-fi (default), linear, clean

- Monophonic --- new hit cuts previous with short overlap fade. Always
  plays from beginning.

- Sample missing warning in sample bar with locate button

**Voice Engine**

- Amplitude ADSR per rhythm with retrigger mode (Reset/Legato)

- Short overlap fade on voice cut (1-10ms, user configurable, default
  2ms)

- Resonant filter per rhythm: LP, HP, BP, notch (StateVariableTPTFilter)

- Filter ADSR with depth knob and retrigger mode (Reset/Legato) per
  rhythm

**FX Chain**

- Effect slot: soft clip, hard clip, foldback, bitcrush, ladder filter,
  chorus, phaser, comb filter

- Oversampling on waveshaper algorithms: hard clip and foldback 4x,
  bitcrush 2x, ladder filter drive 2x

- Delay: ping pong, sync/free time, feedback, spread, dirt

- Reverb: room, hall, plate, spring algorithms (Signalsmith library)

- Effect/delay send crossfade (0-50% blend in, 50-100% fade dry)

- Reverb pure send --- no dry interaction

- Global intra-FX routing sends: Effect→Delay, Effect→Reverb,
  Delay→Reverb (knobs on FX strips)

**Mixer**

- Per-rhythm channel strip: fader, VU meter, pan, effect/delay/reverb
  sends, mute, solo

- FX return channels: Effect (with →Delay, →Reverb sends), Delay (with
  →Reverb send), Reverb (no sends). All with pan, fader, VU, mute, solo.

- Master channel with Internal/External routing toggle

- VU meters: peak with slow-falling ballistics and peak hold line.
  Green/amber/red zones.

- Mixer accessed via transport bar button. Replaces rhythm panel.
  Sidebar stays visible.

- All mixer parameters are valid modulation destinations for per-rhythm
  control sequences

**UI**

- Left sidebar with animated Euclid ring circles per rhythm

- Each ring has its own arc position indicator sweeping independently
  --- visual polyrhythm

- All arc indicators start aligned at top at song position 0.0.0.0

- Sidebar items appear and disappear immediately when rhythms are added
  or removed

- Expanding pulse rings on hit dots, proportional to circle size

- Dark background aesthetic --- colours appear as outlines and
  highlights

- Ring colours are fixed per sequence type across all rhythms

- Rhythm colour used for sidebar item, mixer channel colour dot, and
  rhythm header

- Full UI component library --- consistent controls throughout

- Status bar --- single source of parameter value feedback, never clears

- Transport bar with preset browser (search + categories) and save
  dialog

- Fixed colour palette picker (30 colours) opened by clicking rhythm
  colour dot

- Settings overlay (gear icon) --- visual, sequencer, performance,
  voice, presets, standalone sections

- About panel (logo click) --- version, company, credits, easter egg

- Resizable window --- minimum 780×580px, all elements scale
  proportionally

**General**

- VST3 and CLAP formats

- Full state save/restore via JUCE APVTS

- DAW automation: key parameters grouped by rhythm/FX/mixer

- User-definable default preset stored globally in plugin preferences

- Factory demo patch on fresh instantiation

- Confirmation dialog on rhythm delete

- Add rhythm button in transport bar --- always visible, disabled when 8
  rhythms active

- Default fader position -6dB for new rhythms, configurable in Settings

**v2 --- Depth and Polish**

- Standalone application with internal BPM, tap tempo, audio device
  selector

- Individual rhythm preset save/load with full browser

- Factory preset library shipped with plugin

- Hot-swap rhythm slots while playing (thread-safe atomic pointer)

- Per-channel fade out/in on rhythm swap

- Internal sidechain --- any rhythm as master, per-target
  depth/attack/release

- Meta-modulation UI --- assign a modulator\'s output to another
  modulator\'s depth value

- Rubber Band Library upgrade for stretch mode (via TimeStretcherBase
  interface swap)

- Auto-relocate missing samples --- recursive folder search

- Drag-to-reorder sidebar items

- HiDPI / Retina display support

- Undo/redo

- Shimmer reverb algorithm

- Bitcrusher per voice

- Probability per step

- Sample start/end point, reverse

- Choke groups

- Swing/groove per rhythm

- Variable step length per step

- Additional Effect algorithms

**v3 --- Power Features**

- MIDI input, clock sync, pattern control, scene switching (MIDI output
  already in v1)

- VST3 plugin hosting in Effect/Delay/Reverb slots (FXSlotBase interface
  already designed for this)

- Pattern chaining and scenes

- Multiple sidechain sources, selectable per target rhythm

- MIDI CC as modulation source

- Pattern randomise and morphing

- Multiple insert pads per euclidean layer

**Wishlist**

- Frickin lasers

**15. Business Plan**

**15.1 The Market**

- 60 million+ independent music creators globally as of 2024

- VST plugin market approximately \$800 million in 2025 and growing at
  \~16% per year

- 5 million+ plugin licences activated annually

- Techno is a global genre with passionate communities in Berlin,
  Detroit, London, Amsterdam, Tokyo

- Euclidean sequencer niche is small but highly engaged --- producers
  talk to each other

**15.2 Pricing**

  ---------------------- ----------- ------------------------------------
  **Tier**               **Price**   **Notes**

  Launch price (first    \$39        Drive early sales and reviews
  month)                             

  Standard price         \$50        Main ongoing price

  v1 to v2 upgrade       \$15        Loyalty pricing for existing
                                     customers

  Bundle (future)        TBD         If additional TDP plugins released
  ---------------------- ----------- ------------------------------------

**15.3 Annual Costs**

  ---------------------- ------------ ------------------------------------
  **Item**               **Cost**     **Notes**

  JUCE Indie licence     \$50/year    Required. Upgrade to Pro (\$800/yr)
                                      above \$50k revenue.

  Claude Pro             \$240/year   Primary development tool
  subscription                        

  Domain name            \$15/year    

  Website hosting        \$60/year    

  Distribution fees      10% of       Gumroad or Itch.io
                         revenue      

  Total fixed year 1     \~\$365 +    Break even at 8 sales
                         10%          
  ---------------------- ------------ ------------------------------------

**15.4 Realistic Sales Projections**

  ------------------ --------------- -------------------- ---------------------
  **Scenario**       **Year 1        **Revenue**          **Profit after fees**
                     sales**                              

  Pessimistic (no    50-200 copies   \$2,500-\$10,000     \$1,900-\$8,500
  marketing)                                              

  Realistic          200-800 copies  \$10,000-\$40,000    \$8,600-\$35,600
  (social + forums)                                       

  Good (YouTube +    500-2,000       \$25,000-\$100,000   \$22,100-\$89,600
  press)             copies                               

  Breakout (viral    5,000-20,000    \$250k-\$1M          \$224k-\$899k
  demo)              copies                               
  ------------------ --------------- -------------------- ---------------------

**15.5 Distribution Strategy**

**Phase 1 --- Launch**

- Sell via Gumroad --- zero setup cost, 10% fee, instant global reach

- List on KVR Audio --- essential for plugin community discovery

- Post on r/edmproduction, r/synthesizers, r/techno, r/producersofreddit

- Post on Gearspace in the instruments and effects forum

- Create a compelling demo video showing the rotating ring UI,
  polyrhythm, and modulation capabilities

**Phase 2 --- Growth**

- Apply to Plugin Boutique for wider distribution reach

- Reach out to Attack Magazine and Resident Advisor for techno
  production coverage

- Contact techno producers on YouTube for demo collaborations

- Move to own website with Stripe for lower fees (3-5% vs 10%)

**15.6 Copy Protection**

- Serial key validation via Polar (free tier available, no hardware
  dongle)

- No iLok --- adds friction, puts buyers off, not appropriate for indie
  at this scale

- Simple licence file approach --- industry standard for indie plugins

**15.7 Why μ-Clid Has Good Odds**

- Genuinely unique concept --- not another compressor or EQ

- Visually striking --- the rotating concentric ring circles with
  independent Euclid rings are shareable and demo-able

- Fills a real gap --- polyrhythm + sample triggering + euclidean with
  padding + integrated modulation is not done elsewhere at this depth

- Techno-native aesthetic --- lo-fi interpolation, bitcrush, spring
  reverb, insert pads, dark UI

- Low development cost --- Claude-assisted build means minimal sunk cost

- Expandable platform --- v1 ships tight, v2/v3 add significant value,
  upgrades generate ongoing revenue

**16. Key Decisions Reference**

  --------------------- --------------------- ----------------------------
  **Decision**          **Choice**            **Reason**

  Plugin name           μ-Clid by Transwarp   Musical, references Euclid,
                        Development Project   distinctive
                        (TDP)                 

  FX unit names         Effect, Delay, Reverb Effect is generic --- any
                                              algorithm not just
                                              distortion

  FX send behaviour     Effect/Delay          Musical wet/dry blend.
                        crossfade. Reverb     Reverb always ambient.
                        pure send.            

  Intra-FX routing      Global send knobs on  Simple, consistent signal
                        each FX strip         flow, not per-rhythm

  Time stretching v1    SoundTouch (LGPL,     Low cost, grittier character
                        free)                 suits lo-fi aesthetic

  Time stretching v2    Rubber Band           Quality upgrade when revenue
                        (commercial)          supports the £499 cost

  Stretcher             TimeStretcherBase     Zero refactoring needed when
  architecture          interface             upgrading to Rubber Band

  Reverb library        Signalsmith (MIT,     High quality, free
                        free)                 commercial use, easy
                                              integration

  Shimmer reverb        v2 feature            Requires pitch shifting in
                                              feedback loop --- too
                                              complex for v1

  Interpolation default Lo-fi (nearest        Lo-fi techno aesthetic, very
                        neighbour)            low CPU

  Oversampling          Per-algorithm flag in Reduces aliasing on
                        FXAlgorithmDef. Hard  aggressive distortion while
                        clip/foldback 4x,     keeping CPU efficient
                        bitcrush 2x, ladder   
                        drive 2x.             

  Accent                Euclidean             More musical than
                        distribution across   modulation, stays in phase
                        hit positions         with rhythm

  Euclidean layers      Always show both A    Encourages use of polyrhythm
                        and B, always visible and logic --- core feature

  Euclidean logic       OR, AND, XOR, A only, Comprehensive boolean
  options               B only                relationships between two
                                              patterns

  Mute                  Per euclidean layer   Independent control of each
                                              layer

  New euclid layer      0 hits, OR logic      Silent until user adds hits
  default                                     --- safe for live use

  Delete rhythm         Confirmation popup    Rhythm could have
                                              significant work in it

  Insert pad modes      Mute / Pad            Two distinct musical
                                              behaviours from same
                                              controls

  ADSR retrigger modes  Reset (default) and   Reset suits percussive use.
                        Legato, per voice.    Legato suits tonal/pad use.
                        Both amplitude and    
                        filter ADSR.          

  Voice cut behaviour   Short overlap fade    Prevents clicks without
                        (1-10ms, default 2ms, introducing rhythmic delay
                        user configurable)    

  Sample path storage   Absolute paths only   Simple, reliable for fixed
                                              library workflows

  Missing sample on     Immediate warning in  User knows before hitting
  load                  sample bar            play

  Auto-relocate missing v2 feature            Useful but not critical for
  samples                                     v1

  DAW sync              Global song timeline  Correct behaviour for
                        always, no            Bitwig/Ableton clip-based
                        clip-relative         workflows
                        position              

  Scrub behaviour       Silent --- position   Chaotic audio during scrub
                        updates, no triggers  would be wrong

  Arc indicators when   Static. Update        Responsive without wasting
  stopped               immediately on manual CPU on animation when paused
                        playhead move         
                        (\~10-15hz poll).     

  Playback start        Wait for next step    No partial or clipped hits
  mid-step              boundary              on play

  Clock hand            Removed. Each ring    Single clock hand cannot
                        rotates               represent polyrhythmic
                        counter-clockwise so  sequences with different
                        active step is always lengths
                        at top.               

  Ring circle design    Concentric rings,     Euclidean is primary
                        outermost = Euclid A, feature, visually dominant.
                        then B, then Mod      Modulators secondary.
                        rings inward          

  Ring colours          Fixed per sequence    Type identity more important
                        type across all       than rhythm identity on ring
                        rhythms. Euclid A     level
                        purple, Euclid B      
                        coral, Mod A teal     
                        etc.                  

  Ring rotation         Each ring rotates     More dramatic than arc
  animation             counter-clockwise,    indicator. Polyrhythm
                        active step always at visible as rings drift at
                        top. Global visual    different rates.
                        pause button. Mod A   
                        hidden first when     
                        space limited.        

  Ring spacing          Equal gap between     Consistent layout. Euclid
                        rings, constant       always visible as primary
                        RING_SPACING_PX.      feature.
                        Euclid rings always   
                        shown. Mod rings      
                        hidden from Mod A     
                        inward when space     
                        limited.              

  Smooth LFO curve      Unlimited nodes.      Familiar to Serum/Vital
                        Straight lines by     users. Straight default is
                        default. ALT-click    simple, bending is
                        segment creates       discoverable.
                        bezier handle.        

  Default fader gain    -6dB for new rhythms. Prevents clipping when
                        User-configurable in  multiple rhythms active.
                        Settings (-12dB to    Musical default.
                        0dB range).           

  Add rhythm button     Always visible in     Discoverable. Consistent
                        transport bar.        location regardless of
                        Disabled when 8       rhythm count.
                        rhythms active.       

  MuClidLookAndFeel     TBD --- to be defined Defer until component
  colour enum           at start of Stage 5.  library build begins.

  Modulation system     Serum/Vital style     Removes need for separate
                        drawable LFO per      LFO and step sequencer
                        modulator. Familiar   systems. One unified
                        to producers.         approach.

  Modulation sources    ControlSequence       Simpler architecture, more
                        replaces              flexible musically, familiar
                        ParameterSequence and UI paradigm
                        LFO as unified system 

  Modulation            Any per-rhythm        Rhythm self-containment
  destinations          parameter: voice      required for hot-swap. FX
                        chain + mixer. Global are global.
                        FX excluded.          

  Modulator count       Up to 8 per rhythm    Sufficient for complex
                        (Mod A through Mod H) modulation without
                                              overwhelming UI

  Modulator targets     Multiple targets per  One shape driving multiple
                        modulator, each with  parameters simultaneously
                        independent depth     

  Depth range           Depth -100 to +100.   Bipolar depth allows
                        Step values -100 to   inversion. Consistent range
                        +100.                 throughout.

  Depth inversion       Negative depth        Same shape, opposite effect.
                        inverts modulation    Useful for complementary
                        direction             movements.

  Polarity toggle       Unipolar (0-100) or   Different parameters suit
                        bipolar (-100 to      different ranges
                        +100) per modulator   

  Unipolar/bipolar      Negative values       Non-destructive. Switch back
  switch                preserved in memory   restores original values.
                        when switching to     
                        unipolar              

  Step count derivation Loop length / step    No separate step count
                        length = step count   control needed. Musically
                        (automatic)           consistent.

  Step count on resize  Truncate from end.    Simple and predictable. Most
                        New steps at zero     common hardware sequencer
                        when expanding.       behaviour.

  Mod matrix            Tab in modulator      Complete picture of all
                        panel. Shows all      modulation in one view.
                        assignments. Clicking 
                        row goes to that      
                        modulator.            

  Meta-modulation       Architecturally       Build correct now, surface
                        supported (string     the feature later.
                        IDs). UI in v2.       

  Sidebar item order    Creation order.       Simple for v1, RhythmSidebar
                        Drag-to-reorder in    architected for reorder from
                        v2.                   day one.

  Sidebar item          Items                 Simpler, clarity over
  animation             appear/disappear      decoration.
                        immediately on        
                        add/delete. No glide  
                        animation.            

  Undo/redo             v2 feature            Significant implementation
                                              effort, DAW project save
                                              covers the main need

  MIDI learn            Not needed            DAW handles this at host
                                              level

  HiDPI support         v2 feature            Use JUCE scaled coordinates
                                              throughout for easy retrofit

  Default preset        User-definable,       Persists globally across all
                        stored in plugin      sessions
                        prefs                 

  Copy protection       Polar serial key, no  Low friction for buyers,
                        dongle                appropriate for indie scale

  Distribution launch   Gumroad + KVR Audio   Zero cost to start, maximum
                                              reach

  Launch price          \$39 then \$50        Drive early reviews,
                                              sustainable ongoing price

  visibleWhen schema    notValue condition    Future-proof --- new
                        only --- { paramId,   dropdown options
                        notValue }            automatically visible

  Plugin formats        VST3 + CLAP +         CLAP is native Bitwig
                        Standalone from v1    format, zero extra code
                                              cost, open source

  Unit testing          JUCE unit test        DSP and UI tested manually
                        framework. Pure logic in Standalone and Bitwig
                        classes only.         

  Coding principles     10 principles         Consistent quality, readable
                        defined. Paste        code, spec authority
                        section 25 into every 
                        Claude Code session.  
  --------------------- --------------------- ----------------------------

**17. Starting a New Claude Code Session**

Paste this briefing at the start of each coding session:

*I am building a VST3/CLAP audio plugin called μ-Clid (mu-Clid) --- a
Euclidean rhythm sequencer and sample trigger by Transwarp Development
Project (TDP). Built with JUCE and CMake. JUCE is at \$ENV{JUCE_PATH}.
Manufacturer code TDP1, plugin code MCld. Formats: VST3, CLAP,
Standalone. The full project guide is in my documents. We are currently
working on \[STAGE NAME\]. The file I need help with is \[FILENAME\].
Here is the relevant context: \[paste related headers or previous
code\]. Please follow the coding principles in section 25 of the project
guide throughout this session.*

**Useful Starting Prompts**

- Set up a new JUCE VST3/CLAP plugin project using CMake with JUCE at
  \$ENV{JUCE_PATH}, company Transwarp Development Project, manufacturer
  code TDP1, plugin code MCld, product name mu-Clid, formats VST3, CLAP,
  and Standalone

- Implement EuclideanGenerator.cpp --- static method taking steps, hits,
  pre pad, post pad, insert pad start, insert pad length, insert pad
  mode (Mute/Pad), returns std::vector\<StepState\> where StepState is
  an enum (Empty, Hit, PrePad, PostPad, InsertMuted, InsertPad,
  AccentHit)

- Write the ControlSequence class --- smooth and stepped modes, loop
  length as note value × multiplier, stepped mode derives step count
  from loop/step length, unipolar/bipolar toggle, output range -100 to
  +100, serialises to ValueTree

- Write the ModulationMatrix class --- list of ModulationAssignments,
  processes in dependency order, detects circular dependencies, applies
  offsets each block using string ID destinations

- Write the KnobWithLabel JUCE Component --- rotary slider with category
  colour, label below, value below. On change fires a std::function
  callback for the status bar.

- Write the StepEditor JUCE Component --- bipolar bar graph. Bars
  draggable up/down. Centre line = zero. All bars same teal colour. +100
  = top, -100 = bottom. Step count set externally.

- Write the LFOEditor JUCE Component --- Serum/Vital style drawable
  curve. Draggable control points. Curved segments with tension handles.
  Smooth/stepped toggle affects display.

- Write the TimeStretcherBase interface and SoundTouchStretcher
  implementation

- Write VoiceEngine monophonic trigger handling with short overlap fade
  --- outgoing voice fades out over configurable duration (default 2ms)
  while incoming voice starts immediately from zero

- The build is giving this error: \[paste error\] --- explain it and fix
  it

- Review this class for thread safety --- it will be called from both
  the audio thread and the message thread

**17.1 Reference CMakeLists.txt**

Use this as the starting point for Stage 1 project setup. Copy verbatim
and add new .cpp files to target_sources as each class is created.

cmake_minimum_required(VERSION 3.22)

project(mu-clid VERSION 1.0.0)

add_subdirectory(\$ENV{JUCE_PATH} JUCE)

juce_add_plugin(mu-clid

COMPANY_NAME \"Transwarp Development Project\"

PLUGIN_MANUFACTURER_CODE TDP1

PLUGIN_CODE MCld

FORMATS VST3 CLAP Standalone

PRODUCT_NAME \"mu-Clid\"

IS_SYNTH FALSE

NEEDS_MIDI_INPUT FALSE

NEEDS_MIDI_OUTPUT FALSE

IS_MIDI_EFFECT FALSE

EDITOR_WANTS_KEYBOARD_FOCUS FALSE

)

target_sources(mu-clid

PRIVATE

Source/PluginProcessor.cpp

Source/PluginEditor.cpp

\# Add new .cpp files here as each class is created

)

target_compile_definitions(mu-clid

PUBLIC

JUCE_WEB_BROWSER=0

JUCE_USE_CURL=0

JUCE_VST3_CAN_REPLACE_VST2=0

JUCE_USE_MP3AUDIOFORMAT=1

)

target_link_libraries(mu-clid

PRIVATE

juce::juce_audio_utils

juce::juce_dsp

juce::juce_gui_basics

juce::juce_gui_extra

juce::juce_audio_formats

PUBLIC

juce::juce_recommended_config_flags

juce::juce_recommended_lto_flags

juce::juce_recommended_warning_flags

)

\# SoundTouch (LGPL - link as DLL for compliance)

add_subdirectory(ThirdParty/soundtouch)

target_link_libraries(mu-clid PRIVATE SoundTouch)

\# Signalsmith Reverb (MIT - header only)

target_include_directories(mu-clid PRIVATE
ThirdParty/signalsmith-reverb)

\# Unit Tests

add_executable(mu-clid-tests

Tests/EuclideanGeneratorTests.cpp

Tests/HitGeneratorTests.cpp

Tests/ControlSequenceTests.cpp

Tests/ModulationMatrixTests.cpp

)

target_link_libraries(mu-clid-tests PRIVATE juce::juce_core)

Build commands:

cmake -B build -DCMAKE_BUILD_TYPE=Debug

cmake \--build build

Output: build/mu-clid_artefacts/VST3/mu-Clid.vst3

**18. UI Design Reference**

**18.1 Transport Bar (Locked)**

The transport bar design is finalised. Key decisions: logo area
(μ-CLID + tagline only, no company name) opens the About dialog on
click. HOST SYNC border encloses the play button, BPM, and POS
indicators as a unified group showing DAW control. Preset selector shows
name only with a browse icon on the right --- no label.

**18.2 Voice Editor (Locked)**

The Voice button and the sequencer are mutually exclusive --- pressing
Voice replaces the entire sequencer area (circle, tabs, knob row) with
the voice editor. The Clock button similarly replaces the sequencer with
clock/sync controls. Pressing the active button again returns to the
sequencer. The rhythm top bar remains visible in all states. Signal flow
is left to right: Pitch → Filter → Amp → Insert FX. Each stage shows an
ADSR envelope shape above its knobs (labelled A, D, S, R). Pitch and
Filter each have an additional Depth knob. Filter type is a dropdown
(Low pass shown) to allow new filter algorithms without UI changes.
Insert FX is also a dropdown (Bitcrush shown) --- coded as a slot so
additional insert algorithms can be registered later without UI changes.
The waveform displays full width under the sample bar with − / zoom / +
controls. In Loop mode, draggable Start (teal) and End (pink) markers
appear on the waveform. Play mode, Quality and Reverse sit in a compact
bar between the waveform and the signal chain. Amplitude and filter ADSR
each have a Retrigger toggle (Reset / Legato).

**18.3 Rhythm Panel --- Full Layout (Updated)**

The RhythmCircle shows concentric rings. Dark background (#1C1C1B). No
clock hand. Euclid A is the outermost ring in purple. Euclid B is the
second ring in coral. Control sequence rings appear inward in their
assigned colours (Mod A teal, Mod B amber etc.). Each ring only appears
if that sequence exists. Each ring has an independent arc position
indicator --- a one-step-wide semi-transparent arc with faded edges that
sweeps smoothly around as the song plays. All arcs start at the top at
song position 0.0.0.0. Euclid rings show filled circles for hits and
outline circles for empty steps. ControlSequence rings show a
variable-width band where the ring width reflects the modulation value
at each angular position.

The EuclideanPanel is always visible below the circle. It shows: Euclid
A section (purple border) with its label, mute button, and two rows of 4
knobs (Steps, Hits, Rotate, Pre Pad in row 1; Post Pad, Insert Start,
Insert Length, Insert Mode toggle in row 2). Below Euclid A: the logic
selector row with five pills: OR, AND, XOR, A only, B only. Below that:
Euclid B section (coral border) with identical layout.

The ModulatorPanel is always visible below the EuclideanPanel. It shows
a tab row: Mod A through Mod H (dimmed if unused) and Matrix. When a Mod
tab is selected the panel shows: modulator name and colour,
smooth/stepped toggle, LFO curve editor or step bar graph editor
depending on mode, loop length row (TimeSelector + multiplier nudge +
result display), step length row in stepped mode (same controls + step
count display), and the target list with destination + depth per
assignment. When the Matrix tab is selected the panel hides the
modulator config and shows the full assignment table.

**19. APVTS Parameter Schema**

All plugin parameters are registered with AudioProcessorValueTreeState
(APVTS) at construction time using fixed slot indices. Dynamic data that
cannot be registered upfront (ControlSequence curve/step data,
modulation assignments, per-algorithm FX values) is stored directly in
the ValueTree.

**19.1 Constants**

MAX_RHYTHMS = 8

MAX_EUCLID_LAYERS = 2 // A and B, always both present

// Euclid C (accent) is a separate AccentHitGenerator, not counted in
MAX_EUCLID_LAYERS

MAX_CONTROL_SEQUENCES = 8 // Mod A through Mod H per rhythm

**19.2 Per-Rhythm APVTS Parameters (× 8 slots)**

All parameters below are registered for each rhythm slot 0--7. Replace
{n} with the slot index (0--7) and {layer} with euclidA or euclidB.

**Euclidean layers (registered for euclidA and euclidB):**

- rhythm{n}\_{layer}\_steps --- int, range 1--32

- rhythm{n}\_{layer}\_hits --- int, range 0--steps

- rhythm{n}\_{layer}\_rotate --- int, range 0--(steps-1)

- rhythm{n}\_{layer}\_accent_hits --- int, range 0--hits

- rhythm{n}\_{layer}\_accent_level --- float, range 0.0--1.0

- rhythm{n}\_{layer}\_pre_pad --- int, range 0--12

- rhythm{n}\_{layer}\_post_pad --- int, range 0--12

- rhythm{n}\_{layer}\_insert_start --- int, range 0--(steps-1)

- rhythm{n}\_{layer}\_insert_length --- int, range 0--8

- rhythm{n}\_{layer}\_insert_mode --- int, 0=Mute 1=Pad

- rhythm{n}\_{layer}\_mute --- bool

- rhythm{n}\_logic --- int, 0=OR 1=AND 2=XOR 3=AOnly 4=BOnly (one per
  rhythm, not per layer)

**Euclid C accent layer:**

- rhythm{n}\_euclidC_steps --- int, range 1--32

- rhythm{n}\_euclidC_hits --- int, range 0--steps

- rhythm{n}\_euclidC_rotate --- int, range 0--(steps-1)

- rhythm{n}\_euclidC_advance_mode --- int, 0=Steps 1=Hits

- rhythm{n}\_euclidC_accent_level --- float, range 0.0--12.0dB

- rhythm{n}\_euclidC_accent_vel --- int, range 0--127 (MIDI velocity for
  accented hits)

- rhythm{n}\_euclidC_mute --- bool

**Output mode:**

- rhythm{n}\_output_mode --- int, 0=Sample 1=MIDI

- rhythm{n}\_midi_note --- int, range 0--127 (MIDI note number, default
  36 = C2)

**Amplitude envelope:**

- rhythm{n}\_amp_attack --- float, range 0.001--10.0s

- rhythm{n}\_amp_decay --- float, range 0.001--10.0s

- rhythm{n}\_amp_sustain --- float, range 0.0--1.0

- rhythm{n}\_amp_release --- float, range 0.001--20.0s

- rhythm{n}\_amp_retrigger --- int, 0=Reset 1=Legato

**Filter:**

- rhythm{n}\_filter_type --- int, 0=LP 1=HP 2=BP 3=Notch

- rhythm{n}\_filter_cutoff --- float, range 20.0--20000.0Hz (log)

- rhythm{n}\_filter_resonance --- float, range 0.0--1.0

**Filter envelope:**

- rhythm{n}\_filter_env_attack --- float, range 0.001--10.0s

- rhythm{n}\_filter_env_decay --- float, range 0.001--10.0s

- rhythm{n}\_filter_env_sustain --- float, range 0.0--1.0

- rhythm{n}\_filter_env_release --- float, range 0.001--20.0s

- rhythm{n}\_filter_env_depth --- float, range 0.0--1.0

- rhythm{n}\_filter_env_retrigger --- int, 0=Reset 1=Legato

**Sample playback:**

- rhythm{n}\_sample_mode --- int, 0=OneShot 1=Loop

- rhythm{n}\_sample_quality --- int, 0=LoFi 1=Linear 2=Clean

- rhythm{n}\_sample_stretch_mode --- int, 0=Pitch 1=Stretch

- rhythm{n}\_sample_loop_tempo --- int, 0=Musical 1=Free

- rhythm{n}\_sample_loop_length --- float, note value index (musical) or
  ms (free)

- rhythm{n}\_sample_loop_fit --- int, 0=Pitch 1=Stretch

**Mixer:**

- rhythm{n}\_fader --- float, range 0.0--1.0

- rhythm{n}\_pan --- float, range -1.0--1.0

- rhythm{n}\_mute --- bool

- rhythm{n}\_effect_send --- float, range 0.0--1.0

- rhythm{n}\_delay_send --- float, range 0.0--1.0

- rhythm{n}\_reverb_send --- float, range 0.0--1.0

**19.3 FX APVTS Parameters**

**Effect slot:**

- fx_effect_algorithm --- int, index into registered algorithm list

- fx_effect_send --- float, range 0.0--1.0

- fx_effect_to_delay_send --- float, range 0.0--1.0

- fx_effect_to_reverb_send --- float, range 0.0--1.0

**Delay:**

- fx_delay_time --- float, note value index (sync) or ms (free)

- fx_delay_feedback --- float, range 0.0--1.0

- fx_delay_spread --- float, range 0.0--1.0

- fx_delay_dirt --- float, range 0.0--1.0

- fx_delay_sync --- bool

- fx_delay_send --- float, range 0.0--1.0

- fx_delay_to_reverb_send --- float, range 0.0--1.0

**Reverb:**

- fx_reverb_algorithm --- int, 0=Room 1=Hall 2=Plate 3=Spring

- fx_reverb_size --- float, range 0.0--1.0

- fx_reverb_predel --- float, range 0.0--500.0ms

- fx_reverb_diffusion --- float, range 0.0--1.0

- fx_reverb_damp --- float, range 0.0--1.0

- fx_reverb_mod --- float, range 0.0--1.0

- fx_reverb_dirt --- float, range 0.0--1.0

- fx_reverb_send --- float, range 0.0--1.0

**19.4 Global APVTS Parameters**

- master_volume --- float, range 0.0--1.0

- master_bpm --- float, range 20.0--300.0 (standalone mode only)

- output_routing_mode --- int, 0=Internal 1=External. Global toggle.
  Internal: all rhythms to internal mixer. External: each rhythm to its
  own DAW output bus.

**19.5 ValueTree Dynamic Data (not APVTS registered)**

The following data lives in the ValueTree directly and is serialised via
getStateInformation/setStateInformation. It is not registered as APVTS
parameters.

- Per-rhythm ControlSequence data --- one subtree per sequence per
  rhythm. Contains: mode (smooth/stepped), polarity (unipolar/bipolar),
  loop note value, loop multiplier, step note value (stepped only), step
  multiplier (stepped only), step values array (stepped only, float -100
  to +100), curve points array (smooth only, each point is x/y/tension)

- Per-rhythm ModulationAssignment list --- array of {sourceId,
  destinationId, depth} objects. sourceId is a ControlSequence output
  string ID. destinationId is an APVTS parameter ID or a modulator depth
  string ID (for v2 meta-modulation).

- Per-effect algorithm-specific parameter values --- stored as a subtree
  keyed by algorithm ID

- Rhythm names and colours

- Rhythm slot active/inactive state

- Sample file paths (absolute paths)

- ControlSequence CC assignments --- when a modulator targets a MIDI CC,
  stores CC number (0-127) as destination instead of APVTS parameter ID

**19.6 Modulation String ID Conventions**

String IDs used in ModulationAssignment must follow these conventions to
enable meta-modulation in v2:

- ControlSequence output: mod\_{n}\_{seq}\_output --- e.g.
  mod_0_A_output (rhythm 0, Mod A)

- Assignment depth: mod\_{n}\_{seq}\_depth\_{dest} --- e.g.
  mod_0_A_depth_rhythm0_filter_cutoff (rhythm 0, Mod A, targeting filter
  cutoff)

- All destination IDs match APVTS parameter IDs exactly --- e.g.
  rhythm0_filter_cutoff

**20. RhythmCircle Drawing Spec**

**20.1 Step Dot Positions**

- Dot positions are mathematically computed: angle = −90° + i × 360° /
  steps

- All dots are perfectly evenly spaced regardless of step count

- First step is always at the top (12 o\'clock)

- Each ring computes its own dot positions based on its own step count

- Ring spacing: equal gap between all rings, defined by named constant
  RING_SPACING_PX in MuClidLookAndFeel

- Euclid A and B always shown regardless of window size or active state

- Mod rings shown only for active modulators (those with at least one
  target assignment)

- When window is too small to show all active Mod rings: rings hidden
  from Mod A inward. Mod A hidden first, then Mod B etc. Euclid rings
  never hidden.

- Ring visibility rule: a Mod ring is hidden if its calculated inner
  radius would fall below named constant RING_MIN_RADIUS_PX

- Named constant RING_SCALE_FACTOR controls overall ring size relative
  to component bounds for global size adjustment

**20.2 Euclid Ring Visual States**

- Hit dot --- filled circle, full sequence type colour, 100% opacity.
  Radius = R × 0.095

- Empty step dot --- outline circle only, sequence type colour, 45%
  opacity. Radius = R × 0.075

- Pad dot (pre/post/insert) --- outline circle only, sequence type
  colour, 30% opacity. Radius = R × 0.038

- All dots use the ring\'s fixed sequence type colour

- R = radius of that specific ring

**20.3 Ring Rotation Animation**

- Each ring rotates independently counter-clockwise so the active step
  is always at the top

- Rotation angle: -(currentStep / totalSteps) x 360, calculated from
  absolute song position each frame

- Step 1 at top (12 o\'clock) at song position 0.0.0.0. Applies to
  Euclid A, B, and C rings.

- All rings start aligned. As the song plays rings drift apart at
  different rates making polyrhythm physically visible

- When two rings briefly align it marks a musical moment where patterns
  coincide

- Rotation is smooth and continuous, not snapping between steps

- Static when playback is stopped. Updates immediately on manual
  playhead repositioning (\~10-15hz poll).

- Global visual pause button freezes all rotation without affecting
  audio. Position TBD. Purely cosmetic.

**20.4 ControlSequence Ring Visual**

- Variable width band --- the ring stroke width varies around its
  circumference to reflect the modulation value

- Minimum width: thin track line (muted background colour)

- Maximum width: R × 0.08 at maximum positive value

- The LFO curve shape is drawn as a variable-width band around the ring
  circumference then the entire ring rotates as the loop plays

- The start of the loop (position 0) is at the top (12 o'clock), same
  convention as Euclid rings

- Rotation driven by loop position --- current loop position always at
  top

- Ring colour: sequence type colour at 20% opacity for band fill, 60%
  highlight at current position (top)

**20.5 Pulse Rings**

- Trigger: fires on every Euclid hit as the ring rotates the hit dot
  through the top position

- Shape: expanding circle centred on the hit dot

- Start radius: hit dot radius

- Max expansion: R × 0.18

- Duration: 0.55 seconds, linear fade

- Opacity: fades from 0.8 to 0 over duration

- Stroke weight: reduces from 2px to 0.5px as ring expands

- Colour: sequence type colour of the ring that fired

- Multiple pulses can be active simultaneously

- Sidebar item pulse: on/off user setting in Settings overlay

- Centre hub pulse: on/off user setting

**20.6 Track Ring**

- Thin circle drawn at radius R behind all dots

- Colour: muted background tone

- Stroke width = R × 0.06

**21. FXAlgorithmDef Schema**

FXAlgorithmDef is a pure data class. FXConfigPanel renders its UI
entirely from the definition --- no algorithm-specific code in the UI
layer.

**21.1 Top-Level Fields**

- id --- string. Unique algorithm identifier.

- name --- string. Display name.

- category --- enum: Distortion / Filter / Modulation.

- oversampling --- int. 0=none, 2=2x, 4=4x.

- rows --- ordered list of Row objects.

**21.2 Row**

- params --- ordered list of Param objects displayed left to right.

**21.3 Param Fields**

- id --- string. Unique within this algorithm.

- label --- string. Display label.

- type --- enum: knob / toggle / dropdown.

- range --- float pair \[min, max\]. Knobs only.

- default --- float. Default value on algorithm load.

- options --- string list. Dropdown only.

- visibleWhen --- optional. { paramId, notValue }

**21.4 visibleWhen Condition**

- Single condition only --- no compound AND/OR.

- paramId --- string. ID of another param within the same algorithm.

- notValue --- float or string. Param is VISIBLE when paramId does NOT
  equal this value.

- If visibleWhen is absent the param is always visible.

**21.5 Example --- Soft Clip**

FXAlgorithmDef {

id: \"soft_clip\"

name: \"Soft clip\"

category: Distortion

oversampling: 0

rows: \[

Row { params: \[ {id:\"drive\", label:\"Drive\", type:knob, range:0-1,
default:0.5},

{id:\"output\", label:\"Output\", type:knob, range:0-1, default:0.8} \]
}

Row { params: \[ {id:\"tone_mode\", label:\"Tone\", type:dropdown,

options:\[\"Off\",\"LP\",\"HP\",\"BP\",\"Peak\"\], default:0} \] }

Row { params: \[ {id:\"tone_freq\", label:\"Freq\", type:knob,
range:20-20000, default:2000,

visibleWhen:{paramId:\"tone_mode\", notValue:0}},

{id:\"tone_res\", label:\"Res\", type:knob, range:0-1, default:0.5,

visibleWhen:{paramId:\"tone_mode\", notValue:0}},

{id:\"tone_mix\", label:\"Mix\", type:knob, range:0-1, default:0.5,

visibleWhen:{paramId:\"tone_mode\", notValue:0}} \] }

\]

}

**22. Sidebar & Mixer Channel Spec**

**22.1 Sidebar Layout**

- Fixed left sidebar, \~82px wide. Always visible including when mixer
  overlay is open.

- Contains one SidebarItem per active rhythm. No placeholders for
  inactive slots.

- Item order: creation order top to bottom. Drag-to-reorder is v2
  feature.

- RhythmSidebar must support variable item ordering from day one

- Add rhythm button at bottom: dashed border, + symbol. Disabled when 8
  rhythms active.

**22.2 SidebarItem Visual**

- Fixed height per item, fills sidebar width

- Small RhythmCircle: Euclid A and B rings only, rotating during
  playback. \~54px diameter.

- Rhythm colour dot below circle, \~7px diameter

- Rhythm name in small text below dot

- Selected state: right edge vertical bar in rhythm colour, width 2px.
  Background slightly lighter.

- Hit pulse: circle briefly brightens when rhythm fires

- Clicking selects rhythm for editing in main panel. Also closes mixer
  overlay if open.

**22.3 Mixer Channel Layout**

- Each channel strip is \~62px wide (master slightly wider at \~68px)

- Vertical layout top to bottom: colour dot, name, send rotaries where
  applicable, pan rotary, fader+VU side by side with no gap, level
  label, mute+solo buttons

- Fader: thin vertical slider \~4px wide. VU meter: \~6px wide, directly
  adjacent, no gap

- VU meter: peak ballistics. Green 0%-70%. Amber 70%-88%. Red 88%-100%.
  Peak hold line. Slow release \~1.5 seconds.

- Mute button: amber highlight when active. Solo button: teal highlight
  when active.

- FX return channels use their FX colour for their border and indicator
  dot

- Master channel: slightly wider, Internal/External routing toggle above
  fader area

**22.4 FX Row Layout**

- Three fixed rows below channel strips: Effect (purple border), Delay
  (teal border), Reverb (blue border)

- Each row: on/off pill button, name label, algorithm dropdown (Effect
  and Reverb only), then parameter knobs inline

- Row is horizontally scrollable if algorithm parameters overflow the
  available width

- Delay row: sync/free toggle, TimeSelector + multiplier, then Feedback,
  Spread, Dirt knobs

- EQ section: reserved placeholder row showing \'EQ --- v2\' --- no
  controls in v1

**23. CLAP Format Support**

μ-Clid ships as VST3, CLAP, and Standalone from v1. CLAP (CLever Audio
Plugin) is an open source plugin format created by Bitwig and u-he.

**23.1 Format List**

  ------------- --------------- ------------------------------------------
  **Format**    **Target**      **Notes**

  VST3          Windows + Mac   Primary format. Broadest DAW
                                compatibility.

  CLAP          Windows + Mac   Native Bitwig format. Open source, no
                                Steinberg licence. Zero extra code for
                                basic support.

  Standalone    Windows + Mac   Desktop application for development,
                                testing, and live use without a DAW.
  ------------- --------------- ------------------------------------------

**23.2 CMakeLists.txt Format Declaration**

juce_add_plugin(mu-clid

FORMATS VST3 CLAP Standalone

\...

)

**23.3 CLAP Specific Features**

- Basic CLAP support requires no extra code beyond the format
  declaration

- CLAP specific features wrapped in #if JUCE_CLAP preprocessor blocks

- v1 ships basic CLAP with no CLAP-specific extensions

- v2/v3 may use CLAP per-note parameter modulation as a modulation
  source

- The ModulationMatrix string ID architecture is compatible with CLAP
  modulation sources

**23.4 Platform Requirements**

- Windows build: produces VST3 + CLAP + Standalone on Windows
  development machine

- Mac build: requires Mac running Xcode for code signing and
  notarisation

- Recommended: launch Windows builds first, add Mac via GitHub Actions
  CI pipeline

**24. Unit Testing**

μ-Clid uses JUCE\'s built-in unit test framework for pure logic classes.
Tests run inside the Standalone build via a debug menu option.

**24.1 Folder Structure**

Tests/

├── EuclideanGeneratorTests.cpp

├── HitGeneratorTests.cpp

├── ControlSequenceTests.cpp

└── ModulationMatrixTests.cpp

**24.2 Test Candidates**

  -------------------- ------------------ ---------------------------------
  **Class**            **Testability**    **Key Test Cases**

  EuclideanGenerator   Excellent --- pure Zero hits, all hits, known
                       static algorithm   distributions, padding modes,
                                          rotation

  HitGenerator         Excellent ---      OR/AND/XOR/A-only/B-only
                       deterministic      combinations, mute, accent
                       logic              distribution

  ControlSequence      Excellent --- pure Step count derivation, truncation
                       data logic         on resize, polarity toggle, zero
                                          crossing

  ModulationMatrix     Good --- pure      Known offsets applied correctly,
                       logic              clamping, circular dependency
                                          detection, dependency ordering
  -------------------- ------------------ ---------------------------------

**24.3 Running Tests**

- Tests run inside the Standalone build via a debug keyboard shortcut or
  menu option

- Results output to the JUCE debug log --- pass/fail per test case

- Write tests for a class at the same time as the class itself

- A class is not complete until its unit tests pass

**25. Coding Principles**

These principles apply to every Claude Code session. Paste this section
into every session alongside the section 17 briefing.

**Principle 1 --- Simplest correct implementation within the
architecture**

Respect all architectural interfaces and extensibility points defined in
sections 3.2, 4.3, 4.4, 4.5, and 4.6 --- these are non-negotiable
regardless of v1 scope. Within those boundaries, choose the simplest
implementation that correctly satisfies the v1 spec. Do not add
complexity, abstraction layers, or future-proofing beyond what the spec
explicitly defines.

**Principle 2 --- One class at a time, fully complete**

Implement one class fully before moving to the next. A class is complete
when it compiles without warnings, satisfies its spec description, and
has jasserts at every assumption boundary. Do not produce partial
implementations or placeholder code. If a class cannot be completed in a
session, stop at a clean boundary and document clearly what remains.

**Principle 3 --- No clever code**

Write code that is easy to read and understand, not code that is
impressive or terse. Prefer clear variable names over short ones,
explicit logic over compact tricks, and straightforward control flow
over elegant but hard-to-follow constructions.

**Principle 4 --- Comments at two levels**

Every class and significant method gets a header comment explaining what
it does, where it sits in the architecture, and which other classes it
interacts with. Inline comments explain why a decision was made, not
what the code does. Non-obvious DSP mathematics, lock-free patterns,
JUCE-specific behaviour, and thread safety reasoning all warrant a brief
inline explanation.

**Principle 5 --- No magic numbers**

Every numeric constant that has a meaning must be given a named
constant. Constants defined in the spec --- MAX_RHYTHMS,
MAX_EUCLID_LAYERS, MAX_CONTROL_SEQUENCES --- must be used everywhere
they apply. Drawing constants must be defined in MuClidLookAndFeel.

**Principle 6 --- Handle failure gracefully**

The plugin must never crash due to a missing file, unexpected parameter
value, null pointer, or edge case. Every pointer that could be null must
be checked or guaranteed non-null by design with a jassert. Every file
operation must handle failure. Every parameter value must be clamped
before DSP use. No exception propagates into the audio thread.

**Principle 7 --- One responsibility per class**

Each class does one thing and does it completely. DSP classes contain no
UI code. UI classes contain no DSP code and no business logic. If a
class cannot be described in one sentence without using the word
\'and\', it has too many responsibilities and should be split.

**Principle 8 --- Follow the spec, flag deviations**

The specification document is the authority. If during implementation a
better approach becomes apparent or the spec is ambiguous, stop and flag
it explicitly rather than making a silent decision. State what the spec
says, what was discovered, what options are available, and a recommended
choice. The project manager makes the decision and the spec is updated
before coding continues.

**Principle 9 --- Test at every boundary**

Every completed class is tested before moving on. Compile and run the
Standalone build after every completed class. Test edge cases
explicitly. Test state save and restore after every class that touches
the ValueTree. Run unit tests for pure logic classes. Test in Bitwig at
the end of every build stage.

**Principle 10 --- Consistent naming conventions**

- Classes --- PascalCase, matching spec names exactly

- Member variables --- camelCase, no prefix

- Local variables --- camelCase

- Constants and enums --- ALL_CAPS_WITH_UNDERSCORES

- Methods --- camelCase, verb-first where possible

- APVTS parameter IDs --- lowercase with underscores, matching section
  19 exactly

- Boolean variables and methods --- is/has/should prefix

- Modulation string IDs --- follow section 19.6 conventions exactly

**25.1 Session Checklist**

**At the start of every Claude Code session:**

- Section 17 briefing template pasted with current stage and filename
  filled in

- Relevant header files from previous sessions included as context

- Section 25 coding principles included

- The specific class or feature to implement is clearly stated

**At the end of every Claude Code session:**

- Code compiles without errors or warnings

- Code follows all ten principles

- Unit tests written and passing for pure logic classes

- Spec updated if any new decisions were made

- Next session\'s starting point is clear

**25.2 Session Briefing Template**

*I am building a VST3/CLAP audio plugin called μ-Clid (mu-Clid) --- a
Euclidean rhythm sequencer and sample trigger by Transwarp Development
Project (TDP). Built with JUCE and CMake. JUCE is at \$ENV{JUCE_PATH}.
Manufacturer code TDP1, plugin code MCld. Formats: VST3, CLAP,
Standalone. The full project guide is in my documents. We are currently
working on \[STAGE NAME\]. The file I need help with is \[FILENAME\].
Here is the relevant context: \[paste related headers or previous
code\]. Please follow the coding principles in section 25 of the project
guide throughout this session.*
