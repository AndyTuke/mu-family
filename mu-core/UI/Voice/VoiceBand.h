#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <vector>
#include "UI/Components/MuLookAndFeel.h"
#include "UI/Voice/InsertSubsection.h"

// The family voice band (mu-Clid's layout): Pitch | Filter | Amp | Effects, each section two
// Size-2 knob rows. Pitch and Amp are four columns, Filter six narrower ones, and Effects holds
// the insert (dropdown narrowed, its parameters below) with the three FX sends right-aligned in
// its top row. Products supply the section components — their data binding stays product-side —
// and this lays them out and draws the section boxes + name plates (metal) or dividers (flat).
class VoiceBand : public juce::Component
{
public:
    using LF = MuLookAndFeel;

    // Geometry, unscaled px relative to the band.
    static constexpr int kCols        = LF::kVoiceUnitW;            // 54
    static constexpr int kFilterColW  = LF::kVoiceFilterColW;       // 50
    static constexpr int kPitchW      = 4 * kCols;                  // 216
    static constexpr int kFilterW     = 6 * kFilterColW;            // 300
    static constexpr int kAmpW        = 4 * kCols;                  // 216
    static constexpr int kInsertW     = 6 * kCols;                  // 324
    static constexpr int kSendCols    = 3;                          // Effect / Delay / Reverb
    // The send knobs close up a little so the Effects box ends short of the panel's corner screw.
    static constexpr int kSendColW    = kCols - 3;                  // 51
    // The band starts kLeftGap in so the Pitch box clears the panel's bottom-left corner screw
    // (screw reach, less the standard channel inset round the band); Effects gives up that width.
    static constexpr int kLeftGap     = LF::kScrewedChannelGap;     // 7
    static constexpr int kEffectsW    = kInsertW - kSendCols * (kCols - kSendColW) - kLeftGap;   // 308
    static constexpr int kSendsX      = kEffectsW - kSendCols * kSendColW;   // sends, right-aligned in the box
    static constexpr int kPitchX      = kLeftGap;
    static constexpr int kFilterX     = kPitchX  + kPitchW  + LF::kVoiceDivW;
    static constexpr int kAmpX        = kFilterX + kFilterW + LF::kVoiceDivW;
    static constexpr int kEffectsX    = kAmpX    + kAmpW    + LF::kVoiceDivW;
    static constexpr int kHeight      = LF::kVoiceLabelH + LF::kVoiceSubH;   // plate band + the two rows

    // The four sections (not owned; made children here) and the three FX send knobs.
    void setSections(juce::Component& pitch, juce::Component& filter, juce::Component& amp,
                     InsertSubsection& effects, std::array<juce::Component*, kSendCols> sends);

    void resized() override;
    void paint(juce::Graphics&) override;

private:
    juce::Component*  pitchSec  = nullptr;
    juce::Component*  filterSec = nullptr;
    juce::Component*  ampSec    = nullptr;
    InsertSubsection* effectsSec = nullptr;
    std::array<juce::Component*, kSendCols> sendKnobs {};
};

// One voice-band section built from controls the product already owns: two Size-2 rows of
// `cols` columns `colW` wide. place() puts a control in a cell; a dropdown spans its columns
// at half the row height, centred in the row and kept kDropdownEdgeGap off the box edges.
class VoiceBandSection : public juce::Component
{
public:
    VoiceBandSection(int cols, int colW) : numCols(cols), colWidth(colW) {}

    void place(juce::Component& c, int row, int col, int span = 1, bool dropdown = false);
    int  widthPx() const noexcept { return numCols * colWidth; }   // unscaled

    void resized() override;

private:
    struct Cell { juce::Component* comp; int row, col, span; bool dropdown; };
    std::vector<Cell> cells;
    int numCols, colWidth;
};
