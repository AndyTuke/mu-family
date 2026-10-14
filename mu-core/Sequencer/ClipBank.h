#pragma once

#include "MuLimits.h"

#include <juce_data_structures/juce_data_structures.h>

#include <array>
#include <cstdint>

namespace mu_core
{

// The stored clips of one layer: kClipsPerLayer slots, each holding an opaque layer-preset tree and an
// optional name. mu-core never looks inside a payload; the product that stored it reads it back.
//
// Threading: message thread only. The audio thread never touches a bank, so there is no lock.
//
// juce::ValueTree copies share their data, so every store and every read-out deep-copies; a caller can
// never alias a stored tree, and copying a bank copies the trees too.
class ClipBank
{
public:
    static constexpr int kCount = mu_limits::kClipsPerLayer;

    ClipBank() = default;
    ClipBank(const ClipBank& other) { copyFrom(other); }
    ClipBank& operator=(const ClipBank& other)
    {
        if (this != &other)
            copyFrom(other);
        return *this;
    }

    // Moves hand the trees over without a deep copy (a prepared full preset hands its bank to the layer).
    ClipBank(ClipBank&&) noexcept = default;
    ClipBank& operator=(ClipBank&&) noexcept = default;

    static bool isValidIndex(int index) noexcept { return index >= 0 && index < kCount; }

    // Stores a deep copy of `payload` in clip `index`. An invalid tree empties the clip instead.
    // Returns false (nothing changed) when `index` is out of range.
    bool store(int index, const juce::ValueTree& payload, const juce::String& name = {})
    {
        if (! isValidIndex(index))
            return false;
        if (! payload.isValid())
            return clear(index);
        clips[(size_t) index] = payload.createCopy();
        names[(size_t) index] = name;
        return true;
    }

    // A deep copy of the stored tree, or an invalid tree when the clip is empty or out of range.
    juce::ValueTree get(int index) const
    {
        return isValidIndex(index) ? clips[(size_t) index].createCopy() : juce::ValueTree();
    }

    juce::String getName(int index) const
    {
        return isValidIndex(index) ? names[(size_t) index] : juce::String();
    }

    bool isOccupied(int index) const noexcept
    {
        return isValidIndex(index) && clips[(size_t) index].isValid();
    }

    bool clear(int index)
    {
        if (! isValidIndex(index))
            return false;
        clips[(size_t) index] = juce::ValueTree();
        names[(size_t) index] = juce::String();
        return true;
    }

    void clearAll()
    {
        for (int i = 0; i < kCount; ++i)
            clear(i);
    }

    // Bit i set = clip i holds a tree.
    uint8_t mask() const noexcept
    {
        static_assert(kCount <= 8, "the occupancy mask is one byte");
        uint8_t m = 0;
        for (int i = 0; i < kCount; ++i)
            if (clips[(size_t) i].isValid())
                m = (uint8_t) (m | (1u << i));
        return m;
    }

    bool isEmpty() const noexcept { return mask() == 0; }

    // Replaces any <Clips> child of `node` with this bank's: <Clips><Clip idx name>payload</Clip>...</Clips>.
    // An empty bank writes no child at all, so a file without clips stays byte-identical to before.
    void writeTo(juce::ValueTree& node) const
    {
        const auto old = node.getChildWithName(kClipsTag);
        if (old.isValid())
            node.removeChild(old, nullptr);

        if (isEmpty())
            return;

        juce::ValueTree list(kClipsTag);
        for (int i = 0; i < kCount; ++i)
        {
            if (! clips[(size_t) i].isValid())
                continue;
            juce::ValueTree clip(kClipTag);
            clip.setProperty(kIdxProp, i, nullptr);
            if (names[(size_t) i].isNotEmpty())
                clip.setProperty(kNameProp, names[(size_t) i], nullptr);
            clip.appendChild(clips[(size_t) i].createCopy(), nullptr);
            list.appendChild(clip, nullptr);
        }
        node.appendChild(list, nullptr);
    }

    // Replaces this bank with the <Clips> child of `node`. Tolerant: a missing child (or an invalid
    // node) gives an empty bank; a clip with an out-of-range idx or without a payload is skipped.
    void readFrom(const juce::ValueTree& node)
    {
        clearAll();

        const auto list = node.getChildWithName(kClipsTag);
        for (int c = 0; c < list.getNumChildren(); ++c)
        {
            const auto clip = list.getChild(c);
            if (! clip.hasType(kClipTag) || ! clip.hasProperty(kIdxProp))
                continue;
            const int idx = (int) clip.getProperty(kIdxProp);
            if (! isValidIndex(idx) || clip.getNumChildren() < 1)
                continue;
            store(idx, clip.getChild(0), clip.getProperty(kNameProp).toString());
        }
    }

private:
    inline static const juce::Identifier kClipsTag { "Clips" };
    inline static const juce::Identifier kClipTag  { "Clip" };
    inline static const juce::Identifier kIdxProp  { "idx" };
    inline static const juce::Identifier kNameProp { "name" };

    void copyFrom(const ClipBank& other)
    {
        for (int i = 0; i < kCount; ++i)
        {
            clips[(size_t) i] = other.clips[(size_t) i].createCopy();
            names[(size_t) i] = other.names[(size_t) i];
        }
    }

    std::array<juce::ValueTree, kCount> clips;
    std::array<juce::String, kCount>    names;
};

} // namespace mu_core
