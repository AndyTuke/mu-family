#include "Plugin/PluginProcessor.h"
#include "Plugin/PluginEditor.h"
#include "Audio/Scales.h"
#include "Modulation/MuTantModDest.h"
#include "Modulation/ModulatorSerialise.h"   // mu-core: shared modulator (de)serialise

#include <thread>

namespace mu_tant
{

namespace
{
    // Gate-pattern (de)serialise — mu-tant-specific (GatePattern is a mu-tant type).
    juce::ValueTree serialiseGate(const GatePattern& g)
    {
        juce::ValueTree t("Gate");
        t.setProperty("subdiv", (int) g.subdivision, nullptr);
        for (const auto& e : g.envelopes)
        {
            juce::ValueTree env("Env");
            env.setProperty("start", e.startCell,        nullptr);
            env.setProperty("len",   e.lengthCells,      nullptr);
            env.setProperty("split", e.split,            nullptr);
            env.setProperty("atk",   e.attackBend,       nullptr);
            env.setProperty("dec",   e.decayBend,        nullptr);
            env.setProperty("rev",   e.reverse ? 1 : 0,  nullptr);
            t.addChild(env, -1, nullptr);
        }
        return t;
    }

    // Restore a <Gate> tree into `g` (clears + rebuilds). An invalid/absent tree
    // clears the pattern. Always holds editLock, so it's safe to call without an
    // outer suspend/voicesLock — an in-flight gate read on the audio thread can't
    // see a half-rebuilt envelope vector.
    void deserialiseGate(const juce::ValueTree& t, GatePattern& g)
    {
        const bool valid = t.isValid() && t.getType() == juce::Identifier("Gate");

        while (g.editLock.exchange(true, std::memory_order_acquire))
            std::this_thread::yield();

        g.envelopes.clear();
        if (valid)
        {
            g.subdivision = (GatePattern::Subdivision)(int)
                t.getProperty("subdiv", (int) GatePattern::Subdivision::Sixteenth);
            for (int i = 0; i < t.getNumChildren(); ++i)
            {
                auto c = t.getChild(i);
                if (c.getType() != juce::Identifier("Env")) continue;
                GateEnvelope e;
                e.startCell   =        (int)    c.getProperty("start", 0);
                e.lengthCells = juce::jmax(1, (int) c.getProperty("len", 1));
                e.split       = (float)(double) c.getProperty("split", 0.0);
                e.attackBend  = (float)(double) c.getProperty("atk",   0.0);
                e.decayBend   = (float)(double) c.getProperty("dec",   0.0);
                e.reverse     =        (int)    c.getProperty("rev", 0) != 0;
                g.envelopes.push_back(e);
            }
        }
        else
        {
            g.subdivision = GatePattern::Subdivision::Sixteenth;
        }
        g.resetGateCache();
        g.editLock.store(false, std::memory_order_release);
    }

    // mu-tant modulation-destination validator (drops assignments to dests this
    // product doesn't expose; source IDs are ControlSequence ids, left unchecked).
    bool isValidModDest(const std::string& id)
    {
        for (int i = 0; i < kModDestCount; ++i)
            if (id == kModDestTable[i].id) return true;
        return false;
    }
}


PluginProcessor::PluginProcessor()
    : ProcessorBase(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true),
                    createParameterLayout(),
                    juce::Identifier("MuTantState"))
{
    bank.generateBuiltIn();      // procedural sine->saw morph table (first stab)
    for (int v = 0; v < kMaxVoices; ++v)
    {
        voices[(size_t) v] = std::make_unique<VoiceEngine>();
        voices[(size_t) v]->setBank(&bank);
    }

    cacheParamPointers();        // resolve all APVTS atomics once (audio thread reads these)

    // Pre-allocate modParamValues entries once so the audio-thread map
    // never allocates — keys live in static storage (the kModDestTable string
    // literals), making string_view safe for the lifetime of the entries.
    modParamValues.reserve((size_t) kModDestCount);
    for (int i = 0; i < kModDestCount; ++i)
        modParamValues[kModDestTable[i].id] = 0.0f;

    // Render hook for the shared MixerEngine — captures only `this`, so no
    // per-block std::function construction (which would allocate). The per-block
    // transport snapshot it reads lives in the blk* members, set in processBlock.
    renderVoiceCb = [this](int v, juce::AudioBuffer<float>& buf, int n) { renderVoice(v, buf, n); };

    // Mixer + FX state is listener-synced into mixerEngine/fxChain (channel strips,
    // sends, sidechain, returns, master, FX slots) — mirrors mu-clid. Seed it now
    // since JUCE doesn't fire parameterChanged on construction.
    registerFxListeners();
    syncAllFxParams();
}

PluginProcessor::~PluginProcessor()
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p))
        {
            const juce::String id = rp->getParameterID();
            if (id.startsWith("ch") || mu_mixfx::isGlobalFxParamId(id))
                apvts.removeParameterListener(id, this);
        }
}

void PluginProcessor::registerFxListeners()
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p))
        {
            const juce::String id = rp->getParameterID();
            if (id.startsWith("ch") || mu_mixfx::isGlobalFxParamId(id))
                apvts.addParameterListener(id, this);
        }
}

void PluginProcessor::syncAllFxParams()
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p))
        {
            const juce::String id = rp->getParameterID();
            if (id.startsWith("ch") || mu_mixfx::isGlobalFxParamId(id))
                if (auto* a = apvts.getRawParameterValue(id))
                    syncGlobalFxParam(id, a->load());
        }
}

void PluginProcessor::parameterChanged(const juce::String& id, float v)
{
    if (id.startsWith("ch") || mu_mixfx::isGlobalFxParamId(id))
        syncGlobalFxParam(id, v);
}

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    for (auto& v : voices)
        if (v) v->prepare(sampleRate, samplesPerBlock);

    for (auto& ins : inserts)
        ins.prepare(sampleRate, samplesPerBlock);

    mixerEngine.prepare(sampleRate, samplesPerBlock);
    fxChain.prepare(sampleRate, samplesPerBlock);   // shared Effect/Delay/Reverb rack
}

void PluginProcessor::cacheParamPointers()
{
    auto P = [this](const juce::String& id) { return apvts.getRawParameterValue(id); };
    globalPtrs.root    = P("root");
    globalPtrs.scale   = P("scale");

    for (int v = 0; v < kMaxVoices; ++v)
    {
        auto vid = [v](const char* base) { return voiceParamId(v, base); };
        auto& p = voicePtrs[(size_t) v];
        p.o1Oct  = P(vid("o1_oct"));  p.o1Semi = P(vid("o1_semi")); p.o1Fine = P(vid("o1_fine")); p.o1Pos = P(vid("o1_pos"));
        p.o2Oct  = P(vid("o2_oct"));  p.o2Semi = P(vid("o2_semi")); p.o2Fine = P(vid("o2_fine")); p.o2Pos = P(vid("o2_pos"));
        p.xmod   = P(vid("xmod"));    p.xmode  = P(vid("xmode"));
        p.o1Lvl  = P(vid("o1_lvl"));  p.o2Lvl  = P(vid("o2_lvl"));  p.noiseLvl = P(vid("noise_lvl")); p.noiseType = P(vid("noise_type"));
        p.fltType= P(vid("flt_type"));p.fltCut = P(vid("flt_cut")); p.fltRes = P(vid("flt_res"));
        p.level  = P(vid("level"));
        p.gateGap= P(vid("gate_gap"));p.gateBypass = P(vid("gate_bypass"));
        p.drvChar= P(vid("drvChar")); p.insP1  = P(vid("insP1")); p.insP2 = P(vid("insP2")); p.insP3 = P(vid("insP3")); p.insP4 = P(vid("insP4"));
    }
}

VoiceConfig PluginProcessor::readConfig(int voiceIdx) const
{
    const auto& p = voicePtrs[(size_t) voiceIdx];
    VoiceConfig c;
    // Tonal centre is global.
    c.root         = (int) globalPtrs.root->load();
    c.scaleIdx     = (int) globalPtrs.scale->load();
    // Per-voice osc pitch (integer) + position.
    c.osc1Octave   = (int) p.o1Oct->load();
    c.osc1Semi     = (int) p.o1Semi->load();
    c.osc1Fine     = (int) p.o1Fine->load();
    c.osc1Pos      = p.o1Pos->load();
    c.osc2Octave   = (int) p.o2Oct->load();
    c.osc2Semi     = (int) p.o2Semi->load();
    c.osc2Fine     = (int) p.o2Fine->load();
    c.osc2Pos      = p.o2Pos->load();
    c.xmod         = (int) p.xmod->load();
    c.xmodMode     = (int) p.xmode->load();
    // Per-source levels.
    c.osc1LevelDb  = p.o1Lvl->load();
    c.osc2LevelDb  = p.o2Lvl->load();
    c.noiseLevelDb = p.noiseLvl->load();
    c.noiseType    = (int) p.noiseType->load();
    c.filterType   = (int) p.fltType->load();
    c.filterCutoff = p.fltCut->load();
    c.filterRes    = p.fltRes->load();
    c.levelDb      = p.level->load();
    return c;
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();

    // Exclude the audio thread while a voice is being added/removed (the message
    // thread holds voicesLock + shifts per-voice data). On contention, output a
    // silent block — a sub-millisecond gap during an edit.
    const juce::ScopedTryLock voicesTryLock(voicesLock);
    if (! voicesTryLock.isLocked())
        return;

    const int numActiveVoices = numVoices.load(std::memory_order_relaxed);

    // Per-block transport snapshot for the render hook (audio-thread-only members,
    // read by renderVoice within this same call). Beat advances only while playing;
    // when stopped the gate is held open so the oscillators stay auditionable.
    blkPlaying        = playing.load(std::memory_order_relaxed);
    blkBeatStart      = internalBeatPos.load(std::memory_order_relaxed);
    const double bpm  = internalBpm.load(std::memory_order_relaxed);   // snapshot once for this block
    blkBeatsPerSample = (bpm / 60.0) / currentSampleRate;

    // Channel-strip + master + FX state lives in mixerEngine/fxChain, kept in sync
    // by the parameterChanged listener (see syncGlobalFxParam) — no per-block push.

    // Render → gate → insert → mixer through the shared path (engine→insert→mixer,
    // the family-wide signal flow). The render hook fills each channel buffer; the
    // mixer owns the strip/master mix, so the VU meters (channelPeaks) now update.
    processCoreBlock(buffer, nullptr, numActiveVoices, numSamples, bpm,
                     nullptr, nullptr, nullptr, &renderVoiceCb);

    // Advance the transport beat only while playing. Wrap at the 2-bar pattern
    // length (8 beats in 4/4) so the gate + the gating-grid playhead loop cleanly
    // and floating-point precision never drifts. Modulators evaluate against the
    // same wrapped position.
    if (blkPlaying)
    {
        double pos = blkBeatStart + blkBeatsPerSample * (double) numSamples;
        const double patBeats = (double) GatePattern::kTotalBars * 4.0;   // 8 beats
        if (pos >= patBeats) pos -= patBeats;
        internalBeatPos.store(pos, std::memory_order_relaxed);
    }
}

// One voice's full chain into the channel buffer: modulation → engine → gate →
// insert. Invoked by MixerEngine::processBlock (Phase 1) per active channel; the
// mixer then applies the channel strip + master. Renders even muted/un-soloed
// voices (the mixer mutes at the mix), matching mu-clid's render-then-mute model.
void PluginProcessor::renderVoice(int v, juce::AudioBuffer<float>& buf, int numSamples)
{
    const auto& vp = voicePtrs[(size_t) v];

    // Modulation pass — seed paramValues from the current APVTS-derived config,
    // then let this voice's matrix overwrite any modulated keys. The matrix is
    // empty for voices the user hasn't assigned, so this collapses to a no-op.
    VoiceConfig cfg = readConfig(v);
    modParamValues["osc1.octave"]      = (float) cfg.osc1Octave;
    modParamValues["osc1.semi"]        = (float) cfg.osc1Semi;
    modParamValues["osc1.fine"]        = (float) cfg.osc1Fine;
    modParamValues["osc1.pos"]         = cfg.osc1Pos;
    modParamValues["osc2.octave"]      = (float) cfg.osc2Octave;
    modParamValues["osc2.semi"]        = (float) cfg.osc2Semi;
    modParamValues["osc2.fine"]        = (float) cfg.osc2Fine;
    modParamValues["osc2.pos"]         = cfg.osc2Pos;
    modParamValues["xmod"]             = (float) cfg.xmod;
    modParamValues["osc1.level"]       = cfg.osc1LevelDb;
    modParamValues["osc2.level"]       = cfg.osc2LevelDb;
    modParamValues["noise.level"]      = cfg.noiseLevelDb;
    modParamValues["filter.cutoff"]    = cfg.filterCutoff;
    modParamValues["filter.resonance"] = cfg.filterRes;
    modParamValues["level"]            = cfg.levelDb;

    // tryLock-equivalent: ModulationMatrix mutations on the message thread hold
    // modLock; on contention we skip the modulation pass this block (the voice
    // still plays with un-modulated config).
    auto& slot = voiceSlots[(size_t) v];
    bool expected = false;
    if (slot.modLock.compare_exchange_strong(expected, true, std::memory_order_acquire))
    {
        slot.modulationMatrix.process(slot.controlSequences, blkBeatStart, modParamValues);
        slot.modLock.store(false, std::memory_order_release);

        // Publish post-matrix values for the UI live-arc indicators.
        auto& snap = voiceSnap[(size_t) v];
        snap[mu_tant::kTantSnapOsc1Octave] .store(modParamValues["osc1.octave"]);
        snap[mu_tant::kTantSnapOsc1Semi]   .store(modParamValues["osc1.semi"]);
        snap[mu_tant::kTantSnapOsc1Fine]   .store(modParamValues["osc1.fine"]);
        snap[mu_tant::kTantSnapOsc1Pos]    .store(modParamValues["osc1.pos"]);
        snap[mu_tant::kTantSnapOsc2Octave] .store(modParamValues["osc2.octave"]);
        snap[mu_tant::kTantSnapOsc2Semi]   .store(modParamValues["osc2.semi"]);
        snap[mu_tant::kTantSnapOsc2Fine]   .store(modParamValues["osc2.fine"]);
        snap[mu_tant::kTantSnapOsc2Pos]    .store(modParamValues["osc2.pos"]);
        snap[mu_tant::kTantSnapXMod]       .store(modParamValues["xmod"]);
        snap[mu_tant::kTantSnapOsc1Level]  .store(modParamValues["osc1.level"]);
        snap[mu_tant::kTantSnapOsc2Level]  .store(modParamValues["osc2.level"]);
        snap[mu_tant::kTantSnapNoiseLevel] .store(modParamValues["noise.level"]);
        snap[mu_tant::kTantSnapFilterCutoff].store(modParamValues["filter.cutoff"]);
        snap[mu_tant::kTantSnapFilterRes]  .store(modParamValues["filter.resonance"]);
        snap[mu_tant::kTantSnapLevel]      .store(modParamValues["level"]);

        cfg.osc1Octave   = (int) modParamValues["osc1.octave"];
        cfg.osc1Semi     = (int) modParamValues["osc1.semi"];
        cfg.osc1Fine     = (int) modParamValues["osc1.fine"];
        cfg.osc1Pos      = modParamValues["osc1.pos"];
        cfg.osc2Octave   = (int) modParamValues["osc2.octave"];
        cfg.osc2Semi     = (int) modParamValues["osc2.semi"];
        cfg.osc2Fine     = (int) modParamValues["osc2.fine"];
        cfg.osc2Pos      = modParamValues["osc2.pos"];
        cfg.xmod         = (int) modParamValues["xmod"];
        cfg.osc1LevelDb  = modParamValues["osc1.level"];
        cfg.osc2LevelDb  = modParamValues["osc2.level"];
        cfg.noiseLevelDb = modParamValues["noise.level"];
        cfg.filterCutoff = modParamValues["filter.cutoff"];
        cfg.filterRes    = modParamValues["filter.resonance"];
        cfg.levelDb      = modParamValues["level"];
    }

    buf.clear();
    voices[(size_t) v]->setConfig(cfg);
    voices[(size_t) v]->process(buf, numSamples);

    // ── Gate stage ────────────────────────────────────────────────────────────
    // The per-voice gater shapes the (post-filter) voice output:
    //   bypassed             → raw drone passes (audition / configure)
    //   stopped              → silence (gate closed — nothing audible on load)
    //   playing, no envelopes→ silence (nothing drawn → nothing passes)
    //   playing, envelopes   → per-sample envelope gate
    // See applyGateBlock — the audio path + the audio test harness share it.
    auto& pattern = gatePatterns[(size_t) v];
    const float gateGap    = vp.gateGap->load() * 0.01f;   // 0..100% → 0..1
    const bool  gateBypass = vp.gateBypass->load() > 0.5f;
    float* gl = buf.getWritePointer(0);
    float* gr = buf.getNumChannels() > 1 ? buf.getWritePointer(1) : nullptr;
    applyGateBlock(pattern, gl, gr, numSamples, gateGap, gateBypass, blkPlaying,
                   blkBeatStart, blkBeatsPerSample, currentSampleRate);

    // ── Insert effect ───────────────────────────────────────────────────────
    // Shared mu-core InsertProcessor, post-gate. Algo 0 (None) is a passthrough.
    VoiceParams ip;
    ip.insertAlgo     = (int) vp.drvChar->load();
    ip.insertParam[0] = vp.insP1->load();
    ip.insertParam[1] = vp.insP2->load();
    ip.insertParam[2] = vp.insP3->load();
    ip.insertParam[3] = vp.insP4->load();
    inserts[(size_t) v].process(buf, numSamples, buf.getNumChannels(), ip);

    // Feed the sidebar spectrum glyph — capture post-insert audio on the audio thread.
    voiceRingBuffers[(size_t) v].write(buf, numSamples);
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor(*this);
}

void PluginProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    apvts.state.setProperty("numVoices", numVoices.load(), nullptr);
    apvts.state.setProperty("voiceColours", serialiseVoiceColours(), nullptr);
    writeVoiceDataToState();
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void PluginProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
    {
        const juce::ScopedLock sl(voicesLock);
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
        numVoices.store(juce::jlimit(1, kMaxVoices, (int) apvts.state.getProperty("numVoices", 1)));
        restoreVoiceColours(apvts.state.getProperty("voiceColours", "").toString());
        readVoiceDataFromState();
        syncAllFxParams();   // re-seed mixer/FX engine state (unchanged values skip listeners)
    }
}

// ── Dynamic voice management ─────────────────────────────────────────────────
int PluginProcessor::firstUnusedColourIndex() const
{
    const int n = numVoices.load();
    std::array<bool, kMaxVoices> used { };
    for (int i = 0; i < n; ++i)
    {
        const int c = voiceColourIndex[(size_t) i];
        if (c >= 0 && c < kMaxVoices) used[(size_t) c] = true;
    }
    for (int c = 0; c < kMaxVoices; ++c)
        if (! used[(size_t) c]) return c;
    return 0;
}

int PluginProcessor::addVoice()
{
    const juce::ScopedLock sl(voicesLock);
    const int n = numVoices.load();
    if (n >= kMaxVoices) return -1;
    resetVoiceSlot(n);                              // fresh defaults for the new slot
    voiceColourIndex[(size_t) n] = firstUnusedColourIndex();   // allocate its palette colour
    numVoices.store(n + 1);
    apvts.state.setProperty("numVoices", n + 1, nullptr);
    return n;
}

void PluginProcessor::removeVoice(int idx)
{
    const juce::ScopedLock sl(voicesLock);
    const int n = numVoices.load();
    if (n <= 1 || idx < 0 || idx >= n) return;     // never remove the last voice

    // Shift every higher voice down one slot — APVTS values + gate + modulators.
    for (int d = idx; d < n - 1; ++d)
    {
        copyVoiceParams(d + 1, d);
        voiceColourIndex[(size_t) d] = voiceColourIndex[(size_t) (d + 1)];   // colour follows the voice
        voiceSlots[(size_t) d] = voiceSlots[(size_t) (d + 1)];          // CopyableSpinLock-safe
        gatePatterns[(size_t) d].copyDataFrom(gatePatterns[(size_t) (d + 1)]);
    }
    resetVoiceSlot(n - 1);                          // clear the vacated top slot
    numVoices.store(n - 1);
    apvts.state.setProperty("numVoices", n - 1, nullptr);
}

void PluginProcessor::swapVoices(int a, int b)
{
    const juce::ScopedLock sl(voicesLock);
    const int n = numVoices.load();
    if (a < 0 || b < 0 || a >= n || b >= n || a == b) return;

    auto swapPrefix = [this](const juce::String& pa, const juce::String& pb)
    {
        for (auto* p : getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p))
            {
                const juce::String id = rp->getParameterID();
                if (id.startsWith(pa))
                    if (auto* bp = apvts.getParameter(pb + id.substring(pa.length())))
                    {
                        const float va = rp->getValue();
                        const float vb = bp->getValue();
                        rp->setValueNotifyingHost(vb);
                        bp->setValueNotifyingHost(va);
                    }
            }
    };
    swapPrefix("v"  + juce::String(a) + "_", "v"  + juce::String(b) + "_");
    swapPrefix("ch" + juce::String(a) + "_", "ch" + juce::String(b) + "_");

    std::swap(voiceColourIndex[(size_t) a], voiceColourIndex[(size_t) b]);   // colour follows the voice

    const VoiceSlot tmpSlot = voiceSlots[(size_t) a];
    voiceSlots[(size_t) a] = voiceSlots[(size_t) b];
    voiceSlots[(size_t) b] = tmpSlot;

    GatePattern tmpGate;
    tmpGate.copyDataFrom(gatePatterns[(size_t) a]);
    gatePatterns[(size_t) a].copyDataFrom(gatePatterns[(size_t) b]);
    gatePatterns[(size_t) b].copyDataFrom(tmpGate);
}

void PluginProcessor::resetVoice(int idx)
{
    const juce::ScopedLock sl(voicesLock);
    if (idx < 0 || idx >= numVoices.load()) return;
    const int keepColour = voiceColourIndex[(size_t) idx];
    resetVoiceSlot(idx);                       // params + gate + modulators → defaults
    voiceColourIndex[(size_t) idx] = keepColour;   // identity colour stays
}

void PluginProcessor::saveVoicePreset(int voice, const juce::String& name)
{
    auto dir = getPerSlotPresetDir();
    dir.createDirectory();
    juce::String safe = name.replaceCharacters("\\/:|*?<>\"", "_________");
    if (safe.isEmpty()) safe = "Voice";

    const juce::String prefix = juce::String("v") + juce::String(voice) + "_";
    juce::XmlElement root("MuTantVoice");
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p))
        {
            const juce::String id = rp->getParameterID();
            if (id.startsWith(prefix))
            {
                auto* e = root.createNewChildElement("p");
                e->setAttribute("id", id.substring(prefix.length()));  // voice-agnostic base
                e->setAttribute("v", (double) rp->getValue());         // normalised 0..1
            }
        }

    // Modulators + gate live outside APVTS — serialise them alongside the params
    // so a .muPattern carries the whole voice (matches the full-preset path).
    if (auto mods = mu_pp::serialiseModulators(voiceSlots[(size_t) voice]).createXml())
        root.addChildElement(mods.release());
    if (auto gate = serialiseGate(gatePatterns[(size_t) voice]).createXml())
        root.addChildElement(gate.release());

    root.writeTo(dir.getChildFile(safe + "." + getPerSlotPresetExtension()));
}

void PluginProcessor::loadVoicePreset(int voice, const juce::File& file)
{
    if (! file.existsAsFile()) return;
    auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr || ! xml->hasTagName("MuTantVoice"))
    {
        if (onLoadError) onLoadError("Could not read \"" + file.getFileName() + "\"");
        return;
    }
    const juce::String prefix = juce::String("v") + juce::String(voice) + "_";
    for (auto* e : xml->getChildIterator())
        if (e->hasTagName("p"))
            if (auto* p = apvts.getParameter(prefix + e->getStringAttribute("id")))
                p->setValueNotifyingHost((float) e->getDoubleAttribute("v"));

    // Restore this voice's modulators + gate (per-voice spinlocks inside guard the
    // audio thread, so no suspend/voicesLock needed). Absent children → cleared.
    mu_pp::clearModulators(voiceSlots[(size_t) voice]);
    auto* modsXml = xml->getChildByName("Modulators");
    mu_pp::deserialiseModulators(modsXml ? juce::ValueTree::fromXml(*modsXml) : juce::ValueTree{},
                                 voiceSlots[(size_t) voice], {}, isValidModDest);
    auto* gateXml = xml->getChildByName("Gate");
    deserialiseGate(gateXml ? juce::ValueTree::fromXml(*gateXml) : juce::ValueTree{},
                    gatePatterns[(size_t) voice]);
}

void PluginProcessor::copyVoiceParams(int src, int dst)
{
    auto shift = [this](const juce::String& srcPrefix, const juce::String& dstPrefix)
    {
        for (auto* p : getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p))
            {
                const juce::String id = rp->getParameterID();
                if (id.startsWith(srcPrefix))
                    if (auto* dp = apvts.getParameter(dstPrefix + id.substring(srcPrefix.length())))
                        dp->setValueNotifyingHost(rp->getValue());
            }
    };
    shift("v"  + juce::String(src) + "_", "v"  + juce::String(dst) + "_");
    shift("ch" + juce::String(src) + "_", "ch" + juce::String(dst) + "_");
}

juce::String PluginProcessor::serialiseVoiceColours() const
{
    juce::String s;
    for (int i = 0; i < kMaxVoices; ++i)
        s += (i ? "," : "") + juce::String(voiceColourIndex[(size_t) i]);
    return s;
}

void PluginProcessor::restoreVoiceColours(const juce::String& csv)
{
    if (csv.isEmpty()) return;
    const auto toks = juce::StringArray::fromTokens(csv, ",", "");
    for (int i = 0; i < kMaxVoices && i < toks.size(); ++i)
        voiceColourIndex[(size_t) i] = juce::jlimit(0, kMaxVoices - 1, toks[i].getIntValue());
}

void PluginProcessor::writeVoiceDataToState()
{
    // Rebuild a fresh <VoiceData> child (drop any stale one) holding each active
    // voice's modulators + gate, so apvts.copyState() carries them into the file.
    apvts.state.removeChild(apvts.state.getChildWithName("VoiceData"), nullptr);

    juce::ValueTree vd("VoiceData");
    const int n = numVoices.load();
    for (int v = 0; v < n; ++v)
    {
        juce::ValueTree voice("Voice");
        voice.setProperty("idx", v, nullptr);
        voice.addChild(mu_pp::serialiseModulators(voiceSlots[(size_t) v]), -1, nullptr);
        voice.addChild(serialiseGate(gatePatterns[(size_t) v]),            -1, nullptr);
        vd.addChild(voice, -1, nullptr);
    }
    apvts.state.addChild(vd, -1, nullptr);
}

void PluginProcessor::readVoiceDataFromState()
{
    // Clear the active voices first so loading a preset without <VoiceData> (older
    // or foreign) yields a clean slate rather than stale modulators / gates.
    auto vd = apvts.state.getChildWithName("VoiceData");

    // Clear then restore each active voice. clearModulators is required because
    // deserialiseModulators accumulates assignments; deserialiseGate self-clears.
    // An absent <VoiceData> (older / foreign preset) leaves every voice cleared.
    const int n = numVoices.load();
    for (int v = 0; v < n; ++v)
    {
        mu_pp::clearModulators(voiceSlots[(size_t) v]);
        juce::ValueTree voice;
        for (int i = 0; i < vd.getNumChildren(); ++i)
            if (vd.getChild(i).getType() == juce::Identifier("Voice")
                && (int) vd.getChild(i).getProperty("idx", -1) == v)
            { voice = vd.getChild(i); break; }

        mu_pp::deserialiseModulators(voice.getChildWithName("Modulators"),
                                     voiceSlots[(size_t) v], {}, isValidModDest);
        deserialiseGate(voice.getChildWithName("Gate"), gatePatterns[(size_t) v]);
    }
}

void PluginProcessor::resetVoiceSlot(int idx)
{
    auto resetPrefix = [this](const juce::String& prefix)
    {
        for (auto* p : getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p))
            {
                const juce::String id = rp->getParameterID();
                if (id.startsWith(prefix))
                    rp->setValueNotifyingHost(rp->getDefaultValue());
            }
    };
    resetPrefix("v"  + juce::String(idx) + "_");
    resetPrefix("ch" + juce::String(idx) + "_");
    gatePatterns[(size_t) idx].copyDataFrom(GatePattern{});
    voiceSlots[(size_t) idx] = VoiceSlot{};
}

// ── Presets ──────────────────────────────────────────────────────────────────
juce::File PluginProcessor::getContentDir() const
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
               .getChildFile("TDP").getChildFile("muTant");
}

juce::File PluginProcessor::getPresetsDir()     const { return getContentDir().getChildFile("Presets"); }
juce::File PluginProcessor::getPerSlotPresetDir() const { return getContentDir().getChildFile("Voices"); }

void PluginProcessor::savePreset(const juce::String& name, const juce::String& desc,
                                 const juce::String& category, bool /*embedSamples*/)
{
    auto dir = getPresetsDir();
    dir.createDirectory();

    juce::String safe = name.replaceCharacters("\\/:|*?<>\"", "_________");
    if (safe.isEmpty()) safe = "Preset";

    // Wrap the whole APVTS state with name / description / category metadata.
    juce::XmlElement root("MuTantPreset");
    root.setAttribute("name", name);
    root.setAttribute("description", desc);
    root.setAttribute("category", category);
    apvts.state.setProperty("numVoices", numVoices.load(), nullptr);
    apvts.state.setProperty("voiceColours", serialiseVoiceColours(), nullptr);
    writeVoiceDataToState();
    if (auto state = apvts.copyState().createXml())
        root.addChildElement(state.release());

    root.writeTo(dir.getChildFile(safe + "." + getFullPresetExtension()));
}

void PluginProcessor::loadPreset(const juce::File& file)
{
    if (! file.existsAsFile()) return;

    auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr)
    {
        if (onLoadError) onLoadError("Could not read \"" + file.getFileName() + "\"");
        return;
    }

    // Accept a wrapped MuTantPreset or a bare APVTS state element.
    juce::XmlElement* stateXml = xml->hasTagName("MuTantPreset")
                               ? xml->getChildByName(apvts.state.getType().toString())
                               : xml.get();
    if (stateXml == nullptr)
    {
        if (onLoadError) onLoadError("Preset has no saved state");
        return;
    }

    // Swap the tree under voicesLock — the audio thread tryLocks it in
    // processBlock and outputs a silent block on contention, so the swap is
    // exclusive. Same guard as setStateInformation (one mechanism, not two).
    const juce::ScopedLock sl(voicesLock);
    apvts.replaceState(juce::ValueTree::fromXml(*stateXml));
    numVoices.store(juce::jlimit(1, kMaxVoices, (int) apvts.state.getProperty("numVoices", 1)));
    restoreVoiceColours(apvts.state.getProperty("voiceColours", "").toString());
    readVoiceDataFromState();
    syncAllFxParams();   // re-seed mixer/FX engine state (unchanged values skip listeners)
}

juce::StringArray PluginProcessor::loadCategoryList() const
{
    juce::StringArray cats;
    auto dir = getPresetsDir();
    if (dir.isDirectory())
        for (const auto& f : dir.findChildFiles(juce::File::findFiles, false,
                                                "*." + getFullPresetExtension()))
            if (auto xml = juce::XmlDocument::parse(f))
                if (xml->hasTagName("MuTantPreset"))
                {
                    const auto c = xml->getStringAttribute("category");
                    if (c.isNotEmpty()) cats.addIfNotAlreadyThere(c);
                }
    return cats;
}

} // namespace mu_tant

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new mu_tant::PluginProcessor();
}
