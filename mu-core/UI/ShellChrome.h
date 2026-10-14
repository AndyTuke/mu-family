#pragma once

#include <atomic>
#include <functional>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include "Plugin/ShellBackend.h"
#include "Control/LiveView.h"
#include "UI/Components/MuLookAndFeel.h"
#include "UI/Components/StatusBar.h"
#include "UI/TransportBar.h"
#include "UI/AboutOverlay.h"
#include "UI/SaveDialog.h"
#include "UI/PresetBrowser.h"
#include "UI/PerformanceChooser.h"

namespace mu_ui
{

// ShellChrome — the processor-free half of the family's editor shell, as a helper object
// (not a base class). It is built over a host Component and a mu_core::ShellBackend, owns the
// look and feel, transport bar, status bar, About / Save / Preset Browser overlays, the mixer
// and settings overlay slots and the upgrade banner, and parents all of them directly to the
// host so the host's component tree is exactly the shell's tree. It holds the overlay state
// machine, the main-area layout, the metal style, the knob-status plumbing and the online
// version check. EditorShellBase (a plugin editor) and an app window both use it; it never
// includes a processor header.
//
// It also runs the preset flow (load, save with an overwrite confirm, new with a discard confirm,
// the dirty flag) over the backend, and the play / stop key. The host forwards its resized() and
// parentHierarchyChanged() and decides what else it needs (its own overlays are registered with
// addExtraScreen so they join the state machine and the layout; its preset reactions are the
// on...() hooks below).
class ShellChrome : private juce::MouseListener,
                    public juce::KeyListener,
                    private mu_core::LiveView::Listener
{
public:
    ShellChrome(juce::Component& hostComponent, mu_core::ShellBackend& shellBackend);
    ~ShellChrome() override;

    // ─── Performance mode: the input shield ──────────────────────────────────
    // While the live state is in Performance mode a transparent component sits over the whole host
    // and swallows mouse, wheel and key events, so no panel needs any code to be read-only. It paints
    // nothing. Its hit test lets the pointer through over the exempt components (the Perf toggle, the
    // preset indicator, the sidebar's layer indicator), which have no click handling of their own and
    // must still get hover for their status text. The host window's frame and close button are native
    // and unaffected.
    class InputShield : public juce::Component
    {
    public:
        InputShield();
        void addExempt(juce::Component& c);
        bool hitTest(int x, int y) override;
        void mouseDown(const juce::MouseEvent&) override {}
        void mouseUp(const juce::MouseEvent&) override {}
        void mouseDrag(const juce::MouseEvent&) override {}
        void mouseDoubleClick(const juce::MouseEvent&) override {}
        void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override {}   // the default forwards to the parent
        void mouseMagnify(const juce::MouseEvent&, float) override {}
        // Every key is swallowed, with no exception.
        bool keyPressed(const juce::KeyPress&) override { return true; }
    private:
        std::vector<juce::Component::SafePointer<juce::Component>> exempt;
    };

    // The steady accent frame round the editor edge while the mode is on. It paints over everything but
    // takes no input (hitTest false), so it is not part of the shield and changes no layout.
    class PerformanceFrame : public juce::Component
    {
    public:
        PerformanceFrame() { setInterceptsMouseClicks(false, false); }
        bool hitTest(int, int) override { return false; }
        void paint(juce::Graphics& g) override;
    };

    PerformanceFrame& getPerformanceFrame() noexcept { return frame; }
    InputShield& getInputShield() noexcept { return shield; }
    bool isPerformanceLocked() const noexcept { return shieldUp; }
    // Lets the pointer through the shield over `c` (the Play button, the sidebar's layer indicator).
    void addShieldExemption(juce::Component& c) { shield.addExempt(c); }
    // Re-reads the live state and raises or lowers the shield. Called on every live-state change.
    void syncPerformanceMode();
    // The key that enters the mode (Ctrl+P); it never leaves it.
    static bool isPerformanceKey(const juce::KeyPress& key) noexcept;

    // ─── Pieces, for the product to wire ─────────────────────────────────────
    MuLookAndFeel&        getLookAndFeel()  { return lookAndFeel; }
    TransportBar&         getTransportBar() { return transportBar; }
    StatusBar&            getStatusBar()    { return statusBar; }
    AboutOverlay&         getAboutOverlay() { return aboutOverlay; }
    SaveDialog&           getSaveDialog()   { return saveDialog; }
    PresetBrowser&        getPresetBrowser(){ return presetBrowser; }
    PerformanceChooser&   getPerformanceChooser() { return performanceChooser; }
    juce::ComponentAnimator& getAnimator()  { return animator; }

    // ─── Main area ───────────────────────────────────────────────────────────
    // The shell takes pointers, not ownership; the host owns the component instances.
    void setMainArea(juce::Component* newSidebar, juce::Component* newMainPanel);
    // Registers the mixer overlay (nullptr removes it and hides the bar's mixer toggle).
    void setMixerOverlay(juce::Component* overlay);
    // Registers the settings overlay; its close button (if it is a SettingsOverlayBase) returns
    // to the main view. nullptr removes it.
    void setSettingsOverlay(juce::Component* overlay);
    juce::Component* getSidebar()         const noexcept { return sidebar; }
    juce::Component* getMainPanel()       const noexcept { return mainPanel; }
    juce::Component* getMixerOverlay()    const noexcept { return mixerOverlay; }
    juce::Component* getSettingsOverlay() const noexcept { return settingsOverlay; }

    // ─── Extra screens (the host's own overlays) ─────────────────────────────
    // A Modal fills the whole host and sits over the page (the activation panel); a Page fills the
    // main area and replaces the main panel (the MIDI program-change tables). The screen is
    // parented to the host just before `insertBefore` (nullptr: on top), so the z-order is the
    // host's choice.
    enum class ExtraKind { Modal, Page };
    void addExtraScreen(juce::Component& screen, ExtraKind kind, juce::Component* insertBefore = nullptr);
    void showExtraScreen(juce::Component& screen, bool show);
    bool isExtraScreenVisible(const juce::Component& screen) const;

    // ─── Overlay state machine ───────────────────────────────────────────────
    void showMixer(bool show);
    void showAbout(bool show);
    void showSaveDialog(bool show);
    void showPresetBrowser(bool show);
    void showSettings(bool show);
    void hideAllOverlays();

    bool isMixerVisible()    const noexcept { return mixerVisible; }
    bool isSettingsVisible() const noexcept { return settingsVisible; }
    // True while any overlay (built-in or extra) is up.
    bool anyOverlayVisible() const noexcept;

    // Called just before the About panel is made visible, so the host can fill it (support state).
    std::function<void()> onAboutOpening;

    // ─── Performance chooser ─────────────────────────────────────────────────
    // The screen for choosing and editing a performance (docs/design-ui-family.md 12.5): a page over the
    // main area, reachable only with Performance mode off. Opening starts an editing session from the
    // list in use; the Preset Browser's "Performances" button, the Perf toggle and Ctrl+P with none
    // chosen all come here. A no-op where the backend does not support the mode.
    void showPerformanceChooser();
    bool isPerformanceChooserVisible() const { return isExtraScreenVisible(performanceChooser); }

    // ─── Preset flow ─────────────────────────────────────────────────────────
    // Set whenever the preset's state changes (the host's listener calls markDirty); cleared by
    // a load, save or New. Atomic: a host may flag it from the audio thread (DAW automation).
    void markDirty() noexcept       { presetDirty.store(true,  std::memory_order_relaxed); }
    void clearDirty() noexcept      { presetDirty.store(false, std::memory_order_relaxed); }
    bool isDirty() const noexcept   { return presetDirty.load(std::memory_order_relaxed); }

    // The host's reactions, fired at the moment each step completes: after a preset is loaded
    // (browser, transport dropdown, New's default restore, a staged swap landing), after a
    // successful save, and when New was chosen. getProductCategories adds the host's own
    // categories to the Save card; onCategoriesRefreshed hands back the merged list.
    std::function<void(const juce::File&)>        onPresetLoaded;
    std::function<void(const juce::File&)>        onPresetSaved;
    std::function<void()>                         onPresetNew;
    std::function<juce::StringArray()>            getProductCategories;
    std::function<void(const juce::StringArray&)> onCategoriesRefreshed;

    // ─── Keys ────────────────────────────────────────────────────────────────
    // Standalone only: the play / stop key (space unless keybindings.json says otherwise).
    // The host forwards parentHierarchyChanged() so the first show grabs keyboard focus.
    bool keyPressed(const juce::KeyPress& key, juce::Component* originator) override;
    bool keyStateChanged(bool isKeyDown, juce::Component* originator) override;
    void parentHierarchyChanged();

    // ─── Look ────────────────────────────────────────────────────────────────
    // The family's metal look; `appAccent` is the product's colour (transparent keeps globalAccent).
    void setMetalStyle(bool metal, juce::Colour appAccent = juce::Colours::transparentBlack);
    void setScrews(bool on);

    // Show a panel's controls on the status bar (name + value, when hovered or turned): every
    // shared knob under `root` that has no status handler of its own. `visitor` lets the host
    // handle component types the chrome cannot name (return true when `c` was handled, which
    // stops the walk descending into it).
    using StatusFn      = std::function<void(const juce::String& name, const juce::String& value)>;
    using StatusVisitor = std::function<bool(juce::Component& c, const StatusFn& show)>;
    void forwardKnobStatus(juce::Component& root, const StatusVisitor& visitor = {});

    // ─── Window ──────────────────────────────────────────────────────────────
    // Fixed window size from the UI scale, re-applied when the scale changes in settings.
    void applyWindowSize();
    void paintBackground(juce::Graphics& g) const;
    // Lays out the transport bar, sidebar, main area, overlays, banner and status bar.
    void resized();

    // ─── Upgrade banner ──────────────────────────────────────────────────────
    // Kicks off the async GitHub "latest release" check (fail-silent, off-thread, cached for the
    // process); raises the banner when a newer build exists.
    void startVersionCheck();
    static constexpr int kUpgradeBannerH = 20;

private:
    JUCE_DECLARE_WEAK_REFERENCEABLE(ShellChrome)

    // Applies the stored UI scale; being the first member it runs before any child is built, so
    // the constructor-time font sizes of the children pick up the right scale.
    struct ScaleInit { explicit ScaleInit(mu_core::ShellBackend& b); };

    juce::Component&       host;
    mu_core::ShellBackend& backend;
    ScaleInit              scaleInit;

    MuLookAndFeel       lookAndFeel;
    TransportBar        transportBar;
    AboutOverlay        aboutOverlay;
    SaveDialog          saveDialog;
    PresetBrowser       presetBrowser;
    PerformanceChooser  performanceChooser;
    StatusBar           statusBar;
    juce::Label         upgradeBanner;   // shown when a newer release is available online

    juce::Component* sidebar         = nullptr;
    juce::Component* mainPanel       = nullptr;
    juce::Component* mixerOverlay    = nullptr;
    juce::Component* settingsOverlay = nullptr;

    struct Extra { juce::Component* screen; ExtraKind kind; bool visible; };
    std::vector<Extra> extras;
    Extra* findExtra(const juce::Component& screen);
    const Extra* findExtra(const juce::Component& screen) const;

    bool mixerVisible    = false;
    bool aboutVisible    = false;
    bool saveVisible     = false;
    bool browserVisible  = false;
    bool settingsVisible = false;

    juce::ComponentAnimator animator;
    juce::TooltipWindow     tooltipWindow;   // parents itself to the host in its constructor
    void fadeSwitch(juce::Component* outgoing, juce::Component* incoming, int durationMs = 80);

    InputShield      shield;
    PerformanceFrame frame;
    bool             shieldUp = false;
    const mu_core::LiveView* liveView = nullptr;
    void liveStateChanged() override { syncPerformanceMode(); }
    void enterLock();     // closes everything open as a cancel, releases text focus, raises the shield
    void leaveLock();
    void raiseShield();   // back on top after a child was added above it

    std::atomic<bool> upgradeAvailable { false };
    void showUpgradeAvailable(const juce::String& latestTag);
    void mouseDown(const juce::MouseEvent& e) override;
    static constexpr const char* kDownloadUrl = "https://transwarp.me/download";

    // Opens the Preset Browser in pick mode for the chooser; closing it (picked or not) returns to the chooser.
    void pickPresetForChooser(std::function<void(const juce::File&)> picked);
    bool returnToChooser = false;
    void wirePerformanceChooser();
    void wireBuiltIns();
    void wirePresetFlow();
    void installKeys();
    void fillSaveDialog();
    void loadPresetFromUi(const juce::File& f);
    void doSavePreset(const juce::String& name, const juce::String& desc,
                      const juce::String& category, bool embedSamples);
    void doNewPreset();
    juce::File fullPresetFileFor(const juce::String& name) const;
    void loadKeybindings();

    std::atomic<bool> presetDirty { false };
    std::function<void()> pendingQuitCallback;   // set by a save-and-quit request, run after the save
    bool needsFocusGrab = false;
    juce::KeyPress keybindPlayStop { juce::KeyPress::spaceKey };
};

} // namespace mu_ui
