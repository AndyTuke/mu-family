#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

// Shared confirmation dialogs for the family — one implementation so every
// product's "are you sure?" prompts look + behave identically.
namespace mu_ui {

// Async warning confirm with a confirm + Cancel button. Calls onConfirm() only
// if the user clicks the confirm button (Return). Cancel / Escape do nothing.
inline void confirmAsync(const juce::String& title,
                         const juce::String& message,
                         const juce::String& confirmLabel,
                         std::function<void()> onConfirm)
{
    auto* w = new juce::AlertWindow(title, message, juce::MessageBoxIconType::WarningIcon);
    w->addButton(confirmLabel, 1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton("Cancel",     0, juce::KeyPress(juce::KeyPress::escapeKey));
    w->enterModalState(true, juce::ModalCallbackFunction::create(
        [onConfirm = std::move(onConfirm)](int result)
        {
            if (result == 1 && onConfirm) onConfirm();
        }), true);   // deleteWhenDismissed
}

// Async "Close <product>?" prompt with OK / Save / Cancel. onClose() = quit
// without an explicit save; onSaveAndClose() = save then quit. Cancel does
// nothing. Used by each product's standalone close handler.
inline void confirmQuitAsync(const juce::String& productName,
                             std::function<void()> onClose,
                             std::function<void()> onSaveAndClose,
                             std::function<void()> onCancel = {})
{
    auto opts = juce::MessageBoxOptions()
        .withIconType(juce::MessageBoxIconType::QuestionIcon)
        .withTitle("Close " + productName + "?")
        .withMessage("Are you sure you want to close?")
        .withButton("OK")        // result 1
        .withButton("Save")      // result 2
        .withButton("Cancel");   // result 0
    juce::AlertWindow::showAsync(opts,
        [onClose = std::move(onClose), onSaveAndClose = std::move(onSaveAndClose),
         onCancel = std::move(onCancel)](int result)
        {
            if      (result == 1) { if (onClose)        onClose(); }
            else if (result == 2) { if (onSaveAndClose) onSaveAndClose(); }
            else                  { if (onCancel)       onCancel(); }
        });
}

} // namespace mu_ui
