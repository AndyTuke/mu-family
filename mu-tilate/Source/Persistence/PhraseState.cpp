#include "Persistence/PhraseState.h"

namespace mu_tilate
{

namespace
{
    // Every setting as a property of the slice's node; the names are short because a loop can have hundreds of slices.
    void writeSettings(juce::ValueTree& m, const SliceSettings& s)
    {
        const SliceSettings d;
        auto put = [&](const char* name, const auto& value, const auto& def) { if (value != def) m.setProperty(name, value, nullptr); };
        put("rev", s.reverse ? 1 : 0, 0);       put("lvl", s.levelDb, d.levelDb);          put("pan", s.pan, d.pan);
        put("fi", s.fadeInMs, d.fadeInMs);      put("fo", s.fadeOutMs, d.fadeOutMs);
        put("pit", s.pitchSemi, d.pitchSemi);   put("fine", s.fineCents, d.fineCents);
        put("co", s.cutoffOct, d.cutoffOct);    put("ro", s.resOffset, d.resOffset);
        put("fea", s.filterEnvAmt, d.filterEnvAmt);
        put("aa", s.ampA, d.ampA); put("ad", s.ampD, d.ampD); put("as", s.ampS, d.ampS); put("ar", s.ampR, d.ampR);
        put("fa", s.fltA, d.fltA); put("fd", s.fltD, d.fltD); put("fs", s.fltS, d.fltS); put("fr", s.fltR, d.fltR);
        put("tin", s.trimInBeats, 0.0f); put("rt", s.retime, 1.0f); put("vol", s.volume, 1.0f); put("ei", s.envIn, 0.0f); put("eo", s.envOut, 0.0f); put("io0", s.insOff[0], 0.0f); put("io1", s.insOff[1], 0.0f); put("io2", s.insOff[2], 0.0f); put("io3", s.insOff[3], 0.0f);
        put("se0", s.sendOff[0], 0.0f); put("se1", s.sendOff[1], 0.0f); put("se2", s.sendOff[2], 0.0f);
        put("pf", s.preFade ? 1 : 0, 0);       put("pfl", s.preLevelDb, d.preLevelDb);
        put("pfn", s.preLengthMs, d.preLengthMs); put("pft", s.preTone, d.preTone);
        put("one", s.oneShot ? 1 : 0, 1);     put("chk", s.chokeGroup, d.chokeGroup);
    }

    SliceSettings readSettings(const juce::ValueTree& m)
    {
        SliceSettings s;
        auto f = [&](const char* name, float def) { return (float) (double) m.getProperty(name, (double) def); };
        s.reverse = (int) m.getProperty("rev", 0) != 0;
        s.levelDb = f("lvl", s.levelDb);  s.pan = f("pan", s.pan);
        s.fadeInMs = f("fi", s.fadeInMs);  s.fadeOutMs = f("fo", s.fadeOutMs);
        s.pitchSemi = f("pit", s.pitchSemi);  s.fineCents = f("fine", s.fineCents);
        s.cutoffOct = f("co", s.cutoffOct);  s.resOffset = f("ro", s.resOffset);  s.filterEnvAmt = f("fea", s.filterEnvAmt);
        s.ampA = f("aa", s.ampA); s.ampD = f("ad", s.ampD); s.ampS = f("as", s.ampS); s.ampR = f("ar", s.ampR);
        s.fltA = f("fa", s.fltA); s.fltD = f("fd", s.fltD); s.fltS = f("fs", s.fltS); s.fltR = f("fr", s.fltR);
        s.trimInBeats = f("tin", 0.0f); s.retime = f("rt", 1.0f); s.volume = f("vol", 1.0f); s.envIn = f("ei", 0.0f); s.envOut = f("eo", 0.0f);
        for (int i = 0; i < 4; ++i) s.insOff[i] = f(("io" + juce::String(i)).toRawUTF8(), 0.0f);
        for (int i = 0; i < 3; ++i) s.sendOff[i] = f(("se" + juce::String(i)).toRawUTF8(), 0.0f);
        s.preFade = (int) m.getProperty("pf", 0) != 0;
        s.preLevelDb = f("pfl", s.preLevelDb);  s.preLengthMs = f("pfn", s.preLengthMs);  s.preTone = f("pft", s.preTone);
        s.oneShot = (int) m.getProperty("one", 1) != 0;
        s.chokeGroup = (int) m.getProperty("chk", 0);
        return sanitised(s);   // a damaged file must not hand the engine a NaN
    }
}

void writePhraseState(juce::ValueTree& node, const PhraseState& s)
{
    node.removeChild(node.getChildWithName(kPhraseNodeTag), nullptr);

    juce::ValueTree p(kPhraseNodeTag);
    p.setProperty("loopPath", s.loopPath, nullptr);
    p.setProperty("loopName", s.loopName, nullptr);
    p.setProperty("loopLength", s.loopLength, nullptr);
    p.setProperty("regionStart", s.regionStart, nullptr);
    p.setProperty("regionEnd", s.regionEnd, nullptr);
    p.setProperty("bars", s.timeline.lengthBeats() / 4.0, nullptr);

    juce::ValueTree slices("Slices");
    for (int i = 0; i < s.slices.size(); ++i)
    {
        juce::ValueTree m("M");
        m.setProperty("p", s.slices.marker(i).position, nullptr);
        m.setProperty("pin", s.slices.marker(i).pinned ? 1 : 0, nullptr);
        if (s.slices.marker(i).name.isNotEmpty()) m.setProperty("nm", s.slices.marker(i).name, nullptr);
        writeSettings(m, s.slices.settings(i));
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
        writeSettings(v, b.own);   // each hit's own sound
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
    s.regionStart = juce::jmax(0, (int) p.getProperty("regionStart", 0));
    s.regionEnd = juce::jmax(0, (int) p.getProperty("regionEnd", 0));

    s.timeline = Timeline((double) p.getProperty("bars", 4.0) * 4.0);
    for (const auto& v : p.getChildWithName("Blocks"))
    {
        const int slice = (int) v.getProperty("i", 0);   // -1: the slice was cropped off
        s.timeline.place(std::max(0, slice), (double) v.getProperty("s", 0.0), 0.0, readSettings(v));
        const int at = s.timeline.indexAt((double) v.getProperty("s", 0.0));
        if (at >= 0)
        {
            s.timeline.setBlockLength(at, (double) v.getProperty("l", 0.0), 0.0);
            if (slice < 0) s.timeline.setSlice(at, -1);
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
    {
        const int idx = s.slices.indexAt((int) m.getProperty("p", 0));
        if (idx < 0 || s.slices.marker(idx).position != (int) m.getProperty("p", 0)) continue;
        if ((int) m.getProperty("pin", 0) != 0) s.slices.setPinned(idx, true);
        s.slices.setSettings(idx, readSettings(m));
        s.slices.setName(idx, m.getProperty("nm", "").toString());
    }
    return s;
}

} // namespace mu_tilate
