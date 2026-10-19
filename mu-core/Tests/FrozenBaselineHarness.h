#pragma once

// Shared machinery for the layer-extraction safety net: a frozen parameter manifest per product and a
// compat corpus of real preset files, both checked against the REAL product processor built headless.
// The product test files (Source/Tests/FrozenBaselineTests.cpp) only name their processor type and
// their two file extensions; everything else lives here so the four products cannot drift apart.
//
// Frozen files are compared as text and never rewritten by a normal run. A deliberate change goes through
// the regenerate step: run the test exe with MU_REGENERATE_FROZEN=1 (tests/scripts/regenerate-frozen-baselines.ps1
// does this for all four), review the diff, and commit it with the change that caused it.

#include "Tests/ProcessorSeamHelpers.h"
#include "Persistence/PerformanceFile.h"
#include "MuLimits.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

namespace mu_frozen
{
    // True only when the owner asked for the frozen files to be rewritten.
    inline bool regenerateRequested()
    {
        return juce::SystemStats::getEnvironmentVariable("MU_REGENERATE_FROZEN", {}) == "1";
    }

    // Repo root from a product test source (<root>/<product>/Source/Tests/<file>.cpp).
    inline juce::File repoRootFrom(const char* testSourceFile)
    {
        return juce::File(testSourceFile).getParentDirectory().getParentDirectory()
                                         .getParentDirectory().getParentDirectory();
    }

    // The four products the corpus covers, with the extensions of their full and layer presets.
    struct ProductFormats { const char* dir; const char* fullExt; const char* layerExt; };
    inline const ProductFormats* allProducts(int& count)
    {
        static const ProductFormats products[] = {
            { "mu-clid", "muClid", "muRhythm" },
            { "mu-tant", "muTant", "muPattern" },
            { "mu-toni", "muToni", "muArp" },
            { "mu-on",   "muOn",   "muTrack" },
        };
        count = (int) std::size(products);
        return products;
    }

    //==============================================================================
    // Text helpers

    // Stable number format for frozen text (locale-independent, six significant digits).
    inline juce::String num(double v)
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.6g", v == 0.0 ? 0.0 : v);   // folds -0 into 0
        return buf;
    }

    // SHA-256 (FIPS 180-4), self-contained so the test targets need no extra JUCE module.
    inline juce::String sha256Hex(const void* data, size_t size)
    {
        static const uint32_t K[64] = {
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
            0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
            0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
            0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
            0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
            0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };
        uint32_t h[8] = { 0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19 };
        std::vector<uint8_t> m((const uint8_t*) data, (const uint8_t*) data + size);
        m.push_back(0x80);
        while (m.size() % 64 != 56) m.push_back(0);
        const uint64_t bits = (uint64_t) size * 8;
        for (int i = 7; i >= 0; --i) m.push_back((uint8_t) (bits >> (i * 8)));
        auto rotr = [](uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
        for (size_t off = 0; off < m.size(); off += 64)
        {
            uint32_t w[64];
            for (int i = 0; i < 16; ++i)
                w[i] = ((uint32_t) m[off + 4 * i] << 24) | ((uint32_t) m[off + 4 * i + 1] << 16)
                     | ((uint32_t) m[off + 4 * i + 2] << 8) | (uint32_t) m[off + 4 * i + 3];
            for (int i = 16; i < 64; ++i)
            {
                const uint32_t s0 = rotr(w[i-15], 7) ^ rotr(w[i-15], 18) ^ (w[i-15] >> 3);
                const uint32_t s1 = rotr(w[i-2], 17) ^ rotr(w[i-2], 19) ^ (w[i-2] >> 10);
                w[i] = w[i-16] + s0 + w[i-7] + s1;
            }
            uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
            for (int i = 0; i < 64; ++i)
            {
                const uint32_t t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
                const uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
                hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
            }
            h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
        }
        char out[65];
        for (int i = 0; i < 8; ++i) std::snprintf(out + 8 * i, 9, "%08x", h[i]);
        return out;
    }

    inline juce::String sha256Of(const juce::MemoryBlock& mb) { return sha256Hex(mb.getData(), mb.getSize()); }

    inline juce::String sha256Of(const juce::File& f)
    {
        juce::MemoryBlock mb;
        f.loadFileAsData(mb);
        return sha256Of(mb);
    }

    inline juce::String sha256OfText(const juce::String& s) { return sha256Hex(s.toRawUTF8(), std::strlen(s.toRawUTF8())); }

    inline juce::String readText(const juce::File& f)
    {
        return f.loadFileAsString().replace("\r\n", "\n");
    }

    inline void writeText(const juce::File& f, const juce::String& text)
    {
        f.getParentDirectory().createDirectory();
        f.deleteFile();
        juce::FileOutputStream out(f);
        if (out.openedOk())
            out.write(text.toRawUTF8(), std::strlen(text.toRawUTF8()));   // LF only, UTF-8, no BOM
    }

    // Compares `actual` with the frozen file as text. On a difference the failure message lists the
    // lines that went and the lines that came, so the changed id, range or value is readable at a glance.
    // In regenerate mode the file is written instead (and the test passes with a log line).
    inline void checkFrozen(juce::UnitTest& t, const juce::File& frozen, const juce::String& actual,
                            const juce::String& what)
    {
        if (regenerateRequested())
        {
            writeText(frozen, actual);
            t.logMessage("REGENERATED " + frozen.getFullPathName());
            t.expect(true);
            return;
        }
        if (! frozen.existsAsFile())
        {
            t.expect(false, what + ": frozen file missing: " + frozen.getFullPathName()
                            + " (create it with MU_REGENERATE_FROZEN=1 if this is a new baseline)");
            return;
        }
        const auto expected = readText(frozen);
        if (expected == actual) { t.expect(true); return; }

        const auto exp = juce::StringArray::fromLines(expected);
        const auto act = juce::StringArray::fromLines(actual);
        juce::String msg = what + " differs from " + frozen.getFullPathName()
                         + "\n(- frozen, + now; a deliberate change: re-run with MU_REGENERATE_FROZEN=1 and review the diff)";
        int shown = 0;
        for (const auto& l : exp)
            if (! act.contains(l) && shown++ < 40) msg << "\n  - " << l;
        for (const auto& l : act)
            if (! exp.contains(l) && shown++ < 80) msg << "\n  + " << l;
        if (shown == 0) msg << "\n  (same lines, different order or line count)";
        t.expect(false, msg);
    }

    //==============================================================================
    // Parameter dumps

    inline std::vector<juce::RangedAudioParameter*> sortedParams(juce::AudioProcessorValueTreeState& apvts)
    {
        std::vector<juce::RangedAudioParameter*> v;
        for (auto* p : apvts.processor.getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(p)) v.push_back(r);
        std::sort(v.begin(), v.end(), [](auto* a, auto* b) { return a->paramID < b->paramID; });
        return v;
    }

    // One manifest line: id | type | min | max | step | skew | default | normalised default [| choice count].
    inline juce::String manifestLine(juce::RangedAudioParameter& p)
    {
        const auto range = p.getNormalisableRange();
        juce::String type = "other";
        if (dynamic_cast<juce::AudioParameterFloat*>(&p))  type = "float";
        else if (dynamic_cast<juce::AudioParameterInt*>(&p))   type = "int";
        else if (dynamic_cast<juce::AudioParameterBool*>(&p))  type = "bool";
        else if (auto* c = dynamic_cast<juce::AudioParameterChoice*>(&p)) type = "choice(" + juce::String(c->choices.size()) + ")";

        return p.paramID + " | " + type + " | " + num(range.start) + " | " + num(range.end) + " | "
             + num(range.interval) + " | " + num(range.skew) + " | "
             + num(p.convertFrom0to1(p.getDefaultValue())) + " | " + num(p.getDefaultValue());
    }

    // The leading id token up to the first '_' (the per-layer prefix such as "r3" or "t2"), for the per-prefix counts.
    inline juce::String idPrefix(const juce::String& id)
    {
        const int u = id.indexOfChar('_');
        return u > 0 ? id.substring(0, u) : juce::String("(none)");
    }

    // Every parameter of a processor in a stable sorted text form, with a per-prefix count table so a
    // change in how many ids each layer owns shows up as one line.
    inline juce::String manifestText(const juce::String& product, juce::AudioProcessorValueTreeState& apvts,
                                     int layersAtStart, int maxLayers)
    {
        const auto params = sortedParams(apvts);
        juce::String s;
        s << "# " << product << " parameter manifest (FROZEN - see tests/manifests/README.md before changing)\n"
          << "# columns: id | type | min | max | step | skew | default (plain) | default (normalised) \n"
          << "product=" << product << "\n"
          << "layers_at_start=" << layersAtStart << "\n"
          << "max_layers=" << maxLayers << "\n"
          << "param_count=" << (int) params.size() << "\n"
          << "---\n";
        std::map<juce::String, int> perPrefix;
        for (auto* p : params)
        {
            s << manifestLine(*p) << "\n";
            ++perPrefix[idPrefix(p->paramID)];
        }
        s << "--- ids per prefix\n";
        for (const auto& kv : perPrefix) s << kv.first << "=" << kv.second << "\n";
        return s;
    }

    // Parameters whose current value is not the default, as sorted "id=plain" lines. A loaded fixture's
    // frozen dump is this list, so the file stays small and a changed default shows up in the manifest instead.
    inline juce::String deltaText(juce::AudioProcessorValueTreeState& apvts)
    {
        juce::String s;
        for (auto* p : sortedParams(apvts))
            if (std::abs(p->getValue() - p->getDefaultValue()) > 1.0e-6f)
                s << p->paramID << "=" << num(p->convertFrom0to1(p->getValue())) << "\n";
        return s;
    }

    //==============================================================================
    // The tests

    // E0.3: the parameter-id manifest. Also proves the id set does not depend on the layer count, so a
    // later change to the layer capacity can be compared against this file line for line.
    template <class Proc>
    void runManifestTest(juce::UnitTest& t, const char* product, const char* testSourceFile)
    {
        const auto frozen = repoRootFrom(testSourceFile).getChildFile("tests").getChildFile("manifests")
                                .getChildFile(juce::String(product) + ".txt");

        t.beginTest(juce::String(product) + " parameter manifest matches the frozen file");
        mu_seam_test::TempStorage storage;
        Proc p;
        const int layersAtStart = p.getNumLayers();
        const auto text = manifestText(product, p.apvts, layersAtStart, mu_limits::kMaxLayers);
        checkFrozen(t, frozen, text, "parameter manifest");

        t.beginTest(juce::String(product) + " parameter ids do not depend on the layer count");
        std::vector<juce::String> before;
        for (auto* q : sortedParams(p.apvts)) before.push_back(q->paramID + "|" + manifestLine(*q));
        int guard = 0;
        while (p.addLayer() >= 0 && ++guard < 64) {}
        t.expectEquals(p.getNumLayers(), mu_limits::kMaxLayers, "grew to the layer capacity");
        std::vector<juce::String> after;
        for (auto* q : sortedParams(p.apvts)) after.push_back(q->paramID + "|" + manifestLine(*q));
        t.expect(before == after, "the parameter set at " + juce::String(layersAtStart) + " layer(s) and at "
                                  + juce::String(p.getNumLayers()) + " layers is identical");
    }

    // The processor with the protected layer-tree staging entry point made callable (some products keep it protected).
    template <class Proc>
    struct Exposed : Proc { using Proc::stageLayerPresetTree; };

    // Collects load errors a processor reports.
    struct ErrorLog
    {
        juce::StringArray messages;
        template <class Proc> void attach(Proc& p) { p.onLoadError = [this](const juce::String& m) { messages.add(m); }; }

        // Notices that a load still succeeded: the fixture names a sample the test's empty content folder lacks,
        // or its samples came from embedded data. Everything else (unreadable, invalid, wrong product) is a real failure.
        static bool isNotice(const juce::String& m)
        {
            return m.contains("missing from content folder") || m.contains("originated from embedded data");
        }
        int hardErrors() const
        {
            int n = 0;
            for (auto& m : messages) if (! isNotice(m)) ++n;
            return n;
        }
    };

    // Frozen text for one loaded fixture.
    inline juce::String fixtureText(const juce::String& name, int layers, const juce::StringArray& errors,
                                    const juce::String& resaveLabel, const juce::MemoryBlock& resave,
                                    juce::AudioProcessorValueTreeState& apvts)
    {
        juce::String s;
        s << "# frozen load result for fixture: " << name << "\n"
          << "layers=" << layers << "\n"
          << "load_errors=" << errors.size() << "\n";
        for (auto& e : errors) s << "load_error=" << e.replace("\n", " ") << "\n";
        s << resaveLabel << "_bytes=" << (int) resave.getSize() << "\n"
          << resaveLabel << "_sha256=" << sha256Of(resave) << "\n"
          << "--- parameters differing from default (id=plain value)\n"
          << deltaText(apvts);
        return s;
    }

    inline juce::File expectFileFor(const juce::File& fixture)
    {
        return fixture.getSiblingFile(fixture.getFileName() + ".params.txt");
    }

    // A full preset: loads clean, parameter values match the frozen dump, a re-save matches the frozen
    // baseline re-save, and saving again after a reload gives the same bytes.
    template <class Proc>
    void checkFullPreset(juce::UnitTest& t, const juce::File& fixture)
    {
        t.beginTest("full preset " + fixture.getFileName());
        mu_seam_test::TempStorage storage;
        Proc p;
        ErrorLog log; log.attach(p);
        p.loadPreset(fixture);
        p.flushPendingAsyncUpdates();
        t.expectEquals(log.hardErrors(), 0, "the fixture loads without error (sample notices are frozen in its dump)");

        // Saved under the fixture's own file name (in separate folders): the preset's name is the file name.
        const auto save1 = storage.root.getChildFile("save1").getChildFile(fixture.getFileName());
        save1.getParentDirectory().createDirectory();
        t.expect(p.saveFullPresetTo(save1), "re-save wrote a file");
        juce::MemoryBlock bytes1;
        save1.loadFileAsData(bytes1);
        checkFrozen(t, expectFileFor(fixture),
                    fixtureText(fixture.getFileName(), p.getNumLayers(), log.messages, "resave", bytes1, p.apvts),
                    fixture.getFileName());

        // A second save from the same processor, then from a processor that loaded the first save.
        const auto save1b = storage.root.getChildFile("save1b").getChildFile(fixture.getFileName());
        save1b.getParentDirectory().createDirectory();
        p.saveFullPresetTo(save1b);
        juce::MemoryBlock bytes1b;
        save1b.loadFileAsData(bytes1b);
        t.expect(bytes1 == bytes1b, "saving twice from one processor gives the same bytes");

        Proc q;
        ErrorLog log2; log2.attach(q);
        q.loadPreset(save1);
        q.flushPendingAsyncUpdates();
        const auto save2 = storage.root.getChildFile("save2").getChildFile(fixture.getFileName());
        save2.getParentDirectory().createDirectory();
        q.saveFullPresetTo(save2);
        juce::MemoryBlock bytes2;
        save2.loadFileAsData(bytes2);
        t.expect(bytes1 == bytes2, "save, reload, save is idempotent (byte-identical)");
        t.expectEquals(deltaText(q.apvts), deltaText(p.apvts), "the re-saved file reloads to the same parameter values");
        t.expectEquals(log2.hardErrors(), 0, "the re-saved file reloads without error");
    }

    // A layer preset, loaded into layer 0. Its "re-save" is the layer's captured preset tree as XML.
    template <class Proc>
    void checkLayerPreset(juce::UnitTest& t, const juce::File& fixture)
    {
        t.beginTest("layer preset " + fixture.getFileName());
        mu_seam_test::TempStorage storage;
        Proc p;
        ErrorLog log; log.attach(p);
        p.loadLayerPreset(0, fixture);
        p.flushPendingAsyncUpdates();

        t.expectEquals(log.hardErrors(), 0, "the fixture loads without error (sample notices are frozen in its dump)");

        t.expectEquals(log.hardErrors(), 0, "the fixture loads without error (sample notices are frozen in its dump)");

        const auto tree = p.captureLayerPresetTree(0);
        const auto xml1 = tree.toXmlString();
        juce::MemoryBlock bytes1(xml1.toRawUTF8(), std::strlen(xml1.toRawUTF8()));
        checkFrozen(t, expectFileFor(fixture),
                    fixtureText(fixture.getFileName(), p.getNumLayers(), log.messages, "capture", bytes1, p.apvts),
                    fixture.getFileName());

        // Capture, stage into a fresh processor, capture again: the layer tree is stable.
        Exposed<Proc> q;
        ErrorLog log2; log2.attach(q);
        q.stageLayerPresetTree(0, tree, false);
        q.flushPendingAsyncUpdates();
        const auto xml2 = q.captureLayerPresetTree(0).toXmlString();
        t.expectEquals(xml2, xml1, "capture, load, capture is idempotent");
        t.expectEquals(deltaText(q.apvts), deltaText(p.apvts), "the captured tree restores the same parameter values");
        t.expectEquals(log2.hardErrors(), 0, "the captured tree loads without error");
    }

    // A Performance file: parses, every item resolves to a corpus preset, and it re-saves to the same text.
    template <class Proc>
    void checkPerformance(juce::UnitTest& t, const juce::File& fixture)
    {
        t.beginTest("performance " + fixture.getFileName());
        mu_seam_test::TempStorage storage;
        Proc p;
        const auto own = p.apvts.state.getType().toString();

        mu_pp::PerformanceFile perf;
        t.expect(mu_pp::PerformanceFile::load(fixture, perf) == mu_pp::PerformanceFile::LoadResult::Ok, "loads");

        juce::String s;
        s << "# frozen load result for fixture: " << fixture.getFileName() << "\n"
          << "name=" << perf.name << "\nitems=" << perf.count() << "\nignored=" << perf.ignoredItems << "\n";
        for (int i = 0; i < mu_pp::PerformanceFile::kMaxItems; ++i)
        {
            if (! perf.items[(size_t) i].used) continue;
            const auto status = perf.status(i, fixture.getParentDirectory(), own);
            t.expect(status == mu_pp::PerformanceFile::ItemStatus::Ok,
                     "item " + juce::String(i) + " (" + perf.items[(size_t) i].path + ") resolves to a corpus preset");
            s << "item" << i << "=" << perf.items[(size_t) i].path << " | " << perf.items[(size_t) i].product
              << " | status " << (int) status << "\n";
        }
        const auto xml = perf.toXml();
        s << "resave_sha256=" << sha256OfText(xml) << "\n";
        checkFrozen(t, expectFileFor(fixture), s, fixture.getFileName());

        // Saving and loading again gives the same text.
        const auto copy = storage.root.getChildFile("copy.muPerformance");
        t.expect(perf.save(copy), "re-save wrote a file");
        mu_pp::PerformanceFile again;
        t.expect(mu_pp::PerformanceFile::load(copy, again) == mu_pp::PerformanceFile::LoadResult::Ok, "re-save reloads");
        t.expectEquals(again.toXml(), xml, "performance save, load, save is idempotent");
    }

    // Other products' files offered to this product: nothing crashes, and what each one does is frozen
    // (rejected with a message, or accepted with the resulting parameter delta). A sample of the first two
    // fixtures of each foreign format keeps the run short.
    template <class Proc>
    void checkCrossProduct(juce::UnitTest& t, const char* product, const juce::File& corpusRoot)
    {
        t.beginTest(juce::String(product) + " is offered other products' layer and full presets");
        juce::String s;
        s << "# frozen cross-product load behaviour of " << product << " (each foreign file offered to layer 0 / as a full preset)\n";
        int count = 0;
        const auto* products = allProducts(count);
        for (int i = 0; i < count; ++i)
        {
            if (juce::String(products[i].dir) == product) continue;
            const auto dir = corpusRoot.getChildFile(products[i].dir);
            for (const bool full : { false, true })
            {
                const juce::String ext = full ? products[i].fullExt : products[i].layerExt;
                auto files = dir.findChildFiles(juce::File::findFiles, false, "*." + ext);
                files.removeIf([](const juce::File& f) { return f.getFileName().startsWith("FACTORY_STATE"); });   // generated, so not a stable pick
                std::sort(files.begin(), files.end(), [](auto& a, auto& b) { return a.getFileName() < b.getFileName(); });
                for (int n = 0; n < juce::jmin(2, files.size()); ++n)
                {
                    mu_seam_test::TempStorage storage;
                    Proc p;
                    ErrorLog log; log.attach(p);
                    if (full) p.loadPreset(files[n]); else p.loadLayerPreset(0, files[n]);
                    p.flushPendingAsyncUpdates();
                    s << products[i].dir << "/" << files[n].getFileName() << (full ? " (full)" : " (layer)")
                      << ": errors=" << log.messages.size()
                      << (log.messages.isEmpty() ? juce::String() : " first=\"" + log.messages[0].replace("\n", " ") + "\"")
                      << " params_sha256=" << sha256OfText(deltaText(p.apvts)).substring(0, 16) << "\n";
                }
            }
        }
        checkFrozen(t, corpusRoot.getChildFile(product).getChildFile("CROSSLOAD.txt"), s, "cross-product loads");
    }

    // MANIFEST.txt: a SHA-256 of every file in the product's corpus folder, so a fixture cannot change unnoticed.
    inline void checkCorpusManifest(juce::UnitTest& t, const juce::File& dir)
    {
        t.beginTest(dir.getFileName() + " corpus MANIFEST.txt hashes every file");
        auto files = dir.findChildFiles(juce::File::findFiles, false);
        std::sort(files.begin(), files.end(), [](auto& a, auto& b) { return a.getFileName() < b.getFileName(); });
        juce::String s;
        s << "# SHA-256, size, file. Fixtures are read-only copies (see tests/corpus/README.md); *.params.txt are the frozen\n"
          << "# load results next to them. A change here is deliberate only via MU_REGENERATE_FROZEN=1.\n";
        for (auto& f : files)
            if (f.getFileName() != "MANIFEST.txt")
                s << sha256Of(f) << "  " << (juce::int64) f.getSize() << "  " << f.getFileName() << "\n";
        checkFrozen(t, dir.getChildFile("MANIFEST.txt"), s, "corpus manifest");
    }

    // Writes the freshly saved factory-state full preset fixture (regenerate mode only; otherwise it must exist).
    template <class Proc>
    void maybeWriteFactoryState(juce::UnitTest& t, const juce::File& dir, const char* fullExt)
    {
        const auto file = dir.getChildFile(juce::String("FACTORY_STATE.") + fullExt);
        if (regenerateRequested())
        {
            mu_seam_test::TempStorage storage;
            Proc p;
            t.expect(p.saveFullPresetTo(file), "wrote the factory-state fixture");
        }
        else
            t.expect(file.existsAsFile(), "factory-state fixture present: " + file.getFullPathName());
    }

    // The whole corpus pass for one product.
    template <class Proc>
    void runCorpusTests(juce::UnitTest& t, const char* product, const char* fullExt, const char* layerExt,
                        const char* testSourceFile)
    {
        const auto corpusRoot = repoRootFrom(testSourceFile).getChildFile("tests").getChildFile("corpus");
        const auto dir = corpusRoot.getChildFile(product);

        t.beginTest(juce::String(product) + " corpus folder exists");
        t.expect(dir.isDirectory(), dir.getFullPathName());
        if (! dir.isDirectory()) return;

        maybeWriteFactoryState<Proc>(t, dir, fullExt);

        auto files = dir.findChildFiles(juce::File::findFiles, false);
        std::sort(files.begin(), files.end(), [](auto& a, auto& b) { return a.getFileName() < b.getFileName(); });
        for (auto& f : files)
        {
            const auto ext = f.getFileExtension().substring(1);
            if (ext == fullExt)             checkFullPreset<Proc>(t, f);
            else if (ext == layerExt)       checkLayerPreset<Proc>(t, f);
            else if (ext == "muPerformance") checkPerformance<Proc>(t, f);
        }
        checkCrossProduct<Proc>(t, product, corpusRoot);
        checkCorpusManifest(t, dir);   // last: in regenerate mode it hashes the files written above
    }
}
