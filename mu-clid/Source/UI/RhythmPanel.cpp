#include "RhythmPanel.h"
#include "SampleBrowser.h"

#include <string_view>
#include <unordered_set>

namespace {

// Euclidean panel params — all use r{ri}_ prefix.
const char* const kEuclidSuffixes[] = {
    "stepsA", "hitsA", "rotA", "prePadA", "postPadA", "insStA", "insLenA",
    "prePadModeA", "postPadModeA", "insModeA",
    "stepsB", "hitsB", "rotB", "prePadB", "postPadB", "insStB", "insLenB",
    "prePadModeB", "postPadModeB", "insModeB",
    "stepsC", "hitsC", "rotC", "prePadC", "postPadC", "insStC", "insLenC",
    "prePadModeC", "postPadModeC", "insModeC",
    "logic", "patLeg", "vMono"
};

// hash-set membership check for the 31-entry euclid suffix table. Was a
// linear `for (auto* s : kEuclidSuffixes) if (suffix == s)` in three places —
// the parameterChanged path runs on every host-automation event + every knob
// drag tick, so O(N=31) compares per call became visible in profiles. The
// register/deregister sites still iterate the table (they need the full list,
// not just membership), so the const char* table stays.
bool isEuclidSuffix(const juce::String& suffix) noexcept
{
    static const auto kSet = []() {
        std::unordered_set<std::string_view> out;
        out.reserve(std::size(kEuclidSuffixes));
        for (auto* p : kEuclidSuffixes) out.emplace(p);
        return out;
    }();
    // toRawUTF8() points into the juce::String's storage (no copy) — string_view
    // wraps it for the O(1) hash lookup. Suffixes are ASCII so UTF-8 ≡ char bytes.
    return kSet.find(std::string_view(suffix.toRawUTF8())) != kSet.end();
}

// Voice panel params — all use r{ri}_ prefix.
const char* const kVoiceSuffixes[] = {
    "pitchOct", "pitchSemi", "pitchFine",
    "pEnvAtk", "pEnvDec", "pEnvSus", "pEnvRel", "pEnvDep",
    "fltType", "fltCut", "fltRes", "fltLoCut",
    "fEnvAtk", "fEnvDec", "fEnvSus", "fEnvRel", "fEnvDep",
    "ampLvl", "accentDb",
    "aEnvAtk", "aEnvDec", "aEnvSus", "aEnvRel",
    "drvChar", "drvDrv", "drvOut", "drvDit", "drvTon", "eqMidGain", "drvBits", "drvRate"
};

// Send knob params — use ch{ri}_ prefix (shared with mixer channel strip).
const char* const kSendSuffixes[] = { "sendEff", "sendDly", "sendRev" };

} // namespace

//==============================================================================
// RhythmSaveDialog implementation

RhythmSaveDialog::RhythmSaveDialog()
{
    nameEditor.setTextToShowWhenEmpty("Preset name", juce::Colours::grey);
    nameEditor.setFont(juce::Font(juce::FontOptions{}.withHeight(13.0f)));
    addAndMakeVisible(nameEditor);

    descEditor.setTextToShowWhenEmpty("Description (optional)", juce::Colours::grey);
    descEditor.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    addAndMakeVisible(descEditor);

    // Populate with just "Uncategorised" + "New..." until setKnownCategories() is called.
    setKnownCategories({});
    categoryDropdown.onChange = [this](int id) {
        const bool isNew = (id == knownCategories.size() + 2);
        newCategoryEditor.setVisible(isNew);
        if (isNew) newCategoryEditor.grabKeyboardFocus();
        resized();
    };
    addAndMakeVisible(categoryDropdown);

    newCategoryEditor.setTextToShowWhenEmpty("New category name", juce::Colours::grey);
    newCategoryEditor.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    newCategoryEditor.setVisible(false);
    addAndMakeVisible(newCategoryEditor);

    addAndMakeVisible(embedToggle);

    saveAsDefaultToggle.onClick = [this] { updateDefaultModeState(); };
    addAndMakeVisible(saveAsDefaultToggle);

    saveBtn.onClick = [this]
    {
        const auto name = nameEditor.getText().trim();
        if (!isSaveAsDefault() && name.isEmpty()) return;
        if (onSave) onSave(name, descEditor.getText().trim(),
                           resolveCategory(), embedToggle.getToggleState());
    };
    cancelBtn.onClick = [this] { if (onCancel) onCancel(); };
    addAndMakeVisible(saveBtn);
    addAndMakeVisible(cancelBtn);
}

void RhythmSaveDialog::updateDefaultModeState()
{
    const bool isDefault = saveAsDefaultToggle.getToggleState();
    nameEditor        .setEnabled(!isDefault);
    descEditor        .setEnabled(!isDefault);
    categoryDropdown  .setEnabled(!isDefault);
    newCategoryEditor .setEnabled(!isDefault);
    resized();
}

void RhythmSaveDialog::setKnownCategories(const juce::StringArray& cats)
{
    knownCategories = cats;
    categoryDropdown.clear();
    categoryDropdown.addItem("Uncategorised", 1);
    for (int i = 0; i < cats.size(); ++i)
        categoryDropdown.addItem(cats[i], i + 2);
    categoryDropdown.addItem("New...", cats.size() + 2);
    categoryDropdown.setSelectedId(1, false);
    newCategoryEditor.setVisible(false);
    newCategoryEditor.clear();
}

juce::String RhythmSaveDialog::resolveCategory() const
{
    const int id    = categoryDropdown.getSelectedId();
    const int newId = knownCategories.size() + 2;
    if (id == newId)
    {
        const auto t = newCategoryEditor.getText().trim();
        return t.isNotEmpty() ? t : juce::String();
    }
    if (id >= 2 && id - 2 < knownCategories.size())
        return knownCategories[id - 2];
    return {};   // "Uncategorised" → empty = no category in preset XML
}

void RhythmSaveDialog::visibilityChanged()
{
    if (isVisible())
    {
        if (pendingDefaultDesc.isNotEmpty())
        {
            descEditor.setText(pendingDefaultDesc, false);
            pendingDefaultDesc.clear();
        }
        else
        {
            descEditor.clear();
        }

        embedToggle.setToggleState(pendingDefaultEmbed, juce::dontSendNotification);
        pendingDefaultEmbed = false;

        categoryDropdown.setSelectedId(1, false);
        if (pendingDefaultCategory.isNotEmpty())
        {
            for (int i = 0; i < knownCategories.size(); ++i)
            {
                if (knownCategories[i].equalsIgnoreCase(pendingDefaultCategory))
                {
                    categoryDropdown.setSelectedId(i + 2, false);
                    break;
                }
            }
            pendingDefaultCategory.clear();
        }
        newCategoryEditor.setVisible(false);
        newCategoryEditor.clear();
        saveAsDefaultToggle.setToggleState(false, juce::dontSendNotification);
        updateDefaultModeState();
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
    const int cardX  = (getWidth()  - kCardW) / 2;
    const int cardY  = (getHeight() - kCardH) / 2;
    const int pad    = 20;
    const int fieldW = kCardW - pad * 2;
    const bool isDefault = saveAsDefaultToggle.getToggleState();

    int y = cardY + 40;
    nameEditor       .setBounds(cardX + pad, y, fieldW, 26);  y += 32;
    descEditor       .setBounds(cardX + pad, y, fieldW, 24);  y += 30;
    categoryDropdown .setBounds(cardX + pad, y, fieldW, 24);  y += 28;
    if (newCategoryEditor.isVisible() && !isDefault)
    {
        newCategoryEditor.setBounds(cardX + pad, y, fieldW, 22);  y += 26;
    }
    embedToggle        .setBounds(cardX + pad,              y, fieldW / 2, 22);
    saveAsDefaultToggle.setBounds(cardX + pad + fieldW / 2, y, fieldW / 2, 22);

    const int btnW = 80;
    const int btnY = cardY + kCardH - 36;
    cancelBtn.setBounds(cardX + pad,                    btnY, btnW, 26);
    saveBtn  .setBounds(cardX + kCardW - pad - btnW,    btnY, btnW, 26);
}

void RhythmSaveDialog::paint(juce::Graphics& g)
{
    using Id = MuClidLookAndFeel::ColourIds;

    g.setColour(MuClidLookAndFeel::colour(Id::backgroundModalDim));
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
            if (!proc.sequencerPlaying.load())
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

    addAndMakeVisible(rhythmSaveDialog);
    rhythmSaveDialog.setVisible(false);
    rhythmSaveDialog.onCancel = [this] { rhythmSaveDialog.setVisible(false); };
    rhythmSaveDialog.onSave = [this](const juce::String& name,
                                      const juce::String& desc,
                                      const juce::String& category, bool embed)
    {
        if (currentRhythmIndex < 0) return;

        juce::File destDir = proc.getRhythmsDir().isDirectory()
                                 ? proc.getRhythmsDir()
                                 : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);

        if (rhythmSaveDialog.isSaveAsDefault())
        {
            proc.saveRhythmPresetToFile(currentRhythmIndex,
                                        destDir.getChildFile("_default.muRhythm"),
                                        embed, {}, {});
            rhythmSaveDialog.setVisible(false);
            return;
        }

        juce::String safeName = name.replaceCharacters("\\/:|*?<>\"", "_");
        juce::File destFile   = destDir.getChildFile(safeName).withFileExtension(".muRhythm");

        if (destFile.existsAsFile())
        {
            juce::Component::SafePointer<RhythmPanel> safeThis(this);
            auto* w = new juce::AlertWindow("Overwrite Rhythm Preset",
                                            "Overwrite \"" + name + "\"?",
                                            juce::AlertWindow::QuestionIcon);
            w->addButton("Overwrite", 1, juce::KeyPress(juce::KeyPress::returnKey));
            w->addButton("Cancel",    0, juce::KeyPress(juce::KeyPress::escapeKey));
            w->enterModalState(true,
                juce::ModalCallbackFunction::create(
                    [safeThis, destFile, name, desc, category, embed](int result)
                    {
                        if (!safeThis || result != 1) return;
                        safeThis->proc.ensureCategoryInList(category);
                        safeThis->proc.saveRhythmPresetToFile(safeThis->currentRhythmIndex,
                                                              destFile, embed, category, desc);
                        safeThis->loadedRhythmPresetFile = destFile;
                        safeThis->rhythmSaveDialog.setVisible(false);
                        safeThis->refreshRhythmPresets();
                        for (int i = 0; i < (int)safeThis->rhythmPresetFiles.size(); ++i)
                        {
                            if (safeThis->rhythmPresetFiles[i].getFullPathName()
                                    == destFile.getFullPathName())
                            {
                                safeThis->rhythmPresetDropdown.setSelectedId(
                                    i + 1, juce::dontSendNotification);
                                break;
                            }
                        }
                    }),
                true);
            return;
        }

        proc.ensureCategoryInList(category);
        proc.saveRhythmPresetToFile(currentRhythmIndex, destFile, embed, category, desc);
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
    // JUCE invokes parameterChanged on whatever thread called setValueNotifyingHost.
    // Some DAWs run host automation on the audio thread — and the refresh below mutates
    // juce::Slider state (not audio-thread-safe). Marshal to the message thread.
    // refresh only the single control matching `suffix`, not the whole panel
    // (was 21 euclid knobs + 9 segments OR 28+ voice knobs per parameter change).
    // skip during bulk APVTS loads (state restore, swap commit, swap-rhythms).
    // The bulk-load orchestrator calls setRhythm() (full re-bind) afterwards, so the
    // per-param refresh during the push is pure waste — and worse, the marshalled
    // refreshes land AFTER setRhythm completes, re-running setValue on already-bound
    // sliders. Net cost was ~30 redundant refreshSuffix calls per swap commit.
    if (proc.isApvtsLoading()) return;

    juce::Component::SafePointer<RhythmPanel> safeThis(this);
    const juce::String suffix = parameterID.fromFirstOccurrenceOf("_", false, false);

    auto refresh = [safeThis, suffix]
    {
        if (auto* self = safeThis.getComponent())
        {
            if (isEuclidSuffix(suffix))
            {
                self->euclidPanel.refreshSuffix(suffix);
                self->refreshCircle();
            }
            else
            {
                self->voiceSection.refreshSuffix(suffix);
            }
        }
    };

    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
        refresh();
    else
        juce::MessageManager::callAsync(std::move(refresh));
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
    // Pass modulated euclid overrides through to getStepTypes so the ring reflects
    // active modulation of hits/rotate/prePad/postPad/insSt/insLen. When no modulation is
    // assigned, getModulatedEuclidOverrides falls back to the rhythm's base values.
    const EuclidOverrides ov = proc.getModulatedEuclidOverrides(currentRhythmIndex);
    circle.setPatterns(r.genA.getStepTypes(ov.a), r.genB.getStepTypes(ov.b), r.genC.getStepTypes(ov.c));
    lastCircleOverrides = ov;
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
        return MuClidLookAndFeel::rhythmPalette[r.colourIndex % MuClidLookAndFeel::kRhythmPaletteSize];
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

    // default landing folder is the user's Primary Sample Library
    // (configured in Settings; falls back to OS user Music dir if unset).
    // The previous-session lastBrowseDir takes precedence so the user lands
    // back where they were if they're working through a library subfolder.
    // The Content/Samples folder is still one click away via the in-dialog
    // Library/Content toggle.
    const juce::File primaryLib = proc.getPrimarySampleDir();
    const juce::File startDir = lastBrowseDir.isDirectory()
                                    ? lastBrowseDir
                                    : (primaryLib.isDirectory()
                                           ? primaryLib
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
    opts.dialogBackgroundColour = MuClidLookAndFeel::colour(MuClidLookAndFeel::backgroundDialog);
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
        struct Entry { juce::File file; juce::String name, category; };
        std::vector<Entry> entries;

        for (const auto& f : rhythmsDir.findChildFiles(juce::File::findFiles, false, "*.muRhythm"))
        {
            if (f.getFileNameWithoutExtension().equalsIgnoreCase("_default")) continue;
            Entry e { f, f.getFileNameWithoutExtension(), "Uncategorised" };
            if (auto xml = juce::parseXML(f))
            {
                auto state = juce::ValueTree::fromXml(*xml);
                juce::String cat = state.getProperty("presetCategory", "").toString();
                if (cat.isNotEmpty() && cat != "All" && cat != "Uncategorised")
                    e.category = cat;
            }
            entries.push_back(std::move(e));
        }

        std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
            const bool aU = (a.category == "Uncategorised");
            const bool bU = (b.category == "Uncategorised");
            if (aU != bU) return bU;
            const int cc = a.category.compareIgnoreCase(b.category);
            return cc != 0 ? cc < 0 : a.name.compareIgnoreCase(b.name) < 0;
        });

        bool multiCat = false;
        if (!entries.empty())
        {
            const auto& first = entries.front().category;
            for (const auto& e : entries)
                if (e.category.compareIgnoreCase(first) != 0) { multiCat = true; break; }
        }

        // Collect unique non-uncategorised categories for getKnownCategories()
        knownRhythmCategories.clear();
        for (const auto& e : entries)
            if (e.category != "Uncategorised" && !knownRhythmCategories.contains(e.category))
                knownRhythmCategories.add(e.category);
        knownRhythmCategories.sort(false);

        juce::String currentCat;
        for (const auto& e : entries)
        {
            if (multiCat && e.category != currentCat)
            {
                currentCat = e.category;
                rhythmPresetDropdown.addSectionHeading(currentCat);
            }
            rhythmPresetFiles.push_back(e.file);
            rhythmPresetDropdown.addItem(e.name, (int)rhythmPresetFiles.size());
        }
    }
}

void RhythmPanel::setKnownCategories(const juce::StringArray& cats)
{
    rhythmSaveDialog.setKnownCategories(cats);
}

void RhythmPanel::saveRhythmPreset()
{
    if (currentRhythmIndex < 0) return;

    juce::String defaultName;
    juce::String defaultDesc;
    juce::String defaultCat;
    bool         defaultEmbed = false;
    if (loadedRhythmPresetFile.existsAsFile())
    {
        defaultName = loadedRhythmPresetFile.getFileNameWithoutExtension();
        if (auto xml = juce::parseXML(loadedRhythmPresetFile))
        {
            auto s = juce::ValueTree::fromXml(*xml);
            defaultCat   = s.getProperty("presetCategory",    "").toString();
            defaultDesc  = s.getProperty("presetDescription", "").toString();
            defaultEmbed = (int)s.getProperty("presetEmbedSamples", 0) != 0;
        }
    }
    else
    {
        defaultName = juce::String(proc.getRhythm(currentRhythmIndex).name)
                          .replaceCharacters("\\/:|*?<>\"", "_");
    }

    setKnownCategories(proc.loadCategoryList());
    rhythmSaveDialog.setDefaultName(defaultName);
    rhythmSaveDialog.setDefaultDescription(defaultDesc);
    rhythmSaveDialog.setDefaultCategory(defaultCat);
    rhythmSaveDialog.setDefaultEmbed(defaultEmbed);
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
            g.setColour(MuClidLookAndFeel::colour(MuClidLookAndFeel::sampleBarMissingWarning));
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
    // Fixed Medium-baseline layout — see MuLookAndFeel for the constants.
    // Every dimension wrapped in s() so scale toggles propagate.
    using mu_ui::s;
    const int w = getWidth();
    const int h = getHeight();

    topH    = s(MuClidLookAndFeel::kRhythmTopH);
    circleW = s(MuClidLookAndFeel::kCircleSize);

    const int hdrH = s(kHeaderH);
    const int sbH  = s(kSampleBarH);
    const int voiH = s(kVoiceH);
    const int topY = hdrH + sbH;
    const int modY = topY + topH + voiH;

    sampleRect = { 0,       hdrH,        w,           sbH                     };
    circleRect = { 0,       topY,        circleW,     topH                    };
    euclidRect = { circleW, topY,        w - circleW, topH                    };
    voiceRect  = { 0,       topY + topH, w,           voiH                    };
    modRect    = { 0,       modY,        w,           juce::jmax(0, h - modY) };

    // Header right-side controls (right to left).
    // rhythmDropLeft is set by PluginEditor after TransportBar layout so the
    // dropdown's left edge aligns with (and is indented 10 px from) the main
    // preset dropdown directly above. Falls back to a minimum if not yet set.
    const int btnH = s(20);
    const int btnY = (hdrH - btnH) / 2;
    const int rightEdge = w - s(4);
    const int iconBtnW  = s(kIconBtnW);
    const int presetBtnW = s(kPresetBtnW);
    const int dropRight = rightEdge - iconBtnW * 2 - s(4) - presetBtnW - s(4);
    const int dropLeft  = (rhythmDropLeft > 0 && rhythmDropLeft < dropRight - s(80))
                              ? rhythmDropLeft : juce::jmax(s(26), dropRight - s(200));
    deleteBtn           .setBounds(rightEdge - iconBtnW,             btnY, iconBtnW,    btnH);
    resetBtn            .setBounds(rightEdge - iconBtnW * 2 - s(4),  btnY, iconBtnW,    btnH);
    saveRhythmBtn       .setBounds(dropRight,                         btnY, presetBtnW,  btnH);
    rhythmPresetDropdown.setBounds(dropLeft,                          btnY, dropRight - s(4) - dropLeft, btnH);
    const int nameX   = s(26);
    const int nameEnd = dropLeft - s(6);
    nameRect = { nameX, 0, juce::jmax(0, nameEnd - nameX), hdrH };
    nameLabel.setBounds(nameRect.reduced(s(4), s(2)));

    const int rhythmInset = s(kPanelPad + 1);
    circle.setBounds        (circleRect.reduced(rhythmInset));
    euclidPanel.setBounds   (euclidRect.reduced(rhythmInset));
    voiceSection.setBounds  (voiceRect.reduced(rhythmInset));
    modulatorPanel.setBounds(modRect.reduced(rhythmInset));
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

    // route through PluginProcessor::renameRhythm so the write happens under
    // rhythmsLock instead of a raw message-thread mutation of the Rhythm struct.
    proc.renameRhythm(currentRhythmIndex, newName);
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
                    // PluginProcessor::resetRhythm owns the concurrency dance
                    // (suspendProcessing + rhythmsLock). No more UI-thread spin on modLock.
                    safeThis->proc.resetRhythm(idx);
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
    // gate the indicator refreshes on play state — when stopped, no
    // modulators run, so values never change. The helpers iterate ~60 knobs
    // each calling setIsModulated(false) which no-ops on unchanged state,
    // but the iteration itself burned ~1800 method calls/sec for nothing.
    // The play→stop edge still needs one final refresh pass so the cyan mod
    // rings + live arcs visibly clear (they're gated on `playing` inside the
    // refresh helpers — see EuclideanPanel.cpp:328 / VoiceSection.cpp:329).
    const bool playing = proc.sequencerPlaying.load();
    if (! playing && ! wasPlayingLastTick) return;

    if (playing)
        modulatorPanel.setPlayheadBeat(proc.lastBeatPos.load());
    voiceSection.refreshModulatedIndicators();
    euclidPanel.refreshModulatedIndicators();

    // Re-render the RhythmCircle when euclid modulation changes the pattern
    // (hits/rotate/prePad/etc.). Audio thread writes lastEuclidOverrides per block;
    // we compare against the most recently applied snapshot and refresh on change.
    if (currentRhythmIndex >= 0 && currentRhythmIndex < proc.getNumRhythms())
    {
        const EuclidOverrides ov = proc.getModulatedEuclidOverrides(currentRhythmIndex);
        if (ov != lastCircleOverrides)
            refreshCircle();
    }

    wasPlayingLastTick = playing;
}
