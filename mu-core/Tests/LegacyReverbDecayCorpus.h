#pragma once

// Every corpus full preset through the REAL product processor: a file saved before the reverb Decay
// knob (no rev_decay in it) loads with the Decay its reverb algorithm had then
// (mu_pp::legacyReverbDecay), by preset load and by host state restore alike, and never keeps the
// Decay the instance held before. A file that saves rev_decay (the factory state) keeps its value.
// The product test files only name their processor type and full-preset extension.

#include "Tests/FrozenBaselineHarness.h"   // repoRootFrom, TempStorage
#include "Persistence/LegacyReverbDecay.h"

#include <algorithm>

namespace mu_legacy_reverb
{
    // True when the file saves rev_decay (its text names it).
    inline bool savesDecay(const juce::File& f) { return f.loadFileAsString().contains(mu_pp::kReverbDecayId); }

    template <class Proc>
    void runCorpusDecayTests(juce::UnitTest& t, const char* product, const char* fullExt, const char* testSourceFile)
    {
        const auto dir = mu_frozen::repoRootFrom(testSourceFile).getChildFile("tests").getChildFile("corpus")
                                                                .getChildFile(product);
        auto files = dir.findChildFiles(juce::File::findFiles, false, juce::String("*.") + fullExt);
        std::sort(files.begin(), files.end(), [](auto& a, auto& b) { return a.getFileName() < b.getFileName(); });

        t.beginTest(juce::String(product) + " corpus full presets load the reverb Decay their algorithm had before the knob");
        t.expect(! files.isEmpty(), "the corpus has full presets: " + dir.getFullPathName());

        // Each file by preset load and by host restore, into a processor whose Decay was 9 s.
        int legacy = 0;
        for (const auto& f : files)
        {
            const bool saved = savesDecay(f);
            if (! saved) ++legacy;
            for (const bool host : { false, true })
            {
                mu_seam_test::TempStorage storage;
                Proc p;
                auto* decayParam = p.apvts.getParameter(mu_pp::kReverbDecayId);
                decayParam->setValueNotifyingHost(decayParam->convertTo0to1(9.0f));
                if (host)
                {
                    // The file's state as an old session of this product: XML text (mu-Clid's session is its
                    // preset tree as is), or JUCE's binary XML of the state the preset file wraps.
                    juce::MemoryBlock probe, bytes;
                    p.getStateInformation(probe);
                    const bool text = probe.getSize() > 0 && static_cast<const char*>(probe.getData())[0] == '<';
                    if (text)
                        f.loadFileAsData(bytes);
                    else if (auto xml = juce::parseXML(f))
                    {
                        const auto file = juce::ValueTree::fromXml(*xml);
                        const auto session = file.hasType(p.apvts.state.getType()) ? file
                                                                                    : file.getChildWithName(p.apvts.state.getType());
                        if (auto out = session.createXml())
                            juce::AudioProcessor::copyXmlToBinary(*out, bytes);
                    }
                    t.expect(bytes.getSize() > 0, f.getFileName() + ": host state built");
                    p.setStateInformation(bytes.getData(), (int) bytes.getSize());
                }
                else
                {
                    p.loadPreset(f);
                }
                p.flushPendingAsyncUpdates();

                const int algorithm = juce::roundToInt(p.apvts.getRawParameterValue(mu_pp::kReverbAlgoId)->load());
                const float decay   = p.apvts.getRawParameterValue(mu_pp::kReverbDecayId)->load();
                const auto& slot    = p.fxChain.reverbSlot();
                const juce::String what = f.getFileName() + (host ? " (host restore)" : " (preset load)");
                t.logMessage(what + ": algorithm " + juce::String(algorithm) + ", decay " + juce::String(decay, 9)
                             + (saved ? " (saved in the file)" : " (migrated)"));

                t.expectEquals(slot.getAlgorithmIndex(), algorithm, what + ": the slot plays the loaded algorithm");
                t.expect(slot.getDecay() == decay, what + ": the slot's decay is the parameter's");
                if (saved)
                    t.expectWithinAbsoluteError(decay, 2.5f, 1.0e-5f, what + ": the saved Decay (2.5) is kept");
                else
                    t.expect(decay == mu_pp::legacyReverbDecay(algorithm),
                             what + ": Decay " + juce::String(decay, 9) + " is the algorithm's old "
                                  + juce::String(mu_pp::legacyReverbDecay(algorithm), 9));
            }
        }
        t.expect(legacy > 0, "the corpus has full presets saved before the Decay knob");
    }
}
