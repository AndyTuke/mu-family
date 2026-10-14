#pragma once

#include <functional>

// What the standalone does when the quit prompt is answered. JUCE-core free and header-only, so the order
// can be tested without a window: the control surface is given back first on Close and Save (a slow save must
// never leave a pad in its host mode), and Cancel touches nothing.
namespace mu_standalone
{

struct QuitFlow
{
    std::function<void()> giveBackDevices;                              // the quit hook of the control surface
    std::function<void(std::function<void()> thenQuit)> saveAndQuit;    // the processor's save-and-quit, empty if none
    std::function<void()> saveState;                                    // fallback save when there is no saveAndQuit
    std::function<void()> quit;

    void close() const
    {
        if (giveBackDevices) giveBackDevices();
        if (quit) quit();
    }

    void save() const
    {
        if (giveBackDevices) giveBackDevices();
        if (saveAndQuit) { saveAndQuit(quit); return; }
        if (saveState) saveState();
        if (quit) quit();
    }

    void cancel() const {}
};

} // namespace mu_standalone
