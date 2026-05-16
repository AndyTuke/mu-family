#pragma once
#include <juce_graphics/juce_graphics.h>

// Runtime-mutable colour theme for the whole plugin. Grouped into semantic
// categories so a future Theme Editor can present them in tabs/sections.
//
// Access model:
//   - MuTheme::current() returns a mutable singleton.
//   - All drawing code reads via MuLookAndFeel::colour(id) (preferred, lets the
//     ColourIds map redirect tokens later without touching call sites) OR via
//     direct MuTheme::current().<group>.<field> when the consumer is already
//     theme-aware (e.g. a new feature added after this refactor).
//   - A future Theme Editor mutates current().* and triggers a global repaint;
//     no rebuild of LookAndFeel state required.
//
// Defaults below match the historical hardcoded palette so this refactor is a
// pure structural change at the start — same pixels on screen, but every value
// is now editable from one place.
struct MuTheme
{
    // ── Backgrounds & surfaces ────────────────────────────────────────────
    struct Backgrounds
    {
        juce::Colour window            { 0xff1c1c1b };  // plugin root
        juce::Colour panel             { 0xff232322 };  // secondary panels
        juce::Colour sidebar           { 0xff1a1a19 };  // left sidebar
        juce::Colour sidebarItem       { 0xff252524 };  // sidebar item bg
        juce::Colour sidebarItemSelected { 0xff2d2d2b };
        juce::Colour overlay           { 0xff111110 };  // mod / FX overlay bg
        juce::Colour dialog            { 0xff1a1a1a };  // modal dialog cards
        juce::Colour modalDim          { 0xe6000000 };  // dim behind modal cards
        juce::Colour fxRowDim          { 0x60000000 };  // FX/Delay row disabled overlay
        juce::Colour mixerStripDim     { 0x88000000 };  // inactive mixer-channel overlay
    } backgrounds;

    // ── Knob category colours ─────────────────────────────────────────────
    struct Knobs
    {
        juce::Colour euclidean         { 0xff7F77DD };  // purple
        juce::Colour insertPad         { 0xffD4537E };  // pink
        juce::Colour level             { 0xffEF9F27 };  // amber
        juce::Colour fxSend            { 0xffD85A30 };  // coral
        juce::Colour reverb            { 0xff378ADD };  // blue
        juce::Colour pan               { 0xff888780 };  // grey
        juce::Colour modulation        { 0xffD4537E };  // pink (alias of insertPad)
        juce::Colour prePad            { 0xff2BB5C5 };  // cyan-teal
        juce::Colour postPad           { 0xff1D9E75 };  // teal
    } knobs;

    // ── RhythmCircle ring colours ─────────────────────────────────────────
    struct Rings
    {
        juce::Colour euclidA           { 0xff7F77DD };  // outermost (purple)
        juce::Colour euclidB           { 0xffD85A30 };  // middle    (coral)
        juce::Colour euclidC           { 0xffEF9F27 };  // accent    (amber)
        juce::Colour modA              { 0xff1D9E75 };
        juce::Colour modB              { 0xffEF9F27 };
        juce::Colour modC              { 0xffD4537E };
        juce::Colour modD              { 0xff378ADD };
        juce::Colour inactive          { 0xff333332 };
        juce::Colour prePad            { 0xff2BB5C5 };
        juce::Colour postPad           { 0xff1D9E75 };
        juce::Colour insertPad         { 0xffD4537E };
    } rings;

    // ── SegmentControl states ─────────────────────────────────────────────
    struct Segments
    {
        juce::Colour activeBg          { 0xff3C3489 };
        juce::Colour activeBorder      { 0xff7F77DD };
        juce::Colour positiveBg        { 0xff085041 };
        juce::Colour positiveBorder    { 0xff1D9E75 };
        juce::Colour warningBg         { 0xff854F0B };
        juce::Colour warningBorder     { 0xffEF9F27 };
        juce::Colour inactiveBg        { 0xff2a2a2a };
        juce::Colour inactiveBorder    { 0xff444444 };
        juce::Colour inactiveText      { 0xff888888 };
    } segments;

    // ── StepEditor ────────────────────────────────────────────────────────
    struct StepEditor
    {
        juce::Colour bar               { 0xff1D9E75 };
        juce::Colour zeroLine          { 0xff555554 };
        juce::Colour background        { 0xff1e1e1d };
        juce::Colour gridLine          { 0xff333332 };
    } stepEditor;

    // ── LFOEditor ─────────────────────────────────────────────────────────
    struct LFOEditor
    {
        juce::Colour background        { 0xff1e1e1d };
        juce::Colour curve             { 0xff1D9E75 };
        juce::Colour curveFill         { 0x301D9E75 };
        juce::Colour point             { 0xffffffff };
        juce::Colour pointHover        { 0xff7F77DD };
        juce::Colour handle            { 0xff888780 };
        juce::Colour zeroLine          { 0xff444444 };
        juce::Colour playhead          { 0xffD4537E };
    } lfoEditor;

    // ── VU meter zones ────────────────────────────────────────────────────
    struct VUMeter
    {
        juce::Colour background        { 0xff111110 };
        juce::Colour green             { 0xff44cc44 };  // safe zone
        juce::Colour yellow            { 0xffffcc00 };  // hot zone
        juce::Colour red               { 0xffff3333 };  // near-clip / clip
        juce::Colour clipFlash         { 0xffff0000 };  // hard-clip indicator
        juce::Colour peakHold          { 0xffffffff };
    } vuMeter;

    // ── Sample bar (RhythmPanel sample readout) ───────────────────────────
    struct SampleBar
    {
        juce::Colour noSample          { 0xff444444 };
        juce::Colour loaded            { 0xff999999 };
        juce::Colour missing           { 0xffEF9F27 };  // amber, generic
        juce::Colour background        { 0xff1a1a19 };
        juce::Colour missingWarning    { 0xffe69500 };  // amber, RhythmPanel warning tint
    } sampleBar;

    // ── Status bar ────────────────────────────────────────────────────────
    struct StatusBar
    {
        juce::Colour background        { 0xff141413 };
        juce::Colour text              { 0xff888780 };
        juce::Colour value             { 0xffcccccc };
    } statusBar;

    // ── General text ──────────────────────────────────────────────────────
    struct Text
    {
        juce::Colour label             { 0xff888780 };  // secondary labels
        juce::Colour value             { 0xffcccccc };  // parameter values
        juce::Colour heading           { 0xffe8e8e6 };  // headings
        juce::Colour muted             { 0xff555554 };
        juce::Colour bright            { 0xffeeeeee };  // transport-btn active text
        juce::Colour disabledButton    { 0xff666666 };  // transport-btn disabled text
    } text;

    // ── Buttons (non-state-driven extras) ─────────────────────────────────
    struct Buttons
    {
        juce::Colour addBorder         { 0xff555554 };
        juce::Colour addText           { 0xff888780 };
        juce::Colour addHoverBg        { 0xff2a2a28 };
    } buttons;

    // ── Transport play/stop tinted backgrounds ────────────────────────────
    // Convention: colour reflects the state the transport is currently IN.
    // While stopped → green (press to play). While playing → red (press to stop).
    struct Transport
    {
        juce::Colour whileStoppedBg    { 0xff1a4a26 };  // green
        juce::Colour whilePlayingBg    { 0xff5c1a1a };  // red
    } transport;

    // ── Knob overlay indicators (modulation ring, GR arc) ────────────────
    struct Indicators
    {
        juce::Colour modulationTint    { 0xff89e0ff };  // soft cyan ring around modulated knob
        juce::Colour grTint            { 0xffff6633 };  // orange GR arc on compressor/limiter
        juce::Colour grMeterBg         { 0xff111111 };  // GRMeter strip background
        juce::Colour grMeterBar        { 0xaa7799cc };  // semi-transparent blue-grey bar
    } indicators;

    // ── Mixer overlay extras ──────────────────────────────────────────────
    struct Mixer
    {
        juce::Colour inactiveNameBg    { 0xff404040 };  // inactive-rhythm name strip
    } mixer;

    // Mutable singleton.
    static MuTheme& current() noexcept;
};
