#pragma once

#include "LayerTypeInfo.h"

#include <juce_data_structures/juce_data_structures.h>

namespace mu_core
{

// The one clip clipboard of the process: Copy / Cut put a clip here, Paste reads it. It names no processor, so every
// instance in a host process shares it (copy in one plugin, paste in another) and a removed layer cannot dangle it.
// It does not touch the OS clipboard; clipClipboard() is the seam if that is ever added.
//
// Threading: message thread only, like the clip bank it feeds.
class ClipClipboard
{
public:
    // Holds a deep copy of `payload` (a layer-preset tree, root tag included) with its name and kind.
    // An invalid tree empties the clipboard instead.
    void set(const juce::ValueTree& payload, const juce::String& clipName, LayerTypeId typeId = LayerTypeId::Empty)
    {
        if (! payload.isValid())
        {
            clear();
            return;
        }
        tree     = payload.createCopy();
        name     = clipName;
        rootTag  = payload.getType().toString();
        layerType = typeId;
    }

    bool hasClip() const noexcept { return tree.isValid(); }

    // A deep copy, so a paste can never alias (or edit) the clipboard's own tree. Invalid when empty.
    juce::ValueTree get() const { return tree.createCopy(); }

    const juce::String& getName() const noexcept { return name; }

    // The payload kind: the root tag, and for a typed slot host the layer type the tree was copied from.
    const juce::String& getRootTag() const noexcept { return rootTag; }
    LayerTypeId         getLayerType() const noexcept { return layerType; }

    void clear()
    {
        tree      = juce::ValueTree();
        name      = juce::String();
        rootTag   = juce::String();
        layerType = LayerTypeId::Empty;
    }

private:
    juce::ValueTree tree;
    juce::String    name;
    juce::String    rootTag;
    LayerTypeId     layerType = LayerTypeId::Empty;
};

// A function-local static, so there is no static-initialisation order hazard.
inline ClipClipboard& clipClipboard()
{
    static ClipClipboard board;
    return board;
}

} // namespace mu_core
