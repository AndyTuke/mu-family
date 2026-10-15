#pragma once

// Allocation gate for the golden renders: wires AllocationProbe.h into GoldenRender.h's AllocationCounter
// hook so every processBlock of every golden scenario is checked for heap allocations and frees.
//
// A scenario is expected to be clean. Where a block legitimately allocates or frees today (deferred work,
// a staged commit, a retired engine freed on the audio thread) the product lists it in a named
// KnownAllocation table, with the exact counts and a reason: the finding is then reported (logged) but
// does not fail. Anything not listed fails, and so does a listed block whose counts changed. The tables are
// informational records of present behaviour, not an approval to allocate.
//
// The including executable must define MU_ALLOCATION_PROBE_DEFINE_OPERATORS in exactly one translation unit.

#include "Tests/AllocationProbe.h"
#include "Tests/GoldenRender.h"

#include <optional>
#include <set>
#include <vector>

namespace mu_golden
{
    // One audio block that allocated or freed.
    struct AllocFinding
    {
        juce::String scenario, stream, events;
        int block = 0;
        juce::int64 frame = 0;
        int allocs = 0, frees = 0;
        juce::String allocKind, allocStack, freeStack;

        juce::String where() const
        {
            return scenario + " " + stream + " block " + juce::String(block) + " (frame " + juce::String(frame) + ")"
                 + (events.isNotEmpty() ? " after [" + events + "]" : juce::String());
        }
    };

    // A block that is known to allocate or free today. block < 0 matches every block of the stream.
    struct KnownAllocation
    {
        const char* scenario;
        const char* stream;     // "<schedule>.<master|stemN>", e.g. "fixed512.master"
        int block;
        int allocs, frees;
        const char* why;
    };

    class AllocationProbeCounter : public AllocationCounter
    {
    public:
        explicit AllocationProbeCounter(int backtraceBudget = 3000) { mu_alloc::captureBudget = backtraceBudget; }

        void setScenario(const juce::String& name) { scenario = name; }

        void setContext(const juce::String& s, int b, juce::int64 f, const juce::String& e) override
        {
            stream = s; block = b; frame = f; events = e;
        }

        void beginBlock() override { region.emplace(); }

        juce::int64 endBlock() override
        {
            region->stop();
            const int a = region->allocs(), f = region->frees();
            if (a != 0 || f != 0)
            {
                AllocFinding r;
                r.scenario = scenario; r.stream = stream; r.events = events; r.block = block; r.frame = frame;
                r.allocs = a; r.frees = f;
                r.allocKind = region->firstAllocKind() != nullptr ? region->firstAllocKind() : "";
                r.allocStack = region->firstAllocStack(); r.freeStack = region->firstFreeStack();
                found.push_back(std::move(r));
            }
            region.reset();
            return a;
        }

        std::vector<AllocFinding> take() { auto out = std::move(found); found.clear(); return out; }

    private:
        std::optional<mu_alloc::Armed> region;
        juce::String scenario, stream, events;
        int block = 0;
        juce::int64 frame = 0;
        std::vector<AllocFinding> found;
    };

    struct AllocVerdict
    {
        juce::StringArray failures;   // unlisted findings and listed blocks whose counts changed
        juce::StringArray notes;      // listed findings (reported, informational) and stale entries
    };

    // The backtrace from the probe's own frames down to the outermost processBlock, blank lines removed.
    inline juce::String trimmedStack(const juce::String& stack)
    {
        juce::StringArray kept;
        bool inProbe = true;
        for (const auto& l : juce::StringArray::fromLines(stack))
        {
            if (l.trim().isEmpty()) continue;
            if (inProbe && (l.contains("mu_alloc::") || l.contains("getStackBacktrace") || l.contains("operator new")
                            || l.contains("operator delete") || l.contains("_dbg") || l.contains("malloc")
                            || l.contains("realloc") || l.contains("free +")))
                continue;
            inProbe = false;
            kept.add(l);
            if (l.contains("PluginProcessor::processBlock") || l.contains("renderStream")) break;
        }
        return kept.joinIntoString("\n");
    }

    // The innermost frame that is neither library nor lambda glue: the function that asked for the allocation.
    inline juce::String productSite(const juce::String& stack)
    {
        for (const auto& l : juce::StringArray::fromLines(trimmedStack(stack)))
            if (! l.contains("juce::") && ! l.contains("std::") && ! l.contains("lambda") && ! l.contains("operator()")
                && ! l.contains("`"))
                return l.fromFirstOccurrenceOf(": ", false, false).fromFirstOccurrenceOf(": ", false, false).upToLastOccurrenceOf(" + ", false, false);
        return "unknown";
    }

    // True the first time a call site is seen in this run, so a repeated finding prints its backtrace once.
    inline bool isNewCallSite(const juce::String& stack)
    {
        static std::set<juce::String> seen;
        if (stack.isEmpty()) return false;
        juce::String sig;
        for (const auto& l : juce::StringArray::fromLines(trimmedStack(stack)))
            sig << l.fromFirstOccurrenceOf(": ", false, false).upToLastOccurrenceOf(" + ", false, false) << "|";
        return seen.insert(sig).second;
    }

    inline AllocVerdict judgeAllocations(const juce::String& scenario, const std::vector<AllocFinding>& found,
                                         const std::vector<KnownAllocation>& known)
    {
        AllocVerdict v;
        std::vector<bool> used(known.size(), false);
        for (const auto& f : found)
        {
            const KnownAllocation* match = nullptr;
            for (size_t i = 0; i < known.size(); ++i)
            {
                const auto& k = known[i];
                if (scenario == k.scenario && (juce::String(k.stream) == "*" || f.stream == k.stream)
                    && (k.block < 0 || k.block == f.block))
                { match = &k; used[i] = true; break; }
            }
            juce::String line = f.where() + ": " + juce::String(f.allocs) + " alloc(s), " + juce::String(f.frees) + " free(s)";
            if (f.allocKind.isNotEmpty()) line << " [first alloc via " << f.allocKind << " in " << productSite(f.allocStack) << "]";
            if (isNewCallSite(f.allocStack)) line << "\n  first alloc at:\n" << trimmedStack(f.allocStack);
            if (isNewCallSite(f.freeStack)) line << "\n  first free at:\n" << trimmedStack(f.freeStack);

            if (match == nullptr)
                v.failures.add("unexpected heap activity inside processBlock: " + line);
            else if (match->allocs != f.allocs || match->frees != f.frees)
                v.failures.add("known heap activity changed (expected " + juce::String(match->allocs) + " alloc(s), "
                               + juce::String(match->frees) + " free(s); " + match->why + "): " + line);
            else
                v.notes.add("known heap activity (" + juce::String(match->why) + "): " + line);
        }
        for (size_t i = 0; i < known.size(); ++i)
            if (! used[i] && scenario == known[i].scenario)
                v.notes.add(juce::String("listed heap activity no longer seen: ") + known[i].scenario + " "
                            + known[i].stream + " block " + juce::String(known[i].block));
        return v;
    }

    // Proves the probe sees what it must (a raw new / delete, a malloc / free on the debug CRT) and ignores
    // what it must not (a paused scope, the debug STL's container proxies). Empty = healthy.
    inline juce::StringArray probeSelfTest()
    {
        juce::StringArray bad;
        int* volatile keep = nullptr;

        {
            mu_alloc::Armed r;
            keep = new int(7);
            r.stop();
            if (r.allocs() != 1) bad.add("operator new was counted " + juce::String(r.allocs()) + " time(s), expected 1");
        }
        {
            mu_alloc::Armed r;
            delete keep;
            r.stop();
            if (r.frees() != 1) bad.add("operator delete was counted " + juce::String(r.frees()) + " time(s), expected 1");
        }
       #if defined(_MSC_VER) && defined(_DEBUG)
        {
            mu_alloc::Armed r;
            char* volatile m = static_cast<char*>(std::malloc(64));
            std::free(m);
            r.stop();
            if (r.allocs() != 1 || r.frees() != 1)
                bad.add("malloc / free counted " + juce::String(r.allocs()) + " / " + juce::String(r.frees()) + ", expected 1 / 1");
        }
       #endif
        {
            mu_alloc::Armed r;
            {
                mu_alloc::Paused p;
                keep = new int(1);
                delete keep;
            }
            r.stop();
            if (r.allocs() != 0 || r.frees() != 0) bad.add("a paused scope was counted");
        }
        {
            mu_alloc::Armed r;
            { std::vector<int> empty; std::string shortText("abc"); }   // only the debug STL's container proxies
            r.stop();
            if (r.allocs() != 0 || r.frees() != 0)
                bad.add("debug container proxies were counted (" + juce::String(r.allocs()) + " / " + juce::String(r.frees()) + ")");
        }
        return bad;
    }
}
