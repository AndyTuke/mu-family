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
        juce::Colour modalDim          { 0x73000000 };  // dim behind modal cards (~45% — keeps the app clearly visible)
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

    // ── Global / non-rhythm accent ────────────────────────────────────────
    // Purple — used for borders and accents on views that aren't tied to a
    // specific rhythm (mixer overlay, global FX rows, etc.). Kept deliberately
    // OUT of the rhythm palette so a "rhythm border" and a "mixer border"
    // never share a hue.
    struct Global
    {
        juce::Colour accent            { 0xff7F77DD };  // purple
    } global;

    // ── Lighting ──────────────────────────────────────────────────────────
    // Every shadow, highlight and tint the family draws, in one place. The light comes
    // from the top right. The two master amounts scale everything below them: raise
    // shadowAmount for deeper shadows in every product, highlightAmount for brighter
    // highlights / glows. Per-element values are the strengths at amount 1.0.
    struct Lighting
    {
        float shadowAmount       = 1.0f;    // master scale on every shadow
        float highlightAmount    = 1.0f;    // master scale on every highlight, specular and glow

        // Knobs
        float knobCastShadow     = 0.9f;    // whole knob's shadow onto the panel (KnobWithLabel)
        float knobCastBlur       = 0.38f;   //   blur, fraction of ring radius
        float knobCastOffset     = 0.18f;   //   down-left offset, fraction of ring radius
        float knobBodyShadow     = 0.45f;   // shadow inside the rotary's own bounds (bare sliders too)
        float knobDiscShadow     = 0.65f;   // the disc's shadow onto the ring
        float knobOcclusion      = 0.40f;   // shade on the disc's lower left
        float knobSpecular       = 0.20f;   // disc highlight at the top right
        float knobSpecularMid    = 0.06f;
        float knobRingGlow       = 0.10f;   // each soft pass of the glowing ring
        float knobDotHalo        = 0.55f;   // halo round the position dot
        float knobDotHaloMid     = 0.26f;
        float knobDotBloom       = 0.40f;   // white bloom at the dot
        float knobTicks          = 0.216f;  // white tick marks (not scaled by the masters)

        // Slide switches
        float switchShadow       = 0.75f;   // disc's soft shadow onto the panel
        float switchContact      = 1.0f;    // disc's crisp contact shadow
        float switchTrackShadow  = 0.6f;    // recessed track's edge
        float switchTrackLip     = 0.07f;   // light catching the track's lower lip
        float switchTrackGlow    = 0.18f;   // accent line down the track
        float switchRingGlow     = 0.14f;   // disc ring glow (resting)
        float switchRingGlowHover= 0.22f;   //   … and when hovered

        // Panels (drawAccentPanel)
        float panelTint          = 0.07f;   // accent wash (not scaled by the masters)
        float panelHighlight     = 0.20f;   // top-right glow
        float panelHighlightReach= 0.6f;    //   reach, fraction of the panel diagonal
        float panelHighlightMaxPx= 380.0f;  //   … capped at this many px
        float panelOutlineWidth  = 2.0f;
        float panelSheen         = 0.05f;   // diagonal reflection bands (metallic)
        float panelBrush         = 0.06f;   // brushed-metal grain opacity

        // Raised sub-panels (drawRaisedSubPanel — e.g. mu-Clid's Euclid / Pad / Insert boxes)
        float subPanelShadow     = 0.55f;   // cast shadow, down-left
        float subPanelFace       = 0.03f;   // face lift over the panel
        float subPanelSheen      = 0.05f;   // face light from the top right
        float subPanelEdgeLight  = 0.22f;   // edge catching the light, top right
        float subPanelEdgeShade  = 0.35f;   // edge in shade, bottom left
        float subPanelOutline    = 0.5f;    // accent outline (not scaled by the masters)
        float subPanelBrush      = 0.07f;   // brushed-metal grain on the face
        float subPanelBands      = 0.05f;   // diagonal reflection bands on the face

        // Step ring (mu-Clid's RhythmCircle)
        float ringTrack          = 0.45f;   // recessed track under each ring
        float ringBaseDarken     = 1.1f;    // opaque base under each ring: panel colour darkened by this
        float ringLampOff        = 0.07f;   // unlit step: how much of its colour shows through the dark lens
        float ringLampPad        = 0.38f;   // pad / insert steps: dimly lit
        float ringLampOn         = 0.66f;   // hit steps: lit, but not bright
        float ringLampHot        = 0.30f;   // brighter centre on a lit lamp (lens hot-spot)
        float ringLampPlayhead   = 0.25f;   // extra brightness on the playhead step
        float ringHitGlow        = 0.22f;   // halo round hit steps (playhead × 1.8)
        float ringHitGlowWidth   = 0.22f;   //   halo stroke, fraction of ring width
        float ringBevelDark      = 0.32f;   // inner edge of each ring
        float ringBevelLight     = 0.10f;   // outer edge of each ring
        float ringLight          = 0.18f;   // top-right highlight over the rings
        float ringShade          = 0.22f;   // bottom-left shade over the rings
        float ringHubInset       = 0.9f;    // hub radius, fraction of the space inside ring C
        float ringHubShadow      = 0.6f;    // embossed hub's cast shadow
        float ringHubSpecular    = 0.14f;
        float ringHubRim         = 0.22f;   // rim light top-right / shade bottom-left
        float ringHubFlash       = 0.35f;   // trigger flash (not scaled by the masters)

        // Apply the master amounts.
        float shadow   (float a) const noexcept { return juce::jlimit(0.0f, 1.0f, a * shadowAmount); }
        float highlight(float a) const noexcept { return juce::jlimit(0.0f, 1.0f, a * highlightAmount); }
    } lighting;

    // Mutable singleton.
    static MuTheme& current() noexcept;
};
