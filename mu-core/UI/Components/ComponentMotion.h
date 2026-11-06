#pragma once

#include <juce_animation/juce_animation.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <map>
#include <memory>

namespace mu_ui
{

// Moves and fades components over a short time, one tick per screen refresh (juce_animation, synchronised to the monitor through the owner
// component's peer). It replaces juce::ComponentAnimator with the same three actions: animate, cancel (optionally jumping to the end), and
// "is it still moving". The owner must be a component that is on screen whenever the animated ones are (the shell or the sidebar).
class ComponentMotion
{
public:
    explicit ComponentMotion(juce::Component& owner) : updater(&owner) {}

    // Takes `c` from where it is now to `bounds` and `alpha` in `ms`, with the family ease-in-out. `startAlpha` < 0 keeps the current alpha.
    void animate(juce::Component* c, juce::Rectangle<int> bounds, float alpha, int ms, float startAlpha = -1.0f)
    {
        if (c == nullptr) return;
        cancel(c, false);
        auto it = std::make_unique<Item>();
        it->comp = c;
        it->fromBounds = c->getBounds();
        it->toBounds = bounds;
        it->fromAlpha = startAlpha >= 0.0f ? startAlpha : c->getAlpha();
        it->toAlpha = alpha;
        Item* raw = it.get();
        it->animator = juce::ValueAnimatorBuilder{}
                           .withDurationMs((double) ms)
                           .withEasing(juce::Easings::createEaseInOut())
                           .withValueChangedCallback([raw](float t) { raw->apply(t); })
                           .build();
        updater.addAnimator(it->animator);
        it->animator.start();
        items[c] = std::move(it);
    }

    // Stops the movement of `c`; with `jumpToEnd` the component is put at its final bounds and alpha first.
    void cancel(juce::Component* c, bool jumpToEnd)
    {
        const auto found = items.find(c);
        if (found == items.end()) return;
        if (jumpToEnd) found->second->apply(1.0f);
        // Dropping the Animator is enough: the updater forgets an Animator nobody holds any more.
        items.erase(found);
    }

private:
    struct Item
    {
        juce::Component::SafePointer<juce::Component> comp;
        juce::Rectangle<int> fromBounds, toBounds;
        float fromAlpha = 1.0f, toAlpha = 1.0f;
        juce::Animator animator { juce::ValueAnimatorBuilder{}.build() };

        // Puts the component at progress t (0 = start, 1 = end); overshooting eases are fine, the rectangle just follows.
        void apply(float t)
        {
            auto* c = comp.getComponent();
            if (c == nullptr) return;
            const auto lerp = [t](int a, int b) { return a + (int) std::lround((double) (b - a) * (double) t); };
            if (fromBounds != toBounds)
                c->setBounds(lerp(fromBounds.getX(), toBounds.getX()), lerp(fromBounds.getY(), toBounds.getY()),
                             lerp(fromBounds.getWidth(), toBounds.getWidth()), lerp(fromBounds.getHeight(), toBounds.getHeight()));
            c->setAlpha(juce::jlimit(0.0f, 1.0f, fromAlpha + (toAlpha - fromAlpha) * t));
        }
    };

    juce::VBlankAnimatorUpdater updater;
    std::map<juce::Component*, std::unique_ptr<Item>> items;
};

} // namespace mu_ui
