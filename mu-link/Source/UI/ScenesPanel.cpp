#include "ScenesPanel.h"
#include "ClientColumns.h"

namespace mu_link
{

ScenesPanel::ScenesPanel()
{
    sceneButtons.setFiresOnReselect(true);
    sceneButtons.onChange = [this](int scene)
    {
        selectScene(scene);
        if (onRecall) onRecall(scene);
    };
    addAndMakeVisible(sceneButtons);

    for (int i = 0; i < kNumClients; ++i)
    {
        auto& c = cells[(size_t) i];

        // The switch is On when its disc is at the top (index 0).
        c.on.onChange = [this, i](int index) { editCell(i, [index](Cell& cell) { cell.enabled = (index == 0); }); };
        c.program.setShowStepButtons(false);
        c.program.onChange = [this, i](int v) { editCell(i, [v](Cell& cell) { cell.program = juce::jlimit(0, 127, v); }); };
        c.channel.setShowStepButtons(false);
        c.channel.onChange = [this, i](int v) { editCell(i, [v](Cell& cell) { cell.channel = juce::jlimit(1, 16, v); }); };

        addAndMakeVisible(c.on);
        addAndMakeVisible(c.program);
        addAndMakeVisible(c.channel);

        const juce::String who = "Client " + juce::String(i + 1);
        hover.watch(c.on,      who + " scene", "On: sends this app its program change when the scene is recalled");
        hover.watch(c.program, who + " program", "Program number sent on recall");
        hover.watch(c.channel, who + " channel", "Channel 9 loads a full preset, 1 to 8 swap a single layer");
    }
    hover.watch(sceneButtons, "Scene", "Recall: sends each ticked app its program change");
    hover.onStatus = [this](const juce::String& n, const juce::String& v) { if (onStatus) onStatus(n, v); };

    refreshCells();
}

void ScenesPanel::setScenes(const Table& table)
{
    scenes = table;
    refreshCells();
}

void ScenesPanel::selectScene(int scene)
{
    editScene = juce::jlimit(0, kNumScenes - 1, scene);
    sceneButtons.setSelectedIndex(editScene, false);
    refreshCells();
}

// Loads the selected scene's cells into the controls without reporting them as user edits.
void ScenesPanel::refreshCells()
{
    const juce::ScopedValueSetter<bool> guard(refreshing, true);
    for (int i = 0; i < kNumClients; ++i)
    {
        const auto& cell = scenes[(size_t) editScene][(size_t) i];
        cells[(size_t) i].on.setSelectedIndex(cell.enabled ? 0 : 1, false);
        cells[(size_t) i].program.setValue(cell.program, false);
        cells[(size_t) i].channel.setValue(cell.channel, false);
    }
}

void ScenesPanel::editCell(int client, const std::function<void(Cell&)>& change)
{
    if (refreshing) return;
    change(scenes[(size_t) editScene][(size_t) client]);
    if (onEdited) onEdited();
}

// Scene buttons across the full width inside the screw inset; each client's cell under its strip.
void ScenesPanel::resized()
{
    using LF = MuLookAndFeel;
    using mu_ui::s;
    const auto cols = ClientColumns::forWidth(getWidth());
    const int top = s(LF::kChannelInset) + s(LF::kSectionPlateH);   // under the plate band

    sceneButtons.setBounds(cols.x0, top, cols.contentRight - cols.x0, s(kSceneRowH));

    const int cellY = top + s(kSceneRowH) + s(kSceneRowGap);
    for (int i = 0; i < kNumClients; ++i)
    {
        auto& c = cells[(size_t) i];
        const int x = cols.stripX(i);
        c.on     .setBounds(x, cellY, s(LF::kSlideSwitchW), s(kSwitchH));
        const int fieldW = juce::jmin(cols.stripW, s(LF::kMixerChanW));   // the caption takes what is left of the column
        const int fieldX = x + cols.stripW - fieldW;
        c.program.setBounds(fieldX, programRowY(cellY), fieldW, s(kFieldH));
        c.channel.setBounds(fieldX, channelRowY(cellY), fieldW, s(kFieldH));
    }
}

int ScenesPanel::programRowY(int cellY) { return cellY + mu_ui::s(kSwitchH) + mu_ui::s(kCellGap); }
int ScenesPanel::channelRowY(int cellY) { return programRowY(cellY) + mu_ui::s(kFieldH) + mu_ui::s(kCellGap); }

void ScenesPanel::paint(juce::Graphics& g)
{
    using LF = MuLookAndFeel;
    using mu_ui::s;
    g.fillAll(LF::colour(LF::panelBackground));
    LF::drawAccentPanel(g, getLocalBounds().reduced(2).toFloat(), LF::appAccent(*this));

    const auto cols = ClientColumns::forWidth(getWidth());
    const float plateH = (float) s(LF::kSectionPlateH);
    const float plateW = LF::namePlateWidth("Scenes", plateH);
    LF::drawNamePlate(g, { (float) cols.x0, (float) s(LF::kChannelInset), plateW, plateH }, "Scenes");

    // Each field's caption, engraved to its left in the room the column has beside the field.
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(mu_ui::sf(LF::kKnobLabelFont))));
    for (int i = 0; i < kNumClients; ++i)
    {
        const auto& c = cells[(size_t) i];
        const int captionW = c.program.getX() - cols.stripX(i);
        for (auto row : { std::pair<const NudgeInput*, const char*> { &c.program, "Program" },
                          std::pair<const NudgeInput*, const char*> { &c.channel, "Channel" } })
            LF::drawEngravedText(g, row.second, { cols.stripX(i), row.first->getY(), captionW, row.first->getHeight() },
                                 juce::Justification::centredLeft, LF::colour(LF::labelText));
    }
}

void ScenesPanel::paintOverChildren(juce::Graphics& g)
{
    if (MuLookAndFeel::hasScrews(*this))
        MuLookAndFeel::drawPanelScrews(g, getLocalBounds().reduced(2).toFloat());
}

} // namespace mu_link
