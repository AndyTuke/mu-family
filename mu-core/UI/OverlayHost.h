#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "UI/ModalCard.h"   // shared dim backdrop

#include <functional>
#include <memory>

// Hosts a content component as a dimmed in-editor overlay, centred at the content's own size,
// over the anchor's enclosing AudioProcessorEditor (so a DAW dims the editor, not its window).
// Click outside the content dismisses. Self-owning: deletes itself after dismiss.
//
// Use this for RICH modals that bring their own controls (e.g. a sample browser with its own
// Load/Cancel). For simple confirm / prompt / message dialogs use mu_ui::ModalDialog instead.
namespace mu_ui {

class OverlayHost : public juce::Component
{
public:
    std::function<void()> onDismiss;

    // Takes ownership of `content` and shows the overlay over anchor's editor. The content
    // should have set its own size (the overlay centres it). Self-owning — do not delete.
    void show(juce::Component* anchor, std::unique_ptr<juce::Component> contentToOwn)
    {
        content = std::move(contentToOwn);
        if (content != nullptr) addAndMakeVisible(*content);

        juce::Component* host = nullptr;
        for (auto* c = anchor; c != nullptr; c = c->getParentComponent())
            if (dynamic_cast<juce::AudioProcessorEditor*>(c) != nullptr) { host = c; break; }
        if (host == nullptr && anchor != nullptr) host = anchor->getTopLevelComponent();
        if (host == nullptr) { delete this; return; }

        host->addAndMakeVisible(this);
        setBounds(host->getLocalBounds());
        setAlwaysOnTop(true);
        toFront(true);
        resized();
    }

    void dismiss()
    {
        if (finished) return;
        finished = true;
        juce::Component::SafePointer<OverlayHost> self(this);
        if (auto* p = getParentComponent()) p->removeChildComponent(this);
        if (onDismiss) onDismiss();
        juce::MessageManager::callAsync([self] { if (auto* o = self.getComponent()) delete o; });
    }

    void paint(juce::Graphics& g) override { fillModalDim(g); }

    void resized() override
    {
        if (content != nullptr)
            content->setBounds(juce::Rectangle<int>(0, 0, content->getWidth(), content->getHeight())
                                   .withCentre(getLocalBounds().getCentre()));
    }

    void parentSizeChanged() override
    {
        if (auto* p = getParentComponent()) setBounds(p->getLocalBounds());
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (content != nullptr && ! content->getBounds().contains(e.getPosition()))
            dismiss();
    }

private:
    std::unique_ptr<juce::Component> content;
    bool finished = false;
};

} // namespace mu_ui
