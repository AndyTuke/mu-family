#include "PluginEditor.h"
#include "Audio/FX/Slots/FXAlgorithmDef.h"
#include <BinaryData.h>

PluginEditor::PluginEditor(PluginProcessor& p)
    : EditorShellBase(p),
      proc(p),
      masterLoop(p),
      sidebar(p),
      rhythmPanel(p),
      mixerOverlay(p, p.mixerEngine),
      settingsOverlay(p)
{
    // ── Product chrome on shared overlays ───────────────────────────────────
    getAboutPanel().setProductInfo(
        juce::String(juce::CharPointer_UTF8("\xce\xbc")) + "-Clid",
        juce::StringArray {
            juce::String(juce::CharPointer_UTF8("JUCE \xe2\x80\x94 Proprietary (JUCE 7 license)")),
            juce::String(juce::CharPointer_UTF8("Signalsmith Reverb \xe2\x80\x94 MIT")),
            juce::String(juce::CharPointer_UTF8("Monocypher \xe2\x80\x94 BSD-2-Clause")),
            juce::String(juce::CharPointer_UTF8("clap-juce-extensions \xe2\x80\x94 MIT")),
            juce::String(juce::CharPointer_UTF8("Bj\xc3\xb6rklund algorithm \xe2\x80\x94 public domain")),
        });
    getActivationPanel().setProductName(juce::String(juce::CharPointer_UTF8("\xce\xbc-Clid")));
    getSaveDialog().setLogoImage(juce::ImageCache::getFromMemory(BinaryData::muclid_png,
                                                                  BinaryData::muclid_pngSize));
    getTransportBar().setLogoText(juce::String(juce::CharPointer_UTF8("\xce\xbc-Clid")));
    getTransportBar().setLoopSection(&masterLoop, MasterLoopSection::kWidth);
    masterLoop.onStatusUpdate = [this](const juce::String& name, const juce::String& val)
    {
        getStatusBar().showParam(name, val);
    };

    // ── Product main area + overlays ────────────────────────────────────────
    setMainArea(&sidebar, &rhythmPanel);
    setMixerOverlay(&mixerOverlay);
    setSettingsOverlay(&settingsOverlay);

    // ── Sidebar callbacks ───────────────────────────────────────────────────
    sidebar.onRhythmSelected = [this](int idx)
    {
        rhythmPanel.setRhythm(idx);
        if (!isMixerVisible())
        {
            hideAllOverlays();
            rhythmPanel.setVisible(true);
        }
    };

    sidebar.onRhythmsReordered = [this](int newSelected)
    {
        selectRhythmAndRefresh(newSelected, /*fullSidebarRefresh=*/false, MixerRefresh::FullReload);
    };

    sidebar.onAddRhythm = [this]
    {
        if (!proc.canAddChannel())
        {
            getStatusBar().showParam("Demo", juce::String::fromUTF8(u8"Demo limited to 1 rhythm — purchase a license to unlock all 8"),
                                     MuLookAndFeel::colour(MuLookAndFeel::knobLevel));
            return;
        }
        if (proc.getNumRhythms() >= SequencerEngine::MaxRhythms) return;
        Rhythm r;
        r.name        = "<unnamed>";
        // Pick the first palette index not already used by an existing rhythm.
        {
            constexpr int N = MuLookAndFeel::kChannelPaletteSize;
            std::array<bool, N> used{};
            const int nExisting = proc.getNumRhythms();
            for (int i = 0; i < nExisting; ++i)
                used[(size_t) (proc.getRhythm(i).colourIndex % N)] = true;
            const int startProbe = nExisting % N;
            int chosen = startProbe;
            for (int i = 0; i < N; ++i)
            {
                const int candidate = (startProbe + i) % N;
                if (! used[(size_t) candidate]) { chosen = candidate; break; }
            }
            r.colourIndex = chosen;
        }
        proc.addRhythm(r);
        const int newIdx = proc.getNumRhythms() - 1;
        proc.applyDefaultRhythm(newIdx);
        selectRhythmAndRefresh(newIdx, /*fullSidebarRefresh=*/true, MixerRefresh::RefreshOnly);
    };

    // ── RhythmPanel callbacks ───────────────────────────────────────────────
    rhythmPanel.onStatusUpdate = [this](const juce::String& name,
                                        const juce::String& val,
                                        juce::Colour col)
    {
        getStatusBar().showParam(name, val, col);
    };

    mixerOverlay.onStatusUpdate = [this](const juce::String& name,
                                          const juce::String& val,
                                          juce::Colour col)
    {
        getStatusBar().showParam(name, val, col);
    };

    // Mirror the mixer's effect-slot algorithm name on the voice section's
    // Amp "Effect" send knob so the user can read it without opening the
    // mixer overlay. Two paths feed this: (a) the mixer's own dropdown
    // (synchronous from updateEffectSendLabels), (b) host automation of
    // eff_algo (asynchronous via the APVTS listener registered below — fires
    // regardless of mixer visibility).
    mixerOverlay.onEffectAlgorithmNameChanged = [this](const juce::String& name)
    {
        rhythmPanel.setVoiceEffectSendLabel(name);
    };
    proc.apvts.addParameterListener("eff_algo", this);

    rhythmPanel.onRhythmRenamed = [this]
    {
        sidebar.repaintItems();
        if (isMixerVisible()) mixerOverlay.refresh();
    };

    proc.onRhythmHotSwapCommitted = [this](int r)
    {
        if (r == rhythmPanel.getCurrentRhythmIndex())
            rhythmPanel.setRhythm(r);
        sidebar.repaintItems();
        if (isMixerVisible()) mixerOverlay.refresh();
    };

    rhythmPanel.onRhythmDeleted = [this](int idx)
    {
        proc.removeRhythm(idx);
        const int newIdx = juce::jmax(0, juce::jmin(idx, proc.getNumRhythms() - 1));
        sidebar.refreshItems();
        sidebar.setSelectedIndex(newIdx);
        rhythmPanel.setRhythm(newIdx);
        if (isMixerVisible()) mixerOverlay.refresh();
    };

    // ── Settings overlay ────────────────────────────────────────────────────
    settingsOverlay.onClose = [this] { showSettings(false); };
    settingsOverlay.onContentDirChanged = [this] { getTransportBar().refreshPresets(); };
    settingsOverlay.onMidiPresetsClicked = [this]
    {
        showSettings(false);
        showMidiPresets(true);
    };
    settingsOverlay.onFullPresetsClicked = [this]
    {
        showSettings(false);
        showMidiFullPresets(true);
    };

    // ── Startup ─────────────────────────────────────────────────────────────
    if (proc.getNumRhythms() == 0)
    {
        Rhythm r;
        r.name        = "<unnamed>";
        r.colourIndex = 0;
        proc.addRhythm(r);
        sidebar.refreshItems();
    }

    rhythmPanel.setRhythm(0);
    sidebar.setSelectedIndex(0);

    // Sync mixer UI from APVTS — in standalone, state is restored before the editor
    // is created, so without this call the scSourceBox and other controls would show
    // defaults rather than the restored values.
    mixerOverlay.loadFromAPVTS();
    clearPresetDirty();
}

PluginEditor::~PluginEditor()
{
    // Clear product-owned processor callbacks before teardown — same UAF
    // contract as the shell base's destructor.
    proc.apvts.removeParameterListener("eff_algo", this);
    proc.onRhythmHotSwapCommitted = nullptr;
}

void PluginEditor::selectRhythmAndRefresh(int idx,
                                          bool fullSidebarRefresh,
                                          MixerRefresh mixerRefresh)
{
    if (fullSidebarRefresh)
        sidebar.refreshItems();
    sidebar.setSelectedIndex(idx);
    rhythmPanel.setRhythm(idx);
    if (isMixerVisible())
    {
        if (mixerRefresh != MixerRefresh::Skip)
            mixerOverlay.refresh();
        if (mixerRefresh == MixerRefresh::FullReload)
            mixerOverlay.loadFromAPVTS();
    }
}

// ── EditorShellBase hooks ───────────────────────────────────────────────────
void PluginEditor::onPresetLoaded(const juce::File& /*file*/)
{
    selectRhythmAndRefresh(0, /*fullSidebarRefresh=*/true, MixerRefresh::FullReload);
}

void PluginEditor::onPresetSaved(const juce::File& /*file*/)
{
    // Save doesn't change the rhythm set — no refresh needed beyond what the
    // shell already does (transport preset dropdown + dirty flag).
}

void PluginEditor::onPresetNew()
{
    selectRhythmAndRefresh(0, /*fullSidebarRefresh=*/true, MixerRefresh::FullReload);
}

juce::StringArray PluginEditor::getProductKnownCategories() const
{
    return rhythmPanel.getKnownCategories();
}

void PluginEditor::onCategoriesRefreshed(const juce::StringArray& merged)
{
    rhythmPanel.setKnownCategories(merged);
}

// ── APVTS listener — host automation of eff_algo ────────────────────────────
// fires from any thread. Marshal to the message thread before touching the UI.
void PluginEditor::parameterChanged(const juce::String& parameterID, float)
{
    if (parameterID != "eff_algo") return;

    juce::Component::SafePointer<PluginEditor> safeThis(this);
    juce::MessageManager::callAsync([safeThis]
    {
        if (safeThis) safeThis->syncVoiceEffectSendLabel();
    });
}

void PluginEditor::syncVoiceEffectSendLabel()
{
    const auto& algos = FXAlgorithmRegistry::effectAlgorithms();
    const int ai = proc.fxChain.effectSlot().getAlgorithmIndex();
    const juce::String name = (ai >= 0 && ai < (int) algos.size())
                              ? algos[(size_t) ai].name
                              : juce::String("Effect");
    rhythmPanel.setVoiceEffectSendLabel(name);
}
