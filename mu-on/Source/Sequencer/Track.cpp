#include "Sequencer/Track.h"
#include "Persistence/TrackParams.h"      // the t{N}_ ids
#include "Modulation/LaneModulation.h"    // mu-core: mu_mod::resolveLane
#include "Audio/SpinLock.h"               // mu-core: spin lock helpers
#include "Modulation/ModulatorSerialise.h"  // mu-core: the modulators the base saves
#include "Persistence/PresetFiles.h"      // mu-core: enum names in saved data

#include <limits>

// The Track layer's half of the render contract: the parameter binding and the per-block render that used to
// be mu-On's processor glue (GrooveVoices: engine ownership, modulation resolve, trigger, render).
// docs/mu-tronic/design.md 2.1.
namespace mu_on
{

namespace
{
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

bool Track::isValidDest(const std::string& id) const { return isValidEngineDest(engineSetting(), id); }

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

Track::~Track() = default;

void Track::prepare(double sampleRate, int blockSize)
{
    const juce::ScopedLock sl(engineBuild);
    preparedRate  = sampleRate > 0.0 ? sampleRate : 44100.0;
    preparedBlock = juce::jmax(1, blockSize);
    silence.setSize(2, preparedBlock);
    silence.clear();
    if (engine != nullptr) engine->prepare(preparedRate, preparedBlock);
    // The next rendered block starts from silence and resolves its engine afresh.
    parked  = true;
    current = -1;
}

void Track::bindParams(juce::AudioProcessorValueTreeState& apvts, int s)
{
    engineRow   = mu_core::ParamBinding(apvts.getRawParameterValue(layerParamId(s, "eng")), this);
    sourceRow   = mu_core::ParamBinding(apvts.getRawParameterValue(layerParamId(s, "src")), this);
    bassWaveRow = mu_core::ParamBinding(apvts.getRawParameterValue(engineParamId(s, EngineType::Bass, "wave")), this);   // a choice, not a target
    swingRow    = mu_core::ParamBinding(apvts.getRawParameterValue("seq_swing"), this);
    accentRow   = mu_core::ParamBinding(apvts.getRawParameterValue("seq_accent"), this);
    jassert(engineRow.isBound() && sourceRow.isBound() && bassWaveRow.isBound());   // an id drift from the layout (debug)

    // Every engine's modulation inputs for this slot, in its target table's order (the order setParams takes).
    for (int e = 0; e < kNumEngineTypes; ++e)
    {
        int n = 0;
        const ModDestEntry* t = destsForEngine(engineFromIndex(e), n);
        auto& L = mods[(size_t) e];
        L.count = n;
        L.map.reserve((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const auto id = layerParamPrefix(s) + t[i].param;
            L.ids[(size_t) i]    = t[i].id;
            L.atoms[(size_t) i]  = apvts.getRawParameterValue(id);
            jassert(L.atoms[(size_t) i] != nullptr);   // a target whose parameter drifted from the layout
            L.ranges[(size_t) i] = apvts.getParameterRange(id);
            L.map.emplace(std::string_view(t[i].id), 0.0f);   // pre-insert: the audio thread only overwrites
        }
    }
}

int Track::typeId() const
{
    return (int) mu_core::LayerTypeId::Kick + toIndex(engineSetting());
}

EngineType Track::engineSetting() const noexcept
{
    return engineFromIndex(juce::roundToInt(engineRow.load(0.0f)));
}

bool Track::syncEngine()
{
    // Message thread. prepare (the host's thread, not processing) and this never overlap.
    const juce::ScopedLock sl(engineBuild);
    const auto want = engineSetting();
    if (engine != nullptr && engine->type() == want) return false;

    // Built and prepared outside the audio thread's lock; only the pointer swap happens under it, and the
    // audio thread holds that lock for at most one call of this track's render.
    auto fresh = std::make_unique<TrackEngine>(want);
    fresh->prepare(preparedRate, preparedBlock);
    mu_core::spinLock(engineLock);
    std::swap(engine, fresh);
    heldEngine.store(toIndex(want), std::memory_order_release);
    mu_core::spinUnlock(engineLock);
    return true;   // `fresh` now holds the old engine, freed here on the message thread
}

void Track::resetEngineToPrepared() noexcept
{
    if (engine != nullptr) engine->resetToPrepared();
    current = -1;
}

float Track::getModLive(EngineType e, int dest) const noexcept
{
    if (dest < 0 || dest >= kMaxDests) return std::numeric_limits<float>::quiet_NaN();
    // Acquire pairs with the release store of the tag in resolve(): the values read below are at least as new
    // as the block that published this engine.
    if (modLiveEngine.load(std::memory_order_acquire) != toIndex(e)) return std::numeric_limits<float>::quiet_NaN();
    return modLive[(size_t) dest].load(std::memory_order_relaxed);
}

// The engine's parameters through the track's modulation matrix (the shared range-based resolve: proportion
// seed, try-locked matrix, back to parameter units), then published for the knobs' live arcs.
void Track::resolve(int e, double beat, float* outValues) noexcept
{
    auto& L = mods[(size_t) e];
    mu_mod::resolveLane(this, beat, L.count, L.ids.data(), L.atoms.data(), L.ranges.data(), L.map, outValues);
    for (int k = 0; k < L.count; ++k)
        modLive[(size_t) k].store(outValues[k], std::memory_order_relaxed);
    modLiveEngine.store(e, std::memory_order_release);   // after the values: the tag publishes them
}

// The Rumble envelope level (0..1) at `beat`, evaluated under its try-lock; on contention with a UI edit, the
// last value stands.
float Track::evaluateRumbleEnv(double beat) noexcept
{
    if (mu_core::trySpinLock(rumbleEnvLock))
    {
        lastRumbleEnv = juce::jlimit(0.0f, 1.0f, rumbleEnv.evaluate(beat) * 0.01f);
        mu_core::spinUnlock(rumbleEnvLock);
    }
    return lastRumbleEnv;
}

// The row follows the global groove, then (while playing) its clock fires the steps of this call into the
// engine (`fireInto` null: the clock only keeps step, nothing sounds). A Rumble's row does not step.
void Track::clockRow(const mu_core::RenderContext& ctx, TrackEngine* fireInto, int e, int n) noexcept
{
    row.setGroove(swingRow.load(0.0f), accentRow.load(1.0f));
    if (! ctx.playing) return;
    clock.processSpan(row, ctx.startBeat, n, ctx.beatsPerSample, row.swingAmount(), row.accentVelocity(),
                      [this, fireInto, e](float velocity, int offset)
                      {
                          if (fireInto == nullptr || e == toIndex(EngineType::Rumble)) return;
                          fireInto->trigger(velocity, offset);
                          triggers.fetch_add(1, std::memory_order_relaxed);
                      });
}

void Track::render(const mu_core::RenderContext& ctx, juce::AudioBuffer<float>& out, int n)
{
    const int e = toIndex(engineSetting());

    // The engine is swapped by the message thread under this lock; a miss (that swap is running) or an engine
    // that is not yet the one the parameter names (a change from another thread waits for the message thread
    // to build it) renders the track silent for this call. The row keeps step either way.
    if (! mu_core::trySpinLock(engineLock)) { clockRow(ctx, nullptr, e, n); return; }
    TrackEngine* eng = engine.get();
    if (eng == nullptr || toIndex(eng->type()) != e)
    {
        clockRow(ctx, nullptr, e, n);
        mu_core::spinUnlock(engineLock);
        return;
    }

    // Phase 1: a slot that has just gone live (or was prepared since) starts from silence, so a tail left by
    // an earlier state (a sustaining bass has no note-off) never resumes; an engine change stops the old one dead.
    if (parked) { eng->reset(); current = -1; parked = false; }
    if (current != e) { eng->reset(); current = e; }

    // Phase 2: this block's parameters through the modulation matrix, at the call's beat.
    float p[kMaxDests] {};
    resolve(e, ctx.startBeat, p);
    const bool rumble = e == toIndex(EngineType::Rumble);
    eng->setParams(p, juce::roundToInt(bassWaveRow.load(0.0f)), ctx.bpm, rumble ? evaluateRumbleEnv(ctx.startBeat) : 1.0f);

    // Phase 3: the stop edge silences the voice after the block's parameters are in (a sustaining bass has no
    // note-off), so values set in the stop block survive the reset. A stop hidden in blocks that rendered
    // nothing arrives as stoppedUnrendered (the transport has restarted since).
    if (ctx.justStopped || ctx.stoppedUnrendered) eng->reset();

    // Phase 4: the row's steps in this call trigger the engine (before the render, so a step sounds in its own block).
    clockRow(ctx, eng, e, n);

    // Phase 5: the engine's block. A Rumble processes its source track's block (a priority-0 peer, post-insert and
    // pre-strip); a source that is not a live non-Rumble track (out of range, itself, another Rumble) is silence.
    if (rumble)
    {
        const int src = juce::roundToInt(sourceRow.load(0.0f));
        const auto* peer = src != ctx.slot ? ctx.peer(src) : nullptr;
        if (peer != nullptr)
        {
            float* chans[2] = { const_cast<float*>(peer->channel[0]), const_cast<float*>(peer->channel[1]) };
            sourceView.setDataToReferTo(chans, 2, 0, n);   // a view of the peer's scratch: no allocation
            eng->render(out, n, sourceView, true);
        }
        else
            eng->render(out, n, silence, false);
    }
    else
        eng->render(out, n, silence, false);

    mu_core::spinUnlock(engineLock);
}

void Track::onParkedBlock(const mu_core::RenderContext& ctx)
{
    clockRow(ctx, nullptr, toIndex(engineSetting()), ctx.blockSize);
    parked = true;
}

} // namespace mu_on
