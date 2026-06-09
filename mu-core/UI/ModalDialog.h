#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <limits>
#include <memory>
#include <vector>

// Shared themed modal dialog for the whole family — the replacement for the default
// juce::AlertWindow. Renders as an in-editor overlay (dim backdrop + centred rounded
// card) styled with MuLookAndFeel, exactly matching the SaveDialog / About card look.
//
// For the common cases use the one-line builders in ConfirmDialog.h
// (mu_ui::messageAsync / confirmAsync / promptTextAsync / confirmQuitAsync). For a
// richer dialog, construct a ModalDialog, inject a custom content component, and show().
//
// Presentation: show(anchor) hosts the dialog over anchor's enclosing AudioProcessorEditor
// (so in a DAW it dims the plugin editor, not the host window), fills it, intercepts input
// via the backdrop, and deletes itself after the result callback. Esc / click-outside the
// card answer the cancel result; the primary button answers Return.
namespace mu_ui {

class ModalDialog : public juce::Component
{
public:
    enum class Icon { None, Info, Question, Warning };

    ModalDialog();
    ~ModalDialog() override;

    // ── Build (chainable) ────────────────────────────────────────────────────
    ModalDialog& title  (const juce::String& t);
    ModalDialog& message(const juce::String& m);
    ModalDialog& icon   (Icon i);
    // Add a button. `primary` styles it as the accent action and makes it the Return
    // default (the first primary added wins). Buttons display left→right in add order,
    // right-aligned as a group (convention: Cancel first/left, primary last/right).
    ModalDialog& button (const juce::String& label, int result, bool primary = false);
    // Result delivered on Esc / click-outside / host teardown (default 0).
    ModalDialog& cancelResult(int r) { cancelResultId = r; return *this; }
    // Inject custom content shown between the message and the buttons (owned by the dialog).
    ModalDialog& content(std::unique_ptr<juce::Component> c, int height);
    // Override the default card width (px before UI scaling; default 360).
    ModalDialog& width(int w) { cardW = w; return *this; }

    std::function<void(int result)> onResult;

    // Present over anchor's enclosing editor. Self-owning — do not delete.
    void show(juce::Component* anchor);
    // Programmatically dismiss with a result (used by injected content, e.g. a prompt's
    // Return/Esc handlers).
    void close(int result) { finish(result); }
    // The injected content component (e.g. for grabbing focus), or nullptr.
    juce::Component* contentComponent() const noexcept { return contentComp.get(); }

    void paint(juce::Graphics&) override;
    void resized() override;
    void parentSizeChanged() override;
    void mouseDown(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    struct Btn { juce::String label; int result; bool primary; };

    void finish(int result);
    juce::Rectangle<int> cardBounds() const;
    int  measureMessageHeight(int contentWidth) const;
    int  computeCardHeight() const;

    juce::String titleText, messageText;
    Icon iconType = Icon::None;

    std::vector<Btn> buttonSpecs;
    std::vector<std::unique_ptr<juce::TextButton>> buttons;
    int cancelResultId  = 0;
    int defaultResultId = std::numeric_limits<int>::min();

    std::unique_ptr<juce::Component> contentComp;
    int contentHeight = 0;
    int cardW = 360;
    bool finished = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModalDialog)
};

} // namespace mu_ui
