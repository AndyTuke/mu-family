#pragma once

#include "Persistence/PerformanceFile.h"
#include <juce_core/juce_core.h>
#include <cstdint>

// What the shell and the surface drivers may READ about the live state (Performance mode): the loaded
// performance, which item plays and which is queued, the selected layer. Read-only, message thread only;
// listeners are told on the message thread after any change. The owner (mu_core::LiveState) changes it
// through ControlActions, never through this view.
namespace mu_core
{

class LiveView
{
public:
    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void liveStateChanged() = 0;   // message thread
    };

    virtual ~LiveView() = default;

    virtual bool                          isPerformanceMode() const noexcept = 0;
    virtual const mu_pp::PerformanceFile& getPerformance()    const noexcept = 0;   // empty until one is loaded
    virtual juce::File                    getPerformanceFile() const = 0;
    virtual int                           getCurrentItem()    const noexcept = 0;   // -1 = none yet
    virtual int                           getQueuedItem()     const noexcept = 0;   // -1 = nothing queued
    virtual int                           getSelectedLayer()  const noexcept = 0;
    virtual int                           getLayerCount()     const = 0;            // how many layers are live
    virtual int                           getLayerColourIndex(int layer) const = 0;   // index into the shared layer palette
    virtual bool                          isItemPlayable(int item) const = 0;           // this product's, and its file exists
    virtual uint32_t                      getRevision()       const noexcept = 0;   // bumped on every change

    // Listening is not a change of the state, so a holder of the read-only view may register.
    virtual void addListener(Listener* l)    const = 0;
    virtual void removeListener(Listener* l) const = 0;
};

} // namespace mu_core
