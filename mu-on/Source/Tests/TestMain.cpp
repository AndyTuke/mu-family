// mu-on test runner - console entry that runs every juce::UnitTest registered in the
// link graph (each test file defines a `static MyTest x;` at file scope).
//
// Build:  cmake --build build --config Debug --target mu-on-tests
// Run:    build/mu-on/mu-on-tests_artefacts/Debug/mu-on-tests.exe
// Scope:  shared global-FX APVTS layout (the mixer binds to it), the 909 sequencer,
//         and the channel engines. The full PluginProcessor is not linked (its
//         createEditor() drags the editor/UI tree into a console app - see the backlog).

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

    std::printf("\n=== mu-on-tests: %d failure(s) across %d test result(s) ===\n",
                totalFailures, runner.getNumResults());
    juce::Logger::setCurrentLogger(nullptr);
    return totalFailures == 0 ? 0 : 1;
}
