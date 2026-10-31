#include "Persistence/PhraseState.h"

namespace mu_tilate
{

void writePhraseState(juce::ValueTree& node, const PhraseState& s)
{
    node.removeChild(node.getChildWithName(kPhraseNodeTag), nullptr);

    juce::ValueTree p(kPhraseNodeTag);
    p.setProperty("loopPath", s.loopPath, nullptr);
    p.setProperty("loopName", s.loopName, nullptr);
    p.setProperty("loopLength", s.loopLength, nullptr);
    p.setProperty("bars", s.timeline.lengthBeats() / 4.0, nullptr);

    juce::ValueTree slices("Slices");
    for (int i = 0; i < s.slices.size(); ++i)
    {
        juce::ValueTree m("M");
        m.setProperty("p", s.slices.marker(i).position, nullptr);
        m.setProperty("pin", s.slices.marker(i).pinned ? 1 : 0, nullptr);
        slices.appendChild(m, nullptr);
    }
    p.appendChild(slices, nullptr);

    juce::ValueTree blocks("Blocks");
    for (const auto& b : s.timeline.blocks())
    {
        juce::ValueTree v("B");
        v.setProperty("s", b.start, nullptr);
        v.setProperty("l", b.length, nullptr);
        v.setProperty("i", b.slice, nullptr);
        v.setProperty("r", b.reverse ? 1 : 0, nullptr);
        blocks.appendChild(v, nullptr);
    }
    p.appendChild(blocks, nullptr);

    node.appendChild(p, nullptr);
}

PhraseState readPhraseState(const juce::ValueTree& node)
{
    PhraseState s;
    const auto p = node.getChildWithName(kPhraseNodeTag);
    if (! p.isValid()) return s;

    s.loopPath   = p.getProperty("loopPath").toString();
    s.loopName   = p.getProperty("loopName").toString();
    s.loopLength = juce::jmax(0, (int) p.getProperty("loopLength", 0));

    s.timeline = Timeline((double) p.getProperty("bars", 4.0) * 4.0);
    for (const auto& v : p.getChildWithName("Blocks"))
    {
        s.timeline.place((int) v.getProperty("i", 0), (double) v.getProperty("s", 0.0), 0.0);
        const int at = s.timeline.indexAt((double) v.getProperty("s", 0.0));
        if (at >= 0)
        {
            s.timeline.setBlockLength(at, (double) v.getProperty("l", 0.0), 0.0);
            s.timeline.setReverse(at, (int) v.getProperty("r", 0) != 0);
        }
    }

    // The markers are only meaningful against the loop they were made on; a saved length of 0 means no loop was
    // loaded, and the list is then just the marker at 0.
    s.slices = SliceList(s.loopLength);
    std::vector<int> positions;
    for (const auto& m : p.getChildWithName("Slices"))
        positions.push_back((int) m.getProperty("p", 0));
    s.slices.applyDetected(positions, 1);
    for (const auto& m : p.getChildWithName("Slices"))
        if ((int) m.getProperty("pin", 0) != 0)
        {
            const int idx = s.slices.indexAt((int) m.getProperty("p", 0));
            if (idx >= 0 && s.slices.marker(idx).position == (int) m.getProperty("p", 0)) s.slices.setPinned(idx, true);
        }
    return s;
}

} // namespace mu_tilate
