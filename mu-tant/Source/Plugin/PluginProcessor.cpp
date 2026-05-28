#include "Plugin/PluginProcessor.h"
#include "Plugin/PluginEditor.h"
#include "Audio/Scales.h"
#include "Modulation/MuTantModDest.h"

namespace mu_tant
{

namespace
{
    juce::StringArray rootNames()
    {
        return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    }
    juce::StringArray scaleNames()
    {
        juce::StringArray a;
        for (const auto& s : kScales) a.add(s.name);
        return a;
    }

    // Build the parameter family for a single voice. Called for v0..v7 so each
    // voice gets its own independent osc/xmod/filter/level state in the APVTS.
    void addVoiceParams(juce::AudioProcessorValueTreeState::ParameterLayout& layout,
                        int voice)
    {
        using namespace juce;
        auto f = [](float lo, float hi, float step) { return NormalisableRange<float>(lo, hi, step); };
        auto id = [voice](const char* base) {
            return PluginProcessor::voiceParamId(voice, base);
        };
        auto label = [voice](const char* base) {
            return juce::String("V") + juce::String(voice + 1) + " " + base;
        };

        // Per-oscillator pitch — all integer-stepped. Octave ±3 offset, Semi
        // ±12 (scale-degree, see Scales.h), Fine ±100 cents. Wavetable position
        // is a 0..255 frame index (256-frame Serum/Vital tables).
        layout.add(std::make_unique<AudioParameterInt>(ParameterID{id("o1_oct"),  1}, label("Osc1 Octave"),   -3, 3, 0));
        layout.add(std::make_unique<AudioParameterInt>(ParameterID{id("o1_semi"), 1}, label("Osc1 Semi"),     -12, 12, 0));
        layout.add(std::make_unique<AudioParameterInt>(ParameterID{id("o1_fine"), 1}, label("Osc1 Fine"),     -100, 100, 0));
        layout.add(std::make_unique<AudioParameterInt>(ParameterID{id("o1_pos"),  1}, label("Osc1 Position"), 0, 255, 0));
        layout.add(std::make_unique<AudioParameterInt>(ParameterID{id("o2_oct"),  1}, label("Osc2 Octave"),   -3, 3, 0));
        layout.add(std::make_unique<AudioParameterInt>(ParameterID{id("o2_semi"), 1}, label("Osc2 Semi"),     -12, 12, 2));
        layout.add(std::make_unique<AudioParameterInt>(ParameterID{id("o2_fine"), 1}, label("Osc2 Fine"),     -100, 100, 0));
        layout.add(std::make_unique<AudioParameterInt>(ParameterID{id("o2_pos"),  1}, label("Osc2 Position"), 0, 255, 0));

        // Cross-mod.
        layout.add(std::make_unique<AudioParameterInt>   (ParameterID{id("xmod"), 1},  label("X-Mod"),      0, 127, 0));
        layout.add(std::make_unique<AudioParameterChoice>(ParameterID{id("xmode"), 1}, label("X-Mod Mode"), StringArray{ "Off", "FM", "Sync" }, 0));

        // Per-source levels (replace the old osc-balance "mix").
        layout.add(std::make_unique<AudioParameterFloat> (ParameterID{id("o1_lvl"),   1}, label("Osc1 Level"),  f(-60.0f, 6.0f, 0.1f), 0.0f));
        layout.add(std::make_unique<AudioParameterFloat> (ParameterID{id("o2_lvl"),   1}, label("Osc2 Level"),  f(-60.0f, 6.0f, 0.1f), -6.0f));
        layout.add(std::make_unique<AudioParameterFloat> (ParameterID{id("noise_lvl"),1}, label("Noise Level"), f(-60.0f, 6.0f, 0.1f), -60.0f));
        layout.add(std::make_unique<AudioParameterChoice>(ParameterID{id("noise_type"),1}, label("Noise Type"), StringArray{ "White", "Pink" }, 0));

        // Filter (mu-core) — match mu-clid's filter feel: cutoff 20..20000 Hz
        // log-skewed so the dial centre lands on 640 Hz; resonance 0..0.99.
        // Value formatting lives on the parameter (not the slider) because the
        // JUCE SliderParameterAttachment overwrites the slider's
        // textFromValueFunction with one that calls param.getText — so a
        // slider-side formatter would be clobbered on every voice rebind.
        NormalisableRange<float> cutoff(20.0f, 20000.0f);
        cutoff.setSkewForCentre(640.0f);
        auto cutoffText = [](float v, int) -> String {
            return v < 1000.0f ? String((int) std::round(v))
                               : String(v / 1000.0f, 1);
        };
        auto resText = [](float v, int) -> String { return String((int) std::round(v * 100.0f)); };
        layout.add(std::make_unique<AudioParameterInt>  (ParameterID{id("flt_type"), 1}, label("Filter Type"), 0, 15, 0));
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id("flt_cut"), 1},  label("Cutoff"), cutoff, 8000.0f,
                    AudioParameterFloatAttributes().withStringFromValueFunction(cutoffText)));
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id("flt_res"), 1},  label("Resonance"), f(0.0f, 0.99f, 0.001f), 0.2f,
                    AudioParameterFloatAttributes().withStringFromValueFunction(resText)));

        // Per-voice slot output level — distinct from the mixer fader (engine-level
        // trim before the channel strip; the mixer adds its own per-channel level
        // / pan / mute / solo on top, matching the mu-clid signal flow).
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id("level"), 1}, label("Level"), f(-60.0f, 6.0f, 0.1f), -6.0f));

        // Gate Gap — percentage of every gate-envelope region forced to silence
        // at its end, for a cleaner gate. Integer 0..100 %; consumed as /100.
        auto gapText = [](float v, int) -> String { return String((int) std::round(v)) + " %"; };
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id("gate_gap"), 1}, label("Gate Gap"), f(0.0f, 100.0f, 1.0f), 0.0f,
                    AudioParameterFloatAttributes().withStringFromValueFunction(gapText)));

        // Gater bypass — when on, the gate stage is skipped (raw drone passes,
        // for audition / configuration).
        layout.add(std::make_unique<AudioParameterBool>(ParameterID{id("gate_bypass"), 1}, label("Gate Bypass"), false));

        // Insert effect (shared mu-core InsertProcessor) — same schema as mu-clid:
        // `drvChar` = algorithm 0..(N-1), `insP1..insP4` = generic 0..1 slot params.
        layout.add(std::make_unique<AudioParameterInt>  (ParameterID{id("drvChar"), 1}, label("Insert Algo"),
                                                         0, InsertProcessor::kNumInsertAlgos - 1, 0));
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id("insP1"), 1}, label("Insert P1"), f(0.0f, 1.0f, 0.0f), 0.0f));
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id("insP2"), 1}, label("Insert P2"), f(0.0f, 1.0f, 0.0f), 0.0f));
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id("insP3"), 1}, label("Insert P3"), f(0.0f, 1.0f, 0.0f), 0.0f));
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id("insP4"), 1}, label("Insert P4"), f(0.0f, 1.0f, 0.0f), 0.0f));
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    auto f = [](float lo, float hi, float step) { return NormalisableRange<float>(lo, hi, step); };

    // Shared tonal centre.
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID{"root",  1}, "Root",  rootNames(),  0));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID{"scale", 1}, "Scale", scaleNames(), 0));

    for (int v = 0; v < kMaxVoices; ++v)
        addVoiceParams(layout, v);

    // ── Mixer channel strips (4 params × 8 channels) — matches the shared
    //    mu-core MixerChannel binding prefix `ch{N}_`. FX sends (sendEff/Dly/Rev)
    //    and sidechain params are deliberately omitted until the MixerEngine
    //    voice-render-callback refactor lets mu-tant route through the shared
    //    mixer / FX path. Until then the strips' send / sidechain knobs are
    //    inert (the MixerChannel binding tolerates missing params).
    for (int i = 0; i < kMaxVoices; ++i)
    {
        const String c = "ch" + String(i) + "_";
        const String n = "Voice " + String(i + 1) + " Ch ";
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{c+"lvl",  1}, n+"Level", f(0.0f, 1.0f, 0.001f), 1.0f));
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{c+"pan",  1}, n+"Pan",   f(-1.0f, 1.0f, 0.001f), 0.0f));
        layout.add(std::make_unique<AudioParameterBool> (ParameterID{c+"mute", 1}, n+"Mute",  false));
        layout.add(std::make_unique<AudioParameterBool> (ParameterID{c+"solo", 1}, n+"Solo",  false));
    }

    // ── Master fader ────────────────────────────────────────────────────────
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"mstr_lvl", 1}, "Master Level", f(0.0f, 1.0f, 0.001f), 1.0f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"mstr_pan", 1}, "Master Pan",   f(-1.0f, 1.0f, 0.001f), 0.0f));

    return layout;
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

    // Pre-allocate modParamValues entries once so the audio-thread map
    // never allocates — keys live in static storage (the kModDestTable string
    // literals), making string_view safe for the lifetime of the entries.
    modParamValues.reserve((size_t) kModDestCount);
    for (int i = 0; i < kModDestCount; ++i)
        modParamValues[kModDestTable[i].id] = 0.0f;
}

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    for (auto& v : voices)
        if (v) v->prepare(sampleRate, samplesPerBlock);

    // Pre-allocate per-voice render scratch so processBlock never allocates.
    for (auto& buf : voiceBuffers)
        buf.setSize(2, samplesPerBlock, false, true, true);

    for (auto& ins : inserts)
        ins.prepare(sampleRate, samplesPerBlock);

    mixerEngine.prepare(sampleRate, samplesPerBlock);
}

VoiceConfig PluginProcessor::readConfig(int voiceIdx) const
{
    auto raw = [this](const juce::String& id) {
        return apvts.getRawParameterValue(id)->load();
    };
    auto vid = [voiceIdx](const char* base) {
        return voiceParamId(voiceIdx, base);
    };

    VoiceConfig c;
    // Tonal centre is global.
    c.root         = (int) raw("root");
    c.scaleIdx     = (int) raw("scale");
    // Per-voice osc pitch (integer) + position.
    c.osc1Octave   = (int) raw(vid("o1_oct"));
    c.osc1Semi     = (int) raw(vid("o1_semi"));
    c.osc1Fine     = (int) raw(vid("o1_fine"));
    c.osc1Pos      = raw(vid("o1_pos"));
    c.osc2Octave   = (int) raw(vid("o2_oct"));
    c.osc2Semi     = (int) raw(vid("o2_semi"));
    c.osc2Fine     = (int) raw(vid("o2_fine"));
    c.osc2Pos      = raw(vid("o2_pos"));
    c.xmod         = (int) raw(vid("xmod"));
    c.xmodMode     = (int) raw(vid("xmode"));
    // Per-source levels.
    c.osc1LevelDb  = raw(vid("o1_lvl"));
    c.osc2LevelDb  = raw(vid("o2_lvl"));
    c.noiseLevelDb = raw(vid("noise_lvl"));
    c.noiseType    = (int) raw(vid("noise_type"));
    c.filterType   = (int) raw(vid("flt_type"));
    c.filterCutoff = raw(vid("flt_cut"));
    c.filterRes    = raw(vid("flt_res"));
    c.levelDb      = raw(vid("level"));
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

    // Transport snapshot for this block. Beat advances only while playing; when
    // stopped the gate is held fully open so the oscillators stay auditionable.
    const bool   isPlaying     = playing.load(std::memory_order_relaxed);
    const double beatStart     = internalBeatPos.load(std::memory_order_relaxed);
    const double beatsPerSample = (internalBpm / 60.0) / currentSampleRate;

    // Read mixer state directly from APVTS — keeps the channel-strip UI as the
    // single source of truth (no parameter-listener push into a separate engine
    // struct). Solo wins over mute, matching mu-clid's mixer convention.
    auto raw = [this](const juce::String& id) {
        return apvts.getRawParameterValue(id)->load();
    };
    auto chId = [](int i, const char* base) {
        return juce::String("ch") + juce::String(i) + "_" + base;
    };

    bool anySolo = false;
    for (int v = 0; v < numActiveVoices; ++v)
        if (raw(chId(v, "solo")) > 0.5f) { anySolo = true; break; }

    for (int v = 0; v < numActiveVoices; ++v)
    {
        const bool muted   = raw(chId(v, "mute")) > 0.5f;
        const bool soloed  = raw(chId(v, "solo")) > 0.5f;
        const bool audible = anySolo ? soloed : !muted;
        if (!audible) continue;

        const float level = raw(chId(v, "lvl"));
        const float pan   = juce::jlimit(-1.0f, 1.0f, raw(chId(v, "pan")));

        // Modulation pass — seed paramValues from the current APVTS-derived
        // config, then let this voice's matrix overwrite any modulated keys.
        // The matrix is empty for voices the user hasn't assigned, so this
        // collapses to a no-op + the seeded values pass through unchanged.
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

        // tryLock-equivalent: ModulationMatrix mutations on the message thread
        // hold modLock; on contention we skip the modulation pass this block
        // (the voice still plays with un-modulated config).
        auto& slot = voiceSlots[(size_t) v];
        bool expected = false;
        if (slot.modLock.compare_exchange_strong(expected, true, std::memory_order_acquire))
        {
            slot.modulationMatrix.process(slot.controlSequences, beatStart, modParamValues);
            slot.modLock.store(false, std::memory_order_release);

            // Read modulated values back into the config.
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

        auto& voiceBuf = voiceBuffers[(size_t) v];
        voiceBuf.clear();
        voices[(size_t) v]->setConfig(cfg);
        voices[(size_t) v]->process(voiceBuf, numSamples);

        // ── Gate stage ────────────────────────────────────────────────────────
        // The per-voice gater shapes the (post-filter) voice output:
        //   bypassed             → raw drone passes (audition / configure)
        //   stopped              → silence (gate closed — nothing audible on load)
        //   playing, no envelopes→ silence (nothing drawn → nothing passes)
        //   playing, envelopes   → per-sample envelope gate
        // See applyGateBlock — the audio path + the audio test harness share it.
        auto& pattern = gatePatterns[(size_t) v];
        const float gateGap    = raw(voiceParamId(v, "gate_gap")) * 0.01f;   // 0..100% → 0..1
        const bool  gateBypass = raw(voiceParamId(v, "gate_bypass")) > 0.5f;
        float* gl = voiceBuf.getWritePointer(0);
        float* gr = voiceBuf.getNumChannels() > 1 ? voiceBuf.getWritePointer(1) : nullptr;
        applyGateBlock(pattern, gl, gr, numSamples, gateGap, gateBypass, isPlaying,
                       beatStart, beatsPerSample);

        // ── Insert effect ───────────────────────────────────────────────────
        // Shared mu-core InsertProcessor, post-gate: engine → insert → mixer
        // (the family-wide signal flow). Algo 0 (None) is a passthrough.
        {
            VoiceParams ip;
            ip.insertAlgo     = (int) raw(voiceParamId(v, "drvChar"));
            ip.insertParam[0] = raw(voiceParamId(v, "insP1"));
            ip.insertParam[1] = raw(voiceParamId(v, "insP2"));
            ip.insertParam[2] = raw(voiceParamId(v, "insP3"));
            ip.insertParam[3] = raw(voiceParamId(v, "insP4"));
            inserts[(size_t) v].process(voiceBuf, numSamples, voiceBuf.getNumChannels(), ip);
        }

        // Equal-power constant-power pan, matching MixerEngine::applyPanGain.
        const float angle = (pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
        const float gainL = level * std::cos(angle);
        const float gainR = level * std::sin(angle);

        buffer.addFrom(0, 0, voiceBuf, 0, 0, numSamples, gainL);
        buffer.addFrom(1, 0, voiceBuf, 1, 0, numSamples, gainR);
    }

    // Master fader.
    buffer.applyGain(raw("mstr_lvl"));

    // Advance the transport beat only while playing. Wrap at the 2-bar pattern
    // length (8 beats in 4/4) so the gate + the gating-grid playhead loop cleanly
    // and floating-point precision never drifts. Modulators evaluate against the
    // same wrapped position.
    if (isPlaying)
    {
        double pos = beatStart + beatsPerSample * (double) numSamples;
        const double patBeats = (double) GatePattern::kTotalBars * 4.0;   // 8 beats
        if (pos >= patBeats) pos -= patBeats;
        internalBeatPos.store(pos, std::memory_order_relaxed);
    }
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor(*this);
}

void PluginProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    apvts.state.setProperty("numVoices", numVoices.load(), nullptr);
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
    }
}

// ── Dynamic voice management ─────────────────────────────────────────────────
int PluginProcessor::addVoice()
{
    const juce::ScopedLock sl(voicesLock);
    const int n = numVoices.load();
    if (n >= kMaxVoices) return -1;
    resetVoiceSlot(n);                              // fresh defaults for the new slot
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

    const VoiceSlot tmpSlot = voiceSlots[(size_t) a];
    voiceSlots[(size_t) a] = voiceSlots[(size_t) b];
    voiceSlots[(size_t) b] = tmpSlot;

    GatePattern tmpGate;
    tmpGate.copyDataFrom(gatePatterns[(size_t) a]);
    gatePatterns[(size_t) a].copyDataFrom(gatePatterns[(size_t) b]);
    gatePatterns[(size_t) b].copyDataFrom(tmpGate);
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

    // Swap the tree off the audio thread.
    suspendProcessing(true);
    apvts.replaceState(juce::ValueTree::fromXml(*stateXml));
    numVoices.store(juce::jlimit(1, kMaxVoices, (int) apvts.state.getProperty("numVoices", 1)));
    suspendProcessing(false);
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
