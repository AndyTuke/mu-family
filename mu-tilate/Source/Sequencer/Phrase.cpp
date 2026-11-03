#include "Sequencer/Phrase.h"

#include "Modulation/LaneModulation.h"   // mu-core: mu_mod::resolveLane
#include "Audio/LoopLoader.h"            // mu-core: resampleLoop
#include "Modulation/StripMods.h"        // mu-core: paramIdFor

#include <algorithm>
#include <cstring>

// The Phrase layer's half of the render contract (docs/mu-tronic/design.md 2.1).
namespace mu_tilate
{

namespace
{
    // The ppi row a modulation target drives, found from its table parameter name.
    int rowFor(const char* param)
    {
        for (int k = 0; k < ppi::COUNT; ++k)
            if (std::strcmp(ppi::suffix[k], param) == 0) return k;
        jassertfalse;   // a table row names a parameter that does not exist
        return 0;
    }
}

void Phrase::setData(std::shared_ptr<const PhraseData> d)
{
    const double beats = d != nullptr ? d->timeline.lengthBeats() : -1.0;
    {
        mu_core::ScopedSpinLock<CopyableSpinLock> lock(dataLock);
        data.swap(d);
    }
    loopLengthBeats.store(beats, std::memory_order_relaxed);
    // `d` now holds the old data and is released here, on the message thread, outside the lock.
}

std::shared_ptr<const PhraseData> Phrase::getData() const
{
    mu_core::ScopedSpinLock<CopyableSpinLock> lock(dataLock);
    return data;
}

void Phrase::attachResampled(PhraseData& d) const
{
    d.resampled.reset();
    d.resampledRate = 0.0;
    if (d.audio == nullptr || hostRate <= 0.0 || std::abs(hostRate - d.fileSampleRate) < 0.5) return;
    d.resampled = mu_core::resampleLoop(*d.audio, d.fileSampleRate, hostRate);
    d.resampledRate = hostRate;
}

void Phrase::installLoop(std::shared_ptr<const juce::AudioBuffer<float>> audio, double fileSampleRate,
                         const juce::String& newPath, const juce::String& newName,
                         std::shared_ptr<const juce::AudioBuffer<float>> src, int rStart, int rEnd)
{
    path = newPath;
    loadedPath = newPath;
    displayName = newName;
    auto d = std::make_shared<PhraseData>();
    d->version = nextPhraseId();
    d->audio = std::move(audio);
    d->source = src != nullptr ? std::move(src) : d->audio;
    d->regionStart = rStart;
    d->regionEnd = rEnd > 0 ? rEnd : (d->source != nullptr ? d->source->getNumSamples() : 0);
    d->fileSampleRate = fileSampleRate;
    const int len = d->audio != nullptr ? d->audio->getNumSamples() : 0;
    // Saved edits belong to the loop they were made on: the same length means the same loop.
    const bool sameLoop = len > 0 && pending.loopLength == len;
    d->slices   = sameLoop ? pending.slices : SliceList(len);
    d->timeline = pending.timeline;
    attachResampled(*d);
    d->audioId = nextPhraseId();
    setData(std::move(d));
}

void Phrase::startPreview(PreviewKind kind, int slice, double bpm)
{
    const auto d = getData();
    if (d == nullptr || d->audio == nullptr || d->audio->getNumSamples() < 2) return;
    previewIsSlice = kind == PreviewKind::Slice;
    previewAudioLen = d->audio->getNumSamples();
    previewSliceStart = 0; previewSliceLen = 0;
    if (previewIsSlice)
    {
        const auto r = d->slices.slice(juce::jlimit(0, d->slices.size() - 1, slice));
        previewSliceStart = r.start; previewSliceLen = r.length();
    }
    // The whole-loop auditions play the play area (which can move while they sound); a single slice plays all of itself.
    applyPlayArea();
    preview.setLooping(previewLoop && ! previewIsSlice);
    if (kind == PreviewKind::Original) { preview.startBuffer(*d->audio, d->fileSampleRate); return; }

    // The settings that time the loop (the layer's unmodulated values), then the loop or one slice laid out from beat 0.
    auto g = [this](int row) { return pp[(size_t) row].load(0.0f); };
    PhraseSettings s;
    s.mode = (TempoMode) juce::jlimit(0, 1, (int) g(ppi::mode));
    s.stretchMix = g(ppi::tmix);
    s.loopTempo = (double) g(ppi::ltempo);
    s.holdMs = g(ppi::hold);
    s.strength = g(ppi::strength);
    s.fadeInMs = g(ppi::fadeIn);
    s.fadeOutMs = g(ppi::fadeOut);
    if (s.loopTempo <= 0.0 || bpm <= 0.0) return;
    const double sr = hostRate > 0.0 ? hostRate : 48000.0;

    std::vector<double> beats;
    for (int i = 0; i < d->slices.size(); ++i)
        beats.push_back((double) d->slices.slice(i).length() / d->fileSampleRate * s.loopTempo / 60.0);
    Timeline tl(1.0);
    double total = 0.0;
    if (kind == PreviewKind::AtTempo)
    {
        for (double b : beats) total += b;
        tl.setLengthBeats(std::ceil(std::max(1.0, total) - 1.0e-6));
        tl.originalOrder(beats);
    }
    else
    {
        const int at = juce::jlimit(0, d->slices.size() - 1, slice);
        total = beats[(size_t) at];
        tl.setLengthBeats(std::ceil(std::max(1.0, total) - 1.0e-6));
        tl.place(at, 0.0, 0.0);
    }

    PhraseLoop loop;
    const bool useCopy = d->resampled != nullptr && std::abs(d->resampledRate - sr) < 0.5;
    loop.audio = useCopy ? d->resampled.get() : d->audio.get();
    loop.fileSampleRate = useCopy ? d->resampledRate : d->fileSampleRate;
    loop.sliceScale = useCopy ? d->resampledRate / d->fileSampleRate : 1.0;
    loop.slices = &d->slices;
    loop.timeline = &tl;

    // Offline: a scheduler of its own plays the layout through its blocks into a buffer, which is then published to the player.
    PhrasePlayback pb;
    pb.prepare(sr, 512, pp[ppi::window].load(1.0f) < 0.5f);
    const int frames = juce::jmax(2, (int) std::lround(total * 60.0 / bpm * sr));
    juce::AudioBuffer<float> out(2, frames);
    out.clear();
    for (int pos = 0; pos < frames; pos += 512)
    {
        const int n = juce::jmin(512, frames - pos);
        PhraseClock c;
        c.playing = true; c.bpm = bpm; c.sampleRate = sr;
        c.beatsPerSample = bpm / 60.0 / sr;
        c.startBeat = (double) pos * c.beatsPerSample;
        juce::AudioBuffer<float> view(out.getArrayOfWritePointers(), 2, pos, n);
        pb.render(loop, s, c, view, n);
    }
    preview.startBuffer(out, sr);
}

// The loop cut to [start, end): the audio copied, the slices cut with it and the hits remapped (a hit whose slice went has none).
std::shared_ptr<const PhraseData> Phrase::croppedData(const PhraseData& d, int start, int end, std::vector<int>* sliceMap) const
{
    const int n = d.audio->getNumSamples();
    const int a = juce::jlimit(0, n - 2, start), b = juce::jlimit(a + 2, n, end);
    auto cut = std::make_shared<juce::AudioBuffer<float>>(d.audio->getNumChannels(), b - a);
    for (int ch = 0; ch < cut->getNumChannels(); ++ch) cut->copyFrom(ch, 0, *d.audio, ch, a, b - a);

    auto c = std::make_shared<PhraseData>();
    c->audio = std::move(cut);
    // The source stays whole, so the section can be widened again from the range bar.
    c->source = d.source != nullptr ? d.source : d.audio;
    const int base = d.source != nullptr ? d.regionStart : 0;
    c->regionStart = base + a;
    c->regionEnd = base + b;
    c->fileSampleRate = d.fileSampleRate;
    std::vector<int> map;
    c->slices = d.slices.cropped(a, b, map);
    c->timeline = d.timeline;
    c->timeline.remapSlices(map);
    attachResampled(*c);
    if (sliceMap != nullptr) *sliceMap = std::move(map);
    return c;
}

// Gives the audition the play area as fractions of what it plays (a slice plays all of itself).
void Phrase::applyPlayArea() noexcept
{
    if (previewIsSlice || previewAudioLen <= 0 || areaEnd - areaStart < 2) { preview.setRegion(0.0f, 1.0f); return; }
    preview.setRegion((float) areaStart / (float) previewAudioLen, (float) areaEnd / (float) previewAudioLen);
}

double Phrase::previewPlayhead() const noexcept
{
    const float f = preview.getPosition();
    if (f < 0.0f) return -1.0;
    return previewIsSlice ? (double) previewSliceStart + (double) f * (double) previewSliceLen : (double) f * (double) previewAudioLen;
}

void Phrase::cropTo(int start, int end)
{
    const auto d = getData();
    if (d == nullptr || d->audio == nullptr) return;
    const int n = d->audio->getNumSamples();
    if (end - start < 2 || (start <= 0 && end >= n)) return;
    setData(croppedData(*d, start, end, nullptr));
    areaStart = areaEnd = 0;
}

void Phrase::commitEdit(const SliceList& slices, const Timeline& timeline)
{
    auto cur = getData();
    auto d = std::make_shared<PhraseData>();
    d->version = nextPhraseId();
    if (cur != nullptr)
    {
        d->audioId = cur->audioId;   // the same audio: the sounding slice plays on
        d->audio = cur->audio; d->fileSampleRate = cur->fileSampleRate;
        d->source = cur->source; d->regionStart = cur->regionStart; d->regionEnd = cur->regionEnd;
        d->resampled = cur->resampled; d->resampledRate = cur->resampledRate;   // the audio is the same: so is its copy
    }
    d->slices = slices;
    d->timeline = timeline;
    setData(std::move(d));
}

void Phrase::writeExtras(juce::ValueTree& node) const
{
    Layer::writeExtras(node);
    PhraseState st;
    const auto d = getData();
    if (d != nullptr && d->audio != nullptr)
    {
        st.loopLength = d->audio->getNumSamples();
        const int srcLen = d->source != nullptr ? d->source->getNumSamples() : 0;
        const bool whole = d->regionStart <= 0 && (d->regionEnd <= 0 || d->regionEnd >= srcLen);
        st.regionStart = whole ? 0 : d->regionStart;
        st.regionEnd = whole ? 0 : d->regionEnd;
        st.slices = d->slices;
        st.timeline = d->timeline;
    }
    else
        st = pending;          // no loop loaded yet: keep what the file said
    st.loopPath = path;
    st.loopName = displayName;
    writePhraseState(node, st);
}

void Phrase::applyExtras(const juce::ValueTree& node)
{
    Layer::applyExtras(node);
    pending = readPhraseState(node);
    path = pending.loopPath;
    displayName = pending.loopName;

    // A loop already loaded keeps its audio; the saved slices apply if they fit it, and the timeline always does.
    const auto cur = getData();
    if (cur != nullptr && cur->audio != nullptr)
    {
        auto d = std::make_shared<PhraseData>();
        d->version = nextPhraseId();
        d->audioId = cur->audioId;
        d->audio = cur->audio;
        d->source = cur->source; d->regionStart = cur->regionStart; d->regionEnd = cur->regionEnd;
        d->fileSampleRate = cur->fileSampleRate;
        d->resampled = cur->resampled; d->resampledRate = cur->resampledRate;
        d->slices = pending.loopLength == cur->audio->getNumSamples() ? pending.slices : SliceList(cur->audio->getNumSamples());
        d->timeline = pending.timeline;
        setData(std::move(d));
    }
}

void Phrase::prepare(double sampleRate, int blockSize)
{
    // A host rate that differs from the one the loop was resampled for needs a new copy (Stretch plays only a loop at the host rate).
    hostRate = sampleRate;
    if (const auto cur = getData(); cur != nullptr && cur->audio != nullptr)
    {
        const bool needs = std::abs(sampleRate - cur->fileSampleRate) >= 0.5;
        if ((needs && std::abs(cur->resampledRate - sampleRate) >= 0.5) || (! needs && cur->resampled != nullptr))
        {
            auto d = std::make_shared<PhraseData>(*cur);
            attachResampled(*d);
            d->version = nextPhraseId();
            d->audioId = nextPhraseId();   // a new host-rate copy: the voices may point into the old one
            setData(std::move(d));
        }
    }

    // The stretch window is a per-layer choice made here (Short for drums, Default for tonal loops).
    playback->prepare(sampleRate, juce::jmax(16, blockSize), pp[ppi::window].load(1.0f) < 0.5f);
    insert->prepare(sampleRate, blockSize);
    preview.prepare(juce::jmax(16, blockSize), sampleRate);
    tailScratch.setSize(2, juce::jmax(1, blockSize));
    tailScratch.clear();
    parked = true;
    lastAudioId = 0;
    lastResolved.reset();   // a prepare is a fresh start: no held modulated values
}

void Phrase::bindParams(juce::AudioProcessorValueTreeState& apvts, int slot)
{
    const auto prefix = layerParamPrefix(slot);
    for (int k = 0; k < kNumPhraseParams; ++k)
        pp[(size_t) k] = mu_core::ParamBinding(apvts.getRawParameterValue(prefix + ppi::suffix[k]), this);

    // The modulation inputs are the same atomics, in target-table order.
    for (int k = 0; k < kNumModDests; ++k)
    {
        const auto id = mu_mod::paramIdFor(prefix, slot, kModDestTable[k].param);
        modDestIds[(size_t) k]    = kModDestTable[k].id;
        modDestRanges[(size_t) k] = apvts.getParameterRange(id);
        modDestAtoms[(size_t) k]  = apvts.getRawParameterValue(id);
        modParamValues[kModDestTable[k].id] = 0.0f;   // pre-size the map: the audio thread only overwrites
    }
    lastResolved.reset();   // values held belong to the slot bound before
}

void Phrase::readParams(double modBeat) noexcept
{
    auto g = [this](int row) { return pp[(size_t) row].load(0.0f); };

    // Resolve this layer's modulation matrix over its control sequences -> out[] (param units). A block that misses
    // the matrix lock holds the last resolved values, so no target snaps back to its knob for a block.
    float out[kNumModDests];
    const bool resolved = mu_mod::resolveLane(this, modBeat, kNumModDests, modDestIds.data(), modDestAtoms.data(),
                                              modDestRanges.data(), modParamValues, out);
    lastResolved.apply(resolved, out);
    for (int k = 0; k < kNumModDests; ++k)
        modLive.v[(size_t) k].store(out[k], std::memory_order_relaxed);
    stripMod.publish(out[D_stripPan], out[D_sendEff], out[D_sendDly], out[D_sendRev]);   // the host gives these to the layer's mixer strip

    settings.mode      = (TempoMode) juce::jlimit(0, 1, (int) g(ppi::mode));   // not modulated
    settings.stretchMix = out[D_tempoMix];
    settings.loopTempo = (double) out[D_loopTempo];
    settings.holdMs    = out[D_hold];
    settings.strength  = out[D_strength];
    settings.fadeInMs  = out[D_fadeIn];
    settings.fadeOutMs = out[D_fadeOut];
    settings.gain      = out[D_level];
    settings.filterType = juce::jlimit(0, (int) filterChoices().size() - 1, (int) g(ppi::fType));   // the filter's type is a choice: not modulated
    settings.cutoffHz   = out[D_cutoff];
    settings.resonance  = out[D_res];
    settings.pitchSemi  = out[D_pitch];   settings.fineCents = out[D_fine];   settings.pan = out[D_pan];
    settings.ampA = out[D_ampA];  settings.ampD = out[D_ampD];  settings.ampS = out[D_ampS];  settings.ampR = out[D_ampR];
    settings.filterEnvAmt = out[D_filterEnv];
    settings.fltA = out[D_fltA];  settings.fltD = out[D_fltD];  settings.fltS = out[D_fltS];  settings.fltR = out[D_fltR];

    insertCfg.insertAlgo     = (int) g(ppi::drvChar);
    insertCfg.insertParam[0] = out[D_insP1];
    insertCfg.insertParam[1] = out[D_insP2];
    insertCfg.insertParam[2] = out[D_insP3];
    insertCfg.insertParam[3] = out[D_insP4];
    insertCfg.insertSync     = g(ppi::insSync) > 0.5f;
}

void Phrase::renderBlock(const PhraseData* d, PhraseClock clock, juce::AudioBuffer<float>& buf, int n) noexcept
{
    PhraseLoop loop;
    if (d != nullptr)
    {
        const bool useCopy = d->resampled != nullptr && std::abs(d->resampledRate - clock.sampleRate) < 0.5;
        loop.audio = useCopy ? d->resampled.get() : d->audio.get();
        loop.fileSampleRate = useCopy ? d->resampledRate : d->fileSampleRate;
        loop.sliceScale = useCopy ? d->resampledRate / d->fileSampleRate : 1.0;
        loop.slices = &d->slices;
        loop.timeline = &d->timeline;
    }
    playback->render(loop, settings, clock, buf, n);
    // The newest sounding hit moves the insert's knobs by its own offsets (held to the knob's travel) for this block.
    float held[4], off[4];
    for (int i = 0; i < 4; ++i) held[i] = insertCfg.insertParam[i];
    if (playback->insertOffsets(off))
        for (int i = 0; i < 4; ++i) insertCfg.insertParam[i] = juce::jlimit(0.0f, 1.0f, held[i] + off[i]);
    insert->process(buf, n, buf.getNumChannels(), insertCfg);
    for (int i = 0; i < 4; ++i) insertCfg.insertParam[i] = held[i];
}

void Phrase::render(const mu_core::RenderContext& ctx, juce::AudioBuffer<float>& out, int n)
{
    // A slot that has just gone live (or was prepared since) starts from silence.
    if (parked) { playback->hardReset(); lastAudioId = 0; lastResolved.reset(); parked = false; }
    if (stopRequested) { playback->reset(); stopRequested = false; }

    readParams(ctx.startBeat);

    // The loop data for this block, or nothing: a miss keeps time without sound, the next block carries on.
    if (! mu_core::trySpinLock(dataLock)) return;
    const PhraseData* d = data.get();
    const uint64_t audioId = d != nullptr ? d->audioId : 0;
    if (audioId != lastAudioId) { playback->hardReset(); lastAudioId = audioId; }   // the voices point into the old audio: drop them

    PhraseClock clock;
    clock.playing        = ctx.playing != 0;
    clock.startBeat      = ctx.startBeat;
    clock.beatsPerSample = ctx.beatsPerSample;
    clock.bpm            = ctx.bpm;
    clock.sampleRate     = ctx.sampleRate;
    insert->setTempo(ctx.bpm);
    renderBlock(d, clock, out, n);
    mu_core::spinUnlock(dataLock);
    preview.mixInto(out, n);   // an audition sounds with the layer, after its insert, with or without the transport
}

// The tail of a layer that left its slot: the scheduler with the transport stopped (the sounding slice fades through
// its cut fade), faded by the bin, then the insert with the configuration it last had. The layer is out of its slot, so
// nobody swaps its data, and no parameter is read.
bool Phrase::renderRetiring(const mu_core::RenderContext& ctx, juce::AudioBuffer<float>& out, int n, mu_core::RetireState& rs)
{
    juce::AudioBuffer<float> aside;
    if (rs.exclusive == 0)
    {
        aside.setDataToReferTo(tailScratch.getArrayOfWritePointers(), juce::jmin(2, out.getNumChannels()),
                               juce::jmin(n, tailScratch.getNumSamples()));
        aside.clear();
    }
    auto& buf = rs.exclusive != 0 ? out : aside;
    const int frames = juce::jmin(n, buf.getNumSamples());

    // The tail takes the data under the same try-lock and identity check as a live block: a loop replaced since the voices
    // started (they point into the old audio) drops them, and a missed lock renders the voices with no loop in hand.
    PhraseClock idle;
    idle.sampleRate = ctx.sampleRate;
    const bool locked = mu_core::trySpinLock(dataLock);
    const PhraseData* d = locked ? data.get() : nullptr;
    if (locked)
    {
        const uint64_t audioId = d != nullptr ? d->audioId : 0;
        if (audioId != lastAudioId) { playback->hardReset(); lastAudioId = audioId; }
    }
    if (locked)   // the voices read the loop's audio, which the lock protects: a missed lock leaves this block's tail silent
    {
        renderBlock(d, idle, buf, frames);
        mu_core::spinUnlock(dataLock);
    }
    mu_core::applyLinearFade(buf, frames, rs);

    if (rs.exclusive == 0)
        for (int c = 0; c < buf.getNumChannels(); ++c) out.addFrom(c, 0, buf, c, 0, frames);
    return true;   // the bin ends the fade when its ramp is spent
}

} // namespace mu_tilate
