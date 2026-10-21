#include "Sequencer/Track.h"
#include "Persistence/TrackParams.h"      // the t{N}_ ids
#include "Modulation/LaneModulation.h"    // mu-core: mu_mod::resolveLane
#include "Audio/SpinLock.h"               // mu-core: spin lock helpers
#include "Modulation/ModulatorSerialise.h"  // mu-core: the modulators the base saves
#include "Persistence/PresetFiles.h"      // mu-core: enum names in saved data

#include <algorithm>
#include <limits>

// The Track layer's half of the render contract: the parameter binding and the per-block render that used to
// be mu-On's processor glue (the former GrooveVoices: engine ownership, modulation resolve, trigger, render).
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
    tailScratch.setSize(2, preparedBlock);
    tailScratch.clear();
    for (auto& e : engines)
        if (e != nullptr) e->prepare(preparedRate, preparedBlock);
    // The next rendered block starts from silence and resolves its engine afresh (no held C12 values).
    parked  = true;
    current = -1;
    haveResolved = false;
}

void Track::bindParams(juce::AudioProcessorValueTreeState& apvts, int s)
{
    engineRow   = mu_core::ParamBinding(apvts.getRawParameterValue(layerParamId(s, "eng")), this);
    sourceRow   = mu_core::ParamBinding(apvts.getRawParameterValue(layerParamId(s, "src")), this);
    bassWaveRow = mu_core::ParamBinding(apvts.getRawParameterValue(engineParamId(s, EngineType::Bass, "wave")), this);   // a choice, not a target
    algoRow     = mu_core::ParamBinding(apvts.getRawParameterValue(engineParamId(s, EngineType::FmPerc, "algo")), this);   // a choice, not a target
    swingRow    = mu_core::ParamBinding(apvts.getRawParameterValue(layerParamId(s, "swing")), this);
    accentRow   = mu_core::ParamBinding(apvts.getRawParameterValue(layerParamId(s, "accent")), this);
    jassert(engineRow.isBound() && sourceRow.isBound() && bassWaveRow.isBound() && algoRow.isBound() && swingRow.isBound() && accentRow.isBound());   // an id drift from the layout (debug)
    haveResolved = false;   // values held for C12 belong to the slot bound before

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

void Track::completeSetup(juce::AudioProcessorValueTreeState& apvts, int s)
{
    // A locked track's parameter is a record of its engine: write it so the saved rows and a saved .muTrack
    // name the right engine. engineSetting() already ignores it, so this is not what makes the track run it.
    if (engineLocked)
        if (auto* p = apvts.getParameter(layerParamId(s, "eng")))
            if (juce::roundToInt(p->convertFrom0to1(p->getValue())) != toIndex(lockedEngine))
                p->setValueNotifyingHost(p->convertTo0to1((float) toIndex(lockedEngine)));
    syncEngine();   // the engines the track needs, built and prepared now
}

int Track::typeId() const
{
    return (int) layerTypeOf(engineSetting());
}

EngineType Track::engineSetting() const noexcept
{
    if (engineLocked) return lockedEngine;   // the parameter is only a record on a locked track
    return engineFromIndex(juce::roundToInt(engineRow.load(0.0f)));
}

bool Track::syncEngine()
{
    // Message thread. prepare (the host's thread, not processing) and this never overlap.
    const juce::ScopedLock sl(engineBuild);
    const int want = toIndex(engineSetting());
    auto needed = [this, want](int e) { return ! engineLocked || e == want; };

    // Phase 1: the missing engines, built and prepared outside the audio thread's lock.
    std::array<std::unique_ptr<TrackEngine>, kNumEngineTypes> built;
    bool change = false;
    for (int e = 0; e < kNumEngineTypes; ++e)
    {
        if (needed(e) && engines[(size_t) e] == nullptr)
        {
            built[(size_t) e] = std::make_unique<TrackEngine>(engineFromIndex(e));
            built[(size_t) e]->prepare(preparedRate, preparedBlock);
            change = true;
        }
        change = change || (! needed(e) && engines[(size_t) e] != nullptr);
    }
    if (! change) return false;

    // Phase 2: pointer moves only, under the lock the audio thread holds for at most one call of this track's
    // render. The engines taken out land in `built` and are freed below, here on the message thread.
    unsigned mask = 0;
    mu_core::spinLock(engineLock);
    for (int e = 0; e < kNumEngineTypes; ++e)
    {
        if (needed(e) && engines[(size_t) e] == nullptr) std::swap(engines[(size_t) e], built[(size_t) e]);
        else if (! needed(e))                            std::swap(engines[(size_t) e], built[(size_t) e]);
        if (engines[(size_t) e] != nullptr) mask |= 1u << (unsigned) e;
    }
    heldMask.store(mask, std::memory_order_release);
    mu_core::spinUnlock(engineLock);
    return true;
}

void Track::resetEngines() noexcept
{
    for (auto& e : engines)
        if (e != nullptr) e->reset();
}

void Track::resetEngineToPrepared() noexcept
{
    // Under layersLock (the edit's order: layersLock, then engineBuild); no block renders the track meanwhile.
    const juce::ScopedLock sl(engineBuild);
    for (auto& e : engines)
        if (e != nullptr) e->resetToPrepared();
    current = -1;
    haveResolved = false;
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
    const bool resolved = mu_mod::resolveLane(this, beat, L.count, L.ids.data(), L.atoms.data(), L.ranges.data(), L.map, outValues);

    // A block that misses the matrix lock (the message thread is editing this track's modulators) holds the last
    // values resolved for this engine, so no target snaps back to its unmodulated knob for a block (C12). Before
    // the engine's first resolve it takes the unmodulated values, as a miss always did.
    if (lastResolvedEngine != e) haveResolved = false;
    if (resolved || ! haveResolved)
    {
        std::copy(outValues, outValues + L.count, lastResolved.begin());
        haveResolved = haveResolved || resolved;
        lastResolvedEngine = e;
    }
    else
        std::copy(lastResolved.begin(), lastResolved.begin() + L.count, outValues);

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

// The row follows the track's own groove rows (swing, accent), then (while playing) its clock fires the steps of this call into the
// engine (`fireInto` null: the clock only keeps step, nothing sounds). A Rumble's row does not step.
void Track::clockRow(const mu_core::RenderContext& ctx, TrackEngine* fireInto, int e, int n) noexcept
{
    row.setGroove(swingRow.load(0.0f), accentRow.load(1.0f));
    if (! ctx.playing) return;
    if (ctx.justStarted) clock.seek(ctx.startBeat);   // the step the transport starts in fires, even on a quick replay
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

    // Engines are installed by the message thread under this lock; a miss (an install is running) renders the
    // track silent for this call. An unlocked track holds all five, so the engine the parameter names is always
    // there, in the same block it is named; only a locked track whose single engine is not yet the named one
    // (a change from another thread waits for the message thread to build it) is silent. The row keeps step.
    if (! mu_core::trySpinLock(engineLock)) { clockRow(ctx, nullptr, e, n); return; }
    TrackEngine* eng = engines[(size_t) e].get();
    if (eng == nullptr)
    {
        clockRow(ctx, nullptr, e, n);
        mu_core::spinUnlock(engineLock);
        return;
    }

    // Phase 1: a slot that has just gone live (or was prepared since) starts from silence, so a tail left by
    // an earlier state (a sustaining bass has no note-off) never resumes; an engine change stops the old one dead.
    // Both silence every engine the track holds, as the per-slot engines' reset always did.
    if (parked) { resetEngines(); current = -1; haveResolved = false; parked = false; }
    if (current != e) { resetEngines(); current = e; }

    // Phase 2: this block's parameters through the modulation matrix, at the call's beat.
    float p[kMaxDests] {};
    resolve(e, ctx.startBeat, p);
    const bool rumble = e == toIndex(EngineType::Rumble);
    // The engine's own choice row: the Bass wave, or the FM Perc algorithm (neither is a modulation target).
    const int engineChoice = juce::roundToInt(e == toIndex(EngineType::FmPerc) ? algoRow.load(0.0f) : bassWaveRow.load(0.0f));
    eng->setParams(p, engineChoice, ctx.bpm, rumble ? evaluateRumbleEnv(ctx.startBeat) : 1.0f);

    // Phase 3: the stop edge silences the voice after the block's parameters are in (a sustaining bass has no
    // note-off), so values set in the stop block survive the reset. A stop hidden in blocks that rendered
    // nothing arrives as stoppedUnrendered (the transport has restarted since).
    if (ctx.justStopped || ctx.stoppedUnrendered) resetEngines();

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

// The tail of a track that left its slot (docs/mu-tronic/design.md 2.1.11 point 3). It reads no parameter: the
// engine of the last rendered block (`current`, audio-thread state) still holds the parameters it was given, and
// is rendered with no trigger and no setParams, so a ringing voice or reverb dies away under the bin's fade.
bool Track::renderRetiring(const mu_core::RenderContext&, juce::AudioBuffer<float>& out, int n, mu_core::RetireState& rs)
{
    if (current < 0) return false;   // never rendered live: nothing can sound
    if (! mu_core::trySpinLock(engineLock)) return true;   // an install is running: silent this call, the fade runs on
    TrackEngine* eng = engines[(size_t) current].get();
    if (eng == nullptr) { mu_core::spinUnlock(engineLock); return false; }

    // The bin hands the tail the slot's block exclusively (write it) or not (render aside, then add).
    juce::AudioBuffer<float> aside;
    if (rs.exclusive == 0)
    {
        aside.setDataToReferTo(tailScratch.getArrayOfWritePointers(), juce::jmin(2, out.getNumChannels()),
                               juce::jmin(n, tailScratch.getNumSamples()));
        aside.clear();
    }
    auto& buf = rs.exclusive != 0 ? out : aside;
    const int frames = juce::jmin(n, buf.getNumSamples(), silence.getNumSamples());

    // A Rumble has no peer in a tail: it processes the silence buffer, as a Rumble with no source does.
    eng->render(buf, frames, silence, false);
    mu_core::applyLinearFade(buf, frames, rs);

    if (rs.exclusive == 0)
        for (int c = 0; c < buf.getNumChannels(); ++c) out.addFrom(c, 0, buf, c, 0, frames);
    mu_core::spinUnlock(engineLock);
    return true;   // the bin ends the fade when its ramp is spent
}

void Track::onParkedBlock(const mu_core::RenderContext& ctx)
{
    clockRow(ctx, nullptr, toIndex(engineSetting()), ctx.blockSize);
    parked = true;
}

} // namespace mu_on
