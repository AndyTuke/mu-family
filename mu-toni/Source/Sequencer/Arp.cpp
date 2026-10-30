#include "Sequencer/Arp.h"
#include "Persistence/LayerParamIds.h"     // layerParamId
#include "Modulation/LaneModulation.h"     // mu-core: mu_mod::resolveLane

#include <algorithm>
#include <cstring>

// The Arp layer's half of the render contract: the parameter binding and the per-block render that used to
// be mu-Toni's processor glue (readVoice, the held-note scan, the render callback). docs/mu-tronic/design.md 2.1.
namespace mu_toni
{

namespace
{
    // The vpi row a modulation target drives, found from its table parameter name.
    int vpSlotFor(const char* param)
    {
        for (int k = 0; k < vpi::COUNT; ++k)
            if (std::strcmp(vpi::suffix[k], param) == 0) return k;
        jassertfalse;   // a table row names a parameter that does not exist
        return 0;
    }
}

void Arp::prepare(double sampleRate, int blockSize)
{
    runner.prepare(sampleRate, blockSize);
    insert->prepare(sampleRate, blockSize);
    tailScratch.setSize(2, juce::jmax(1, blockSize));
    tailScratch.clear();
    parked = true;
}

void Arp::bindParams(juce::AudioProcessorValueTreeState& apvts, int slot)
{
    // The slot's rows in vpi order; the modulation inputs are the same atomics, in target-table order.
    for (int k = 0; k < kNumVoiceParams; ++k)
        vp[(size_t) k] = mu_core::ParamBinding(apvts.getRawParameterValue(layerParamId(slot, vpi::suffix[k])), this);
    midiInParam = mu_core::ParamBinding(apvts.getRawParameterValue(kMidiInChannelParamId), this);

    for (int k = 0; k < kNumModDests; ++k)
    {
        const bool strip = mu_mod::isMixerStripParam(kModDestTable[k].param);   // a pan or send is the mixer strip's parameter
        const auto id = mu_mod::paramIdFor(layerParamId(slot, ""), slot, kModDestTable[k].param);
        modDestIds[(size_t) k]    = kModDestTable[k].id;
        modDestRanges[(size_t) k] = apvts.getParameterRange(id);
        modDestAtoms[(size_t) k]  = strip ? apvts.getRawParameterValue(id)
                                          : apvts.getRawParameterValue(layerParamId(slot, vpi::suffix[vpSlotFor(kModDestTable[k].param)]));
        modParamValues[kModDestTable[k].id] = 0.0f;   // pre-size the map: the audio thread only overwrites
    }
}

void Arp::hearMidi(const mu_core::RenderContext& ctx) noexcept
{
    // MIDI-in: the processor-wide row, on top of the context's channel mask (Omni today; per-layer later).
    const int inCh = midiInChannel();
    const auto* first = ctx.midi.events;
    const int carried = ctx.midi.carried;
    context.noteOnEdge = false;
    if (ctx.midi.inputReset) heldNotes.clear();   // MIDI In changed: a release on the old channel will not arrive
    mu_core::forEachMidi(ctx, [&](const mu_core::MidiEvent& e, int)
    {
        heldNotes.apply(e, inCh, (int) (&e - first) >= carried, context.noteOnEdge);
    });
    context.anyNoteHeld      = heldNotes.anyHeld();
    context.rootOverrideMidi = heldNotes.newest();
}

void Arp::readParams(double modBeat) noexcept
{
    auto g = [this](int slot) { return vp[(size_t) slot].load(0.0f); };

    // Resolve this layer's modulation matrix over its control sequences → out[] (param units).
    // Modulated destinations come from out[]; everything else reads the raw param.
    float out[kNumModDests];
    const bool resolved = mu_mod::resolveLane(this, modBeat, kNumModDests,
                                              modDestIds.data(), modDestAtoms.data(),
                                              modDestRanges.data(), modParamValues, out);

    // A block that misses the matrix lock (the message thread is editing this layer's modulators) holds the
    // last resolved values, so no target snaps back to its unmodulated knob for a block (C12).
    lastResolved.apply(resolved, out);

    stripMod.publish(out[D_pan], out[D_sendEff], out[D_sendDly], out[D_sendRev]);   // the host gives these to the layer's mixer strip

    // Publish the post-matrix values for the knobs' live modulation arcs.
    for (int k = 0; k < kNumModDests; ++k)
        modLive.v[(size_t) k].store(out[k], std::memory_order_relaxed);

    // Arp (integer dests round; direction/gate/porta continuous).
    ArpParams ap;
    ap.scale        = (int) g(vpi::scale);                  // not modulated
    ap.rootNote     = juce::roundToInt(out[D_root]);
    ap.rootOctave   = juce::roundToInt(out[D_roct]);
    ap.chord        = juce::roundToInt(out[D_chord]);
    ap.inversion    = juce::roundToInt(out[D_inv]);
    ap.octavesSpan  = juce::roundToInt(out[D_octs]);
    ap.direction    = out[D_dir];
    ap.diatonicSnap = g(vpi::snap) > 0.5f;                  // not modulated

    ToniVoiceParams tv;
    tv.osc1Table = (int) g(vpi::o1wt); tv.osc1Oct = (int) g(vpi::o1o); tv.osc1Fine = g(vpi::o1f); tv.osc1Pos = out[D_o1pos]; tv.osc1LevelDb = out[D_o1lvl];
    tv.osc2Table = (int) g(vpi::o2wt); tv.osc2Oct = (int) g(vpi::o2o); tv.osc2Semi = out[D_o2semi]; tv.osc2Fine = g(vpi::o2f);
    tv.osc2Pos   = out[D_o2pos]; tv.osc2LevelDb = out[D_o2lvl];
    tv.noiseLevelDb = out[D_noise]; tv.noiseType = (int) g(vpi::ntype);

    // Cross-mod (index / depth are 0..100 / ±100 knobs → 0..1 / ±1).
    tv.xmod.phaseMode = (int) g(vpi::xmPh);
    tv.xmod.index     = out[D_xmIdx] * 0.01f;
    tv.xmod.sync      = g(vpi::sync) > 0.5f;
    tv.xmod.feedback  = g(vpi::xmFb) > 0.5f;
    tv.xmod.ampMode   = (int) g(vpi::xmAm);
    tv.xmod.depth     = out[D_xmDep] * 0.01f;
    tv.xmod.ssbHz     = out[D_xmSsb];
    tv.filterType = (int) g(vpi::ft); tv.cutoff = out[D_cut]; tv.resonance = out[D_res]; tv.drive = out[D_drv]; tv.lowCutHz = g(vpi::locut);

    tv.ampA = out[D_aeA]; tv.ampD = out[D_aeD]; tv.ampS = out[D_aeS]; tv.ampR = out[D_aeR]; tv.ampLevelDb = out[D_amp];
    tv.fA = out[D_feA]; tv.fD = out[D_feD]; tv.fS = out[D_feS]; tv.fR = out[D_feR]; tv.filterEnvDepth = out[D_fenv];
    tv.pA = out[D_peA]; tv.pD = out[D_peD]; tv.pS = out[D_peS]; tv.pR = out[D_peR]; tv.pitchEnvDepth = out[D_penv];
    tv.pitchEnvTarget = (int) g(vpi::ptgt);

    tv.portamentoMs = out[D_porta];
    tv.legato       = g(vpi::leg) > 0.5f;
    tv.pan          = 0.0f;   // pan handled by the mixer strip

    ArpAccent accent;
    accent.amount   = out[D_acc] * 0.01f;
    accent.length   = (int) g(vpi::accLen);
    accent.pattern  = (unsigned) juce::jmax(0, (int) g(vpi::accPat));
    accent.stepMask = (unsigned) juce::jmax(0, (int) g(vpi::stepMask));

    // Insert config (applied post-VCA by renderBlock).
    insertCfg.insertAlgo     = (int) g(vpi::drvChar);
    insertCfg.insertParam[0] = out[D_insP1];   // modulated
    insertCfg.insertParam[1] = out[D_insP2];
    insertCfg.insertParam[2] = out[D_insP3];
    insertCfg.insertParam[3] = out[D_insP4];

    runner.setAccent(accent);
    runner.setArp(ap);
    runner.setVoiceParams(tv);
    runner.setStep(juce::roundToInt(out[D_rate]), out[D_gate] * 0.01f, g(vpi::trig) > 0.5f);
}

void Arp::render(const mu_core::RenderContext& ctx, juce::AudioBuffer<float>& out, int n)
{
    // A slot that has just gone live (or was prepared since its last block) starts from silence: a deleted
    // layer's held note or voice tail never resumes in it.
    if (parked) { runner.reset(); parked = false; }

    hearMidi(ctx);
    context.playing        = ctx.playing != 0;
    context.sampleRate     = ctx.sampleRate;
    context.bpm            = ctx.bpm;
    context.startBeat      = ctx.startBeat;
    context.beatsPerSample = ctx.beatsPerSample;

    readParams(ctx.startBeat);   // the control-sequence playhead runs on the call's beat
    renderBlock(out, n);
}

// The tail of a layer that left its slot (docs/mu-tronic/design.md 2.1.11 point 3): the voice with no keys held
// (its note is released and rings out its release), faded, then the insert with the configuration it last had.
// Reads no parameter and no modulation: only the voice, the insert and its stored configuration.
bool Arp::renderRetiring(const mu_core::RenderContext&, juce::AudioBuffer<float>& out, int n, mu_core::RetireState& rs)
{
    // The bin hands the tail the slot's block exclusively (write it) or not (render aside, then add).
    juce::AudioBuffer<float> aside;
    if (rs.exclusive == 0)
    {
        aside.setDataToReferTo(tailScratch.getArrayOfWritePointers(), juce::jmin(2, out.getNumChannels()),
                               juce::jmin(n, tailScratch.getNumSamples()));
        aside.clear();
    }
    auto& buf = rs.exclusive != 0 ? out : aside;
    const int frames = juce::jmin(n, buf.getNumSamples());

    const ArpContext idle;   // not playing, no key held: the runner only releases its note and renders the voice's tail
    runner.render(buf, frames, idle);
    mu_core::applyLinearFade(buf, frames, rs);
    insert->process(buf, frames, buf.getNumChannels(), insertCfg);

    if (rs.exclusive == 0)
        for (int c = 0; c < buf.getNumChannels(); ++c) out.addFrom(c, 0, buf, c, 0, frames);
    return true;   // the bin ends the fade when its ramp is spent
}

void Arp::onParkedBlock(const mu_core::RenderContext& ctx)
{
    hearMidi(ctx);
    parked = true;
}

} // namespace mu_toni
