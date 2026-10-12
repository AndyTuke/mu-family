#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include "Link/MuLinkProtocol.h"                 // kMaxClients
#include "UI/Components/MuLookAndFeel.h"
#include "UI/Components/SegmentControl.h"
#include "UI/Components/SlideSwitch.h"
#include "UI/Components/NudgeInput.h"
#include "UI/Components/HoverStatus.h"

namespace mu_link
{

// ScenesPanel - the mu-link Scenes panel (docs/mu-link/design-ui.md section 6): eight scene
// buttons, and under each client strip the cell of the selected scene (is the client sent a
// program change, which program, on which MIDI channel). Channel 9 loads a full preset, 1 to 8
// swap a single layer. The panel only edits the table; the owner recalls a scene and saves.
class ScenesPanel : public juce::Component
{
public:
    static constexpr int kNumScenes    = 8;
    static constexpr int kNumClients   = mu_link::kMaxClients;
    static constexpr int kScenesPanelH = 152;   // Medium baseline height of the whole panel
    static constexpr int kSceneRowH    = 32;    // the scene buttons
    static constexpr int kSceneRowGap  = 6;     // scene buttons to the cells
    static constexpr int kSwitchH      = 22;    // On switch
    static constexpr int kFieldH       = 24;    // Program and Channel fields
    static constexpr int kCellGap      = 4;     // between the rows of one cell
    static constexpr int kCellH        = kSwitchH + kCellGap + kFieldH + kCellGap + kFieldH;   // 78

    struct Cell
    {
        bool enabled = false;
        int  program = 0;     // 0..127
        int  channel = 9;     // 1..16
        bool operator==(const Cell& o) const { return enabled == o.enabled && program == o.program && channel == o.channel; }
    };
    using Table = std::array<std::array<Cell, kNumClients>, kNumScenes>;

    ScenesPanel();

    // A scene button was pressed (also when it was already selected): recall `scene`.
    std::function<void(int scene)> onRecall;
    // A cell was edited by the user: the owner saves the table.
    std::function<void()> onEdited;
    // Hover text for the status bar.
    std::function<void(const juce::String& name, const juce::String& value)> onStatus;

    const Table& getScenes() const noexcept { return scenes; }
    void setScenes(const Table& table);          // replaces the table; no callbacks
    int  getSelectedScene() const noexcept { return editScene; }
    void selectScene(int scene);                 // binds the cells to `scene`; no recall

    // The scene buttons and the cells, for the owner's tests.
    SegmentControl& getSceneButtons() { return sceneButtons; }
    SlideSwitch&    getOnSwitch(int client)     { return cells[(size_t) client].on; }
    NudgeInput&     getProgramField(int client) { return cells[(size_t) client].program; }
    NudgeInput&     getChannelField(int client) { return cells[(size_t) client].channel; }

    void resized() override;
    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;

private:
    struct CellUi
    {
        SlideSwitch on { "On", "", MuLookAndFeel::knobLevel };
        NudgeInput  program { "", 0, 127, 0 };   // captions are engraved by the panel, beside the field
        NudgeInput  channel { "", 1, 16, 9 };
    };

    void refreshCells();
    static int programRowY(int cellY);   // the cell rows' tops, from the top of the cell
    static int channelRowY(int cellY);
    void editCell(int client, const std::function<void(Cell&)>& change);

    Table scenes {};
    int   editScene = 0;
    bool  refreshing = false;   // true while the cells are loaded from the table (no edit callbacks)

    SegmentControl sceneButtons { { "1", "2", "3", "4", "5", "6", "7", "8" },
                                  SegmentControl::ActiveStyle::Warning, SegmentControl::DrawStyle::Pills };
    std::array<CellUi, kNumClients> cells;
    mu_ui::HoverStatus hover;   // after the components it watches

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScenesPanel)
};

} // namespace mu_link
