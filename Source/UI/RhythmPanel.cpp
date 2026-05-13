#include "RhythmPanel.h"

// Euclidean panel params — all use r{ri}_ prefix.
static const char* const kEuclidSuffixes[] = {
    "stepsA", "hitsA", "rotA", "prePadA", "postPadA", "insStA", "insLenA",
    "prePadModeA", "postPadModeA", "insModeA",
    "stepsB", "hitsB", "rotB", "prePadB", "postPadB", "insStB", "insLenB",
    "prePadModeB", "postPadModeB", "insModeB",
    "stepsC", "hitsC", "rotC", "prePadC", "postPadC", "insStC", "insLenC",
    "prePadModeC", "postPadModeC", "insModeC",
    "logic"
};
// Voice panel params — all use r{ri}_ prefix.
static const char* const kVoiceSuffixes[] = {
    "pitchOct", "pitchSemi", "pitchFine",
    "pEnvAtk", "pEnvDec", "pEnvSus", "pEnvRel", "pEnvDep",
    "fltType", "fltCut", "fltRes",
    "fEnvAtk", "fEnvDec", "fEnvSus", "fEnvRel", "fEnvDep",
    "ampLvl", "accentDb",
    "aEnvAtk", "aEnvDec", "aEnvSus", "aEnvRel",
    "drvChar", "drvDrv", "drvOut", "drvDit", "drvTon", "eqMidGain", "drvBits", "drvRate"
};

// Send knob params — use ch{ri}_ prefix (shared with mixer channel strip).
static const char* const kSendSuffixes[] = { "sendEff", "sendDly", "sendRev" };

//==============================================================================
// Custom file browser used for sample loading so the user can audition files
// before committing to a slot. Shows inside a DialogWindow (modal).
class SampleBrowserContent : public juce::Component,
                              public juce::FileBrowserListener
{
public:
    SampleBrowserContent(PluginProcessor& proc,
                         const juce::File& startDir,
                         std::function<void(const juce::File&)> onChosen)
        : proc(proc), onChosen(std::move(onChosen)),
          fileFilter("*.wav;*.aiff;*.aif;*.mp3;*.flac", {}, "Audio files"),
          browser(juce::FileBrowserComponent::openMode |
                  juce::FileBrowserComponent::canSelectFiles,
                  startDir, &fileFilter, nullptr)
    {
        addAndMakeVisible(browser);
        addAndMakeVisible(loadBtn);
        addAndMakeVisible(cancelBtn);
        browser.addListener(this);

        loadBtn.onClick = [this]
        {
            const auto f = browser.getSelectedFile(0);
            if (f.existsAsFile()) commit(f);
        };
        cancelBtn.onClick = [this]
        {
            this->proc.stopSamplePreview();
            if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
                dw->exitModalState(0);
        };

        setSize(560, 440);
    }

    ~SampleBrowserContent() override { proc.stopSamplePreview(); }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8);
        auto btnRow = area.removeFromBottom(32).reduced(0, 4);
        cancelBtn.setBounds(btnRow.removeFromRight(80));
        btnRow.removeFromRight(8);
        loadBtn.setBounds(btnRow.removeFromRight(80));
        browser.setBounds(area.reduced(0, 4));
    }

    // FileBrowserListener — auto-preview on selection change
    void selectionChanged() override
    {
        const auto f = browser.getSelectedFile(0);
        if (f.existsAsFile())
            proc.startSamplePreview(f);
    }
    void fileClicked(const juce::File&, const juce::MouseEvent&) override {}
    void fileDoubleClicked(const juce::File& f) override { commit(f); }
    void browserRootChanged(const juce::File&) override {}

private:
    PluginProcessor& proc;
    std::function<void(const juce::File&)> onChosen;
    juce::WildcardFileFilter fileFilter;
    juce::FileBrowserComponent browser;
    juce::TextButton loadBtn { "Load" }, cancelBtn { "Cancel" };

    void commit(const juce::File& f)
    {
        proc.stopSamplePreview();
        onChosen(f);
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(1);
    }
};

//==============================================================================
// RhythmPresetBrowser implementation

RhythmPresetBrowser::RhythmPresetBrowser()
{
    searchBox.setTextToShowWhenEmpty("Search...", juce::Colours::grey);
    searchBox.onTextChange = [this] { applyFilter(); };
    addAndMakeVisible(searchBox);

    listBox.setModel(this);
    listBox.setRowHeight(26);
    listBox.setColour(juce::ListBox::backgroundColourId,
                      MuClidLookAndFeel::colour(MuClidLookAndFeel::panelBackground));
    addAndMakeVisible(listBox);

    loadBtn.onClick   = [this] { loadSelected(); };
    cancelBtn.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible(loadBtn);
    addAndMakeVisible(cancelBtn);
}

void RhythmPresetBrowser::setAccentColour(juce::Colour c)
{
    accent = c;
    repaint();
}

void RhythmPresetBrowser::refresh(const juce::File& rhythmsDir)
{
    dir = rhythmsDir;
    files.clear();
    if (dir.isDirectory())
    {
        for (const auto& f : dir.findChildFiles(juce::File::findFiles, false, "*.muRhyth"))
            files.push_back(f);
        std::sort(files.begin(), files.end(),
                  [](const juce::File& a, const juce::File& b) {
                      return a.getFileNameWithoutExtension()
                              .compareIgnoreCase(b.getFileNameWithoutExtension()) < 0;
                  });
    }
    selectedRow = -1;
    applyFilter();
}

void RhythmPresetBrowser::applyFilter()
{
    filtered.clear();
    const auto q = searchBox.getText().trim().toLowerCase();
    for (int i = 0; i < (int)files.size(); ++i)
    {
        if (q.isNotEmpty() && !files[i].getFileNameWithoutExtension().toLowerCase().contains(q))
            continue;
        filtered.push_back(i);
    }
    selectedRow = -1;
    listBox.updateContent();
    listBox.repaint();
}

void RhythmPresetBrowser::loadSelected()
{
    if (selectedRow < 0 || selectedRow >= (int)filtered.size()) return;
    const auto& f = files[filtered[selectedRow]];
    if (onLoad) onLoad(f);
    if (onClose) onClose();
}

juce::Rectangle<int> RhythmPresetBrowser::cardBounds() const
{
    // Centre the card, but clamp so it always fits inside the component.
    const int w = getWidth();
    const int h = getHeight();
    const int cw = juce::jmin(kCardW, w - 16);
    const int ch = juce::jmin(kCardH, h - 16);
    return { (w - cw) / 2, (h - ch) / 2, cw, ch };
}

// ── ListBoxModel ──────────────────────────────────────────────────────────────

int RhythmPresetBrowser::getNumRows() { return (int)filtered.size(); }

void RhythmPresetBrowser::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool sel)
{
    using Id = MuClidLookAndFeel::ColourIds;
    if (sel)
    {
        g.setColour(accent.withAlpha(0.25f));
        g.fillRect(0, 0, w, h);
        g.setColour(accent.withAlpha(0.6f));
        g.fillRect(0, 0, 3, h);
    }
    if (row >= (int)filtered.size()) return;
    const auto name = files[filtered[row]].getFileNameWithoutExtension();
    g.setColour(MuClidLookAndFeel::colour(sel ? Id::headingText : Id::labelText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(12.0f)));
    g.drawText(name, 10, 0, w - 14, h, juce::Justification::centredLeft, true);

    g.setColour(MuClidLookAndFeel::colour(Id::segmentInactiveBorder));
    g.drawLine(8.0f, (float)(h - 1), (float)w, (float)(h - 1), 0.5f);
}

void RhythmPresetBrowser::listBoxItemClicked(int row, const juce::MouseEvent& e)
{
    selectedRow = row;
    listBox.repaint();
    if (e.mods.isRightButtonDown())
    {
        juce::PopupMenu menu;
        menu.addItem(1, "Load");
        menu.addItem(2, "Delete");
        menu.showMenuAsync(juce::PopupMenu::Options{}, [this, row](int result)
        {
            if (result == 1) { selectedRow = row; loadSelected(); }
            else if (result == 2 && row < (int)filtered.size())
            {
                files[filtered[row]].deleteFile();
                refresh(dir);
            }
        });
    }
}

void RhythmPresetBrowser::listBoxItemDoubleClicked(int row, const juce::MouseEvent&)
{
    selectedRow = row;
    loadSelected();
}

// ── Paint / Layout ────────────────────────────────────────────────────────────

void RhythmPresetBrowser::paint(juce::Graphics& g)
{
    // Dim background
    g.setColour(juce::Colour(0xcc000000));
    g.fillAll();

    const auto card = cardBounds();
    const float r   = 8.0f;

    // Card body
    g.setColour(MuClidLookAndFeel::colour(MuClidLookAndFeel::panelBackground));
    g.fillRoundedRectangle(card.toFloat(), r);

    // Rhythm-coloured header band
    g.setColour(accent.withAlpha(0.85f));
    g.fillRoundedRectangle(card.toFloat(), r);
    g.setColour(MuClidLookAndFeel::colour(MuClidLookAndFeel::panelBackground));
    g.fillRect(card.getX(), card.getY() + kHeaderH, card.getWidth(), card.getHeight() - kHeaderH);

    // Card border in rhythm colour
    g.setColour(accent);
    g.drawRoundedRectangle(card.toFloat().reduced(0.5f), r, 1.5f);

    // Header label
    g.setColour(juce::Colours::black.withAlpha(0.85f));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(13.0f)));
    g.drawText("Rhythm Presets", card.getX() + kPad, card.getY(),
               card.getWidth() - kPad * 2, kHeaderH,
               juce::Justification::centredLeft, false);

    // Divider below search row
    const int divY = card.getY() + kHeaderH + kSearchH;
    g.setColour(MuClidLookAndFeel::colour(MuClidLookAndFeel::segmentInactiveBorder));
    g.drawLine((float)card.getX(), (float)divY, (float)card.getRight(), (float)divY, 0.5f);
}

void RhythmPresetBrowser::resized()
{
    const auto card = cardBounds();
    const int  cx   = card.getX();
    const int  cy   = card.getY();
    const int  cw   = card.getWidth();
    const int  ch   = card.getHeight();

    searchBox.setBounds(cx + kPad, cy + kHeaderH + (kSearchH - 22) / 2,
                        cw - kPad * 2, 22);

    const int listY = cy + kHeaderH + kSearchH;
    const int listH = ch - kHeaderH - kSearchH - kBotH;
    listBox.setBounds(cx, listY, cw, listH);

    const int btnY  = cy + ch - kBotH + (kBotH - 24) / 2;
    const int btnW  = 80;
    cancelBtn.setBounds(cx + kPad,                   btnY, btnW, 24);
    loadBtn  .setBounds(cx + cw - kPad - btnW,       btnY, btnW, 24);
}

void RhythmPresetBrowser::mouseDown(const juce::MouseEvent& e)
{
    // Click outside the card → cancel.
    if (!cardBounds().contains(e.getPosition()))
        if (onClose) onClose();
}

//==============================================================================
// RhythmSaveDialog implementation

RhythmSaveDialog::RhythmSaveDialog()
{
    nameEditor.setTextToShowWhenEmpty("Preset name", juce::Colours::grey);
    nameEditor.setFont(juce::Font(juce::FontOptions{}.withHeight(13.0f)));
    addAndMakeVisible(nameEditor);

    addAndMakeVisible(embedToggle);

    statusLabel.setJustificationType(juce::Justification::centred);
    statusLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffff9933));
    addAndMakeVisible(statusLabel);

    saveBtn.onClick = [this]
    {
        const auto name = nameEditor.getText().trim();
        if (name.isEmpty()) return;
        if (onSave) onSave(name, embedToggle.getToggleState());
    };
    cancelBtn.onClick = [this] { if (onCancel) onCancel(); };
    addAndMakeVisible(saveBtn);
    addAndMakeVisible(cancelBtn);
}

void RhythmSaveDialog::markFileExists()
{
    pendingOverwrite = true;
    statusLabel.setText(juce::String::fromUTF8("File exists \xe2\x80\x94 Save again to overwrite"),
                        juce::dontSendNotification);
}

void RhythmSaveDialog::visibilityChanged()
{
    if (isVisible())
    {
        pendingOverwrite = false;
        statusLabel.setText({}, juce::dontSendNotification);
        embedToggle.setToggleState(false, juce::dontSendNotification);
        nameEditor.grabKeyboardFocus();
    }
}

void RhythmSaveDialog::mouseDown(const juce::MouseEvent& e)
{
    const int cardX = (getWidth()  - kCardW) / 2;
    const int cardY = (getHeight() - kCardH) / 2;
    const juce::Rectangle<int> card { cardX, cardY, kCardW, kCardH };
    if (!card.contains(e.getPosition()))
        if (onCancel) onCancel();
}

void RhythmSaveDialog::resized()
{
    const int cardX = (getWidth()  - kCardW) / 2;
    const int cardY = (getHeight() - kCardH) / 2;
    const int pad   = 20;
    const int fieldW = kCardW - pad * 2;

    int y = cardY + 40;
    nameEditor  .setBounds(cardX + pad, y, fieldW, 28);  y += 36;
    embedToggle .setBounds(cardX + pad, y, fieldW, 24);  y += 30;
    statusLabel .setBounds(cardX + pad, y, fieldW, 18);

    const int btnW = 80;
    const int btnY = cardY + kCardH - 36;
    cancelBtn.setBounds(cardX + pad,            btnY, btnW, 26);
    saveBtn  .setBounds(cardX + kCardW - pad - btnW, btnY, btnW, 26);
}

void RhythmSaveDialog::paint(juce::Graphics& g)
{
    using Id = MuClidLookAndFeel::ColourIds;

    g.setColour(juce::Colour(0xe6000000));
    g.fillAll();

    const int cardX = (getWidth()  - kCardW) / 2;
    const int cardY = (getHeight() - kCardH) / 2;

    g.setColour(MuClidLookAndFeel::colour(Id::panelBackground));
    g.fillRoundedRectangle((float)cardX, (float)cardY, (float)kCardW, (float)kCardH, 8.0f);

    g.setColour(MuClidLookAndFeel::colour(Id::segmentInactiveBorder));
    g.drawRoundedRectangle((float)cardX, (float)cardY, (float)kCardW, (float)kCardH, 8.0f, 1.0f);

    g.setColour(MuClidLookAndFeel::colour(Id::headingText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(14.0f)));
    g.drawText("Save Rhythm Preset", cardX + 20, cardY + 12, kCardW - 40, 20,
               juce::Justification::centredLeft, false);
}

//==============================================================================
RhythmPanel::RhythmPanel(PluginProcessor& p)
    : proc(p), euclidPanel(p), voiceSection(p)
{
    startTimerHz(30);
    addAndMakeVisible(circle);
    addAndMakeVisible(euclidPanel);
    addAndMakeVisible(voiceSection);
    addAndMakeVisible(modulatorPanel);

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

    resetBtn.onClick      = [this] { confirmReset();  };
    deleteBtn.onClick     = [this] { confirmDelete(); };
    saveRhythmBtn.onClick = [this] { saveRhythmPreset(); };
    addAndMakeVisible(resetBtn);
    addAndMakeVisible(deleteBtn);
    addAndMakeVisible(saveRhythmBtn);

    rhythmPresetDropdown.setPlaceholderText(juce::String::fromUTF8("rhythm preset\xe2\x80\xa6"));
    rhythmPresetDropdown.onChange = [this](int id)
    {
        const int idx = id - 1;
        if (idx >= 0 && idx < (int)rhythmPresetFiles.size() && currentRhythmIndex >= 0)
        {
            const juce::File presetFile = rhythmPresetFiles[idx];
            proc.stageRhythmPreset(currentRhythmIndex, presetFile);
            loadedRhythmPresetFile = presetFile;
            if (!proc.sequencerPlaying.get())
            {
                setRhythm(currentRhythmIndex);   // refreshes rhythmPresetFiles, clears dropdown
                repaint();
                // Re-select the just-loaded preset (refreshRhythmPresets cleared the selection).
                for (int i = 0; i < (int)rhythmPresetFiles.size(); ++i)
                {
                    if (rhythmPresetFiles[i].getFullPathName() == presetFile.getFullPathName())
                    {
                        rhythmPresetDropdown.setSelectedId(i + 1, juce::dontSendNotification);
                        break;
                    }
                }
            }
        }
    };
    addAndMakeVisible(rhythmPresetDropdown);

    addAndMakeVisible(rhythmBrowser);
    rhythmBrowser.setVisible(false);
    rhythmBrowser.onLoad = [this](const juce::File& f)
    {
        if (currentRhythmIndex < 0) return;
        proc.stageRhythmPreset(currentRhythmIndex, f);
        if (!proc.sequencerPlaying.get())
        {
            setRhythm(currentRhythmIndex);
            repaint();
        }
    };
    rhythmBrowser.onClose = [this] { rhythmBrowser.setVisible(false); };

    addAndMakeVisible(rhythmSaveDialog);
    rhythmSaveDialog.setVisible(false);
    rhythmSaveDialog.onCancel = [this] { rhythmSaveDialog.setVisible(false); };
    rhythmSaveDialog.onSave   = [this](const juce::String& name, bool embed)
    {
        if (currentRhythmIndex < 0) return;

        juce::String safeName = name.replaceCharacters("\\/:|*?<>\"", "_");
        juce::File destDir    = proc.getRhythmsDir().isDirectory()
                                    ? proc.getRhythmsDir()
                                    : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
        juce::File destFile   = destDir.getChildFile(safeName).withFileExtension(".muRhyth");

        if (destFile.existsAsFile() && !rhythmSaveDialog.isPendingOverwrite())
        {
            rhythmSaveDialog.markFileExists();
            return;
        }

        proc.saveRhythmPresetToFile(currentRhythmIndex, destFile, embed);
        loadedRhythmPresetFile = destFile;
        rhythmSaveDialog.setVisible(false);
        refreshRhythmPresets();
        for (int i = 0; i < (int)rhythmPresetFiles.size(); ++i)
        {
            if (rhythmPresetFiles[i].getFullPathName() == destFile.getFullPathName())
            {
                rhythmPresetDropdown.setSelectedId(i + 1, juce::dontSendNotification);
                break;
            }
        }
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

RhythmPanel::~RhythmPanel()
{
    stopTimer();
    deregisterRhythmListeners(currentRhythmIndex);
}

void RhythmPanel::registerRhythmListeners(int ri)
{
    if (ri < 0) return;
    const auto rPfx  = "r"  + juce::String(ri) + "_";
    const auto chPfx = "ch" + juce::String(ri) + "_";
    for (auto* s : kEuclidSuffixes)
        proc.apvts.addParameterListener(rPfx + s, this);
    for (auto* s : kVoiceSuffixes)
        proc.apvts.addParameterListener(rPfx + s, this);
    for (auto* s : kSendSuffixes)
        proc.apvts.addParameterListener(chPfx + s, this);
}

void RhythmPanel::deregisterRhythmListeners(int ri)
{
    if (ri < 0) return;
    const auto rPfx  = "r"  + juce::String(ri) + "_";
    const auto chPfx = "ch" + juce::String(ri) + "_";
    for (auto* s : kEuclidSuffixes)
        proc.apvts.removeParameterListener(rPfx + s, this);
    for (auto* s : kVoiceSuffixes)
        proc.apvts.removeParameterListener(rPfx + s, this);
    for (auto* s : kSendSuffixes)
        proc.apvts.removeParameterListener(chPfx + s, this);
}

void RhythmPanel::parameterChanged(const juce::String& parameterID, float /*newValue*/)
{
    const auto suffix = parameterID.fromFirstOccurrenceOf("_", false, false);
    bool isEuclid = false;
    for (auto* s : kEuclidSuffixes)
        if (suffix == s) { isEuclid = true; break; }

    if (isEuclid)
    {
        euclidPanel.loadFromRhythm();
        refreshCircle();
    }
    else
    {
        voiceSection.loadFromRhythm();
    }
}

void RhythmPanel::setRhythm(int index)
{
    // Commit any in-progress edit (Label::hideEditor with discardChanges=false saves
    // current text and fires onTextChange synchronously, which writes the rename to
    // the OLD currentRhythmIndex before we update it).
    if (nameLabel.getCurrentTextEditor() != nullptr)
        nameLabel.hideEditor(false);

    if (currentRhythmIndex != index)
        loadedRhythmPresetFile = {};
    deregisterRhythmListeners(currentRhythmIndex);
    currentRhythmIndex = index;
    registerRhythmListeners(currentRhythmIndex);
    if (index >= 0 && index < proc.getNumRhythms())
    {
        nameLabel.setText(juce::String(proc.getRhythm(index).name), juce::dontSendNotification);
        refreshRhythmPresets();
        euclidPanel.setRhythm(index);
        euclidPanel.setRhythmColour(currentColour());
        // modulatorPanel must be re-pointed BEFORE voiceSection — voiceSection.setRhythm
        // triggers onInsertAlgorithmChanged → modulatorPanel.setInsertAlgorithm →
        // ModulatorEditor::rebuildRows(), which reads cs->id. If cs still points at a
        // just-destroyed Rhythm (e.g. after delete-last), cs->id is garbage and string
        // concat throws std::bad_alloc.
        modulatorPanel.setRhythm(&proc.getRhythm(index));
        voiceSection.setRhythm(index);
        refreshCircle();
    }
    else
    {
        nameLabel.setText("No Rhythm", juce::dontSendNotification);
        // Null out all child-panel rhythm pointers so they don't dereference stale memory
        // if a vector erase invalidated the previous rhythm before re-binding.
        modulatorPanel.setRhythm(nullptr);
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
    if (currentRhythmIndex < 0) return;

    const juce::File startDir = lastBrowseDir.isDirectory()
                                    ? lastBrowseDir
                                    : (proc.getSamplesDir().isDirectory()
                                           ? proc.getSamplesDir()
                                           : juce::File::getSpecialLocation(juce::File::userMusicDirectory));

    juce::Component::SafePointer<RhythmPanel> safeThis(this);
    const int rhythmIdx = currentRhythmIndex;

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned(new SampleBrowserContent(
        proc, startDir,
        [safeThis, rhythmIdx](const juce::File& f)
        {
            if (safeThis == nullptr) return;
            safeThis->lastBrowseDir = f.getParentDirectory();
            safeThis->proc.loadSampleForRhythm(rhythmIdx, f);
            safeThis->repaint();
        }));
    opts.dialogTitle          = "Load Sample";
    opts.dialogBackgroundColour = juce::Colour(0xff1a1a1a);
    opts.useNativeTitleBar    = false;
    opts.resizable            = true;
    opts.launchAsync();
}

void RhythmPanel::refreshRhythmPresets()
{
    rhythmPresetFiles.clear();
    rhythmPresetDropdown.clear();

    const juce::File rhythmsDir = proc.getRhythmsDir().isDirectory()
                                      ? proc.getRhythmsDir()
                                      : juce::File();
    if (rhythmsDir.isDirectory())
    {
        auto files = rhythmsDir.findChildFiles(juce::File::findFiles, false, "*.muRhyth");
        std::sort(files.begin(), files.end(), [](const juce::File& a, const juce::File& b) {
            return a.getFileNameWithoutExtension().compareIgnoreCase(b.getFileNameWithoutExtension()) < 0;
        });
        for (const auto& f : files)
        {
            rhythmPresetFiles.push_back(f);
            rhythmPresetDropdown.addItem(f.getFileNameWithoutExtension(), (int)rhythmPresetFiles.size());
        }
    }
}

void RhythmPanel::saveRhythmPreset()
{
    if (currentRhythmIndex < 0) return;

    if (loadedRhythmPresetFile.existsAsFile())
    {
        // A preset is already loaded — confirm before overwriting.
        const juce::String fileName = loadedRhythmPresetFile.getFileNameWithoutExtension();
        const juce::File   destFile = loadedRhythmPresetFile;
        const int          ri       = currentRhythmIndex;
        juce::Component::SafePointer<RhythmPanel> safeThis(this);

        auto* w = new juce::AlertWindow("Overwrite Preset",
                                        "Overwrite \"" + fileName + "\"?",
                                        juce::AlertWindow::QuestionIcon);
        w->addButton("Overwrite", 1, juce::KeyPress(juce::KeyPress::returnKey));
        w->addButton("Save As\xe2\x80\xa6", 2);
        w->addButton("Cancel",    0, juce::KeyPress(juce::KeyPress::escapeKey));
        w->enterModalState(true,
            juce::ModalCallbackFunction::create([safeThis, destFile, ri](int result)
            {
                if (!safeThis) return;
                if (result == 1)
                {
                    safeThis->proc.saveRhythmPresetToFile(ri, destFile, false);
                    safeThis->refreshRhythmPresets();
                    // Re-select after refresh.
                    for (int i = 0; i < (int)safeThis->rhythmPresetFiles.size(); ++i)
                    {
                        if (safeThis->rhythmPresetFiles[i].getFullPathName() == destFile.getFullPathName())
                        {
                            safeThis->rhythmPresetDropdown.setSelectedId(i + 1, juce::dontSendNotification);
                            break;
                        }
                    }
                }
                else if (result == 2)
                {
                    // User chose Save As from the confirm dialog.
                    safeThis->loadedRhythmPresetFile = {};
                    safeThis->saveRhythmPreset();
                }
            }),
            true);
        return;
    }

    // No preset loaded — show the Save As dialog.
    const juce::String defaultName =
        juce::String(proc.getRhythm(currentRhythmIndex).name).replaceCharacters("\\/:|*?<>\"", "_");

    rhythmSaveDialog.setDefaultName(defaultName);
    rhythmSaveDialog.setVisible(true);
    rhythmSaveDialog.toFront(true);
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
        const bool         missing    = proc.isSampleMissing(currentRhythmIndex);
        if (missing)
        {
            // Linked sample referenced by a preset could not be found at its recorded
            // path nor in the user Samples folder. Show the filename in amber with a
            // "missing — click to find" hint so the user knows what to look for.
            g.setColour(juce::Colour(0xffe69500)); // amber warning
            g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
            g.drawText("Missing: " + sampleName + "  —  click to find",
                       inner.getX() + 5, inner.getY(), inner.getWidth() - 28, inner.getHeight(),
                       juce::Justification::centredLeft, true);
        }
        else if (sampleName.isNotEmpty())
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

    // Header right-side controls (right to left).
    // rhythmDropLeft is set by PluginEditor after TransportBar layout so the
    // dropdown's left edge aligns with (and is indented 10 px from) the main
    // preset dropdown directly above. Falls back to a minimum if not yet set.
    const int btnY     = (kHeaderH - 20) / 2;
    const int rightEdge = w - 4;
    const int dropRight = rightEdge - kIconBtnW * 2 - 4 - kPresetBtnW - 4;
    const int dropLeft  = (rhythmDropLeft > 0 && rhythmDropLeft < dropRight - 80)
                              ? rhythmDropLeft : juce::jmax(26, dropRight - 200);
    deleteBtn           .setBounds(rightEdge - kIconBtnW,           btnY, kIconBtnW,   20);
    resetBtn            .setBounds(rightEdge - kIconBtnW * 2 - 4,  btnY, kIconBtnW,   20);
    saveRhythmBtn       .setBounds(dropRight  - kPresetBtnW,        btnY, kPresetBtnW, 20);
    rhythmPresetDropdown.setBounds(dropLeft,                        btnY, dropRight - kPresetBtnW - 4 - dropLeft, 20);
    const int nameX   = 26;
    const int nameEnd = dropLeft - 6;
    nameRect = { nameX, 0, juce::jmax(0, nameEnd - nameX), kHeaderH };
    nameLabel.setBounds(nameRect.reduced(4, 2));

    circle.setBounds        (circleRect.reduced(kPanelPad + 1));
    euclidPanel.setBounds   (euclidRect.reduced(kPanelPad + 1));
    voiceSection.setBounds  (voiceRect.reduced(kPanelPad + 1));
    modulatorPanel.setBounds(modRect.reduced(kPanelPad + 1));
    rhythmBrowser   .setBounds(getLocalBounds());
    rhythmSaveDialog.setBounds(getLocalBounds());
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
    voiceSection.refreshModulatedIndicators();
    euclidPanel.refreshModulatedIndicators();
}
