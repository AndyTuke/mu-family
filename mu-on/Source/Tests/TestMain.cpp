// mu-on test runner - console entry that runs every juce::UnitTest registered in the
// link graph (each test file defines a `static MyTest x;` at file scope).
//
// Build:  cmake --build build --config Debug --target mu-on-tests
// Run:    build/mu-on/mu-on-tests_artefacts/Debug/mu-on-tests.exe
// Scope:  shared global-FX APVTS layout (the mixer binds to it), the 909 sequencer,
//         and the channel engines, plus the real PluginProcessor built headless
//         (createEditor lives in PluginEditor.cpp, which is not linked).

#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <cstdio>
#include <iostream>

class StdoutLogger : public juce::Logger
{
public:
    void logMessage(const juce::String& msg) override { std::cout << msg << std::endl; }
};

int main(int argc, char** argv)
{
    // ProcessorBase starts a message-thread poll timer, which needs a message manager.
    juce::ScopedJuceInitialiser_GUI juceInit;

    StdoutLogger logger;
    juce::Logger::setCurrentLogger(&logger);

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure(false);
    if (argc > 1) runner.runTestsInCategory(argv[1]);   // optional: run only one category
    else          runner.runAllTests();

    int totalFailures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        totalFailures += runner.getResult(i)->failures;

    std::printf("\n=== mu-on-tests: %d failure(s) across %d test result(s) ===\n",
                totalFailures, runner.getNumResults());
    juce::Logger::setCurrentLogger(nullptr);
    return totalFailures == 0 ? 0 : 1;
}
