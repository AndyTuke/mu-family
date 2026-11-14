#pragma once

#include <juce_core/juce_core.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

// Which tests a product's test app runs, from its command line:
//   (none)        every test except the golden renders (categories ending "-golden"): the quick check after a change
//   --goldens     only the golden renders (they render audio offline twice each and are slow: end of day, before a release)
//   --all         everything
//   --list        print the categories, one per line, and run nothing (run-tests.ps1 uses it to split a slow suite across processes)
//   <category>    that one category, as before
// The golden renders are the bulk of a Debug sweep (mu-Tant: ~60 of its 320 results, most of its 20 minutes).
namespace mu_test
{
    // A UnitTestRunner that times every test, so the slowest can be listed at the end (printSlowest): the first thing to look at when a
    // suite is slow. It changes nothing else.
    class TimedRunner : public juce::UnitTestRunner
    {
    public:
        void printSlowest(int count = 8)
        {
            finishTimed();
            auto sorted = times;
            std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
            std::printf("\nslowest tests:\n");
            for (int i = 0; i < count && i < (int) sorted.size(); ++i)
                std::printf("  %7.1f s  %s\n", sorted[(size_t) i].second, sorted[(size_t) i].first.c_str());
        }

    protected:
        // Called whenever the results change: a new result means a new test just started, so the one before it has finished.
        void resultsUpdated() override
        {
            const int n = getNumResults();
            if (n != lastCount)
            {
                lastCount = n;
                finishTimed();
                if (auto* r = getResult(n - 1))
                {
                    current = (r->unitTestName + " / " + r->subcategoryName).toStdString();
                    started = std::chrono::steady_clock::now();
                    timing = true;
                }
            }
        }

    private:
        void finishTimed()
        {
            if (! timing) return;
            timing = false;
            times.emplace_back(current, std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
        }
        std::vector<std::pair<std::string, double>> times;
        std::string current;
        std::chrono::steady_clock::time_point started;
        bool timing = false;
        int lastCount = 0;
    };

    inline void runSelected(juce::UnitTestRunner& runner, int argc, char** argv)
    {
        const juce::String arg = argc > 1 ? juce::String(argv[1]) : juce::String();
        if (arg == "--all") { runner.runAllTests(); return; }
        if (arg == "--list")
        {
            juce::StringArray categories;
            for (auto* t : juce::UnitTest::getAllTests()) categories.addIfNotAlreadyThere(t->getCategory());
            for (const auto& c : categories) std::printf("%s\n", c.toRawUTF8());
            return;
        }

        const bool onlyGoldens = arg == "--goldens";
        if (arg.isNotEmpty() && ! onlyGoldens) { runner.runTestsInCategory(arg); return; }

        juce::Array<juce::UnitTest*> picked;
        for (auto* t : juce::UnitTest::getAllTests())
            if (t->getCategory().endsWith("-golden") == onlyGoldens)
                picked.add(t);
        runner.runTests(picked);
    }
}
