#pragma once

#include "Control/ControlAttachment.h"
#include <functional>

// Follows the shell's controller attachment from a piece of UI. The shell attaches it after the editor exists and
// destroys it before the editor goes, so the UI re-reads the pointer (update) and is told when it dies; it never
// holds a stale one. Message thread only.
namespace mu_core
{

class AttachmentWatch : private ControlAttachment::Listener
{
public:
    // Called after the watched attachment changed its status, or was replaced or destroyed.
    std::function<void()> onChange;

    AttachmentWatch() = default;
    ~AttachmentWatch() override { detach(); }

    AttachmentWatch(const AttachmentWatch&) = delete;
    AttachmentWatch& operator=(const AttachmentWatch&) = delete;

    ControlAttachment* get() const noexcept { return watched; }

    // Follows `now` (null = none). Returns true when that changed what is watched.
    bool update(ControlAttachment* now)
    {
        if (now == watched) return false;
        detach();
        watched = now;
        if (watched != nullptr) watched->addListener(this);
        return true;
    }

private:
    void detach()
    {
        if (watched != nullptr) watched->removeListener(this);
        watched = nullptr;
    }

    void controlStatusChanged() override { if (onChange) onChange(); }
    void controlAttachmentDestroyed() override { watched = nullptr; if (onChange) onChange(); }

    ControlAttachment* watched = nullptr;
};

} // namespace mu_core
