#pragma once

#include "UI/ModalDialog.h"
#include "UI/Components/MuLookAndFeel.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

// Shared confirmation / message / prompt dialogs for the family — one styled
// implementation (mu_ui::ModalDialog, an in-editor themed overlay) so every product's
// prompts look + behave identically. These replace the old juce::AlertWindow paths.
//
// Each builder takes an `anchor` Component (any component inside the plugin editor); the
// dialog hosts itself over the enclosing editor and deletes itself after the callback.
namespace mu_ui {

// Plain message + dismiss (single primary button). onDismiss fires on OK / Esc.
inline void messageAsync(juce::Component* anchor,
                         const juce::String& title,
                         const juce::String& message,
                         const juce::String& okLabel = "OK",
                         std::function<void()> onDismiss = {})
{
    auto* d = new ModalDialog();
    d->title(title).message(message).icon(ModalDialog::Icon::Info)
      .button(okLabel, 1, true).cancelResult(0);
    d->onResult = [onDismiss = std::move(onDismiss)](int) { if (onDismiss) onDismiss(); };
    d->show(anchor);
}

// Warning confirm with a confirm + Cancel button. onConfirm fires only on confirm
// (Return); Cancel / Esc / click-outside do nothing.
inline void confirmAsync(juce::Component* anchor,
                         const juce::String& title,
                         const juce::String& message,
                         const juce::String& confirmLabel,
                         std::function<void()> onConfirm)
{
    auto* d = new ModalDialog();
    d->title(title).message(message).icon(ModalDialog::Icon::Warning)
      .button("Cancel", 0).button(confirmLabel, 1, true).cancelResult(0);
    d->onResult = [onConfirm = std::move(onConfirm)](int r) { if (r == 1 && onConfirm) onConfirm(); };
    d->show(anchor);
}

// Single-line text prompt. onOk receives the trimmed text (only on OK / Return).
inline void promptTextAsync(juce::Component* anchor,
                            const juce::String& title,
                            const juce::String& prompt,
                            const juce::String& defaultText,
                            const juce::String& okLabel,
                            std::function<void(const juce::String&)> onOk)
{
    auto* d = new ModalDialog();

    auto editor = std::make_unique<juce::TextEditor>();
    editor->setFont(juce::Font(juce::FontOptions{}.withHeight(13.0f)));
    editor->setText(defaultText, false);
    editor->selectAll();
    auto* ed = editor.get();

    // Return submits, Esc cancels — the TextEditor consumes those keys itself, so wire
    // them to the dialog rather than relying on ModalDialog::keyPressed.
    juce::Component::SafePointer<ModalDialog> sp(d);
    ed->onReturnKey = [sp] { if (sp) sp->close(1); };
    ed->onEscapeKey = [sp] { if (sp) sp->close(0); };

    d->title(title).message(prompt).content(std::move(editor), 28)
      .button("Cancel", 0).button(okLabel, 1, true).cancelResult(0);
    d->onResult = [onOk = std::move(onOk), ed](int r)
    {
        if (r == 1 && onOk) onOk(ed->getText().trim());
    };
    d->show(anchor);
}

// "Close <product>?" prompt with Cancel / Save / Close. onClose() = close without an
// explicit save; onSaveAndClose() = save then close; Cancel / Esc do nothing (onCancel).
inline void confirmQuitAsync(juce::Component* anchor,
                             const juce::String& productName,
                             std::function<void()> onClose,
                             std::function<void()> onSaveAndClose,
                             std::function<void()> onCancel = {})
{
    auto* d = new ModalDialog();
    d->title("Close " + productName + "?")
      .message("Are you sure you want to close?")
      .icon(ModalDialog::Icon::Question)
      .button("Cancel", 0).button("Save", 2).button("Close", 1, true)
      .cancelResult(0);
    d->onResult = [onClose = std::move(onClose), onSaveAndClose = std::move(onSaveAndClose),
                   onCancel = std::move(onCancel)](int r)
    {
        if      (r == 1) { if (onClose)        onClose(); }
        else if (r == 2) { if (onSaveAndClose) onSaveAndClose(); }
        else             { if (onCancel)       onCancel(); }
    };
    d->show(anchor);
}

} // namespace mu_ui
