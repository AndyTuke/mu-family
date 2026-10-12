#include "UI/MasterInsertPanel.h"
#include "Audio/AlgorithmNames.h"
#include "UI/InsertSlotUi.h"

namespace mu_ui
{
namespace
{
constexpr int kBoxPad    = 4;    // inside a box, round its contents
constexpr int kSelectH   = 24;   // the algorithm selector
constexpr int kSelectGap = 2;    // selector to the knob grid
constexpr int kEdge      = 4;    // panel edge to a box
constexpr int kBoxGap    = 4;    // a box to the next plate band
constexpr int kInsetTop  = 2;    // gap under the first plate band

int boxHeight()
{
    return s(kBoxPad) + s(kSelectH) + s(kSelectGap) + 2 * s(MuLookAndFeel::kKnobSize2H) + s(kBoxPad);
}
} // namespace

MasterInsertPanel::MasterInsertPanel()
{
    for (int i = 0; i < kSlots; ++i)
    {
        auto& sl = slots[(size_t) i];
        mu_audio::populateInsertAlgoDropdown([&sl](const char* n, int id) { sl.algo.addItem(n, id); });
        sl.algo.setSelectedId(1, false);
        sl.algo.onChange = [this, i](int id) { userChangedAlgorithm(i, id - 1); };
        addAndMakeVisible(sl.algo);
        for (auto& k : sl.knobs) addChildComponent(k);
        configureKnobs(i);
    }
}

// Configure the four knobs for the slot's algorithm from the held normalised values; a user move is
// remembered and reported through onParam.
void MasterInsertPanel::configureKnobs(int slot)
{
    auto& sl = slots[(size_t) slot];
    for (int p = 0; p < kParams; ++p)
        configureKnobFromSlot(sl.knobs[(size_t) p], sl.algorithm, p, sl.norm[p],
            [this, slot, p](float n)
            {
                slots[(size_t) slot].norm[p] = n;
                if (onParam) onParam(slot, p, n);
            });
    resized();
}

void MasterInsertPanel::setAlgorithm(int slot, int algorithm)
{
    if (slot < 0 || slot >= kSlots) return;
    auto& sl = slots[(size_t) slot];
    sl.algorithm = juce::jlimit(0, kInsertAlgoCount - 1, algorithm);
    sl.algo.setSelectedId(sl.algorithm + 1, false);
    configureKnobs(slot);
}

void MasterInsertPanel::setParam(int slot, int param, float normalised)
{
    if (slot < 0 || slot >= kSlots || param < 0 || param >= kParams) return;
    auto& sl = slots[(size_t) slot];
    sl.norm[param] = normalised;
    sl.knobs[(size_t) param].setValue((double) normToActual(normalised, sl.algorithm, param),
                                      juce::dontSendNotification);
}

int MasterInsertPanel::getAlgorithm(int slot) const { return slots[(size_t) slot].algorithm; }
float MasterInsertPanel::getParam(int slot, int param) const { return slots[(size_t) slot].norm[param]; }

// The selector changed: remember the old algorithm's values, load the new one's (its last values or
// its defaults), then tell the owner the algorithm and each parameter.
void MasterInsertPanel::userChangedAlgorithm(int slot, int newAlgorithm)
{
    auto& sl = slots[(size_t) slot];
    newAlgorithm = juce::jlimit(0, kInsertAlgoCount - 1, newAlgorithm);

    const int old = sl.algorithm;
    for (int p = 0; p < kParams; ++p)
        sl.snapshot[old][p] = normToActual(sl.norm[p], old, p);
    sl.snapshotValid[old] = true;

    for (int p = 0; p < kParams; ++p)
    {
        const float actual = sl.snapshotValid[newAlgorithm] ? sl.snapshot[newAlgorithm][p]
                                                            : kInsertAlgoDefaults[newAlgorithm][p];
        sl.norm[p] = actualToNorm(actual, newAlgorithm, p);
    }
    sl.algorithm = newAlgorithm;
    configureKnobs(slot);

    if (onAlgorithm) onAlgorithm(slot, newAlgorithm);
    if (onParam)
        for (int p = 0; p < kParams; ++p) onParam(slot, p, sl.norm[p]);
}

//==============================================================================
juce::Rectangle<int> MasterInsertPanel::boxBounds(int slot) const
{
    const int band = s(MuLookAndFeel::kSectionPlateH);
    const int y = band + s(kInsetTop) + slot * (boxHeight() + s(kBoxGap) + band);
    return { s(kEdge), y, getWidth() - 2 * s(kEdge), boxHeight() };
}

int MasterInsertPanel::preferredHeight()
{
    const int band = s(MuLookAndFeel::kSectionPlateH);
    return band + s(kInsetTop) + kSlots * boxHeight() + (kSlots - 1) * (s(kBoxGap) + band) + s(kEdge);
}

void MasterInsertPanel::resized()
{
    for (int i = 0; i < kSlots; ++i)
    {
        auto& sl = slots[(size_t) i];
        const int pad = s(kBoxPad);
        const auto inner = boxBounds(i).reduced(pad, pad);
        sl.algo.setBounds(inner.getX(), inner.getY(), inner.getWidth(), s(kSelectH));

        // The visible knobs pack into the 2 x 2 grid; an odd last one is centred.
        KnobWithLabel* vis[kParams];
        int n = 0;
        for (auto& k : sl.knobs)
            if (k.isVisible()) vis[n++] = &k;

        const int kw = s(MuLookAndFeel::kKnobSize2W), kh = s(MuLookAndFeel::kKnobSize2H);
        const int cellW = inner.getWidth() / 2;
        const int gridY = inner.getY() + s(kSelectH) + s(kSelectGap);
        for (int j = 0; j < n; ++j)
        {
            const bool lastOdd = (j == n - 1) && (n % 2 == 1);
            const int x = lastOdd ? inner.getX() + (inner.getWidth() - kw) / 2
                                  : inner.getX() + (j % 2) * cellW + (cellW - kw) / 2;
            vis[j]->setBounds(x, gridY + (j / 2) * kh, kw, kh);
        }
    }
}

void MasterInsertPanel::paint(juce::Graphics& g)
{
    MuLookAndFeel::drawSections(g, *this,
        { { boxBounds(0), "Insert 1" }, { boxBounds(1), "Insert 2" } },
        MuLookAndFeel::appAccent(*this));
}
} // namespace mu_ui
