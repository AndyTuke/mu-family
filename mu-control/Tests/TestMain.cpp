// mu-control test runner - console entry that runs every juce::UnitTest registered in the
// link graph (each test file defines a `static MyTest x;` at file scope).
//
// Build:  cmake --build build --config Debug --target mu-control-tests
// Run:    build/mu-control/Tests/mu-control-tests_artefacts/Debug/mu-control-tests.exe
// Scope:  the device-independent surface layer (description, mapping, model); no device, no processor.

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <cstdio>
#include <iostream>

class StdoutLogger : public juce::Logger
{
public:
    void logMessage(const juce::String& msg) override { std::cout << msg << std::endl; }
};

int main(int, char**)
{
    // The manager tests need a message loop for the MIDI-thread hand-off; this thread becomes the message thread.
    juce::ScopedJuceInitialiser_GUI juceInit;

    StdoutLogger logger;
    juce::Logger::setCurrentLogger(&logger);

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure(false);
    runner.runAllTests();

    int totalFailures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        totalFailures += runner.getResult(i)->failures;

    std::printf("\n=== mu-control-tests: %d failure(s) across %d test result(s) ===\n",
                totalFailures, runner.getNumResults());
    juce::Logger::setCurrentLogger(nullptr);
    return totalFailures == 0 ? 0 : 1;
}
