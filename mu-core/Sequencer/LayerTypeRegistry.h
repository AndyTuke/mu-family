#pragma once

#include "Sequencer/LayerTypeInfo.h"

#include <juce_core/juce_core.h>

#include <vector>

// The list of layer types a host offers (docs/mu-tronic/design.md 2.5). A host that lets the user choose a
// slot's type (mu-Tronic) fills one in its constructor, one line per type; a product with a single layer type
// leaves it empty. Empty is not a registered type: every slot can hold it, so the host supplies it through
// ProcessorBase::createLayer. Message thread only; it describes and constructs, it never dispatches.
namespace mu_core
{
    class LayerTypeRegistry
    {
    public:
        // Adds `info`. Refused (false) for the Empty id, a type already registered, or one with no create function.
        bool add(const LayerTypeInfo& info)
        {
            if (info.id == LayerTypeId::Empty || info.create == nullptr || find(info.id) != nullptr)
                return false;
            types.push_back(info);
            return true;
        }

        // The entry for `id`, or null when the host does not offer it (Empty included).
        const LayerTypeInfo* find(LayerTypeId id) const noexcept
        {
            for (const auto& t : types)
                if (t.id == id) return &t;
            return nullptr;
        }

        int size() const noexcept { return (int) types.size(); }
        bool isEmpty() const noexcept { return types.empty(); }
        const LayerTypeInfo& at(int i) const { return types[(size_t) i]; }

        // The distinct `group` names in the order they were first registered: the chooser's bands.
        juce::StringArray groups() const
        {
            juce::StringArray out;
            for (const auto& t : types) out.addIfNotAlreadyThere(juce::String::fromUTF8(t.group));
            return out;
        }

        // The entries of one group, in registration order.
        std::vector<const LayerTypeInfo*> inGroup(const juce::String& group) const
        {
            std::vector<const LayerTypeInfo*> out;
            for (const auto& t : types)
                if (juce::String::fromUTF8(t.group) == group) out.push_back(&t);
            return out;
        }

    private:
        std::vector<LayerTypeInfo> types;
    };
}
