// StepEditor level-cell mode: click cycles Off -> On -> Accented -> Off, Shift+click goes back, a drag paints the
// first click's state into each cell entered (mouse height ignored), and the values stay 0 / 50 / 100.

#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/Components/StepEditor.h"

struct StepEditorLevelCellTests : juce::UnitTest
{
    StepEditorLevelCellTests() : juce::UnitTest("StepEditor level cells", "mu-core") {}

    // A left-button mouse event at (x, y) in the editor, with optional Shift held.
    static juce::MouseEvent mouse(StepEditor& ed, int x, int y, bool shift = false)
    {
        auto& src = juce::Desktop::getInstance().getMainMouseSource();
        const auto mods = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier
                                             | (shift ? juce::ModifierKeys::shiftModifier : 0));
        const auto pos = juce::Point<float>((float) x, (float) y);
        return juce::MouseEvent(src, pos, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &ed, &ed,
                                juce::Time::getCurrentTime(), pos, juce::Time::getCurrentTime(), 1, false);
    }

    void runTest() override
    {
        StepEditor ed;
        ed.setBounds(0, 0, 400, 40);   // 4 cells of 100 px
        ed.setSteps({ 0.0f, 0.0f, 0.0f, 0.0f });
        ed.setLevelCells(3);
        std::vector<std::pair<int, float>> changes;
        ed.onStepChanged = [&](int i, float v) { changes.push_back({ i, v }); };
        int statusCell = -1, statusLevel = -1;
        ed.onCellStatus = [&](int c, int l) { statusCell = c; statusLevel = l; };

        beginTest("levels map to 0 / 50 / 100");
        {
            expectEquals(ed.levelOfValue(0.0f), 0);  expectEquals(ed.levelOfValue(50.0f), 1);  expectEquals(ed.levelOfValue(100.0f), 2);
            expectEquals(ed.valueOfLevel(0), 0.0f);  expectEquals(ed.valueOfLevel(1), 50.0f);  expectEquals(ed.valueOfLevel(2), 100.0f);
        }

        beginTest("click cycles Off -> On -> Accented -> Off, whatever the mouse height");
        {
            ed.mouseDown(mouse(ed, 50, 2));    ed.mouseUp(mouse(ed, 50, 2));
            expectEquals(ed.getSteps()[0], 50.0f);
            ed.mouseDown(mouse(ed, 50, 38));   ed.mouseUp(mouse(ed, 50, 38));
            expectEquals(ed.getSteps()[0], 100.0f);
            ed.mouseDown(mouse(ed, 50, 20));   ed.mouseUp(mouse(ed, 50, 20));
            expectEquals(ed.getSteps()[0], 0.0f);
            expectEquals((int) changes.size(), 3);
            expectEquals(statusCell, 0);
        }

        beginTest("Shift+click goes backwards");
        {
            ed.mouseDown(mouse(ed, 150, 20, true));   ed.mouseUp(mouse(ed, 150, 20, true));
            expectEquals(ed.getSteps()[1], 100.0f);   // Off -> Accented
            ed.mouseDown(mouse(ed, 150, 20, true));   ed.mouseUp(mouse(ed, 150, 20, true));
            expectEquals(ed.getSteps()[1], 50.0f);
            expectEquals(statusLevel, 1);
        }

        beginTest("a drag paints the first click's state into each cell entered");
        {
            ed.setSteps({ 0.0f, 100.0f, 50.0f, 0.0f });
            changes.clear();
            ed.mouseDown(mouse(ed, 50, 20));          // cell 0: Off -> On
            ed.mouseDrag(mouse(ed, 150, 5));          // cell 1 (was Accented) -> On
            ed.mouseDrag(mouse(ed, 160, 30));         // still cell 1: no further change
            ed.mouseDrag(mouse(ed, 250, 35));         // cell 2 already On: no change reported
            ed.mouseDrag(mouse(ed, 350, 20));         // cell 3 -> On
            ed.mouseUp(mouse(ed, 350, 20));
            for (float v : ed.getSteps()) expectEquals(v, 50.0f);
            expectEquals((int) changes.size(), 3);
        }

        beginTest("level cells are off by default and can be switched off again");
        {
            StepEditor plain;
            expectEquals(plain.getLevelCells(), 0);
            ed.setLevelCells(0);
            expectEquals(ed.getLevelCells(), 0);
        }
    }
};

static StepEditorLevelCellTests stepEditorLevelCellTests;
