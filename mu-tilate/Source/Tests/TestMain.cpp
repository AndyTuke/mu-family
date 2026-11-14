// mu-tilate test runner - console entry that runs every juce::UnitTest registered in the link graph (each test
// file defines a `static MyTest x;` at file scope).
//
// Build:  cmake --build build --config Debug --target mu-tilate-tests
// Run:    build/mu-tilate/mu-tilate-tests_artefacts/Debug/mu-tilate-tests.exe
// Scope:  the engine pieces that run without a processor (slice player; later the stretch voice, slice list, timeline).

#include "Tests/TestSelection.h"
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <cstdio>
#include <iostream>

class StdoutLogger : public juce::Logger
{
public:
    void logMessage(const juce::String& msg) override { std::cout << msg << std::endl; }
};

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    StdoutLogger logger;
    juce::Logger::setCurrentLogger(&logger);

    mu_test::TimedRunner runner;
    runner.setAssertOnFailure(false);
    mu_test::runSelected(runner, argc, argv);   // default: everything but the slow golden renders; --goldens, --all, or one category (Tests/TestSelection.h)

    runner.printSlowest();

    int totalFailures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        totalFailures += runner.getResult(i)->failures;

    std::printf("\n=== mu-tilate-tests: %d failure(s) across %d test result(s) ===\n",
                totalFailures, runner.getNumResults());
    juce::Logger::setCurrentLogger(nullptr);
    return totalFailures == 0 ? 0 : 1;
}
