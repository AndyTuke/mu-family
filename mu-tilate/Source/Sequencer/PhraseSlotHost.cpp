#include "Sequencer/PhraseSlotHost.h"

#include "Persistence/PhraseParams.h"
#include "Analysis/LoopTempo.h"   // mu-core: a first tempo guess from the loop length
#include "Audio/LoopLoader.h"     // mu-core: decode a loop file

#include <cmath>

namespace mu_tilate
{

PhraseSlotHost::PhraseSlotHost(mu_core::LayerHostView& hostView) : view(hostView) {}

PhraseSlotHost::~PhraseSlotHost()
{
    alive->store(false);   // a loop load still running on a worker must not reach this object
}

Phrase* PhraseSlotHost::phraseAt(int layer) const noexcept
{
    if (! validSlot(layer)) return nullptr;
    auto* l = view.hostLayer(layer);
    return l != nullptr && l->typeId() == (int) mu_core::LayerTypeId::Phrase ? static_cast<Phrase*>(l) : nullptr;
}

Phrase& PhraseSlotHost::getPhrase(int layer) noexcept
{
    // The content's constructor binds slot 0 before the editor rebinds it to the shown slot, and a content kept for its
    // type can be asked about a slot that has since become another type.
    if (auto* p = phraseAt(juce::jlimit(0, mu_limits::kMaxLayers - 1, layer))) return *p;
    if (standIn == nullptr) standIn = std::make_unique<Phrase>();
    return *standIn;
}

float PhraseSlotHost::getModLive(int layer, int dest) const noexcept
{
    const auto* p = phraseAt(layer);
    return p != nullptr ? p->getModLive(dest) : 0.0f;
}

void PhraseSlotHost::previewSource(int layer, int kind, int slice)
{
    // An audition and the sequence never sound together: it plays only while the transport is stopped.
    if (auto* p = phraseAt(layer); p != nullptr && ! view.hostTransportPlaying())
        p->startPreview((Phrase::PreviewKind) juce::jlimit(0, 2, kind), slice, view.hostTempo());
}

void PhraseSlotHost::auditionFile(int layer, const juce::File& file)
{
    if (auto* p = phraseAt(layer); p != nullptr && ! view.hostTransportPlaying()) p->auditionFile(file);
}

void PhraseSlotHost::stopPreview(int layer)                       { if (auto* p = phraseAt(layer)) p->stopPreview(); }
void PhraseSlotHost::setPreviewLooping(int layer, bool loop)      { if (auto* p = phraseAt(layer)) p->setPreviewLooping(loop); }
void PhraseSlotHost::setPlayArea(int layer, int start, int end)   { if (auto* p = phraseAt(layer)) p->setPlayArea(start, end); }
void PhraseSlotHost::cropLoop(int layer, int start, int end)      { if (auto* p = phraseAt(layer)) p->cropTo(start, end); }

double PhraseSlotHost::getPreviewPlayhead(int layer) const
{
    const auto* p = phraseAt(layer);
    return p != nullptr ? p->previewPlayhead() : -1.0;
}

juce::File PhraseSlotHost::getPrimarySampleDir() const
{
    // The same setting the other products use for the user's sample folder (the Music folder until one is chosen).
    if (auto* settings = view.hostSettings())
    {
        const auto stored = settings->getValue("primarySampleDir");
        if (stored.isNotEmpty()) return juce::File(stored);
    }
    return juce::File::getSpecialLocation(juce::File::userMusicDirectory);
}

juce::File PhraseSlotHost::getSamplesDir() const
{
    auto dir = view.hostContentDir().getChildFile("Samples");
    dir.createDirectory();
    return dir;
}

DetectSettings PhraseSlotHost::detectSettingsOf(int layer) const
{
    auto raw = [&](int row) { return view.hostParameters().getRawParameterValue(layerParamPrefix(layer) + ppi::suffix[row])->load(); };
    DetectSettings s;
    s.method      = (int) raw(ppi::dmethod);
    s.sensitivity = raw(ppi::dsens);
    s.minGapMs    = raw(ppi::dgap);
    s.preRollMs   = raw(ppi::dpre);
    s.lowCutHz    = raw(ppi::dlow);
    s.highCutHz   = raw(ppi::dhigh);
    return s;
}

double PhraseSlotHost::loopTempoOf(int layer) const
{
    return (double) view.hostParameters().getRawParameterValue(layerParamPrefix(layer) + ppi::suffix[ppi::ltempo])->load();
}

// Publishes `slices` with a timeline as long as the slices take back to back (in bars of 4/4), with the length in whole bars:
// empty, or with the slices laid out in source order from beat 0. Returns the total length in beats.
double PhraseSlotHost::layOutInOrder(int layer, const SliceList& slices, double sampleRate, double loopTempo, bool inOrder)
{
    auto* ph = phraseAt(layer);
    if (ph == nullptr) return 0.0;
    std::vector<double> beats;
    double total = 0.0;
    for (int i = 0; i < slices.size(); ++i)
    {
        beats.push_back((double) slices.slice(i).length() / sampleRate * loopTempo / 60.0);
        total += beats.back();
    }
    const int bars = juce::jlimit(1, 64, (int) std::ceil(total / 4.0 - 1.0e-6));
    Timeline timeline((double) bars * 4.0);
    std::vector<SliceSettings> startWith;   // each hit starts with its slice's settings
    for (int i = 0; i < slices.size(); ++i) startWith.push_back(slices.settings(i));
    if (inOrder) timeline.originalOrder(beats, startWith);
    ph->commitEdit(slices, timeline);
    return total;
}

// Detect again on the loaded loop (a worker), then publish the new slices and a timeline in source order.
void PhraseSlotHost::redetectSlices(int layer)
{
    auto* start = phraseAt(layer);
    if (start == nullptr) return;
    const auto data = start->getData();
    if (data == nullptr || data->audio == nullptr) return;
    const int gen = detectGeneration[(size_t) layer].fetch_add(1) + 1;
    const auto detect = detectSettingsOf(layer);
    const double loopTempo = loopTempoOf(layer);
    auto flag = alive;

    juce::Thread::launch([this, layer, gen, data, detect, loopTempo, flag]
    {
        const auto curve = analyseLoop(*data->audio, data->fileSampleRate, detect);
        auto slices = std::make_shared<SliceList>(detectSlices(curve, *data->audio, detect, &data->slices));
        juce::MessageManager::callAsync([this, layer, gen, data, slices, loopTempo, flag]
        {
            if (! flag->load() || detectGeneration[(size_t) layer].load() != gen) return;   // gone, or a newer Detect
            auto* ph = phraseAt(layer);
            if (ph == nullptr || ph->getData() == nullptr || ph->getData()->audioId != data->audioId) return;   // another loop arrived meanwhile
            if (ph->getData()->version != data->version)   // edits made while it ran would be lost: leave them, say so
            {
                loopStatus[(size_t) layer] = ph->loopName() + ": the slices changed while Detect ran, so it was not applied. Press Detect again.";
                return;
            }
            const double total = layOutInOrder(layer, *slices, data->fileSampleRate, loopTempo, true);
            loopStatus[(size_t) layer] = ph->loopName() + ": " + juce::String(slices->size()) + " slices, "
                                       + juce::String(total, 1) + " beats at " + juce::String(loopTempo, 0) + " BPM";
        });
    });
}

// Lays the layer's slices out one after another in source order (the Sequencer's Lay out button).
void PhraseSlotHost::layOutSlices(int layer)
{
    auto* ph = phraseAt(layer);
    if (ph == nullptr) return;
    const auto data = ph->getData();
    if (data == nullptr || data->audio == nullptr) return;
    layOutInOrder(layer, data->slices, data->fileSampleRate, loopTempoOf(layer), true);
}

// Uses the section [start, end) of the layer's source; the layer is laid out afresh, as for a new loop.
void PhraseSlotHost::setSourceRegion(int layer, int start, int end)
{
    auto* ph = phraseAt(layer);
    if (ph == nullptr) return;
    const auto d = ph->getData();
    if (d == nullptr || d->audio == nullptr) return;
    const auto src = d->source != nullptr ? d->source : d->audio;
    loadLoopAsync(layer, juce::File(ph->loadedLoopPath()), false, src, d->fileSampleRate, start, end);
}

void PhraseSlotHost::loadLoopAsync(int layer, const juce::File& file, bool keepSavedEdits)
{
    loadLoopAsync(layer, file, keepSavedEdits, nullptr, 0.0, -1, -1);
}

// Decode on a worker, detect, then publish the slices on the message thread with an empty timeline. A newer request for the
// layer (or a destroyed host) makes an older completion do nothing.
void PhraseSlotHost::loadLoopAsync(int layer, const juce::File& file, bool keepSavedEdits,
                                   std::shared_ptr<const juce::AudioBuffer<float>> decoded, double decodedRate, int regionStart, int regionEnd)
{
    auto* start = phraseAt(layer);
    if (start == nullptr) return;
    const int gen = loopGeneration[(size_t) layer].fetch_add(1) + 1;
    loopStatus[(size_t) layer] = "Loading " + file.getFileName() + " ...";

    const auto detect = detectSettingsOf(layer);
    const double loopTempo = loopTempoOf(layer);
    auto flag = alive;

    // The section of the source to use: asked for, or (a restore) the one the saved layer used.
    int rStart = regionStart, rEnd = regionEnd;
    if (rStart < 0 && keepSavedEdits) start->savedRegion(rStart, rEnd);

    juce::Thread::launch([this, layer, gen, file, detect, loopTempo, flag, keepSavedEdits, decoded, decodedRate, rStart, rEnd]
    {
        auto loaded = std::make_shared<mu_core::LoadedLoop>();
        if (decoded != nullptr)
        {
            loaded->audio = decoded; loaded->sampleRate = decodedRate; loaded->name = file.getFileNameWithoutExtension();
        }
        else
            *loaded = mu_core::loadLoop(file, mu_limits::kMaxLoopBytes);

        // Cut the used section out of the whole file (a copy; the source stays whole for the next trim).
        std::shared_ptr<const juce::AudioBuffer<float>> source = loaded->audio;
        int from = 0, to = source != nullptr ? source->getNumSamples() : 0;
        if (source != nullptr && rStart >= 0)
        {
            const int total = source->getNumSamples();
            const int a = juce::jlimit(0, juce::jmax(0, total - 2), rStart);
            const int b = juce::jlimit(a + 1, total, rEnd <= 0 ? total : rEnd);
            if (a > 0 || b < total)
            {
                auto cut = std::make_shared<juce::AudioBuffer<float>>(source->getNumChannels(), b - a);
                for (int ch = 0; ch < cut->getNumChannels(); ++ch) cut->copyFrom(ch, 0, *source, ch, a, b - a);
                loaded->audio = cut;
                from = a; to = b;
            }
        }

        std::shared_ptr<SliceList> slices;
        if (loaded->ok())
        {
            const auto curve = analyseLoop(*loaded->audio, loaded->sampleRate, detect);
            slices = std::make_shared<SliceList>(detectSlices(curve, *loaded->audio, detect));
        }

        juce::MessageManager::callAsync([this, layer, gen, file, loaded, slices, loopTempo, flag, keepSavedEdits, source, from, to]
        {
            if (! flag->load() || loopGeneration[(size_t) layer].load() != gen) return;   // gone, or superseded
            if (! loaded->ok()) { loopStatus[(size_t) layer] = loaded->error; return; }

            auto* ph = phraseAt(layer);
            if (ph == nullptr) return;   // the slot is another type now

            // A restore whose saved edits fit this loop keeps them (installLoop applies them); anything else gets the
            // detected slices and an empty timeline of the loop's length (in bars of 4/4).
            const bool keep = keepSavedEdits && ph->savedEditsFit(loaded->audio->getNumSamples());
            ph->installLoop(loaded->audio, loaded->sampleRate, file.getFullPathName(), loaded->name, source, from, to);

            // A new loop sets the layer's loop tempo to the estimate from its length (a whole number of beats); the Tempo
            // knob overrides it. A restore keeps the saved tempo.
            double tempo = loopTempo;
            bool estimated = false;
            if (! keep)
            {
                // The detected attacks (marker 0 is always at the start, so it is no evidence) pick between the readings the length allows.
                std::vector<double> attacks;
                for (int i = 1; slices != nullptr && i < slices->size(); ++i)
                    attacks.push_back((double) slices->marker(i).position / loaded->sampleRate);
                const double guess = mu_core::estimateTempoFromOnsets((double) loaded->audio->getNumSamples() / loaded->sampleRate, attacks);
                if (guess > 0.0)
                {
                    tempo = juce::jlimit(20.0, 300.0, guess);
                    estimated = true;
                    if (auto* p = view.hostParameters().getParameter(layerParamPrefix(layer) + ppi::suffix[ppi::ltempo]))
                        p->setValueNotifyingHost(p->convertTo0to1((float) tempo));
                }
            }

            double total = 0.0;
            if (! keep)
                total = layOutInOrder(layer, *slices, loaded->sampleRate, tempo, false);   // a new loop starts with no hits
            else if (const auto kept = ph->getData(); kept != nullptr)   // the restored slices, not the freshly detected ones
                for (int i = 0; i < kept->slices.size(); ++i)
                    total += (double) kept->slices.slice(i).length() / loaded->sampleRate * tempo / 60.0;
            const auto shown = ph->getData();
            loopStatus[(size_t) layer] = loaded->name + ": " + juce::String(shown != nullptr ? shown->slices.size() : 0) + " slices, "
                                       + juce::String(total, 1) + " beats at " + juce::String(tempo, estimated ? 1 : 0) + " BPM"
                                       + (estimated ? " (estimated from the length)" : "");
        });
    });
}

juce::String PhraseSlotHost::getLoopStatus(int layer) const
{
    if (! validSlot(layer)) return {};
    const auto& s = loopStatus[(size_t) layer];
    if (s.isNotEmpty()) return s;
    const auto* ph = phraseAt(layer);
    return ph != nullptr && ph->loopName().isNotEmpty() ? ph->loopName() : juce::String("No loop");
}

void PhraseSlotHost::reloadLoopIfNeeded(int layer)
{
    const auto* ph = phraseAt(layer);
    if (ph == nullptr) return;
    const auto path = ph->loopPath();
    if (path.isEmpty()) return;
    const auto d = ph->getData();
    if (d != nullptr && d->audio != nullptr && ph->loadedLoopPath() == path) return;   // already holds that file's audio
    loadLoopAsync(layer, juce::File(path), true);
}

void PhraseSlotHost::reloadMissingLoops()
{
    for (int l = 0; l < mu_limits::kMaxLayers; ++l)
    {
        const auto* ph = phraseAt(l);
        auto& last = tried[(size_t) l];
        if (ph == nullptr) { last = {}; continue; }
        const auto path = ph->loopPath();
        if (path.isEmpty() || (last.first == ph && last.second == path)) continue;
        last = { ph, path };
        reloadLoopIfNeeded(l);
    }
}

} // namespace mu_tilate
