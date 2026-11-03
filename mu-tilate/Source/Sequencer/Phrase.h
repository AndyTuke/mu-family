#pragma once

#include "Audio/InsertProcessor.h"
#include "Audio/SamplePreview.h"        // mu-core: plays a decoded buffer on the audio thread      // mu-core: shared per-voice insert FX
#include "Audio/SpinLock.h"             // mu-core: the try-lock the audio thread takes around the loop data
#include "Audio/VoiceParams.h"          // mu-core: the insert's parameter snapshot
#include "Modulation/LastResolved.h"    // mu-core: hold-last-value on a matrix-lock miss
#include "Modulation/StripMods.h"       // mu-core: the mixer strip's pan and sends as targets
#include "Modulation/MuTilateModDest.h" // the modulation target table
#include "Persistence/PhraseParams.h"   // the Phrase type's parameter table (ppi)
#include "Persistence/PhraseState.h"    // the saved form of the loop reference, slices and timeline
#include "Sequencer/Layer.h"            // mu-core: name, colour, modulation, the render contract
#include "Sequencer/LayerTypeInfo.h"    // mu-core: mu_core::LayerTypeId
#include "Sequencer/PhrasePlayback.h"   // the transport-anchored scheduler (timeline, slices, player)
#include "Sequencer/RetireTail.h"       // mu-core: mu_core::ParamBinding

#include <array>
#include <atomic>
#include <memory>
#include <string_view>
#include <unordered_map>

namespace mu_tilate
{

// Everything about the loop that is not a parameter: the audio, its sample rate, the slices and the timeline. It is
// immutable once a layer holds it; an edit builds a new one and hands it to Phrase::setData.
// A number that is never used twice: the identity of a PhraseData and of its audio. Addresses cannot serve, a freed
// PhraseData's address can be handed to the next one.
inline uint64_t nextPhraseId() noexcept
{
    static std::atomic<uint64_t> counter { 0 };
    return counter.fetch_add(1, std::memory_order_relaxed) + 1;
}

struct PhraseData
{
    // Fresh numbers for every new PhraseData; a copy that keeps the audio carries the audio's number over (commitEdit).
    uint64_t     version = nextPhraseId();   // the editor refreshes when it changes
    uint64_t     audioId = nextPhraseId();   // the voices are dropped when it changes
    std::shared_ptr<const juce::AudioBuffer<float>> audio;   // shared: an edit of the slices or timeline reuses it; the used section of the source
    // The whole decoded file, and the section of it that `audio` is (a copy; the same buffer when the whole file is used). The original
    // file is never changed; trimming picks another section.
    std::shared_ptr<const juce::AudioBuffer<float>> source;
    int          regionStart = 0, regionEnd = 0;   // in the source's samples; the end is the source's length when it is all used
    double       fileSampleRate = 48000.0;
    // The same loop at the host rate when that differs from the file's (a time-stretcher needs it there); the slices stay
    // in the file's samples and are scaled when played. Null when the rates match.
    std::shared_ptr<const juce::AudioBuffer<float>> resampled;
    double       resampledRate = 0.0;
    SliceList    slices { 0 };
    Timeline     timeline;
};

// One mu-Tilate layer, a Phrase: the shared Layer plus its loop data, the scheduler and the insert, rendered through
// the shared layer contract (docs/mu-tronic/design.md 2.1): bindParams caches the slot's parameter atomics; render
// resolves the modulation, then the scheduler plays the timeline's slices and the insert follows.
//
// Threads. The audio thread renders inside the block's layer access. The loop data is shared with the message
// thread through `data`, guarded by `dataLock`: the audio thread only ever try-locks it for one block (a miss renders
// nothing and keeps time, as mu-Clid's sample buffer does), the message thread swaps the pointer under it. The voices
// read the loop's audio by raw pointer, so a block that finds the audio's identity changed first drops them (hardReset);
// the old data can therefore be destroyed on the message thread as soon as the swap returns, never on the audio thread.
// An edit of the slices or timeline keeps the audio's identity, so the sounding slice plays on through it.
struct Phrase : Layer
{
    SlotBound<PhrasePlayback>  playback;   // belongs to the slot: a layer copy never carries two stretchers
    SlotBound<InsertProcessor> insert;
    VoiceParams                insertCfg;

    Phrase() = default;

    // ── The loop data ────────────────────────────────────────────────────────
    // Message thread: publishes new loop data (nullptr = no loop). The previous one is released after the lock is dropped.
    void setData(std::shared_ptr<const PhraseData> d);
    // Message thread: the data now published (for the editor and for building an edited copy).
    std::shared_ptr<const PhraseData> getData() const;
    // Message thread: a loop has been decoded. The saved slices and timeline apply when they were made on a loop of this
    // length, otherwise the slices start as the single slice from 0 and the caller detects. Publishes the new data.
    void installLoop(std::shared_ptr<const juce::AudioBuffer<float>> audio, double fileSampleRate,
                     const juce::String& path, const juce::String& name,
                     std::shared_ptr<const juce::AudioBuffer<float>> source = nullptr, int regionStart = 0, int regionEnd = 0);
    // Message thread: the section of the source file a saved layer uses (0, 0 = all of it).
    void savedRegion(int& start, int& end) const noexcept { start = pending.regionStart; end = pending.regionEnd; }
    // Message thread: audition the loaded loop without the transport. Original plays the used section as it is, at its own tempo;
    // AtTempo plays the slices back to back in source order at `bpm`, timed by the layer's Tempo Match and Pitch/Stretch; Slice
    // plays one slice the same way. The matched ones are rendered here first (the audio thread only copies the result).
    enum class PreviewKind { Original, AtTempo, Slice };
    void startPreview(PreviewKind kind, int slice, double bpm);
    void stopPreview() { preview.stop(); }
    // Repeating applies to the whole-loop auditions only; a single slice plays once.
    void setPreviewLooping(bool loop) noexcept { previewLoop = loop; preview.setLooping(previewLoop && ! previewIsSlice); }
    // Message thread: the play area, in samples of the loop (0, 0 = all of it). The whole-loop auditions play only this part.
    void setPlayArea(int start, int end) noexcept { areaStart = start; areaEnd = end; applyPlayArea(); }
    // Message thread: where the audition is in the loop (samples of the loop), or a negative value when none sounds.
    double previewPlayhead() const noexcept;
    // Message thread: cuts the loop to [start, end). The slices are cut with it, and the hits keep their places; a hit whose slice is
    // gone has none and is silent. The source file stays whole, so the section can be widened again from the range bar.
    void cropTo(int start, int end);
    // Message thread: an edit of the slices or the timeline (the editor builds the edited copies). Reuses the audio.
    void commitEdit(const SliceList& slices, const Timeline& timeline);

    // Persistence (message thread): the loop reference, slices and timeline as the Phrase child of the layer node.
    void writeExtras(juce::ValueTree& node) const override;
    void applyExtras(const juce::ValueTree& node) override;
    // Message thread: true when the edits a saved file carried were made on a loop of this length, so installLoop applies them.
    bool savedEditsFit(int loopLength) const noexcept { return loopLength > 0 && pending.loopLength == loopLength; }
    const juce::String& loopPath() const noexcept { return path; }
    // The file the audio now loaded came from (empty when none): differs from loopPath() while a preset's loop is not loaded yet.
    const juce::String& loadedLoopPath() const noexcept { return loadedPath; }
    const juce::String& loopName() const noexcept { return displayName; }

    bool stripMods(float (&out)[4]) const noexcept override { return stripMod.read(out); }
    bool isValidDest(const std::string& id) const override { return isValidModDest(id); }
    void onMoved(int newIndex) override { insert->setSlotIndex(newIndex); }
    const std::atomic<float>* insertGainReduction() const override { return &insert->grReduction; }

    // ── The render contract ──────────────────────────────────────────────────
    int  typeId() const override { return (int) mu_core::LayerTypeId::Phrase; }
    void prepare(double sampleRate, int blockSize) override;
    void bindParams(juce::AudioProcessorValueTreeState& apvts, int slot) override;
    void render(const mu_core::RenderContext& ctx, juce::AudioBuffer<float>& out, int n) override;
    void onParkedBlock(const mu_core::RenderContext&) override { parked = true; }
    // The staged swap lands on the timeline's own loop point.
    double loopBeats() const override { return loopLengthBeats.load(std::memory_order_relaxed); }
    // A stop: the sounding slice leaves through the cut fade, and the next start re-anchors.
    void onTransportStopped(const mu_core::RenderContext&) override { stopRequested = true; }

    // ── How the layer leaves its slot ────────────────────────────────────────
    mu_core::RetirePolicy retirePolicy() const override
    {
        mu_core::RetirePolicy p;
        p.kind = mu_core::RetirePolicy::Kind::Fade;
        return p;
    }
    bool renderRetiring(const mu_core::RenderContext& ctx, juce::AudioBuffer<float>& out, int n, mu_core::RetireState& rs) override;

    // Audio thread: resolve this layer's modulation at `modBeat` into the scheduler's settings and the insert.
    void readParams(double modBeat) noexcept;

    // Post-modulation value of target `dest`, in the target's parameter units (any thread).
    float getModLive(int dest) const noexcept
    {
        return modLive.v[(size_t) juce::jlimit(0, kNumModDests - 1, dest)].load(std::memory_order_relaxed);
    }

private:
    struct LiveValues
    {
        alignas(64) std::array<std::atomic<float>, kNumModDests> v {};
        LiveValues() = default;
        LiveValues(const LiveValues&) noexcept {}
        LiveValues& operator=(const LiveValues&) noexcept { return *this; }
    };

    // One block of the scheduler and the insert into `buf` (handed over cleared). `playing` false only fades.
    void renderBlock(const PhraseData* d, PhraseClock clock, juce::AudioBuffer<float>& buf, int n) noexcept;

    std::array<mu_core::ParamBinding, kNumPhraseParams> pp {};   // the slot's rows, in ppi order
    std::array<const std::atomic<float>*, kNumModDests>       modDestAtoms {};
    std::array<const char*, kNumModDests>                     modDestIds {};
    std::array<juce::NormalisableRange<float>, kNumModDests>  modDestRanges {};
    std::unordered_map<std::string_view, float>               modParamValues;
    LiveValues                                                modLive;
    mu_mod::StripMods                                         stripMod;   // the modulated pan and FX sends, for the mixer strip
    mu_mod::LastResolved<kNumModDests>                        lastResolved;

    PhraseSettings settings;                       // the block's settings, set by readParams
    std::shared_ptr<const PhraseData> croppedData(const PhraseData& d, int start, int end, std::vector<int>* sliceMap) const;
    int areaStart = 0, areaEnd = 0;                // the play area (message thread)
    int previewAudioLen = 0, previewSliceStart = 0, previewSliceLen = 0;   // what the audition now plays (message thread)
    void applyPlayArea() noexcept;
    // Fills in the host-rate copy of the loop when its rate differs from the file's (message thread, not processing).
    void attachResampled(PhraseData& d) const;
    double hostRate = 0.0;                         // the rate prepare last ran at (message thread)

    // Message thread only: what a saved layer asked for before its loop was loaded, and where the loop came from.
    PhraseState  pending;
    juce::String path, displayName;
    juce::String loadedPath;

    std::shared_ptr<const PhraseData> data;        // guarded by dataLock
    mutable CopyableSpinLock          dataLock;
    uint64_t                          lastAudioId = 0;      // audio thread: the audio the voices were started from (0 = none)
    std::atomic<double>               loopLengthBeats { -1.0 };

    bool previewLoop = false, previewIsSlice = false;
    mu_core::SamplePreview preview;                // the audition (message thread starts it, the audio thread mixes it)
    juce::AudioBuffer<float> tailScratch;          // a tail that must add rather than write renders here
    bool parked = true;                            // not rendered live in the last rendered block
    bool stopRequested = false;                    // audio thread
};

} // namespace mu_tilate
