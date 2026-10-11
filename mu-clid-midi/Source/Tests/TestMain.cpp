// Console entry point for the mu-Clid MIDI unit tests: runs every juce::UnitTest registered in the
// link graph (each test source defines a file-scope static instance).
//
// Build:  cmake --build build --target mu-clid-midi-tests
// Run:    build/mu-clid-midi/mu-clid-midi-tests_artefacts/Debug/mu-clid-midi-tests.exe
// Exit:   0 on success, non-zero if any test reported a failure.
//
// Scope: the product's data layer only (parameter layout, session state, accent velocities); no
// processor, MessageManager or audio-thread setup.

#include <juce_core/juce_core.h>
#include <cstdio>
#include <iostream>

// Route JUCE's logging to stdout so test progress / failure messages are visible in a console run
// (the default Windows logger goes to OutputDebugString).
class StdoutLogger : public juce::Logger
{
public:
    void logMessage(const juce::String& msg) override
    {
        std::cout << msg << std::endl;
    }
};

int main(int, char**)
{
    StdoutLogger logger;
    juce::Logger::setCurrentLogger(&logger);

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure(false);   // Print failures, don't trap into a debugger.
    runner.runAllTests();

    int totalFailures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        totalFailures += runner.getResult(i)->failures;

    std::printf("\n=== mu-clid-midi-tests: %d failure(s) across %d test result(s) ===\n",
                totalFailures, runner.getNumResults());
    juce::Logger::setCurrentLogger(nullptr);
    return totalFailures == 0 ? 0 : 1;
}
