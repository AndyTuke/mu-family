#include "MidiMapOverlay.h"
#include "ConfirmDialog.h"
#include "Plugin/ProcessorBase.h"
#include "UI/StatusSink.h"

namespace mu_ui
{
using mu_core::ControlActionType;
using mu_core::MidiMapping;

// One mapping row: source + Learn, Action, an optional Layer / Clip / Parameter target, Delete.
class MidiMapOverlay::RowComp : public juce::Component
{
public:
    RowComp(MidiMapOverlay& o, int r) : owner(o), row(r)
    {
        learnBtn.onClick = [this] { owner.startLearn(row); };
        setStatusName(learnBtn, "MIDI Learn");
        addAndMakeVisible(learnBtn);

        actionDd.setStatusName("Action");
        actionDd.onChange = [this](int id)
        {
            auto& data = owner.rows[(size_t) row];
            data.m.action.type = (ControlActionType) id;
            data.m.action.paramId = {};
            owner.applyRow(row);
            owner.refreshRows();
        };
        addAndMakeVisible(actionDd);

        layerDd.setStatusName("Target");
        layerDd.onChange = [this](int id) { owner.rows[(size_t) row].m.action.layer = id - 1; owner.applyRow(row); };
        addChildComponent(layerDd);

        clipDd.setStatusName("Target");
        for (int i = 0; i < 8; ++i) clipDd.addItem("Clip " + juce::String(i + 1), i + 1);
        clipDd.onChange = [this](int id) { owner.rows[(size_t) row].m.action.index = id - 1; owner.applyRow(row); };
        addChildComponent(clipDd);

        paramBtn.onClick = [this] { owner.showParamMenu(row); };
        setStatusName(paramBtn, "Target");
        addChildComponent(paramBtn);

        deleteBtn.onClick = [this] { owner.deleteRow(row); };
        setStatusName(deleteBtn, "MIDI Mapping");
        addAndMakeVisible(deleteBtn);

        for (int i = 0; i < owner.proc.getNumLayers(); ++i)
            layerDd.addItem(owner.proc.getLayerName(i), i + 1);
        refresh(r);
    }

    void refresh(int r)
    {
        row = r;
        auto& data = owner.rows[(size_t) row];
        const bool learning = owner.learningRow == row;
        learnBtn.setButtonText(learning ? "Listening..." : "Learn");
        learnBtn.setToggleState(learning, juce::dontSendNotification);

        const auto choices = midimap::offeredActions(data.m.isNote, owner.proc.hasClipBank());
        actionDd.clear();
        for (const auto& c : choices) actionDd.addItem(c.label, (int) c.type);
        const auto* current = midimap::findChoice(choices, data.m.action.type);
        actionDd.setSelectedId(current != nullptr ? (int) data.m.action.type : 0, false);
        actionDd.setPlaceholderText("Action");

        const bool needLayer = current != nullptr && current->needsLayer;
        const bool needClip  = current != nullptr && current->needsClip;
        const bool needParam = current != nullptr && current->needsParam;
        layerDd.setVisible(needLayer);
        clipDd.setVisible(needClip);
        paramBtn.setVisible(needParam);
        layerDd.setSelectedId(data.m.action.layer + 1, false);
        clipDd.setSelectedId(juce::jmax(0, data.m.action.index) + 1, false);
        paramBtn.setButtonText(data.m.action.paramId.isEmpty() ? "Choose parameter..." : owner.targetText(data));
        resized();
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        using Id = MuLookAndFeel::ColourIds;
        const auto& data = owner.rows[(size_t) row];
        const bool learning = owner.learningRow == row;
        g.setFont(MuLookAndFeel::uiFont(mu_ui::sf(12.0f)));
        g.setColour(MuLookAndFeel::colour(learning ? Id::segmentWarningBorder : data.hasSource ? Id::valueText : Id::mutedText));
        g.drawText(learning ? juce::String("Move a control") : data.hasSource ? MidiMapOverlay::sourceText(data.m) : juce::String("No source"),
                   sourceArea, juce::Justification::centredLeft, true);
        g.setColour(MuLookAndFeel::colour(Id::segmentInactiveBorder).withAlpha(0.3f));
        g.drawLine(0.0f, (float) (getHeight() - 1), (float) getWidth(), (float) (getHeight() - 1), 0.5f);
    }

    void resized() override
    {
        using L = MuLookAndFeel;
        using mu_ui::s;
        const int pad = s(L::kSpaceL), gap = s(L::kSpaceM), h = getHeight() - s(L::kSpaceXS);
        const int y = s(L::kSpaceXS) / 2;
        int x = pad;
        sourceArea = { x, 0, s(L::kMidiMapSourceW), getHeight() };
        x += s(L::kMidiMapSourceW) + gap;
        const int learnW = s(96);
        learnBtn.setBounds(x, y, learnW, h);
        x += learnW + gap;
        actionDd.setBounds(x, y, s(L::kMidiMapActionW), h);
        x += s(L::kMidiMapActionW) + gap;
        const int delW = s(64);
        const int targetW = juce::jmax(s(80), getWidth() - x - delW - pad - gap);
        layerDd.setBounds(x, y, juce::jmin(targetW, s(160)), h);
        clipDd.setBounds(layerDd.isVisible() ? layerDd.getRight() + gap : x, y, s(90), h);
        paramBtn.setBounds(x, y, targetW, h);
        deleteBtn.setBounds(getWidth() - pad - delW, y, delW, h);
    }

private:
    MidiMapOverlay&      owner;
    int                  row;
    juce::Rectangle<int> sourceArea;
    juce::TextButton     learnBtn { "Learn" }, paramBtn { "Choose parameter..." }, deleteBtn { "Delete" };
    DropdownSelect       actionDd, layerDd, clipDd;
};

MidiMapOverlay::MidiMapOverlay(ProcessorBase& p) : proc(p)
{
    setWantsKeyboardFocus(true);

    closeBtn.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible(closeBtn);

    addBtn.onClick = [this] { say("MIDI Mapping", "Clicked"); addDraftAndLearn(); };
    setStatusName(addBtn, "MIDI Mapping");
    addAndMakeVisible(addBtn);

    clearBtn.onClick = [this]
    {
        juce::Component::SafePointer<MidiMapOverlay> safe(this);
        confirmAsync(this, "Clear all mappings", "Remove every MIDI mapping?", "Clear all", [safe]
        {
            if (safe == nullptr) return;
            safe->cancelLearn();
            safe->proc.getMidiControlMap().clear();
            safe->reload();
            safe->say("MIDI Mapping", "Cleared");
        });
    };
    setStatusName(clearBtn, "MIDI Mapping");
    addAndMakeVisible(clearBtn);

    quantiseDd.addItem("Off", (int) mu_core::Quantise::Off);
    quantiseDd.addItem("Beat", (int) mu_core::Quantise::Beat);
    quantiseDd.addItem("Bar", (int) mu_core::Quantise::Bar);
    quantiseDd.setStatusName("Mute / solo timing");
    quantiseDd.onChange = [this](int id) { proc.getMidiControlMap().setQuantise((mu_core::Quantise) id); };
    addAndMakeVisible(quantiseDd);

    listBox.setModel(this);
    listBox.setRowHeight(mu_ui::s(MuLookAndFeel::kMidiMapRowH));
    listBox.setColour(juce::ListBox::backgroundColourId, MuLookAndFeel::colour(MuLookAndFeel::panelBackground));
    addAndMakeVisible(listBox);
}

MidiMapOverlay::~MidiMapOverlay()
{
    proc.getMidiControlMap().cancelLearn();
}

juce::String MidiMapOverlay::sourceText(const MidiMapping& m)
{
    const auto number = m.isNote ? juce::MidiMessage::getMidiNoteName(m.number, true, true, 3) : juce::String(m.number);
    return juce::String(m.isNote ? "Note " : "CC ") + number + "  Ch " + juce::String(m.channel);
}

void MidiMapOverlay::say(const juce::String& name, const juce::String& value)
{
    reportStatus(*this, name, value);
}

void MidiMapOverlay::visibilityChanged()
{
    if (isVisible())
    {
        reload();
        quantiseDd.setSelectedId((int) proc.getMidiControlMap().getQuantise(), false);
        startTimerHz(20);
        grabKeyboardFocus();
    }
    else
    {
        stopTimer();
        cancelLearn();   // learn never outlives the screen
    }
}

bool MidiMapOverlay::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey && learningRow >= 0)
    {
        cancelLearn();
        say("MIDI Learn", "Cancelled");
        return true;
    }
    return false;
}

void MidiMapOverlay::reload()
{
    rows.clear();
    auto& map = proc.getMidiControlMap();
    for (int i = 0; i < map.size(); ++i)
    {
        Row r;
        r.storedIndex = i;
        r.hasSource = true;
        r.m = map.get(i);
        rows.push_back(std::move(r));
    }
    learningRow = -1;
    refreshRows();
}

void MidiMapOverlay::refreshRows()
{
    listBox.updateContent();
    for (int i = 0; i < (int) rows.size(); ++i)
        if (auto* c = dynamic_cast<RowComp*>(listBox.getComponentForRowNumber(i)))
            c->refresh(i);
    listBox.repaint();
}

juce::Component* MidiMapOverlay::refreshComponentForRow(int row, bool, juce::Component* existing)
{
    if (row < 0 || row >= (int) rows.size())
    {
        delete existing;
        return nullptr;
    }
    auto* c = dynamic_cast<RowComp*>(existing);
    if (c == nullptr)
    {
        delete existing;
        c = new RowComp(*this, row);
    }
    else
    {
        c->refresh(row);
    }
    return c;
}

bool MidiMapOverlay::isComplete(const Row& r) const
{
    if (! r.hasSource || r.m.action.type == ControlActionType::None) return false;
    if (r.m.action.type == ControlActionType::Parameter) return r.m.action.paramId.isNotEmpty();
    return true;
}

void MidiMapOverlay::addDraftAndLearn()
{
    rows.push_back({});
    refreshRows();
    listBox.scrollToEnsureRowIsOnscreen((int) rows.size() - 1);
    startLearn((int) rows.size() - 1);
}

void MidiMapOverlay::startLearn(int row)
{
    if (row < 0 || row >= (int) rows.size()) return;
    // Learn pressed again on the row that is listening ends it.
    if (learningRow == row) { cancelLearn(); say("MIDI Learn", "Cancelled"); return; }
    cancelLearn();
    if (row >= (int) rows.size()) return;
    learningRow = row;
    learnStartMs = juce::Time::getMillisecondCounter();
    proc.getMidiControlMap().armLearn();
    say("MIDI Learn", "Listening (Esc cancels)");
    refreshRows();
}

void MidiMapOverlay::cancelLearn()
{
    proc.getMidiControlMap().cancelLearn();
    if (learningRow < 0) return;
    const int row = learningRow;
    learningRow = -1;
    // A new draft that never learned a source goes away; a stored row keeps its old source.
    if (row < (int) rows.size() && rows[(size_t) row].storedIndex < 0 && ! rows[(size_t) row].hasSource
        && rows[(size_t) row].m.action.type == ControlActionType::None)
        rows.erase(rows.begin() + row);
    refreshRows();
}

void MidiMapOverlay::timerCallback()
{
    tickLearn(juce::Time::getMillisecondCounter());
}

void MidiMapOverlay::tickLearn(juce::uint32 nowMs)
{
    if (learningRow < 0) return;
    bool isNote = false;
    int channel = 0, number = 0;
    if (proc.getMidiControlMap().takeLearned(isNote, channel, number))
    {
        finishLearn(learningRow, isNote, channel, number);
    }
    else if (nowMs - learnStartMs >= (juce::uint32) MuLookAndFeel::kMidiLearnTimeoutMs)
    {
        cancelLearn();
        say("MIDI Learn", "Timed out");
    }
}

void MidiMapOverlay::finishLearn(int row, bool isNote, int channel, int number)
{
    proc.getMidiControlMap().cancelLearn();
    learningRow = -1;
    if (row < 0 || row >= (int) rows.size()) return;
    auto& r = rows[(size_t) row];
    r.hasSource = true;
    r.m.isNote = isNote;
    r.m.channel = channel;
    r.m.number = number;
    // A note cannot drive a knob: a Parameter action is dropped when the source turns out to be a note.
    if (isNote && r.m.action.type == ControlActionType::Parameter)
    {
        r.m.action.type = ControlActionType::None;
        r.m.action.paramId = {};
    }
    say("Learned", sourceText(r.m));
    applyRow(row);
    refreshRows();
}

// Stores a complete row: removes the stored entry it came from, adds the new one (which replaces a mapping from the same source).
void MidiMapOverlay::applyRow(int row)
{
    if (row < 0 || row >= (int) rows.size() || ! isComplete(rows[(size_t) row])) return;
    auto& map = proc.getMidiControlMap();

    if (rows[(size_t) row].storedIndex >= 0)
    {
        const int old = rows[(size_t) row].storedIndex;
        map.remove(old);
        for (auto& other : rows)
            if (other.storedIndex > old) --other.storedIndex;
        rows[(size_t) row].storedIndex = -1;
    }

    // A stored row on the same source is replaced by this one.
    bool replaced = false;
    const auto mine = rows[(size_t) row].m;
    for (int i = 0; i < (int) rows.size(); ++i)
    {
        if (i == row) continue;
        const auto& o = rows[(size_t) i];
        if (o.storedIndex >= 0 && o.m.isNote == mine.isNote && o.m.channel == mine.channel && o.m.number == mine.number)
        {
            const int removed = o.storedIndex;
            map.remove(removed);
            rows.erase(rows.begin() + i);
            for (auto& other : rows)
                if (other.storedIndex > removed) --other.storedIndex;
            if (i < row) --row;
            if (learningRow > i) --learningRow;
            replaced = true;
            break;
        }
    }

    rows[(size_t) row].storedIndex = map.add(mine);
    if (replaced) say("MIDI Mapping", "Replaced " + sourceText(mine));
    refreshRows();
}

void MidiMapOverlay::deleteRow(int row)
{
    if (row < 0 || row >= (int) rows.size()) return;
    if (learningRow == row) cancelLearn();
    if (row >= (int) rows.size()) return;
    const int stored = rows[(size_t) row].storedIndex;
    if (stored >= 0)
    {
        proc.getMidiControlMap().remove(stored);
        for (auto& other : rows)
            if (other.storedIndex > stored) --other.storedIndex;
    }
    if (learningRow > row) --learningRow;
    rows.erase(rows.begin() + row);
    say("MIDI Mapping", "Removed");
    refreshRows();
}

juce::String MidiMapOverlay::targetText(const Row& r) const
{
    if (auto* p = proc.apvts.getParameter(r.m.action.paramId))
        return p->getName(40);
    return r.m.action.paramId;
}

// The parameter pop-up: every automatable parameter, in sub-menus by group where the product groups them.
void MidiMapOverlay::showParamMenu(int row)
{
    paramIds.clear();
    juce::PopupMenu menu;

    std::function<void(const juce::AudioProcessorParameterGroup&, juce::PopupMenu&)> addGroup =
        [&](const juce::AudioProcessorParameterGroup& group, juce::PopupMenu& into)
    {
        for (auto* node : group)
        {
            if (auto* sub = node->getGroup())
            {
                juce::PopupMenu subMenu;
                addGroup(*sub, subMenu);
                if (subMenu.getNumItems() > 0)
                    into.addSubMenu(sub->getName(), subMenu);
            }
            else if (auto* param = node->getParameter())
            {
                if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(param))
                {
                    if (! param->isAutomatable()) continue;
                    paramIds.push_back(withId->paramID);
                    into.addItem((int) paramIds.size(), param->getName(40));
                }
            }
        }
    };
    addGroup(proc.getParameterTree(), menu);

    juce::Component::SafePointer<MidiMapOverlay> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
                       [safe, row](int result)
    {
        if (safe == nullptr || result <= 0 || result > (int) safe->paramIds.size()) return;
        if (row < 0 || row >= (int) safe->rows.size()) return;
        safe->rows[(size_t) row].m.action.paramId = safe->paramIds[(size_t) result - 1];
        safe->say("Target", safe->targetText(safe->rows[(size_t) row]));
        safe->applyRow(row);
        safe->refreshRows();
    });
}

void MidiMapOverlay::setRowForTest(int row, bool isNote, int channel, int number, ControlActionType type, int layer, int clip)
{
    if (row < 0 || row >= (int) rows.size()) return;
    auto& r = rows[(size_t) row];
    r.hasSource = true;
    r.m.isNote = isNote;
    r.m.channel = channel;
    r.m.number = number;
    r.m.action.type = type;
    r.m.action.layer = layer;
    r.m.action.index = clip;
    applyRow(row);
}

void MidiMapOverlay::resized()
{
    using L = MuLookAndFeel;
    using mu_ui::s;
    const int w = getWidth();
    const int pad = s(L::kSpaceL), gap = s(L::kSpaceM);
    const int headerH = s(36), topH = s(26);

    closeBtn.setBounds(w - pad - s(60), (headerH - s(26)) / 2, s(60), s(26));

    int x = pad;
    const int topY = headerH + pad;
    addBtn.setBounds(x, topY, s(120), topH);
    x += s(120) + gap;
    clearBtn.setBounds(x, topY, s(90), topH);
    quantiseDd.setBounds(w - pad - s(200), topY, s(200), topH);

    const int listY = topY + topH + s(18) + pad;
    listBox.setBounds(pad, listY, w - pad * 2, getHeight() - listY - pad);
}

void MidiMapOverlay::paint(juce::Graphics& g)
{
    using Id = MuLookAndFeel::ColourIds;
    using mu_ui::s;
    using mu_ui::sf;
    const int pad = s(MuLookAndFeel::kSpaceL);
    const int headerH = s(36), topH = s(26);

    g.setColour(MuLookAndFeel::colour(Id::panelBackground));
    g.fillAll();

    g.setColour(MuLookAndFeel::colour(Id::headingText));
    g.setFont(MuLookAndFeel::uiFont(sf(14.0f)));
    g.drawText("MIDI Mapping", pad, 0, s(420), headerH, juce::Justification::centredLeft, false);

    g.setColour(MuLookAndFeel::colour(Id::segmentInactiveBorder));
    g.drawLine(0.0f, (float) headerH, (float) getWidth(), (float) headerH, 0.5f);

    // Label for the timing drop-down, right-aligned against it.
    g.setColour(MuLookAndFeel::colour(Id::labelText));
    g.setFont(MuLookAndFeel::uiFont(sf(11.0f)));
    g.drawText("Mute / solo timing", quantiseDd.getX() - s(150), quantiseDd.getY(), s(144), quantiseDd.getHeight(), juce::Justification::centredRight, false);

    g.setColour(MuLookAndFeel::colour(Id::mutedText));
    g.setFont(MuLookAndFeel::uiFont(sf(10.0f)));
    g.drawText("Add mapping, then move a control on your controller: it is mapped to the action you choose.",
               pad, headerH + pad + topH + s(2), getWidth() - pad * 2, s(18), juce::Justification::centredLeft, false);
}
} // namespace mu_ui
