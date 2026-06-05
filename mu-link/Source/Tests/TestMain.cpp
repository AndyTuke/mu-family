// mu-link test runner — console entry that runs every juce::UnitTest in the link
// graph (each test file defines a `static MyTest x;` at file scope).
//
// Build:  cmake --build build --config Debug --target mu-link-tests
// Run:    build/mu-link/mu-link-tests_artefacts/Debug/mu-link-tests.exe
// Scope:  the portable IPC foundation — lock-free SPSC AudioRing + sample-accurate
//         TransportClock. The audio server, shared-memory mapping, and GUI are not
//         linked here (they arrive in later increments).

#include <juce_core/juce_core.h>
#include <cstdio>
#include <iostream>

class StdoutLogger : public juce::Logger
{
public:
    void logMessage(const juce::String& msg) override { std::cout << msg << std::endl; }
};

int main(int, char**)
{
    StdoutLogger logger;
    juce::Logger::setCurrentLogger(&logger);

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure(false);
    runner.runAllTests();

    int totalFailures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        totalFailures += runner.getResult(i)->failures;

    std::printf("\n=== mu-link-tests: %d failure(s) across %d test result(s) ===\n",
                totalFailures, runner.getNumResults());
    juce::Logger::setCurrentLogger(nullptr);
    return totalFailures == 0 ? 0 : 1;
}
