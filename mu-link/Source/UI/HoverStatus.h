#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

// HoverStatus — reports a short name and value to the status bar while the pointer is over a
// component that has no status line of its own (a segment control, a number field). The owner
// declares it after the components it watches, so it is destroyed first and detaches cleanly.
namespace mu_link
{

class HoverStatus : private juce::MouseListener
{
public:
    std::function<void(const juce::String& name, const juce::String& value)> onStatus;

    HoverStatus() = default;
    ~HoverStatus() override
    {
        for (auto& w : watched)
            if (auto* c = w.component.getComponent())
                c->removeMouseListener(this);
    }

    // Report `name` and `value` whenever the pointer enters `component`.
    void watch(juce::Component& component, const juce::String& name, const juce::String& value)
    {
        watched.push_back({ &component, name, value });
        component.addMouseListener(this, false);
    }

    // Changes the value a watched component reports (e.g. the clock source's current mode).
    void setValue(juce::Component& component, const juce::String& value)
    {
        for (auto& w : watched)
            if (w.component.getComponent() == &component)
                w.value = value;
    }

private:
    struct Watched
    {
        juce::Component::SafePointer<juce::Component> component;
        juce::String name, value;
    };

    void mouseEnter(const juce::MouseEvent& e) override
    {
        if (! onStatus) return;
        for (const auto& w : watched)
            if (w.component.getComponent() == e.eventComponent)
            {
                onStatus(w.name, w.value);
                return;
            }
    }

    std::vector<Watched> watched;
};

} // namespace mu_link
