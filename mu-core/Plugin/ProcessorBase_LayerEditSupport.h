#pragma once

// Helpers shared by the ProcessorBase_LayerEdits.cpp and ProcessorBase_LayerTypes.cpp translation units (structural
// edit suspension, parameter-row walks and moves). Internal to those files: include it after Plugin/ProcessorBase.h
// and Persistence/PresetFiles.h, and never from anything else.

namespace
{
    // Suspends audio processing for the life of one structural edit and restores the earlier state.
    // suspendProcessing waits out a running block in the wrappers that hold the callback lock
    // (VST3, VST2, AU, AAX, LV2, standalone) but not in CLAP, which reads the flag unsynchronised;
    // layersLock protects the pointer moves in every host. layerEditInFlight additionally tells a
    // product's audio thread to skip rendering until the parameter moves have finished, so no
    // block plays a moved layer with half-shifted parameters.
    // layerEditInFlight is a flag, not a counter: edits never nest (all run on the message thread,
    // one at a time), and a nested guard would lower the flag early. release() ends the guard
    // early for a product whose audio never reads the moved parameters (layerHoldsParamTruth).
    struct ScopedSuspend
    {
        explicit ScopedSuspend(ProcessorBase& p) : proc(p), wasSuspended(p.isSuspended())
        {
            jassert(! proc.layerEditInFlight.load(std::memory_order_relaxed));   // edits do not nest
            proc.layerEditInFlight.store(true, std::memory_order_release);
            proc.suspendProcessing(true);
        }
        ~ScopedSuspend() { release(); }
        ScopedSuspend(const ScopedSuspend&)            = delete;
        ScopedSuspend& operator=(const ScopedSuspend&) = delete;

        // Resumes processing and lowers the flag; a second call does nothing.
        void release()
        {
            if (released) return;
            released = true;
            proc.suspendProcessing(wasSuspended);
            proc.layerEditInFlight.store(false, std::memory_order_release);
        }

        ProcessorBase& proc;
        const bool     wasSuspended;
        bool           released = false;
    };

    // Calls fn(param, idSuffix) for every ranged parameter whose id starts with `prefix`.
    template <class Fn>
    void forEachPrefixed(juce::AudioProcessor& proc, const juce::String& prefix, Fn&& fn)
    {
        for (auto* p : proc.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p))
            {
                const juce::String id = rp->getParameterID();
                if (id.startsWith(prefix)) fn(*rp, id.substring(prefix.length()));
            }
    }

    // Published-mode exclusion for a structural edit (design 2.1.2): raise layerEditInFlight (seq_cst), then wait one grace
    // period, so the block that may have started before the flag has ended and every later block renders nothing until the
    // flag is lowered. On a timeout the flag is lowered again and ok() is false: the caller gives up and moves nothing.
    // Unlike ScopedSuspend it does not suspend processing, so the host keeps calling processBlock (which renders nothing).
    class ScopedQuiesce
    {
    public:
        ScopedQuiesce(std::atomic<bool>& editFlag, const mu_core::AudioEpoch& epoch, int timeoutMs) : flag(editFlag)
        {
            jassert(! flag.load(std::memory_order_relaxed));   // edits do not nest
            flag.store(true, std::memory_order_seq_cst);
            raised = true;
            granted = epoch.waitFor(epoch.stamp(), timeoutMs);
            if (! granted) release();
        }
        ~ScopedQuiesce() { release(); }
        ScopedQuiesce(const ScopedQuiesce&)            = delete;
        ScopedQuiesce& operator=(const ScopedQuiesce&) = delete;

        bool ok() const noexcept { return granted; }
        // Lowers the flag; a second call does nothing.
        void release() noexcept
        {
            if (! raised) return;
            raised = false;
            flag.store(false, std::memory_order_seq_cst);
        }

    private:
        std::atomic<bool>& flag;
        bool raised = false, granted = false;
    };

    // One slot's parameter rows moving to another slot's prefix: each id under `from` goes to the same suffix under `to`.
    struct RowMove { juce::String from, to; };

    // Which parameter feeds which: the structure of a set of row moves, found by walking the parameter list. A row the
    // destination lacks is left out. Done before the audio thread is excluded, because the walk is the slow part.
    using RowPlan = std::vector<std::pair<juce::RangedAudioParameter*, juce::RangedAudioParameter*>>;   // source, destination
    RowPlan planRows(juce::AudioProcessor& proc, juce::AudioProcessorValueTreeState& apvts, const std::vector<RowMove>& moves)
    {
        RowPlan plan;
        for (const auto& m : moves)
            forEachPrefixed(proc, m.from, [&](juce::RangedAudioParameter& rp, const juce::String& suffix)
            {
                if (auto* dst = apvts.getParameter(m.to + suffix)) plan.emplace_back(&rp, dst);
            });
        return plan;
    }

    // Applies a plan. Every source value is read before any is written, so moves that overlap (two same-prefix slots
    // exchanging rows) swap instead of copy; a value that is already right is not written again (no host notification,
    // no listener work for nothing). Message thread.
    void applyRows(const RowPlan& plan)
    {
        std::vector<float> values;
        values.reserve(plan.size());
        for (const auto& pr : plan) values.push_back(pr.first->getValue());
        for (size_t i = 0; i < plan.size(); ++i)
            if (plan[i].second->getValue() != values[i]) plan[i].second->setValueNotifyingHost(values[i]);
    }

    // The same prefix letters on another slot: "t3_" on slot 5 is "t5_". Empty stays empty (no live prefix).
    juce::String prefixOnSlot(const juce::String& prefix, int slot)
    {
        if (prefix.isEmpty()) return {};
        return prefix.dropLastCharacters(1).trimCharactersAtEnd("0123456789") + juce::String(slot) + "_";
    }

    // The two id prefixes a layer slot's parameters sit under: the product's own (v3_) and the mixer strip's (ch3_).
    juce::StringArray layerParamPrefixes(const mu_pp::LayerLayout& layout, int slot)
    {
        juce::StringArray out;
        if (const auto own = layout.prefix(slot); own.isNotEmpty()) out.add(own);
        out.add("ch" + juce::String(slot) + "_");
        return out;
    }
}
