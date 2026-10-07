#include "VoiceSection.h"
#include "Plugin/PluginProcessor.h"
#include "Sequencer/Rhythm.h"
#include "Persistence/ScopedApvtsLoading.h"

VoiceSection::VoiceSection(PluginProcessor& p)
    : proc(p), pitchSub(p), filterSub(p), ampSub(p), insertSub(p, "r")
{
    addAndMakeVisible(pitchSub);
    addAndMakeVisible(filterSub);
    addAndMakeVisible(ampSub);
    addAndMakeVisible(insertSub);

    // FX sends sit in the Insert panel's top row, right of a narrowed dropdown; added
    // after insertSub so they're on top of it.
    for (auto* k : ampSub.sendKnobs())
        addAndMakeVisible(k);
    insertSub.setAlgoColumns(kInsertCols - kSendCols);

    // Forward status updates from each subsection through our own callback.
    auto fwd = [this](const juce::String& n, const juce::String& v) {
        if (onStatusUpdate) onStatusUpdate(n, v);
    };
    pitchSub .onStatusUpdate = fwd;
    filterSub.onStatusUpdate = fwd;
    ampSub   .onStatusUpdate = fwd;
    insertSub.onStatusUpdate = fwd;

    insertSub.onInsertAlgorithmChanged = [this](int charId) {
        if (onInsertAlgorithmChanged) onInsertAlgorithmChanged(charId);
    };

    // mu-clid-specific insert-panel hooks (the shared subsection is product-agnostic).
    insertSub.isPlaying = [this] { return proc.sequencerPlaying.load(); };
    insertSub.isSlotModulated = [this](int slot) -> bool
    {
        if (currentRhythm < 0 || currentRhythm >= proc.getNumRhythms()) return false;
        const char* const dest[4] = { "insert.p1", "insert.p2", "insert.p3", "insert.p4" };
        for (const auto& a : proc.getRhythm(currentRhythm).modulationMatrix.getAssignments())
            if (a.destinationId == dest[slot]) return true;
        return false;
    };
    insertSub.slotModValue = [this](int slot) -> float
    {
        const int snap[4] = { kSnapInsP1, kSnapInsP2, kSnapInsP3, kSnapInsP4 };
        return proc.getModSnapshot(currentRhythm, snap[slot]);
    };
    insertSub.getInsertGR = [this]() -> const std::atomic<float>*
    {
        return proc.getInsertGRReductionPtr(currentRhythm);
    };
    insertSub.runBulkChange = [this](std::function<void()> fn)
    {
        // Suppress the parameterChanged listener during the multi-write algo
        // switch, then resync the engine from APVTS (preset-load pattern).
        mu_core::ScopedApvtsLoading guard(proc.getApvtsLoadingFlag());
        fn();
        if (currentRhythm >= 0 && currentRhythm < proc.getNumRhythms())
            proc.forceSyncRhythmFromAPVTS(currentRhythm);
    };
}

void VoiceSection::setRhythm(int ri)
{
    currentRhythm = ri;
    pitchSub .setRhythm(ri);
    filterSub.setRhythm(ri);
    ampSub   .setRhythm(ri);
    insertSub.setChannel(ri);
}

void VoiceSection::loadFromRhythm()
{
    pitchSub .loadFromRhythm();
    filterSub.loadFromRhythm();
    ampSub   .loadFromRhythm();
    insertSub.loadFromChannel();
}

void VoiceSection::refreshSuffix(const juce::String& suffix)
{
    pitchSub .refreshSuffix(suffix);
    filterSub.refreshSuffix(suffix);
    ampSub   .refreshSuffix(suffix);
    insertSub.refreshSuffix(suffix);
}

void VoiceSection::resized()
{
    // Fixed Medium-baseline layout, wrapped in s() so the whole grid scales.
    // Pitch / Amp / Insert use the standard 54-px column (kVoiceUnitW).
    // Filter gets 6 narrower 50-px columns (kVoiceFilterColW) for the Drive knob.
    // Total: 4×54 + 6×50 + 4×54 + 6×54 + 3×6 = 1074 px (= available width exactly).
    using LF = MuLookAndFeel;
    using mu_ui::s;
    constexpr int divW   = LF::kVoiceDivW;
    constexpr int labelH = LF::kVoiceLabelH;
    constexpr int kW     = LF::kVoiceUnitW;       // 54 — pitch / amp / insert columns
    constexpr int kFltW  = LF::kVoiceFilterColW;  // 50 — filter columns (6 of them)
    constexpr int subH   = LF::kVoiceSubH;

    constexpr int fltX  = kPitchW + divW;            // 222
    constexpr int ampX  = fltX + 6 * kFltW + divW;            // 528
    constexpr int insX  = ampX + kAmpW + divW;       // 750

    pitchSub .setBounds(0,          s(labelH), s(kPitchW), s(subH));
    filterSub.setBounds(s(fltX),    s(labelH), s(6 * kFltW),        s(subH));
    ampSub   .setBounds(s(ampX),    s(labelH), s(kAmpW),   s(subH));
    insertSub.setBounds(s(insX),    s(labelH), s(kEffectsW), s(subH));

    // FX sends: the Insert panel's last kSendCols (slightly narrower) columns, top row.
    constexpr int sendX = insX + (kInsertCols - kSendCols) * kW;
    int col = 0;
    for (auto* k : ampSub.sendKnobs())
        k->setBounds(s(sendX + col++ * kSendColW), s(labelH), s(kSendColW), s(LF::kKnobSize2H));
}

void VoiceSection::paint(juce::Graphics& g)
{
    using Id = MuLookAndFeel::ColourIds;
    using LF = MuLookAndFeel;
    using mu_ui::s;

    const int h          = getHeight();
    constexpr int divW   = LF::kVoiceDivW;
    constexpr int labelH = LF::kVoiceLabelH;
    constexpr int kFltW  = LF::kVoiceFilterColW;

    constexpr int fltX = kPitchW + divW;
    constexpr int ampX = fltX + 6 * kFltW + divW;
    constexpr int insX = ampX + kAmpW + divW;

    if (MuLookAndFeel::isMetal(*this))
    {
        // Metal: each subsection in its own raised box (all shadows first, then the faces),
        // its name plate just above it.
        const juce::Rectangle<float> boxes[] = { pitchSub.getBounds().toFloat(), filterSub.getBounds().toFloat(),
                                                 ampSub.getBounds().toFloat(),   insertSub.getBounds().toFloat() };
        const auto appCol = MuLookAndFeel::appAccent(*this);
        for (const auto& b : boxes) MuLookAndFeel::drawRaisedSubPanelShadow(g, b);
        for (const auto& b : boxes) MuLookAndFeel::drawRaisedSubPanel(g, b, appCol);
        if (MuLookAndFeel::hasScrews(*this))
            for (const auto& b : boxes) MuLookAndFeel::drawSubPanelScrews(g, b);
    }
    else
    {
        // Flat: thin dividers between the subsections.
        g.setColour(MuLookAndFeel::colour(Id::segmentInactiveBorder));
        const float kDivInset = mu_ui::sf(7.0f);
        const float div1X = static_cast<float>(s(kPitchW) + s(divW) / 2);
        const float div2X = static_cast<float>(s(fltX + 6 * kFltW) + s(divW) / 2);
        const float div3X = static_cast<float>(s(ampX + kAmpW) + s(divW) / 2);
        g.drawLine(div1X, kDivInset, div1X, (float)h - kDivInset, 0.5f);
        g.drawLine(div2X, kDivInset, div2X, (float)h - kDivInset, 0.5f);
        g.drawLine(div3X, kDivInset, div3X, (float)h - kDivInset, 0.5f);
    }

    // Section names: a name plate centred over each subsection.
    auto plate = [&](const char* name, int x, int w)
    {
        MuLookAndFeel::drawCentredNamePlate(g, { (float) s(x), 0.0f, (float) s(w), (float) s(labelH) }, name);
    };
    plate("PITCH",  0,    kPitchW);
    plate("FILTER", fltX, 6 * kFltW);
    plate("AMP",    ampX, kAmpW);
    plate("EFFECTS", insX, kEffectsW);
}
