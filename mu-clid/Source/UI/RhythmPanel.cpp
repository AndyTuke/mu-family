#include "UI/PresetDropdown.h"   // mu-core: shared preset scan + selector fill
#include "RhythmPanel.h"
#include "SampleBrowser.h"
#include "UI/ConfirmDialog.h"   // shared themed confirm dialogs (mu_ui::confirmAsync)
#include "UI/OverlayHost.h"     // shared dimmed in-editor overlay host (sample browser)
#include "Persistence/PresetFiles.h"   // mu_pp::safePresetFileName

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
    "fltType", "fltCut", "fltRes", "fltLoCut", "fltDrv",
    "fEnvAtk", "fEnvDec", "fEnvSus", "fEnvRel", "fEnvDep",
    "ampLvl", "accentDb",
    "aEnvAtk", "aEnvDec", "aEnvSus", "aEnvRel",
    "drvChar", "drvDrv", "drvOut", "drvDit", "drvTon", "eqMidGain", "drvBits", "drvRate"
};

// Mixer-strip params shown in the voice band (FX sends + pan) — ch{ri}_ prefix, shared with
// the mixer channel strip.
const char* const kSendSuffixes[] = { "sendEff", "sendDly", "sendRev", "pan" };

} // namespace

//==============================================================================
RhythmPanel::RhythmPanel(PluginProcessor& p)
    : proc(p), euclidPanel(p), voiceSection(p),
      modDestProvider(mu_clid::makeModDestProvider([this] { return effectSendName; }))
{
    startTimerHz(mu_ui::kUiRefreshHz);
    addAndMakeVisible(circle);
    addAndMakeVisible(euclidPanel);
    // Logic sits in the circle panel's corner, drawn over the circle's empty corner area.
    addAndMakeVisible(euclidPanel.getLogicControl());
    addAndMakeVisible(voiceSection);
    addAndMakeVisible(modulatorPanel);
    modulatorPanel.setDestProvider(&modDestProvider);

    // juce::Label provides bulletproof inline editing: handles single-click to edit,
    // Enter to commit, Escape to cancel, click-off to commit, focus management — all
    // natively. Our previous TextEditor + onFocusLost setup was fragile because
    // onFocusLost only fires when another component grabs keyboard focus, which most
    // child components in this UI don't do.
    // Shared per-layer header bar (name / reset / delete / preset / save).
    headerBar.setPresetPlaceholder(juce::String::fromUTF8("rhythm preset\xe2\x80\xa6"));
    headerBar.onReset       = [this] { confirmReset();  };
    headerBar.onDelete      = [this] { confirmDelete(); };
    headerBar.onSave        = [this] { if (proc.canSaveLayerPreset()) saveRhythmPreset(); };
    headerBar.setSaveEnabled(proc.canSaveLayerPreset());   // demo: per-layer save disabled
    headerBar.onNameChanged = [this](juce::String n) { commitNameFromLabel(n); };
    headerBar.onPresetSelected = [this](int id)
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
    addAndMakeVisible(headerBar);

    // The shared save card, worded for a rhythm preset (no logo → the compact card).
    saveDialog.setTitle("Save Rhythm Preset");
    saveDialog.setEmbedLabel("Embed sample in file");
    addAndMakeVisible(saveDialog);
    saveDialog.setVisible(false);
    saveDialog.onCancel = [this] { saveDialog.setVisible(false); };
    saveDialog.onSave = [this](const juce::String& name,
                                      const juce::String& desc,
                                      const juce::String& chosenCategory, bool embed)
    {
        if (currentRhythmIndex < 0) return;
        const juce::String category = chosenCategory == "Uncategorised" ? juce::String() : chosenCategory;

        juce::File destDir = proc.getRhythmsDir().isDirectory()
                                 ? proc.getRhythmsDir()
                                 : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);

        if (saveDialog.isSaveAsDefault())
        {
            proc.saveRhythmPresetToFile(currentRhythmIndex,
                                        destDir.getChildFile("_default.muRhythm"),
                                        embed, {}, {});
            saveDialog.setVisible(false);
            return;
        }

        // Append the extension rather than withFileExtension, which would cut a dotted name ("Kick 1.5").
        juce::File destFile = destDir.getChildFile(mu_pp::safePresetFileName(name, "Rhythm") + ".muRhythm");

        if (destFile.existsAsFile())
        {
            juce::Component::SafePointer<RhythmPanel> safeThis(this);
            mu_ui::confirmAsync(this, "Overwrite Rhythm Preset", "Overwrite \"" + name + "\"?", "Overwrite",
                [safeThis, destFile, name, desc, category, embed]
                {
                    if (!safeThis) return;
                    safeThis->proc.ensureCategoryInList(category);
                    safeThis->proc.saveRhythmPresetToFile(safeThis->currentRhythmIndex,
                                                          destFile, embed, category, desc);
                    safeThis->loadedRhythmPresetFile = destFile;
                    safeThis->saveDialog.setVisible(false);
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
                });
            return;
        }

        proc.ensureCategoryInList(category);
        proc.saveRhythmPresetToFile(currentRhythmIndex, destFile, embed, category, desc);
        loadedRhythmPresetFile = destFile;
        saveDialog.setVisible(false);
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
    headerBar.commitNameEdit();   // commit any in-progress rename to the OLD rhythm first

    if (currentRhythmIndex != index)
        loadedRhythmPresetFile = juce::File();   // explicit: '= {}' is ambiguous on GCC/Clang
    deregisterRhythmListeners(currentRhythmIndex);
    currentRhythmIndex = index;
    registerRhythmListeners(currentRhythmIndex);
    if (index >= 0 && index < proc.getNumRhythms())
    {
        headerBar.setLayerName(juce::String(proc.getRhythm(index).name));
        headerBar.setColour(currentColour());
        repaint();   // the preset-bar panel behind the header is drawn in the rhythm colour
        refreshRhythmPresets();
        euclidPanel.setRhythm(index);
        euclidPanel.setRhythmColour(currentColour());
        // modulatorPanel must be re-pointed BEFORE voiceSection — voiceSection.setRhythm
        // triggers onInsertAlgorithmChanged → modulatorPanel.setInsertAlgorithm →
        // ModulatorEditor::rebuildRows(), which reads cs->id. If cs still points at a
        // just-destroyed Rhythm (e.g. after delete-last), cs->id is garbage and string
        // concat throws std::bad_alloc.
        modulatorPanel.setVoiceSlot(&proc.getRhythm(index));
        voiceSection.setRhythm(index);
        refreshCircle();
    }
    else
    {
        headerBar.setLayerName("No Rhythm");
        // Null out all child-panel rhythm pointers so they don't dereference stale memory
        // if a vector erase invalidated the previous rhythm before re-binding.
        modulatorPanel.setVoiceSlot(nullptr);
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
        return MuLookAndFeel::channelPalette[r.colourIndex % MuLookAndFeel::kChannelPaletteSize];
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
            proc.samples.load(currentRhythmIndex, file);
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
    const juce::File primaryLib = proc.samples.getPrimarySampleDir();
    const juce::File startDir = lastBrowseDir.isDirectory()
                                    ? lastBrowseDir
                                    : (primaryLib.isDirectory()
                                           ? primaryLib
                                           : juce::File::getSpecialLocation(juce::File::userMusicDirectory));

    juce::Component::SafePointer<RhythmPanel> safeThis(this);
    const int rhythmIndex = currentRhythmIndex;

    // Host the sample browser as a dimmed in-editor overlay (consistent with the other modals)
    // rather than a floating DialogWindow. The browser brings its own Load/Cancel; OverlayHost
    // supplies the dim backdrop + centring + click-outside-to-dismiss.
    auto content = std::make_unique<SampleBrowserContent>(
        proc, startDir,
        [safeThis, rhythmIndex](const juce::File& f)
        {
            if (safeThis == nullptr) return;
            safeThis->lastBrowseDir = f.getParentDirectory();
            safeThis->proc.samples.load(rhythmIndex, f);
            safeThis->repaint();
        });

    auto* overlay = new mu_ui::OverlayHost();
    content->onDismiss = [overlay] { overlay->dismiss(); };
    overlay->show(this, std::move(content));
}

void RhythmPanel::refreshRhythmPresets()
{
    const auto entries = mu_pp::listPresetsByCategory(proc.getRhythmsDir(), "muRhythm");
    mu_ui::fillPresetDropdown(rhythmPresetDropdown, rhythmPresetFiles, entries);

    // The named categories in use, for getKnownCategories().
    knownRhythmCategories.clear();
    for (const auto& e : entries)
        if (e.category != "Uncategorised")
            knownRhythmCategories.addIfNotAlreadyThere(e.category);
    knownRhythmCategories.sort(false);
}

void RhythmPanel::setKnownCategories(const juce::StringArray& cats)
{
    saveDialog.setKnownCategories(cats);
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
        defaultName = mu_pp::safePresetFileName(juce::String(proc.getRhythm(currentRhythmIndex).name), {});
    }

    setKnownCategories(proc.loadCategoryList());
    saveDialog.setDefaultName(defaultName);
    saveDialog.setDefaultDescription(defaultDesc);
    saveDialog.setDefaultCategory(defaultCat);
    saveDialog.setDefaultEmbed(defaultEmbed);
    saveDialog.setVisible(true);
    saveDialog.toFront(true);
}

void RhythmPanel::mouseDown(const juce::MouseEvent& e)
{
    // Name editing is handled by nameLabel (a child component); we no longer need
    // the manual nameRect hit-test here.
    if (sampleRect.contains(e.getPosition()))   // the laid-out (scaled) sample strip
        loadSample();
}

void RhythmPanel::paint(juce::Graphics& g)
{
    using Id = MuLookAndFeel::ColourIds;

    g.setColour(MuLookAndFeel::colour(Id::panelBackground));
    g.fillAll();

    // Every panel, the preset bar included, is painted in the app colour; the shared
    // ChannelHeaderBar lights its name + preset displays in the rhythm's own colour.
    using mu_ui::s;
    const juce::Colour appCol = MuLookAndFeel::appAccent(*this);
    MuLookAndFeel::drawAccentPanel(g, juce::Rectangle<int>(0, 0, getWidth(), s(kHeaderH)).reduced(2).toFloat(), appCol);
    for (auto r : { sampleRect, circleRect, euclidRect, voiceRect, modRect })
        MuLookAndFeel::drawAccentPanel(g, r.reduced(2).toFloat(), appCol);

    // Sample bar — content inset from panel outline
    const juce::String sampleName = proc.samples.getSampleName(currentRhythmIndex);
    const bool         missing    = proc.samples.isSampleMissing(currentRhythmIndex);
    if (MuLookAndFeel::isMetal(*this))
    {
        // Metal: an LCD — the file name in lit lettering, a missing sample lit in amber,
        // the empty-slot hint as faint unlit lettering, then the glass's glare + bezel.
        const auto& L    = MuLookAndFeel::lighting();
        const auto inner = sampleDisplayRect().toFloat();
        const auto lit   = MuLookAndFeel::lcdLitColour(*this);
        MuLookAndFeel::drawLcdGlass(g, inner, lit, false);
        g.setFont(MuLookAndFeel::lcdFont(mu_ui::sf((float) MuLookAndFeel::kLcdTextH)));
        const auto textR = inner.toNearestInt().withTrimmedLeft(s(MuLookAndFeel::kLcdTextPadX))
                                                    .withTrimmedRight(s(MuLookAndFeel::kLcdBrowseW + MuLookAndFeel::kSpaceXS));
        if (missing)
        {
            g.setColour(MuLookAndFeel::colour(MuLookAndFeel::sampleBarMissingWarning));
            g.drawText("Missing: " + sampleName + juce::String::fromUTF8("  \xe2\x80\x94  click to find"),
                       textR, juce::Justification::centredLeft, true);
        }
        else if (sampleName.isNotEmpty())
        {
            g.setColour(lit);
            g.drawText(sampleName, textR, juce::Justification::centredLeft, true);
        }
        else
        {
            g.setColour(lit.withAlpha(L.lcdGhost));
            g.drawText("drop sample here or click to browse", textR, juce::Justification::centredLeft, true);
        }
        g.setColour(lit);
        g.drawText("...", inner.toNearestInt().removeFromRight(s(MuLookAndFeel::kLcdBrowseW)),   // browse
                   juce::Justification::centred, false);
        MuLookAndFeel::drawLcdFront(g, inner);
        return;
    }
    {
        const auto inner = sampleRect.reduced(3);
        g.setColour(MuLookAndFeel::colour(Id::sampleBarBackground));
        g.fillRect(inner);

        if (missing)
        {
            // Linked sample referenced by a preset could not be found at its recorded
            // path nor in the user Samples folder. Show the filename in amber with a
            // "missing — click to find" hint so the user knows what to look for.
            g.setColour(MuLookAndFeel::colour(MuLookAndFeel::sampleBarMissingWarning));
            g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
            g.drawText("Missing: " + sampleName + juce::String::fromUTF8("  \xe2\x80\x94  click to find"),
                       inner.getX() + 5, inner.getY(), inner.getWidth() - 28, inner.getHeight(),
                       juce::Justification::centredLeft, true);
        }
        else if (sampleName.isNotEmpty())
        {
            g.setColour(MuLookAndFeel::colour(Id::labelText));
            g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
            g.drawText(sampleName,
                       inner.getX() + 5, inner.getY(), inner.getWidth() - 28, inner.getHeight(),
                       juce::Justification::centredLeft, true);
        }
        else
        {
            g.setColour(MuLookAndFeel::colour(Id::sampleBarNoSample));
            g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f).withStyle("Italic")));
            g.drawText("drop sample here or click to browse",
                       inner.getX() + 5, inner.getY(), inner.getWidth() - 28, inner.getHeight(),
                       juce::Justification::centredLeft, true);
        }

        g.setColour(MuLookAndFeel::colour(Id::labelText));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
        g.drawText("...", inner.getRight() - 24, inner.getY(), 24, inner.getHeight(),
                   juce::Justification::centred, false);
    }
}

void RhythmPanel::resized()
{
    // Fixed Medium-baseline layout — see MuLookAndFeel for the constants.
    // Every dimension wrapped in s() so scale toggles propagate.
    using mu_ui::s;
    const int w = getWidth();
    const int h = getHeight();

    topH    = s(MuLookAndFeel::kChannelTopH);
    circleW = s(MuLookAndFeel::kCircleSize);

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

    // Shared header bar sits just inside its rhythm-colour panel outline (lays out its own controls).
    // With screws the bar is narrower, leaving a screw at each end of its strip.
    const int hdrInsetX = MuLookAndFeel::hasScrews(*this) ? s(MuLookAndFeel::kScrewedPanelInset) : s(kHeaderInsetX);
    headerBar.setBounds(juce::Rectangle<int>(0, 0, w, hdrH).reduced(hdrInsetX, s(kHeaderInsetY)));

    const int rhythmInset = s(kPanelPad + 1);
    circle.setBounds        (circleRect.reduced(rhythmInset));
    euclidPanel.setBounds   (euclidRect.reduced(rhythmInset));

    // Logic dropdown: bottom-right corner of the circle panel, just inside its border,
    // where it clears the outer ring.
    {
        const int lw = s(EuclideanPanel::kLogicDropW);
        const int lh = s(EuclideanPanel::kLogicDropH);
        // With screws it moves in from the corner, clear of the corner screw.
        const int m  = s(2 + MuLookAndFeel::kDropdownEdgeGap);   // clear of the 2 px border + a gap
        const int mx = MuLookAndFeel::hasScrews(*this)   // clear of the corner screw, with a gap
                     ? s(MuLookAndFeel::kScrewedPanelInset + MuLookAndFeel::kDropdownEdgeGap) : m;
        euclidPanel.getLogicControl().setBounds(circleRect.getRight() - mx - lw,
                                                circleRect.getBottom() - m - lh, lw, lh);
    }
    voiceSection.setBounds  (voiceRect.reduced(rhythmInset));
    // With screws the modulator section is a little narrower, so its tabs and boxes clear
    // the panel's corner screws.
    const int modInsetX = MuLookAndFeel::hasScrews(*this) ? s(MuLookAndFeel::kScrewedPanelInset) : rhythmInset;
    modulatorPanel.setBounds(modRect.reduced(modInsetX, rhythmInset));
    saveDialog.setBounds(getLocalBounds());
}

//==============================================================================
// Called by juce::Label when the user finishes editing (Enter, click-off, etc.)
// Writes the Label's text into the current rhythm and notifies listeners.
void RhythmPanel::commitNameFromLabel(const juce::String& rawName)
{
    if (currentRhythmIndex < 0 || currentRhythmIndex >= proc.getNumRhythms()) return;

    auto newName = rawName.trim();
    if (newName.isEmpty())
    {
        newName = "<unnamed>";
        headerBar.setLayerName(newName);
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
    mu_ui::confirmAsync(this, "Reset Rhythm",
                        "Reset \"" + name + "\" to defaults?\nThis cannot be undone.", "Reset",
        [safeThis, idx]
        {
            if (safeThis != nullptr && idx >= 0 && idx < safeThis->proc.getNumRhythms())
            {
                // PluginProcessor::resetRhythm owns the concurrency dance
                // (suspendProcessing + rhythmsLock). No more UI-thread spin on modLock.
                safeThis->proc.resetRhythm(idx);
                safeThis->setRhythm(idx);
            }
        });
}

void RhythmPanel::confirmDelete()
{
    if (currentRhythmIndex < 0 || currentRhythmIndex >= proc.getNumRhythms()) return;
    if (proc.getNumRhythms() <= 1) return; // cannot delete last rhythm
    const int idx = currentRhythmIndex;
    const juce::String name(proc.getRhythm(idx).name);

    juce::Component::SafePointer<RhythmPanel> safeThis(this);
    mu_ui::confirmAsync(this, "Delete Rhythm",
                        "Delete \"" + name + "\"?\nThis cannot be undone.", "Delete",
        [safeThis, idx]
        {
            if (safeThis != nullptr && safeThis->onRhythmDeleted)
                safeThis->onRhythmDeleted(idx);
        });
}

void RhythmPanel::timerCallback()
{
    const bool playing = proc.sequencerPlaying.load();

    if (playing)
        modulatorPanel.setPlayheadBeat(proc.lastBeatPos.load());

    // Re-render the RhythmCircle when euclid modulation changes the pattern
    // (hits/rotate/prePad/etc.). Audio thread writes lastEuclidOverrides per block;
    // we compare against the most recently applied snapshot and refresh on change.
    if (playing && currentRhythmIndex >= 0 && currentRhythmIndex < proc.getNumRhythms())
    {
        const EuclidOverrides ov = proc.getModulatedEuclidOverrides(currentRhythmIndex);
        if (ov != lastCircleOverrides)
            refreshCircle();
    }
}

// Screws in the corners of the full-height panels, set into the painted border over any
// content that reaches the corner (the thin header + sample strips have no room for them).
void RhythmPanel::paintOverChildren(juce::Graphics& g)
{
    if (! MuLookAndFeel::hasScrews(*this)) return;
    for (auto r : { circleRect, euclidRect, voiceRect, modRect })
        MuLookAndFeel::drawPanelScrews(g, r.reduced(2).toFloat());
    // The thin header + sample strips: one screw at each end.
    for (auto r : { headerRect(), sampleRect })
        MuLookAndFeel::drawStripScrews(g, r.reduced(2).toFloat());
}

juce::Rectangle<int> RhythmPanel::headerRect() const
{
    return { 0, 0, getWidth(), mu_ui::s(kHeaderH) };
}

// The sample display inside its strip: with screws, narrower so a screw fits at each end.
juce::Rectangle<int> RhythmPanel::sampleDisplayRect() const
{
    using mu_ui::s;
    const int insetX = MuLookAndFeel::hasScrews(*this) ? s(MuLookAndFeel::kScrewedPanelInset + MuLookAndFeel::kSpaceXS) : 3;
    // As tall as the header's displays, centred in the strip.
    return sampleRect.reduced(insetX, 0).withSizeKeepingCentre(sampleRect.getWidth() - 2 * insetX,
                                                                 s(MuLookAndFeel::kStripDisplayH));
}

void RhythmPanel::setVoiceEffectSendLabel(const juce::String& name)
{
    voiceSection.setEffectSendLabel(name);
    if (name == effectSendName) return;

    // Rebuild the destination dropdowns so the effect-send target carries the new name.
    effectSendName = name;
    modulatorPanel.setDestProvider(&modDestProvider);
}
