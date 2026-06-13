// mu-toni test runner — console entry that runs every juce::UnitTest registered
// in the link graph (each test file defines a `static MyTest x;` at file scope).
//
// Build:  cmake --build build --config Debug --target mu-toni-tests
// Run:    build/mu-toni/mu-toni-tests_artefacts/Debug/mu-toni-tests.exe
// Scope:  scaffold coverage — the shared global-FX APVTS layout the mixer binds
//         to. Grows as mu-toni's engine lands. The full PluginProcessor is not
//         linked (its createEditor() drags the editor/UI tree in — see the backlog).

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

    std::printf("\n=== mu-toni-tests: %d failure(s) across %d test result(s) ===\n",
                totalFailures, runner.getNumResults());
    juce::Logger::setCurrentLogger(nullptr);
    return totalFailures == 0 ? 0 : 1;
}
