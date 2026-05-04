#include "RhythmPanel.h"

RhythmPanel::RhythmPanel(PluginProcessor& p)
    : proc(p), euclidPanel(p), voiceSection(p)
{
    startTimerHz(30);
    addAndMakeVisible(circle);
    addAndMakeVisible(euclidPanel);
    addAndMakeVisible(voiceSection);
    addAndMakeVisible(modulatorPanel);

    midiModeDropdown.addItem("Sample", 1);
    midiModeDropdown.addItem("MIDI",   2);
    midiModeDropdown.setSelectedId(1, false);
    addAndMakeVisible(midiModeDropdown);

    nameEditor.setFont(juce::Font(juce::FontOptions{}.withHeight(13.0f)));
    nameEditor.setJustification(juce::Justification::centredLeft);
    nameEditor.onReturnKey = [this] { finishEditingName(true);  };
    nameEditor.onEscapeKey = [this] { finishEditingName(false); };
    nameEditor.onFocusLost = [this] { finishEditingName(true);  };
    nameEditor.setVisible(false);
    addAndMakeVisible(nameEditor);

    resetBtn.onClick  = [this] { confirmReset();  };
    deleteBtn.onClick = [this] { confirmDelete(); };
    addAndMakeVisible(resetBtn);
    addAndMakeVisible(deleteBtn);

    midiModeDropdown.onChange = [this](int id)
    {
        if (currentRhythmIndex < 0) return;
        const auto paramId = "r" + juce::String(currentRhythmIndex) + "_midiMode";
        if (auto* p = proc.apvts.getParameter(paramId))
            p->setValueNotifyingHost(id == 2 ? 1.0f : 0.0f);
    };

    euclidPanel.onPatternChanged = [this]
    {
        refreshCircle();
    };

    euclidPanel.onStatusUpdate = [this](const juce::String& name, const juce::String& val)
    {
        if (onStatusUpdate) onStatusUpdate(name, val, currentColour());
    };

    voiceSection.onStatusUpdate = [this](const juce::String& name, const juce::String& val)
    {
        if (onStatusUpdate) onStatusUpdate(name, val, currentColour());
    };
}

void RhythmPanel::setRhythm(int index)
{
    // Commit any in-progress name edit before switching rhythms so the name
    // is saved even if the focus-lost event hasn't fired yet.
    if (editingName)
        finishEditingName(true);

    currentRhythmIndex = index;
    if (index >= 0 && index < proc.getNumRhythms())
    {
        euclidPanel.setRhythm(index);
        euclidPanel.setRhythmColour(currentColour());
        voiceSection.setRhythm(index);
        modulatorPanel.setRhythm(&proc.getRhythm(index));
        midiModeDropdown.setSelectedId(proc.getRhythm(index).midiMode ? 2 : 1, false);
        refreshCircle();
    }
    repaint();
}

void RhythmPanel::refreshCircle()
{
    if (currentRhythmIndex < 0 || currentRhythmIndex >= proc.getNumRhythms()) return;
    const Rhythm& r = proc.getRhythm(currentRhythmIndex);
    circle.setPatterns(r.genA.getStepTypes(), r.genB.getStepTypes(), r.genC.getStepTypes());
    circle.setPlayState(&proc.rhythmPlayState[currentRhythmIndex],
                        &proc.beatFraction,
                        &proc.sequencerPlaying,
                        currentColour());
}

juce::Colour RhythmPanel::currentColour() const
{
    if (currentRhythmIndex >= 0 && currentRhythmIndex < proc.getNumRhythms())
    {
        const Rhythm& r = proc.getRhythm(currentRhythmIndex);
        return MuClidLookAndFeel::rhythmPalette[r.colourIndex % 30];
    }
    return juce::Colours::transparentBlack;
}

bool RhythmPanel::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (auto& f : files)
    {
        auto ext = juce::File(f).getFileExtension().toLowerCase();
        if (ext == ".wav" || ext == ".aiff" || ext == ".aif"
            || ext == ".mp3" || ext == ".flac")
            return true;
    }
    return false;
}

void RhythmPanel::filesDropped(const juce::StringArray& files, int, int)
{
    for (auto& f : files)
    {
        juce::File file(f);
        if (file.existsAsFile() && currentRhythmIndex >= 0)
        {
            loadedSampleNames[currentRhythmIndex] = file.getFileNameWithoutExtension();
            proc.loadSampleForRhythm(currentRhythmIndex, file);
            repaint();
            return;
        }
    }
}

void RhythmPanel::loadSample()
{
    const juce::File startDir = lastBrowseDir.isDirectory()
                                    ? lastBrowseDir
                                    : juce::File::getSpecialLocation(juce::File::userMusicDirectory);

    fileChooser = std::make_unique<juce::FileChooser>(
        "Load Sample", startDir, "*.wav;*.aiff;*.aif;*.mp3;*.flac");

    fileChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            auto result = fc.getResult();
            if (result.existsAsFile() && currentRhythmIndex >= 0)
            {
                lastBrowseDir = result.getParentDirectory();
                loadedSampleNames[currentRhythmIndex] = result.getFileNameWithoutExtension();
                proc.loadSampleForRhythm(currentRhythmIndex, result);
                repaint();
            }
        });
}

void RhythmPanel::mouseDown(const juce::MouseEvent& e)
{
    if (nameRect.contains(e.getPosition()) && !editingName)
    {
        startEditingName();
        return;
    }

    const juce::Rectangle<int> sampleBar { 0, kHeaderH, getWidth(), kSampleBarH };
    if (sampleBar.contains(e.getPosition()))
        loadSample();
}

void RhythmPanel::paint(juce::Graphics& g)
{
    using Id = MuClidLookAndFeel::ColourIds;

    g.setColour(MuClidLookAndFeel::colour(Id::panelBackground));
    g.fillAll();

    // Header
    juce::Colour col = currentColour();

    if (currentRhythmIndex >= 0 && currentRhythmIndex < proc.getNumRhythms())
    {
        const Rhythm& r = proc.getRhythm(currentRhythmIndex);
        g.setColour(col);
        g.fillEllipse(10.0f, (kHeaderH - 10) * 0.5f, 10.0f, 10.0f);
        if (!editingName)
        {
            g.setColour(MuClidLookAndFeel::colour(Id::headingText));
            g.setFont(juce::Font(juce::FontOptions{}.withHeight(13.0f)));
            g.drawText(juce::String(r.name), nameRect, juce::Justification::centredLeft, true);
        }
    }
    else
    {
        g.setColour(MuClidLookAndFeel::colour(Id::mutedText));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(13.0f)));
        g.drawText("No Rhythm", nameRect, juce::Justification::centredLeft, true);
    }

    // Sample bar — content inset from panel outline
    {
        const auto inner = sampleRect.reduced(3);
        g.setColour(MuClidLookAndFeel::colour(Id::sampleBarBackground));
        g.fillRect(inner);

        auto it = loadedSampleNames.find(currentRhythmIndex);
        if (it != loadedSampleNames.end())
        {
            g.setColour(MuClidLookAndFeel::colour(Id::labelText));
            g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
            g.drawText(it->second,
                       inner.getX() + 5, inner.getY(), inner.getWidth() - 28, inner.getHeight(),
                       juce::Justification::centredLeft, true);
        }
        else
        {
            g.setColour(MuClidLookAndFeel::colour(Id::sampleBarNoSample));
            g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f).withStyle("Italic")));
            g.drawText("drop sample here or click to browse",
                       inner.getX() + 5, inner.getY(), inner.getWidth() - 28, inner.getHeight(),
                       juce::Justification::centredLeft, true);
        }

        g.setColour(MuClidLookAndFeel::colour(Id::labelText));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
        g.drawText("...", inner.getRight() - 24, inner.getY(), 24, inner.getHeight(),
                   juce::Justification::centred, false);
    }

    // Major panel outlines — 2px in rhythm colour, rounded corners.
    // Each rect is inset by 1px so adjacent panels have a consistent 2px gap.
    g.setColour(col);
    g.drawRoundedRectangle(sampleRect.reduced(2).toFloat(), 6.0f, 2.0f);
    g.drawRoundedRectangle(circleRect.reduced(2).toFloat(), 6.0f, 2.0f);
    g.drawRoundedRectangle(euclidRect.reduced(2).toFloat(), 6.0f, 2.0f);
    g.drawRoundedRectangle(voiceRect.reduced(2).toFloat(),  6.0f, 2.0f);
    g.drawRoundedRectangle(modRect.reduced(2).toFloat(),    6.0f, 2.0f);
}

void RhythmPanel::resized()
{
    const int w        = getWidth();
    const int h        = getHeight();
    const int contentH = h - kHeaderH - kSampleBarH - kVoiceH;

    // Top section: 55% of content height; circle is square, capped at 33% of panel width.
    // This keeps sizing proportional across the full 780–2400 × 580–1600 resize range.
    topH    = juce::jmax(80,  (int)(contentH * 0.55f));
    circleW = juce::jmin(topH, (int)(w * 0.33f));

    const int topY = kHeaderH + kSampleBarH;
    const int modY = topY + topH + kVoiceH;

    sampleRect = { 0,       kHeaderH,    w,           kSampleBarH             };
    circleRect = { 0,       topY,        circleW,     topH                    };
    euclidRect = { circleW, topY,        w - circleW, topH                    };
    voiceRect  = { 0,       topY + topH, w,           kVoiceH                 };
    modRect    = { 0,       modY,        w,           juce::jmax(0, h - modY) };

    // Header right-side controls (right to left)
    const int btnY = (kHeaderH - 20) / 2;
    midiModeDropdown.setBounds(w - kModeSelectorW - 4, btnY, kModeSelectorW, 20);
    const int rightEdge = w - kModeSelectorW - 4 - 6;
    deleteBtn.setBounds(rightEdge - kIconBtnW,               btnY, kIconBtnW, 20);
    resetBtn .setBounds(rightEdge - kIconBtnW * 2 - 4,       btnY, kIconBtnW, 20);
    const int nameX = 26;
    const int nameEnd = rightEdge - kIconBtnW * 2 - 4 - 6;
    nameRect = { nameX, 0, nameEnd - nameX, kHeaderH };
    nameEditor.setBounds(nameX, (kHeaderH - 22) / 2, nameEnd - nameX, 22);

    circle.setBounds        (circleRect.reduced(kPanelPad + 1));
    euclidPanel.setBounds   (euclidRect.reduced(kPanelPad + 1));
    voiceSection.setBounds  (voiceRect.reduced(kPanelPad + 1));
    modulatorPanel.setBounds(modRect.reduced(kPanelPad + 1));
}

//==============================================================================
void RhythmPanel::startEditingName()
{
    if (currentRhythmIndex < 0 || currentRhythmIndex >= proc.getNumRhythms()) return;
    editingName = true;
    editingNameIndex = currentRhythmIndex;
    nameEditor.setText(juce::String(proc.getRhythm(currentRhythmIndex).name), false);
    nameEditor.setVisible(true);
    nameEditor.grabKeyboardFocus();
    nameEditor.selectAll();
    repaint();
}

void RhythmPanel::finishEditingName(bool save)
{
    if (!editingName) return;
    editingName = false;
    nameEditor.setVisible(false);
    if (save && editingNameIndex >= 0 && editingNameIndex < proc.getNumRhythms())
    {
        auto newName = nameEditor.getText().trim();
        if (newName.isEmpty())
            newName = "Rhythm " + juce::String(editingNameIndex + 1);
        proc.getRhythm(editingNameIndex).name = newName.toStdString();
        if (onRhythmRenamed) onRhythmRenamed();
    }
    repaint();
}

void RhythmPanel::confirmReset()
{
    if (currentRhythmIndex < 0 || currentRhythmIndex >= proc.getNumRhythms()) return;
    const int idx = currentRhythmIndex;
    const juce::String name(proc.getRhythm(idx).name);

    juce::Component::SafePointer<RhythmPanel> safeThis(this);
    auto* w = new juce::AlertWindow("Reset Rhythm",
                                    "Reset \"" + name + "\" to defaults?\nThis cannot be undone.",
                                    juce::AlertWindow::WarningIcon);
    w->addButton("Reset",  1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    w->enterModalState(true,
        juce::ModalCallbackFunction::create([safeThis, idx](int result)
        {
            if (result == 1 && safeThis != nullptr)
            {
                if (idx >= 0 && idx < safeThis->proc.getNumRhythms())
                {
                    auto& r = safeThis->proc.getRhythm(idx);
                    auto savedName   = r.name;
                    auto savedColour = r.colourIndex;
                    r = Rhythm{};
                    r.name        = savedName;
                    r.colourIndex = savedColour;
                    safeThis->proc.updatePattern(idx);
                    safeThis->setRhythm(idx);
                }
            }
        }),
        true);
}

void RhythmPanel::confirmDelete()
{
    if (currentRhythmIndex < 0 || currentRhythmIndex >= proc.getNumRhythms()) return;
    if (proc.getNumRhythms() <= 1) return; // cannot delete last rhythm
    const int idx = currentRhythmIndex;
    const juce::String name(proc.getRhythm(idx).name);

    juce::Component::SafePointer<RhythmPanel> safeThis(this);
    auto* w = new juce::AlertWindow("Delete Rhythm",
                                    "Delete \"" + name + "\"?\nThis cannot be undone.",
                                    juce::AlertWindow::WarningIcon);
    w->addButton("Delete", 1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    w->enterModalState(true,
        juce::ModalCallbackFunction::create([safeThis, idx](int result)
        {
            if (result == 1 && safeThis != nullptr)
                if (safeThis->onRhythmDeleted)
                    safeThis->onRhythmDeleted(idx);
        }),
        true);
}

void RhythmPanel::timerCallback()
{
    if (proc.sequencerPlaying.get())
        modulatorPanel.setPlayheadBeat(proc.lastBeatPos.get());
}
