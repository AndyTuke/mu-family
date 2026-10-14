#include "Audio/SpinLock.h"   // mu-core: spin lock helpers
#include "Plugin/PluginProcessor.h"
#include "Modulation/MuOnModDest.h"           // isValidEngineDest
#include "Modulation/ModulatorSerialise.h"    // mu-core: modulator (de)serialise
#include "Persistence/PresetFiles.h"          // mu-core: shared preset-file handling

// mu-On preset I/O on the family composed state (mu-core LayerState): full presets (.muOn),
// per-track presets (.muTrack) and host sessions all build / apply the same track node. Split out of
// PluginProcessor.cpp to mirror mu-Tant's PluginProcessor_Preset.cpp.

namespace mu_on
{

namespace
{
    constexpr const char* kTrackPresetTag = "MuOnTrack";

    // The legacy five-lane colours: Kick red / Bass orange / Hat cyan / Snare magenta / Rumble lime, the
    // palette entries the lanes had, then the three unused slots.
    constexpr const char* kLegacyLaneColours = "0,2,1,3,4,5,6,7";

    // A flat full-level envelope, one bar long: what a track that has not drawn one carries.
    void setDefaultEnvelope(ControlSequence& env)
    {
        env.mode           = ControlSequence::Mode::Smooth;
        env.polarity       = ControlSequence::Polarity::Unipolar;
        env.loopNoteValue  = NoteValue::Quarter;
        env.loopNoteMod    = NoteMod::None;
        env.loopMultiplier = 4;   // 1 bar (4 beats) so it cycles once per bar
        env.curvePoints    = { { 0.0f, 1.0f }, { 1.0f, 1.0f } };
    }

    // The Rumble volume envelope — its length (note value × multiplier, as a modulator's Loop) and
    // curve points — as a <RumbleEnv> node.
    juce::ValueTree envelopeNode(const ControlSequence& env)
    {
        juce::ValueTree node("RumbleEnv");
        node.setProperty("loopNV",   mu_pp::enumName(mu_audio::kNoteValueNames, (int) env.loopNoteValue), nullptr);
        node.setProperty("loopMod",  mu_pp::enumName(mu_audio::kNoteModNames,   (int) env.loopNoteMod),   nullptr);
        node.setProperty("loopMult", env.loopMultiplier, nullptr);
        for (const auto& p : env.curvePoints)
        {
            juce::ValueTree pt("P");
            pt.setProperty("x", p.x, nullptr);
            pt.setProperty("y", p.y, nullptr);
            node.addChild(pt, -1, nullptr);
        }
        return node;
    }

    // Put an older track node into the engine-qualified shape: every parameter row `x` becomes
    // `<engine>_x`, and the engine (and for Rumble its source, the first Kick) is written as rows.
    void qualifyLegacyNode(juce::ValueTree& node, EngineType engine)
    {
        for (int i = 0; i < node.getNumChildren(); ++i)
            if (auto row = node.getChild(i); row.hasType(mu_pp::kRowTag))
                row.setProperty("id", engineParamSuffix(engine, row.getProperty("id").toString()), nullptr);

        juce::ValueTree eng(mu_pp::kRowTag);
        eng.setProperty("id", "eng", nullptr);
        eng.setProperty("x", toIndex(engine), nullptr);
        eng.setProperty("c", engineName(engine), nullptr);
        node.appendChild(eng, nullptr);

        if (engine == EngineType::Rumble)
        {
            juce::ValueTree src(mu_pp::kRowTag);
            src.setProperty("id", "src", nullptr);
            src.setProperty("x", 0, nullptr);
            node.appendChild(src, nullptr);
        }
    }

    bool hasEngineRow(const juce::ValueTree& node)
    {
        for (int i = 0; i < node.getNumChildren(); ++i)
            if (const auto row = node.getChild(i); row.hasType(mu_pp::kRowTag) && row.getProperty("id").toString() == "eng")
                return true;
        return false;
    }
}

Track::Track()
{
    setDefaultEnvelope(rumbleEnv);
}

void Track::resetRumbleEnv()
{
    mu_core::spinLock(rumbleEnvLock);
    setDefaultEnvelope(rumbleEnv);
    mu_core::spinUnlock(rumbleEnvLock);
}

bool Track::rumbleEnvIsDefault() const
{
    ControlSequence ref;
    setDefaultEnvelope(ref);
    mu_core::spinLock(rumbleEnvLock);
    bool same = rumbleEnv.loopNoteValue == ref.loopNoteValue && rumbleEnv.loopNoteMod == ref.loopNoteMod
             && rumbleEnv.loopMultiplier == ref.loopMultiplier && rumbleEnv.curvePoints.size() == ref.curvePoints.size();
    for (size_t i = 0; same && i < ref.curvePoints.size(); ++i)
        same = rumbleEnv.curvePoints[i].x == ref.curvePoints[i].x && rumbleEnv.curvePoints[i].y == ref.curvePoints[i].y;
    mu_core::spinUnlock(rumbleEnvLock);
    return same;
}

// A track saves its step row always (the row is kept when the engine changes) and its Rumble envelope
// only when it differs from the default.
void Track::writeExtras(juce::ValueTree& node) const
{
    node.appendChild(row.serialise(slot), nullptr);
    if (! rumbleEnvIsDefault())
    {
        mu_core::spinLock(rumbleEnvLock);
        node.appendChild(envelopeNode(rumbleEnv), nullptr);
        mu_core::spinUnlock(rumbleEnvLock);
    }
    Layer::writeExtras(node);
}

bool Track::isValidDest(const std::string& id) const { return isValidEngineDest(owner.ptr->engineOf(slot), id); }

// An absent <Track> clears the row and an absent envelope puts the default back, so a sparse or older
// node never leaves the previous tenant's data behind.
void Track::applyExtras(const juce::ValueTree& node)
{
    row.deserialise(node.getChildWithName("Track"));

    const auto env = node.getChildWithName("RumbleEnv");
    std::vector<ControlSequence::CurvePoint> pts;
    for (int i = 0; i < env.getNumChildren(); ++i)
    {
        const auto p = env.getChild(i);
        ControlSequence::CurvePoint cp;
        cp.x = (float) p.getProperty("x", 0.0f);
        cp.y = (float) p.getProperty("y", 0.0f);
        pts.push_back(cp);
    }
    if (pts.size() < 2)
        resetRumbleEnv();
    else
    {
        mu_core::spinLock(rumbleEnvLock);
        rumbleEnv.curvePoints    = std::move(pts);
        rumbleEnv.loopNoteValue  = (NoteValue) mu_pp::readEnumIndex(env, "loopNV",  mu_audio::kNoteValueNames, (int) NoteValue::Quarter);
        rumbleEnv.loopNoteMod    = (NoteMod)   mu_pp::readEnumIndex(env, "loopMod", mu_audio::kNoteModNames,   (int) NoteMod::None);
        rumbleEnv.loopMultiplier = juce::jmax(1, (int) env.getProperty("loopMult", 4));
        mu_core::spinUnlock(rumbleEnvLock);
    }
    Layer::applyExtras(node);
}

// Describe the tracks once (ctor): each track's param prefix; its step row, Rumble envelope and
// modulators save through Track. Every track preset, full preset and host session is built and
// applied from this.
void PluginProcessor::setupLayerState()
{
    juce::StringArray prefixes;
    for (int i = 0; i < mu_limits::kMaxLayers; ++i) prefixes.add(layerParamPrefix(i));
    initLayerState(prefixes, legacyLaneParamPrefixes());   // older APVTS dumps used "k_ b_ h_ s_ r_"
}

// Any saved state (host session or full preset, either format) in the composed shape. Older
// states kept the grid as one <Pattern> and the envelope at the root (each moves into its lane),
// and the oldest called each lane's modulator node <Lane>. A state from before the layers were
// variable (it has no layer count) held five lanes named by instrument: lane i becomes layer i
// running engine i, with its parameter rows renamed engine-qualified.
juce::ValueTree PluginProcessor::toLayerState(const juce::ValueTree& tree) const
{
    juce::ValueTree source = tree;
    if (auto vd = tree.getChildWithName(mu_pp::kChannelDataTag); vd.isValid() && vd.getChildWithName("Lane").isValid())
    {
        source = tree.createCopy();
        auto data = source.getChildWithName(mu_pp::kChannelDataTag);
        for (int i = 0; i < data.getNumChildren(); ++i)
            if (data.getChild(i).hasType("Lane"))
            {
                juce::ValueTree node(mu_pp::kChannelNodeTag);
                node.copyPropertiesAndChildrenFrom(data.getChild(i), nullptr);
                data.removeChild(i, nullptr);
                data.addChild(node, i, nullptr);
            }
    }

    auto state = toComposedState(source, [](const juce::ValueTree& child, juce::ValueTree& composed)
    {
        if (child.hasType("Pattern"))
        {
            for (int i = 0; i < child.getNumChildren(); ++i)
                if (const auto row = child.getChild(i); row.hasType("Track"))
                    if (auto node = mu_pp::findLayerNode(composed, (int) row.getProperty("i", -1)); node.isValid())
                        node.appendChild(row.createCopy(), nullptr);
        }
        else if (child.hasType("RumbleEnv"))
        {
            if (auto node = mu_pp::findLayerNode(composed, toIndex(EngineType::Rumble)); node.isValid())
                node.appendChild(child.createCopy(), nullptr);
        }
    });

    if (! state.hasProperty("numVoices"))
    {
        auto slots = state.getChildWithName(mu_pp::kLayersTag);
        for (int lane = 0; lane < kNumLegacyLanes; ++lane)
        {
            auto node = mu_pp::findLayerNode(state, lane);
            if (! node.isValid())
            {
                node = juce::ValueTree(mu_pp::kLayerTag);
                node.setProperty("idx", lane, nullptr);
                slots.appendChild(node, nullptr);
            }
            qualifyLegacyNode(node, engineFromIndex(lane));
        }
        state.setProperty("numVoices", kNumLegacyLanes, nullptr);
        state.setProperty("voiceColours", kLegacyLaneColours, nullptr);
    }
    return state;
}

// Apply a composed state (host restore, full-preset load or its pattern-wrap commit), then
// re-seed the mixer / FX engines.
void PluginProcessor::applyStateTree(const juce::ValueTree& state)
{
    applyComposedState(state);
    syncAllFxParams();   // re-seed mixer/FX (unchanged values skip listeners)
}

// While playing, stage a loaded full preset for the pattern's wrap (the shared swapper commit); while
// stopped, apply it now. Converted to the composed shape here, so the commit does no parsing.
void PluginProcessor::useLoadedFullPreset(juce::ValueTree state)
{
    layerSwapper.useFull(toLayerState(state));
}

// A track preset: the track's node (engine, every engine's param rows, step row, Rumble envelope,
// modulators). It carries its engine, so it loads into any track.
void PluginProcessor::saveLayerPreset(int track, const juce::String& name)
{
    if (track < 0 || track >= numLayers.load(std::memory_order_relaxed)) return;
    auto node = captureLayerNode(track, kTrackPresetTag);
    node.setProperty("engine", getLayerName(track), nullptr);
    if (auto xml = node.createXml())
        mu_pp::writeXmlAtomically(*xml, getLayerPresetDir().getChildFile(mu_pp::safePresetFileName(name, getLayerName(track))
                                                                           + "." + getLayerPresetExtension()),
                                  onLoadError);
}

void PluginProcessor::loadLayerPreset(int track, const juce::File& file)
{
    if (track < 0 || track >= numLayers.load(std::memory_order_relaxed) || ! file.existsAsFile()) return;
    auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr || ! xml->hasTagName(kTrackPresetTag))
    {
        if (onLoadError) onLoadError("Could not read \"" + file.getFileName() + "\"");
        return;
    }

    // A preset saved when the lanes were fixed has unqualified rows and names its instrument as "lane".
    auto node = juce::ValueTree::fromXml(*xml);
    if (! hasEngineRow(node))
    {
        const int e = engineNames().indexOf(node.getProperty("lane").toString());
        qualifyLegacyNode(node, engineFromIndex(juce::jmax(0, e)));
    }

    // While playing, stage it for the pattern's wrap (the shared swapper commit); while stopped, apply now.
    layerSwapper.useSlot(track, std::move(node));
}

// Apply a track node (the stopped load and the pattern-wrap commit), then tell the editor.
void PluginProcessor::applyTrackTree(int track, const juce::ValueTree& tree)
{
    applyLayerNode(track, tree);
    ensureRumbleSource(track);
    if (onLayerPresetCommitted) onLayerPresetCommitted(track);
}

// The track presets (a preset carries its engine, so every track lists the same ones).
juce::Array<juce::File> PluginProcessor::slotPresetFiles(int) const
{
    juce::Array<juce::File> files;
    for (const auto& f : mu_pp::listPresetFiles(getLayerPresetDir(), getLayerPresetExtension()))
        if (mu_pp::readPresetMeta(f).rootTag == kTrackPresetTag)
            files.add(f);
    return files;
}

// Reset a track: its engines' params back to defaults and its modulators cleared. The engine and the
// Rumble source are the track's identity, and the step row and envelope its pattern: a reset is a
// sound reset, so all four stay.
void PluginProcessor::resetLayer(int track)
{
    if (track < 0 || track >= mu_limits::kMaxLayers) return;
    cancelPendingSwap(track);   // a staged swap would re-fill what we're resetting

    // Rows for the two kept parameters make the apply write them back unchanged; every other
    // parameter of the track has no row and goes to its default.
    juce::ValueTree keep("Slot");
    for (const char* id : { "eng", "src" })
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*>(apvts.getParameter(layerParamId(track, id))))
            mu_pp::appendParamRow(keep, *p, id);
    layerLayout.applyParams(keep, track);
    mu_pp::clearModulators(getTrack(track));

    // Reset empties the clip bank as in the shared reset (this override replaces that one).
    if (layers[(size_t) track] != nullptr)
    {
        layers[(size_t) track]->clips->clearAll();
        layers[(size_t) track]->activeClip = -1;
    }
}

} // namespace mu_on
