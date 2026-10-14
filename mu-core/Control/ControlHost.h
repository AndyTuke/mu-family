#pragma once

#include "Control/ControlAttachment.h"
#include "Plugin/ShellBackend.h"
#include <functional>
#include <memory>

// Owns the standalone shell's controller attachment and the order it is given up in. Header-only and JUCE-core
// only, so the shell and mu-core-tests share it. It names no mu-control type: the attachment comes from the
// factory slot, which stays empty unless mu-control is linked into the executable.
namespace mu_core
{

class ControlHost
{
public:
    // Backstop: the processor never keeps a pointer to an attachment that is about to die.
    ~ControlHost() { shutdown(); }

    // Builds the attachment when the slot is filled and the backend runs Performance mode. `publish` hands the
    // non-owning pointer to the processor (and nullptr on the way out). Windowed runs only: a headless render
    // must never reach a MIDI port, so the shell does not call this there. Returns true when one was built.
    bool start(ShellBackend& backend, const SurfaceLook& look, const juce::File& settingsFolder,
               std::function<void(ControlAttachment*)> publish)
    {
        publishPointer = std::move(publish);
        const auto& factory = controlAttachmentFactory();
        if (! factory || ! backend.supportsPerformanceMode()) return false;

        auto* sink = backend.getControlSink();
        auto* view = backend.getLiveView();
        if (sink == nullptr || view == nullptr) return false;

        attachment = factory(*sink, *view, look, settingsFolder);
        if (attachment != nullptr && publishPointer) publishPointer(attachment.get());
        return attachment != nullptr;
    }

    // The quit hook: the device goes back to its own mode now. Safe to repeat and with no attachment.
    void closeNow()
    {
        if (attachment != nullptr) attachment->closeNow();
    }

    // The last step of shutdown: device back first, then the processor forgets the pointer, then it is destroyed.
    void shutdown()
    {
        if (attachment == nullptr) return;
        closeNow();
        if (publishPointer) publishPointer(nullptr);
        attachment.reset();
    }

    ControlAttachment* get() const noexcept { return attachment.get(); }

private:
    std::unique_ptr<ControlAttachment> attachment;
    std::function<void(ControlAttachment*)> publishPointer;
};

} // namespace mu_core
