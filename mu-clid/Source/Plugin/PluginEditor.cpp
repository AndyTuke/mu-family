#include "PluginEditor.h"
#include "Audio/FX/Slots/FXAlgorithmDef.h"
#include <BinaryData.h>

namespace mu_clid {

PluginEditor::PluginEditor(PluginProcessor& p)
    : EditorShellBase(p),
      proc(p),
      masterLoop(p),
      sidebar(p, "Rhythm"),
      rhythmPanel(p),
      mixerOverlay(p, p.mixerEngine),
      settingsOverlay(p)
{
    // ── Product chrome on shared overlays ───────────────────────────────────
    setProductIdentity(juce::String(juce::CharPointer_UTF8("\xce\xbc-Clid")),
                       juce::StringArray { juce::String(juce::CharPointer_UTF8("Bj\xc3\xb6rklund algorithm \xe2\x80\x94 public domain")) });
    getSaveDialog().setLogoImage(juce::ImageCache::getFromMemory(BinaryData::muclid_png,
                                                                  BinaryData::muclid_pngSize));
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
    // Per-rhythm mini-graphic: a RhythmCircle whose step hit flashes the sidebar item.
    sidebar.createMiniVisual = [this](int i, std::function<void()> pulse) -> std::unique_ptr<juce::Component>
    {
        auto mv = std::make_unique<RhythmMiniVisual>(proc, i);
        mv->onHit = std::move(pulse);
        return mv;
    };
    sidebar.refreshItems();

    sidebar.onLayerSelected = [this](int idx)
    {
        rhythmPanel.setRhythm(idx);
        if (!isMixerVisible())
        {
            hideAllOverlays();
            rhythmPanel.setVisible(true);
        }
    };

    sidebar.onLayersReordered = [this](int newSelected)
    {
        selectRhythmAndRefresh(newSelected, /*fullSidebarRefresh=*/false, MixerRefresh::FullReload);
    };

    sidebar.onAddLayer = [this]
    {
        if (proc.getNumRhythms() >= SequencerEngine::MaxRhythms) return;
        Rhythm r;
        r.name        = ("Rhythm " + juce::String(proc.getNumRhythms() + 1)).toStdString();   // the slot keeps it through the default-rhythm load
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
        proc.rhythms.add(r);
        const int newIndex = proc.getNumRhythms() - 1;
        proc.applyDefaultRhythm(newIndex);
        selectRhythmAndRefresh(newIndex, /*fullSidebarRefresh=*/true, MixerRefresh::RefreshOnly);
    };

    // ── RhythmPanel callbacks ───────────────────────────────────────────────
    rhythmPanel.onStatusUpdate = [this](const juce::String& name,
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

    // A rhythm in MIDI output mode makes no audio, so its mixer strip is drawn inactive. The strip is
    // refreshed when the shown rhythm's mode changes and, for host automation of any rhythm, below.
    mixerOverlay.isChannelSilent = [this](int r) { return isRhythmMidiOutput(r); };
    rhythmPanel.onOutputModeChanged = [this] { if (isMixerVisible()) mixerOverlay.loadFromApvts(); };
    for (int i = 0; i < SequencerEngine::MaxRhythms; ++i)
        proc.apvts.addParameterListener("r" + juce::String(i) + "_outMode", this);

    rhythmPanel.onRhythmRenamed = [this]
    {
        sidebar.repaintItems();
        if (isMixerVisible()) mixerOverlay.refresh();
    };

    proc.onLayerPresetCommitted = [this](int r)
    {
        if (r == rhythmPanel.getCurrentRhythmIndex())
            rhythmPanel.setRhythm(r);
        sidebar.repaintItems();
        if (isMixerVisible()) mixerOverlay.refresh();
    };

    rhythmPanel.onRhythmDeleted = [this](int idx)
    {
        proc.rhythms.remove(idx);
        const int newIndex = juce::jmax(0, juce::jmin(idx, proc.getNumRhythms() - 1));
        sidebar.refreshItems();
        sidebar.setSelectedIndex(newIndex);
        rhythmPanel.setRhythm(newIndex);
        if (isMixerVisible()) mixerOverlay.refresh();
    };

    // ── Settings overlay ────────────────────────────────────────────────────
    settingsOverlay.onContentDirChanged = [this] { getTransportBar().refreshPresets(); };

    // ── Startup ─────────────────────────────────────────────────────────────
    if (proc.getNumRhythms() == 0)
    {
        Rhythm r;
        r.name        = "Rhythm 1";
        r.colourIndex = 0;
        proc.rhythms.add(r);
        sidebar.refreshItems();
    }

    rhythmPanel.setRhythm(0);
    sidebar.setSelectedIndex(0);

    // Sync mixer UI from APVTS — in standalone, state is restored before the editor
    // is created, so without this call the scSourceBox and other controls would show
    // defaults rather than the restored values.
    mixerOverlay.loadFromApvts();
    clearPresetDirty();

    // The family metal look, in mu-Clid's purple (globalAccent).
    setMetalStyle(true, MuLookAndFeel::colour(MuLookAndFeel::appPurple));
    setScrews(true);
}

PluginEditor::~PluginEditor()
{
    // Clear product-owned processor callbacks before teardown — same UAF
    // contract as the shell base's destructor.
    proc.apvts.removeParameterListener("eff_algo", this);
    for (int i = 0; i < SequencerEngine::MaxRhythms; ++i)
        proc.apvts.removeParameterListener("r" + juce::String(i) + "_outMode", this);
    proc.onLayerPresetCommitted = nullptr;
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
            mixerOverlay.loadFromApvts();
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
    // A rhythm's output mode (Sample / MIDI) decides whether its mixer strip is drawn active.
    if (parameterID.endsWith("_outMode"))
    {
        juce::Component::SafePointer<PluginEditor> safeThis(this);
        juce::MessageManager::callAsync([safeThis]
        {
            if (safeThis && safeThis->isMixerVisible()) safeThis->mixerOverlay.loadFromApvts();
        });
        return;
    }
    if (parameterID != "eff_algo") return;

    juce::Component::SafePointer<PluginEditor> safeThis(this);
    juce::MessageManager::callAsync([safeThis]
    {
        if (safeThis) safeThis->syncVoiceEffectSendLabel();
    });
}

// True when rhythm `r` is set to send MIDI notes instead of playing its sample.
bool PluginEditor::isRhythmMidiOutput(int r) const
{
    if (auto* raw = proc.apvts.getRawParameterValue("r" + juce::String(r) + "_outMode"))
        return raw->load() > 0.5f;
    return false;
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

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor(*this);
}

} // namespace mu_clid
