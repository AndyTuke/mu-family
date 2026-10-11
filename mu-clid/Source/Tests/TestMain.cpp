// preset round-trip + data-layer regression tests.
//
// This is a console-app entry point that runs every juce::UnitTest subclass
// registered in the same translation unit's link graph. Each test source file
// in this directory defines a `static MyTest myTestInstance;` at file scope -
// JUCE's UnitTestRunner picks them up automatically.
//
// Build:  cmake --build build --target mu-clid-tests
// Run:    build/mu-clid-tests_artefacts/Release/mu-clid-tests.exe
// Exit:   0 on success, non-zero if any test reported a failure.
//
// Scope: data-layer tests, plus the real PluginProcessor built headless
// (createEditor lives in PluginEditor.cpp, which is not linked). The point is to catch drift in:
//   - kRhythmParamDefs apply/push lambda pairs
//   - writeKindedProperty / readKindedPropertyAsActualV2 round-trip
//   - serialiseModulators / deserialiseModulators round-trip
//   - algorithm-name table sizes vs. dispatch-table sizes
//   - v0 / v1 legacy-preset migration
// Fast (<1s), deterministic, no host dependency.

#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <cstdio>
#include <iostream>

// Route JUCE's logging to stdout so test progress / failure messages are
// visible in the terminal (default Windows logger goes to OutputDebugString,
// which is invisible to a console run).
class StdoutLogger : public juce::Logger
{
public:
    void logMessage(const juce::String& msg) override
    {
        std::cout << msg << std::endl;
    }
};

int main(int argc, char** argv)
{
    // ProcessorBase starts a message-thread poll timer, which needs a message manager.
    juce::ScopedJuceInitialiser_GUI juceInit;

    StdoutLogger logger;
    juce::Logger::setCurrentLogger(&logger);

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure(false);   // Print failures, don't trap into a debugger.
    if (argc > 1) runner.runTestsInCategory(argv[1]);   // optional: run only one category
    else          runner.runAllTests();

    int totalFailures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        totalFailures += runner.getResult(i)->failures;

    std::printf("\n=== mu-clid-tests: %d failure(s) across %d test result(s) ===\n",
                totalFailures, runner.getNumResults());
    juce::Logger::setCurrentLogger(nullptr);
    return totalFailures == 0 ? 0 : 1;
}
