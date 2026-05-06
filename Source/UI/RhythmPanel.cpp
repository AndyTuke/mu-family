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

    // juce::Label provides bulletproof inline editing: handles single-click to edit,
    // Enter to commit, Escape to cancel, click-off to commit, focus management — all
    // natively. Our previous TextEditor + onFocusLost setup was fragile because
    // onFocusLost only fires when another component grabs keyboard focus, which most
    // child components in this UI don't do.
    nameLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(13.0f)));
    nameLabel.setJustificationType(juce::Justification::centredLeft);
    nameLabel.setColour(juce::Label::textColourId,
                        MuClidLookAndFeel::colour(MuClidLookAndFeel::ColourIds::headingText));
    nameLabel.setEditable(true, true, false); // editOnSingleClick, editOnDoubleClick, lossOfFocusDiscardsChanges=false
    nameLabel.onTextChange = [this] { commitNameFromLabel(); };
    addAndMakeVisible(nameLabel);

    resetBtn.onClick      = [this] { confirmReset();       };
    deleteBtn.onClick     = [this] { confirmDelete();      };
    loadRhythmBtn.onClick = [this] { loadRhythmPreset();  };
    saveRhythmBtn.onClick = [this] { saveRhythmPreset();  };
    addAndMakeVisible(resetBtn);
    addAndMakeVisible(deleteBtn);
    addAndMakeVisible(loadRhythmBtn);
    addAndMakeVisible(saveRhythmBtn);

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

    voiceSection.onInsertAlgorithmChanged = [this](int charId)
    {
        modulatorPanel.setInsertAlgorithm(charId);
    };
}

void RhythmPanel::setRhythm(int index)
{
    // Commit any in-progress edit (Label::hideEditor with discardChanges=false saves
    // current text and fires onTextChange synchronously, which writes the rename to
    // the OLD currentRhythmIndex before we update it).
    if (nameLabel.getCurrentTextEditor() != nullptr)
        nameLabel.hideEditor(false);

    currentRhythmIndex = index;
    if (index >= 0 && index < proc.getNumRhythms())
    {
        nameLabel.setText(juce::String(proc.getRhythm(index).name), juce::dontSendNotification);
        euclidPanel.setRhythm(index);
        euclidPanel.setRhythmColour(currentColour());
        voiceSection.setRhythm(index);
        modulatorPanel.setRhythm(&proc.getRhythm(index));
        midiModeDropdown.setSelectedId(proc.getRhythm(index).midiMode ? 2 : 1, false);
        refreshCircle();
    }
    else
    {
        nameLabel.setText("No Rhythm", juce::dontSendNotification);
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
                proc.loadSampleForRhythm(currentRhythmIndex, result);
                repaint();
            }
        });
}

void RhythmPanel::loadRhythmPreset()
{
    if (currentRhythmIndex < 0) return;

    const juce::File startDir = proc.getRhythmsDir().isDirectory()
                                    ? proc.getRhythmsDir()
                                    : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);

    rhythmLoadChooser = std::make_unique<juce::FileChooser>(
        "Load Rhythm Preset", startDir, "*.muRhyth");

    rhythmLoadChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            const auto result = fc.getResult();
            if (!result.existsAsFile() || currentRhythmIndex < 0) return;

            // stageRhythmPreset applies immediately if not playing, or stages for hot-swap.
            proc.stageRhythmPreset(currentRhythmIndex, result);

            // Refresh UI immediately (for the non-playing case or to pick up name/colour).
            if (!proc.sequencerPlaying.get())
            {
                setRhythm(currentRhythmIndex);
                repaint();
            }
        });
}

void RhythmPanel::saveRhythmPreset()
{
    if (currentRhythmIndex < 0) return;

    const juce::String defaultName =
        juce::String(proc.getRhythm(currentRhythmIndex).name).replaceCharacters("\\/:|*?<>\"", "_");

    const juce::File startDir = proc.getRhythmsDir().isDirectory()
                                    ? proc.getRhythmsDir()
                                    : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);

    rhythmSaveChooser = std::make_unique<juce::FileChooser>(
        "Save Rhythm Preset", startDir.getChildFile(defaultName), "*.muRhyth");

    rhythmSaveChooser->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            auto result = fc.getResult();
            if (result == juce::File{} || currentRhythmIndex < 0) return;
            if (result.getFileExtension().toLowerCase() != ".murhyth")
                result = result.withFileExtension(".muRhyth");
            proc.saveRhythmPresetToFile(currentRhythmIndex, result);
        });
}

void RhythmPanel::mouseDown(const juce::MouseEvent& e)
{
    // Name editing is handled by nameLabel (a child component); we no longer need
    // the manual nameRect hit-test here.
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

    // Header colour dot + rhythm-colour border around the name area.
    // Name text itself is rendered by nameLabel (a child component).
    if (currentRhythmIndex >= 0 && currentRhythmIndex < proc.getNumRhythms())
    {
        g.setColour(col);
        g.fillEllipse(10.0f, (kHeaderH - 10) * 0.5f, 10.0f, 10.0f);
        g.drawRoundedRectangle(nameRect.toFloat().reduced(1.0f), 4.0f, 1.5f);
    }

    // Sample bar — content inset from panel outline
    {
        const auto inner = sampleRect.reduced(3);
        g.setColour(MuClidLookAndFeel::colour(Id::sampleBarBackground));
        g.fillRect(inner);

        const juce::String sampleName = proc.getSampleName(currentRhythmIndex);
        if (sampleName.isNotEmpty())
        {
            g.setColour(MuClidLookAndFeel::colour(Id::labelText));
            g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
            g.drawText(sampleName,
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
    // Cap topH so the modulator panel always gets at least minModH pixels:
    // tab(28) + header(28) + editor(100) + 2×timing(56) + gap(4) + addBtn(28) + reduce(14) = 258.
    const int minModH = 258;
    const int avail   = juce::jmax(80, contentH - minModH);
    topH    = juce::jlimit(80, avail, (int)(contentH * 0.55f));
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
    deleteBtn    .setBounds(rightEdge - kIconBtnW,                                           btnY, kIconBtnW,   20);
    resetBtn     .setBounds(rightEdge - kIconBtnW * 2 - 4,                                  btnY, kIconBtnW,   20);
    saveRhythmBtn.setBounds(rightEdge - kIconBtnW * 2 - 4 - kPresetBtnW - 4,               btnY, kPresetBtnW, 20);
    loadRhythmBtn.setBounds(rightEdge - kIconBtnW * 2 - 4 - kPresetBtnW * 2 - 4 - 2,       btnY, kPresetBtnW, 20);
    const int nameX   = 26;
    const int nameEnd = rightEdge - kIconBtnW * 2 - 4 - kPresetBtnW * 2 - 4 - 2 - 6;
    nameRect = { nameX, 0, nameEnd - nameX, kHeaderH };
    nameLabel.setBounds(nameRect.reduced(4, 2));

    circle.setBounds        (circleRect.reduced(kPanelPad + 1));
    euclidPanel.setBounds   (euclidRect.reduced(kPanelPad + 1));
    voiceSection.setBounds  (voiceRect.reduced(kPanelPad + 1));
    modulatorPanel.setBounds(modRect.reduced(kPanelPad + 1));
}

//==============================================================================
// Called by juce::Label when the user finishes editing (Enter, click-off, etc.)
// Writes the Label's text into the current rhythm and notifies listeners.
void RhythmPanel::commitNameFromLabel()
{
    if (currentRhythmIndex < 0 || currentRhythmIndex >= proc.getNumRhythms()) return;

    auto newName = nameLabel.getText().trim();
    if (newName.isEmpty())
    {
        newName = "<unnamed>";
        nameLabel.setText(newName, juce::dontSendNotification);
    }

    proc.getRhythm(currentRhythmIndex).name = newName.toStdString();
    if (onRhythmRenamed) onRhythmRenamed();
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

                    // Acquire modLock so the audio thread can't read modulationMatrix /
                    // controlSequences while operator= replaces them.
                    bool expected = false;
                    while (!r.modLock.compare_exchange_weak(expected, true,
                                                            std::memory_order_acquire))
                        expected = false;
                    r = Rhythm{};
                    r.name        = savedName;
                    r.colourIndex = savedColour;
                    r.modLock.store(false, std::memory_order_release);

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
