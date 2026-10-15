#pragma once

// Golden-render harness: drives a headless ProcessorBase through a scripted scenario and reduces the
// output to a small, human-diffable fingerprint that a code move must not change by one bit.
//
// What it records, per block schedule (fixed 512 and a ragged one with 1-sample blocks), for the
// master and for each layer soloed as a stem:
//   - FNV-1a 64 of the float bit patterns of each channel per 256-frame segment, plus the segment
//     RMS (dBFS) and peak (24-bit LSBs), so a mismatch names the first diverging segment and how loud;
//   - a whole-render hash, the hash of the MIDI the processor emitted, the end-of-render mixer peaks
//     and insert gain reduction, and a hash of the saved state.
// Nothing stores raw audio; on a mismatch the actual render is written as a 24-bit WAV for diffing.
//
// Equality: bit-identical (tolerance 0) for the same config (os-arch-debug/release), which is why the
// goldens are separate files per config. A non-zero tolerance needs a written reason (see Scenario).
//
// A stem is the layer soloed through the mixer (ch{i}_solo) with its three sends at 0; nothing in the
// processor beyond the shared mixer parameters is touched. A product that also needs, say, its master
// insert off for a clean stem supplies that in Scenario::stemSetup. Stem renders start from a fresh
// processor each time, so a stem never inherits the master render's state.
//
// Header-only and product-free: the test supplies a factory for the processor and the scenario's
// frozen state and scripted events. Goldens change only through tests/scripts/update-golden.ps1.

#include "Plugin/ProcessorBase.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace mu_golden
{
    constexpr int kSegmentFrames = 256;
    constexpr int kSchema = 1;

    // ─── Hashing ──────────────────────────────────────────────────────────────
    // FNV-1a 64-bit, fed byte by byte (offset basis and prime from the FNV reference).
    struct Fnv64
    {
        std::uint64_t h = 14695981039346656037ull;
        void byte(std::uint8_t b) noexcept { h ^= b; h *= 1099511628211ull; }
        void bytes(const void* p, size_t n) noexcept
        {
            const auto* c = static_cast<const std::uint8_t*>(p);
            for (size_t i = 0; i < n; ++i) byte(c[i]);
        }
        void u32(std::uint32_t v) noexcept { for (int s = 0; s < 32; s += 8) byte((std::uint8_t) (v >> s)); }
        void f32(float v) noexcept { std::uint32_t bits; std::memcpy(&bits, &v, 4); u32(bits); }
    };

    inline juce::String hex64(std::uint64_t v) { return juce::String::toHexString((juce::int64) v).paddedLeft('0', 16); }
    inline juce::String hex32(float v) { std::uint32_t b; std::memcpy(&b, &v, 4); return juce::String::toHexString((int) b).paddedLeft('0', 8); }

    // Hash of whole files (a scenario's state.xml + scenario.json), for Scenario::inputHash.
    inline juce::String hashFiles(const std::vector<juce::File>& files)
    {
        Fnv64 f;
        for (const auto& file : files)
        {
            juce::MemoryBlock mb;
            file.loadFileAsData(mb);
            f.bytes(mb.getData(), mb.getSize());
            f.byte(0xff);   // separator, so moving bytes between files changes the hash
        }
        return hex64(f.h);
    }

    // ─── Scenario description ─────────────────────────────────────────────────
    // The host the processor sees: a playhead the harness advances per block and events may edit
    // (tempo ramp, locate, stop). Never reads a wall clock.
    struct MockHost : juce::AudioPlayHead
    {
        bool   playing = true;
        double bpm = 120.0;
        double ppq = 0.0;
        juce::int64 timeInSamples = 0;
        int sigNum = 4, sigDen = 4;

        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setIsPlaying(playing);
            info.setBpm(bpm);
            info.setPpqPosition(ppq);
            info.setTimeInSamples(timeInSamples);
            info.setTimeSignature(TimeSignature { sigNum, sigDen });
            return info;
        }
    };

    struct ScriptContext
    {
        ProcessorBase& proc;
        MockHost& host;
        int block;            // index in the schedule
        juce::int64 frame;    // first frame of this block
    };

    struct ScriptedEvent  { int block; juce::String label; std::function<void(ScriptContext&)> apply; };
    struct ScriptedMidi   { int block; int sampleOffset; juce::MidiMessage message; };

    struct Schedule
    {
        juce::String name;
        std::vector<int> blocks;   // repeated in order until the scenario length is covered

        static Schedule fixed512() { return { "fixed512", { 512 } }; }
        // 1-sample blocks and a block larger than the prepared size (prepared at 512).
        static Schedule ragged()   { return { "ragged", { 1, 7, 63, 64, 129, 512, 1024, 3 } }; }
    };

    // Interface only: a test target that counts heap allocations installs one, and the harness calls
    // it around each processBlock. The implementation (a global operator new hook) is a separate piece
    // of work; with none installed the allocation count is reported as -1 (not measured).
    struct AllocationCounter
    {
        virtual ~AllocationCounter() = default;
        virtual void beginBlock() = 0;
        virtual juce::int64 endBlock() = 0;   // allocations since beginBlock
    };

    struct Scenario
    {
        juce::String name;                       // folder name under tests/golden/<product>/
        double sampleRate = 48000.0;
        int preparedBlockSize = 512;
        juce::int64 lengthFrames = 48000;
        double tempoBpm = 120.0;
        double startBeat = 0.0;                  // host position of frame 0 (try 1e6)
        int numStems = 0;                        // layers rendered soloed, 0..numStems-1; 0 = master only
        std::vector<Schedule> schedules { Schedule::fixed512(), Schedule::ragged() };
        std::vector<ScriptedEvent> events;       // run before the named block
        std::vector<ScriptedMidi>  midi;         // delivered in the named block at the sample offset
        std::function<void(ProcessorBase&)> loadState;               // the frozen state
        std::function<void(ProcessorBase&, int stem)> stemSetup;     // optional extra stem isolation

        // Hash of the files the scenario was built from; recorded in the golden and checked on every
        // run, so a hand-edited state cannot silently pass.
        juce::String inputHash;

        // Max allowed per-segment peak difference in 24-bit LSBs. 0 = bit-identical, which is the gate.
        // Setting it needs toleranceReason (a stage must never loosen it to go green; see tests.md).
        // With a tolerance the segment hashes are ignored and the stored per-segment peak is compared.
        double toleranceAbs = 0.0;
        juce::String toleranceReason;
    };

    using ProcessorFactory = std::function<std::unique_ptr<ProcessorBase>()>;

    // ─── Fingerprint ──────────────────────────────────────────────────────────
    using Lines = std::vector<std::pair<juce::String, juce::String>>;   // key, value; order is stable

    struct Capture
    {
        Lines lines;
        std::map<juce::String, juce::AudioBuffer<float>> audio;   // stream prefix -> rendered audio (for the WAV dump)
        juce::int64 allocations = -1;
    };

    inline juce::String dbString(double sumSq, juce::int64 n)
    {
        if (n <= 0 || sumSq <= 0.0) return "-200.00";
        return juce::String(juce::jmax(-200.0, 10.0 * std::log10(sumSq / (double) n)), 2);
    }

    // Renders one stream (stem < 0 = master) of one schedule into `cap`.
    inline void renderStream(const ProcessorFactory& factory, const Scenario& sc, const Schedule& sched,
                             int stem, AllocationCounter* counter, Capture& cap)
    {
        auto procPtr = factory();
        ProcessorBase& proc = *procPtr;
        const juce::String prefix = sched.name + "." + (stem < 0 ? juce::String("master") : "stem" + juce::String(stem));

        // Phase 1: frozen state, then (for a stem) the mixer solo and zero sends.
        if (sc.loadState) sc.loadState(proc);
        if (stem >= 0)
        {
            auto setBool = [&](const juce::String& id, bool v)
            { if (auto* p = proc.apvts.getParameter(id)) p->setValueNotifyingHost(v ? 1.0f : 0.0f); };
            auto setZero = [&](const juce::String& id)
            { if (auto* p = proc.apvts.getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1(0.0f)); };
            for (int k = 0; k < sc.numStems; ++k)
                setBool("ch" + juce::String(k) + "_solo", k == stem);
            for (const char* s : { "sendEff", "sendDly", "sendRev" })
                setZero("ch" + juce::String(stem) + "_" + s);
            if (sc.stemSetup) sc.stemSetup(proc, stem);
        }

        // Phase 2: prepare with the host in place, then walk the schedule block by block.
        MockHost host;
        host.bpm = sc.tempoBpm;
        host.ppq = sc.startBeat;
        proc.setPlayHead(&host);
        proc.setRateAndBufferSizeDetails(sc.sampleRate, sc.preparedBlockSize);
        proc.prepareToPlay(sc.sampleRate, sc.preparedBlockSize);

        const int maxBlock = *std::max_element(sched.blocks.begin(), sched.blocks.end());
        juce::AudioBuffer<float> work(2, maxBlock);
        juce::AudioBuffer<float> captured(2, (int) sc.lengthFrames);
        juce::MidiBuffer midi;

        Fnv64 whole[2], seg[2];
        double segSumSq = 0.0;
        float segPeak = 0.0f;
        juce::int64 segFrames = 0, segIndex = 0;
        Fnv64 midiOut;
        juce::int64 allocs = counter != nullptr ? 0 : -1;
        Lines segLines;

        auto closeSegment = [&]
        {
            if (segFrames == 0) return;
            const auto peak24 = (juce::int64) std::llround((double) segPeak * 8388607.0);
            segLines.emplace_back(prefix + ".seg." + juce::String(segIndex).paddedLeft('0', 6),
                                  hex64(seg[0].h) + " " + hex64(seg[1].h) + " "
                                      + dbString(segSumSq, segFrames * 2) + " " + juce::String(peak24));
            seg[0] = Fnv64(); seg[1] = Fnv64();
            segSumSq = 0.0; segPeak = 0.0f; segFrames = 0; ++segIndex;
        };

        juce::int64 frame = 0;
        int block = 0;
        size_t eventCursor = 0;
        std::vector<ScriptedEvent> events = sc.events;
        std::stable_sort(events.begin(), events.end(), [](const auto& a, const auto& b) { return a.block < b.block; });

        // Loop purpose: one iteration per host block; scripted events run first, then MIDI is built
        // for the block, the processor renders it, and the output is folded into the segment hashes.
        while (frame < sc.lengthFrames)
        {
            const int n = (int) std::min<juce::int64>(sched.blocks[(size_t) block % sched.blocks.size()],
                                                      sc.lengthFrames - frame);
            host.timeInSamples = frame;

            ScriptContext ctx { proc, host, block, frame };
            while (eventCursor < events.size() && events[eventCursor].block <= block)
                events[eventCursor++].apply(ctx);

            midi.clear();
            for (const auto& m : sc.midi)
                if (m.block == block) midi.addEvent(m.message, juce::jmin(m.sampleOffset, n - 1));

            juce::AudioBuffer<float> view(work.getArrayOfWritePointers(), 2, n);
            view.clear();
            if (counter != nullptr) counter->beginBlock();
            proc.processBlock(view, midi);
            if (counter != nullptr) allocs += counter->endBlock();

            for (const auto meta : midi)
            {
                midiOut.u32((std::uint32_t) (frame + meta.samplePosition));
                midiOut.bytes(meta.data, (size_t) meta.numBytes);
            }

            for (int i = 0; i < n; ++i)
            {
                for (int c = 0; c < 2; ++c)
                {
                    const float v = view.getSample(c, i);
                    whole[c].f32(v);
                    seg[c].f32(v);
                    segSumSq += (double) v * (double) v;
                    segPeak = std::max(segPeak, std::abs(v));
                    captured.setSample(c, (int) (frame + i), v);
                }
                if (++segFrames == kSegmentFrames) closeSegment();
            }

            if (host.playing) host.ppq += (double) n * host.bpm / (60.0 * sc.sampleRate);
            frame += n;
            ++block;
        }
        closeSegment();

        // Phase 3: end-of-render state: peaks, gain reduction, and the saved-state hash.
        // Segments first, so the first reported difference is the earliest diverging segment.
        for (auto& l : segLines) cap.lines.push_back(l);
        cap.lines.emplace_back(prefix + ".whole", hex64(whole[0].h) + " " + hex64(whole[1].h));
        cap.lines.emplace_back(prefix + ".midi.hash", hex64(midiOut.h));
        cap.lines.emplace_back(prefix + ".end.masterPeak", hex32(proc.mixerEngine.masterPeakL.load()) + " "
                                                               + hex32(proc.mixerEngine.masterPeakR.load()));
        for (int i = 0; i < sc.numStems && i < MixerEngine::MaxChannels; ++i)
        {
            cap.lines.emplace_back(prefix + ".end.ch" + juce::String(i) + "Peak",
                                   hex32(proc.mixerEngine.channelPeaksL[i].load()) + " "
                                       + hex32(proc.mixerEngine.channelPeaksR[i].load()));
            const auto* gr = proc.getInsertGR(i);
            cap.lines.emplace_back(prefix + ".end.ch" + juce::String(i) + "GR", gr != nullptr ? hex32(gr->load()) : juce::String("none"));
        }
        juce::MemoryBlock state;
        proc.getStateInformation(state);
        Fnv64 st;
        st.bytes(state.getData(), state.getSize());
        cap.lines.emplace_back(prefix + ".state.hash", hex64(st.h));

        cap.audio[prefix] = std::move(captured);
        if (allocs >= 0) cap.allocations = (cap.allocations < 0 ? 0 : cap.allocations) + allocs;
        proc.setPlayHead(nullptr);
        proc.releaseResources();
    }

    // Every stream of the scenario (each schedule: master, then each stem).
    inline Capture renderScenario(const ProcessorFactory& factory, const Scenario& sc, AllocationCounter* counter = nullptr)
    {
        Capture cap;
        for (const auto& sched : sc.schedules)
            for (int stem = -1; stem < sc.numStems; ++stem)
                renderStream(factory, sc, sched, stem, counter, cap);
        return cap;
    }

    // ─── Golden file ──────────────────────────────────────────────────────────
    inline juce::String currentConfig()
    {
       #if JUCE_WINDOWS
        const char* os = "windows";
       #elif JUCE_LINUX
        const char* os = "linux";
       #elif JUCE_MAC
        const char* os = "macos";
       #else
        const char* os = "other";
       #endif
       #if defined(__aarch64__) || defined(_M_ARM64)
        const char* arch = "arm64";
       #else
        const char* arch = "x64";
       #endif
       #if JUCE_DEBUG
        const char* mode = "debug";
       #else
        const char* mode = "release";
       #endif
        return juce::String(os) + "-" + arch + "-" + mode;
    }

    inline Lines parseGolden(const juce::String& text)
    {
        Lines out;
        for (const auto& line : juce::StringArray::fromLines(text))
        {
            const auto t = line.trim();
            if (t.isEmpty() || t.startsWithChar('#')) continue;
            const int eq = t.indexOfChar('=');
            if (eq > 0) out.emplace_back(t.substring(0, eq), t.substring(eq + 1));
        }
        return out;
    }

    inline juce::String renderGoldenText(const Scenario& sc, const juce::String& config, const Lines& lines,
                                         const juce::String& reason, const juce::String& baseline)
    {
        juce::String s;
        s << "# Golden render. Do not edit by hand: only tests/scripts/update-golden.ps1 changes this file.\n";
        s << "schema=" << kSchema << "\n";
        s << "config=" << config << "\n";
        s << "scenarioHash=" << sc.inputHash << "\n";
        s << "sampleRate=" << sc.sampleRate << "\n";
        s << "lengthFrames=" << sc.lengthFrames << "\n";
        s << "toleranceAbs=" << sc.toleranceAbs << "\n";
        s << "toleranceReason=" << sc.toleranceReason << "\n";
        s << "meta.reason=" << reason << "\n";
        s << "meta.baseline=" << baseline << "\n";
        for (const auto& kv : lines) s << kv.first << "=" << kv.second << "\n";
        return s;
    }

    // Splits "hashL hashR rmsDb peak24" (segment line value).
    struct SegValue { juce::String hashL, hashR, rms; juce::int64 peak24 = 0; };
    inline SegValue parseSeg(const juce::String& v)
    {
        const auto t = juce::StringArray::fromTokens(v, " ", "");
        SegValue s;
        if (t.size() >= 4) { s.hashL = t[0]; s.hashR = t[1]; s.rms = t[2]; s.peak24 = t[3].getLargeIntValue(); }
        return s;
    }

    inline juce::String describeKey(const Scenario& sc, const juce::String& key, const juce::String& golden, const juce::String& actual)
    {
        juce::String msg = key + ": golden [" + golden + "] actual [" + actual + "]";
        const int segPos = key.indexOf(".seg.");
        if (segPos >= 0)
        {
            const auto idx = key.substring(segPos + 5).getLargeIntValue();
            const double ms = (double) (idx * kSegmentFrames) * 1000.0 / sc.sampleRate;
            msg << "  (segment " << idx << ", from " << juce::String(ms, 1) << " ms; RMS golden "
                << parseSeg(golden).rms << " dBFS, actual " << parseSeg(actual).rms << " dBFS)";
        }
        return msg;
    }

    // Empty string = equal. Otherwise one line per differing key (first 40), the first being the earliest.
    inline juce::StringArray compareLines(const Scenario& sc, const Lines& golden, const Lines& actual)
    {
        juce::StringArray diffs;
        std::map<juce::String, juce::String> g(golden.begin(), golden.end());
        const bool tolerant = sc.toleranceAbs > 0.0;

        // Header keys (schema, config, scenarioHash, ...) have no dot and are checked by the store.
        auto isHeader = [](const juce::String& k) { return k.startsWith("meta.") || ! k.contains("."); };
        for (const auto& [key, val] : actual)
        {
            if (isHeader(key)) continue;
            auto it = g.find(key);
            if (it == g.end()) { diffs.add(key + ": not in the golden"); continue; }
            const auto goldenVal = it->second;
            g.erase(it);

            if (tolerant && key.contains(".seg."))
            {
                const auto a = parseSeg(val), b = parseSeg(goldenVal);
                if ((double) std::abs(a.peak24 - b.peak24) > sc.toleranceAbs)
                    diffs.add(describeKey(sc, key, goldenVal, val));
                continue;
            }
            // Under a tolerance the whole-render hash and the end-of-render values move with the audio.
            if (tolerant && (key.endsWith(".whole") || key.contains(".end."))) continue;
            if (val != goldenVal) diffs.add(describeKey(sc, key, goldenVal, val));
        }
        for (const auto& [key, val] : g)
            if (! isHeader(key) && ! (tolerant && (key.endsWith(".whole") || key.contains(".end."))))
                diffs.add(key + ": in the golden but not rendered");
        return diffs;
    }

    // ─── Store: read, write and compare goldens ──────────────────────────────
    struct Outcome
    {
        bool ok = true;
        bool skipped = false;
        juce::String message;
    };

    class GoldenStore
    {
    public:
        juce::File goldenRoot;            // tests/golden
        juce::File dumpRoot;              // tests/_out/golden
        juce::String config = currentConfig();
        bool updateMode = false;          // record instead of compare
        juce::String scenarioFilter;      // "product/scenario"; required in update mode (no update-all)
        juce::String reason, baseline;    // written into a recorded golden
        AllocationCounter* allocationCounter = nullptr;   // hook only; E0.7 supplies one

        // MU_GOLDEN_ROOT / _DUMP / _UPDATE / _SCENARIO / _REASON / _BASELINE / _EXPECT_CONFIG override the
        // defaults; the repo root is found by walking up from the executable to the folder with CLAUDE.md.
        static GoldenStore fromEnvironment()
        {
            GoldenStore s;
            auto env = [](const char* n) { return juce::SystemStats::getEnvironmentVariable(n, {}); };

            juce::File repo = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
            while (repo.getParentDirectory() != repo && ! repo.getChildFile("CLAUDE.md").existsAsFile())
                repo = repo.getParentDirectory();

            s.goldenRoot = env("MU_GOLDEN_ROOT").isNotEmpty() ? juce::File(env("MU_GOLDEN_ROOT")) : repo.getChildFile("tests/golden");
            s.dumpRoot   = env("MU_GOLDEN_DUMP").isNotEmpty() ? juce::File(env("MU_GOLDEN_DUMP")) : repo.getChildFile("tests/_out/golden");
            s.updateMode = env("MU_GOLDEN_UPDATE") == "1";
            s.scenarioFilter = env("MU_GOLDEN_SCENARIO");
            s.reason = env("MU_GOLDEN_REASON");
            s.baseline = env("MU_GOLDEN_BASELINE");
            s.expectConfig = env("MU_GOLDEN_EXPECT_CONFIG");
            return s;
        }

        juce::File goldenFile(const juce::String& product, const Scenario& sc) const
        {
            return goldenRoot.getChildFile(product).getChildFile(sc.name).getChildFile("golden." + config + ".txt");
        }

        // Record (update mode) or compare (otherwise) the scenario against its golden for this config.
        Outcome verify(const juce::String& product, const Scenario& sc, const ProcessorFactory& factory) const
        {
            Outcome out;
            if (expectConfig.isNotEmpty() && expectConfig != config)
                return fail("golden config is " + config + " but the caller asked for " + expectConfig);
            if (sc.toleranceAbs > 0.0 && sc.toleranceReason.trim().isEmpty())
                return fail(sc.name + ": toleranceAbs is set without a written toleranceReason");

            if (updateMode)
            {
                if (scenarioFilter.isEmpty())
                    return fail("update mode needs MU_GOLDEN_SCENARIO=<product>/<scenario>; there is no update-all");
                if (scenarioFilter != product + "/" + sc.name) { out.skipped = true; return out; }
                if (reason.trim().isEmpty()) return fail("update mode needs a reason (MU_GOLDEN_REASON)");
            }

            auto cap = renderScenario(factory, sc, allocationCounter);
            const auto file = goldenFile(product, sc);

            if (updateMode)
            {
                file.getParentDirectory().createDirectory();
                if (! file.replaceWithText(renderGoldenText(sc, config, cap.lines, reason, baseline)))
                    return fail("could not write " + file.getFullPathName());
                out.message = "recorded " + file.getFullPathName();
                return out;
            }

            if (! file.existsAsFile())
                return fail(sc.name + ": no golden for config " + config + " (" + file.getFullPathName()
                            + "); record it with tests/scripts/update-golden.ps1");

            const auto stored = parseGolden(file.loadFileAsString());
            std::map<juce::String, juce::String> head(stored.begin(), stored.end());
            if (head["schema"] != juce::String(kSchema))
                return fail(sc.name + ": golden schema " + head["schema"] + " is not " + juce::String(kSchema));
            if (head["scenarioHash"] != sc.inputHash)
                return fail(sc.name + ": scenario inputs changed since the golden was recorded (hash "
                            + head["scenarioHash"] + " vs " + sc.inputHash + "); a deliberate update is needed");

            const auto diffs = compareLines(sc, stored, cap.lines);
            if (diffs.isEmpty()) return out;

            dump(product, sc, file, cap, diffs);
            return fail(sc.name + " [" + config + "]: " + juce::String(diffs.size()) + " difference(s); first: " + diffs[0]
                        + "  (dump in " + dumpDir(product, sc).getFullPathName() + ")");
        }

        // Renders twice in one process and requires identical fingerprints (a hidden clock-seeded
        // generator or uninitialised memory shows up here). Cross-process repeat is done by update-golden.ps1.
        Outcome verifyRepeatable(const Scenario& sc, const ProcessorFactory& factory) const
        {
            const auto a = renderScenario(factory, sc, nullptr);
            const auto b = renderScenario(factory, sc, nullptr);
            const auto diffs = compareLines(sc, a.lines, b.lines);
            Outcome out;
            if (! diffs.isEmpty())
            {
                out.ok = false;
                out.message = sc.name + " is not repeatable: " + diffs[0];
            }
            return out;
        }

    private:
        juce::String expectConfig;

        static Outcome fail(const juce::String& m) { Outcome o; o.ok = false; o.message = m; return o; }

        juce::File dumpDir(const juce::String& product, const Scenario& sc) const
        {
            return dumpRoot.getChildFile(product).getChildFile(sc.name);
        }

        // On a mismatch: the golden, the actual fingerprint, the list of differing keys, and a 24-bit
        // WAV of every stream whose samples differ, so the divergence can be looked at.
        void dump(const juce::String& product, const Scenario& sc, const juce::File& goldenFileIn,
                  const Capture& cap, const juce::StringArray& diffs) const
        {
            const auto dir = dumpDir(product, sc);
            dir.createDirectory();
            goldenFileIn.copyFileTo(dir.getChildFile(config + ".golden.txt"));
            dir.getChildFile(config + ".actual.txt").replaceWithText(renderGoldenText(sc, config, cap.lines, "actual render, not recorded", ""));
            dir.getChildFile(config + ".diff.txt").replaceWithText(diffs.joinIntoString("\n") + "\n");

            juce::WavAudioFormat wav;
            for (const auto& [prefix, audio] : cap.audio)
            {
                if (! diffs.joinIntoString("\n").contains(prefix + ".")) continue;
                std::unique_ptr<juce::OutputStream> stream(dir.getChildFile(config + "." + prefix + ".actual.wav").createOutputStream().release());
                if (stream == nullptr) continue;
                const auto opts = juce::AudioFormatWriterOptions{}.withSampleRate(sc.sampleRate)
                                                                   .withNumChannels(2).withBitsPerSample(24);
                if (auto writer = wav.createWriterFor(stream, opts))
                    writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples());
            }
        }
    };
}
