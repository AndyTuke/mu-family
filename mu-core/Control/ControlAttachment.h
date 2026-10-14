#pragma once

#include "Control/ControlSink.h"
#include "Control/LiveView.h"
#include "Control/SurfaceLook.h"
#include <juce_core/juce_core.h>
#include <functional>
#include <memory>

// The device-neutral handle a controller manager (mu-control) offers the standalone shell and the Settings
// page: a status lamp, a label, and Release / Connect. mu-core owns only this interface and the factory
// slot below; mu-control fills the slot, so mu-core never includes it and no driver type is visible here.
namespace mu_core
{

// The lamp states of the Controller group (docs/design-ui-family.md, "Controller status and release").
enum class ControlStatus { NotFound, Found, InUse, Released, Lost };

// The read-only text beside the lamp.
inline juce::String controlStatusText(ControlStatus s)
{
    switch (s)
    {
        case ControlStatus::Found:    return "Found";
        case ControlStatus::InUse:    return "In use";
        case ControlStatus::Released: return "Released";
        case ControlStatus::Lost:     return "Lost";
        case ControlStatus::NotFound:
        default:                      return "Not found";
    }
}

class ControlAttachment
{
public:
    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void controlStatusChanged() = 0;   // message thread
        // The attachment is being destroyed: forget the pointer. The shell destroys it before the editor, so a
        // watcher in the editor must not call removeListener afterwards.
        virtual void controlAttachmentDestroyed() {}
    };

    virtual ~ControlAttachment() { listeners.call([](Listener& l) { l.controlAttachmentDestroyed(); }); }

    virtual ControlStatus getStatus() const = 0;
    virtual juce::String  getDeviceLabel() const = 0;   // the device's name, or empty when none
    virtual void          release() = 0;                // give the device back to the system
    virtual void          connect() = 0;                // take it again

    // The quit hook: give the device back at once (pads dark, its own mode) and stay closed. Harmless when
    // repeated, and the destructor does the same. The shell calls it ahead of anything slow on the way out.
    virtual void          closeNow() = 0;

    // Listening is not a change, so a holder of a const handle may register.
    void addListener(Listener* l) const    { listeners.add(l); }
    void removeListener(Listener* l) const { listeners.remove(l); }

protected:
    // Implementers call this (message thread) after the status or label changed.
    void notifyStatusChanged() { listeners.call([](Listener& l) { l.controlStatusChanged(); }); }

private:
    mutable juce::ListenerList<Listener> listeners;
};

// How the shell asks for a manager: the sink to act on, the state to read, the colours, and where to keep settings.
using ControlAttachmentFactory = std::function<std::unique_ptr<ControlAttachment>(
    ControlSink&, const LiveView&, const SurfaceLook&, const juce::File& settingsFolder)>;

// The slot mu-control fills from a namespace-scope initialiser. A function-local static, so there is no
// initialisation-order hazard; empty unless mu-control is linked.
inline ControlAttachmentFactory& controlAttachmentFactory()
{
    static ControlAttachmentFactory factory;
    return factory;
}

} // namespace mu_core
