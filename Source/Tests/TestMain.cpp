// preset round-trip + data-layer regression tests.
//
// This is a console-app entry point that runs every juce::UnitTest subclass
// registered in the same translation unit's link graph. Each test source file
// in this directory defines a `static MyTest myTestInstance;` at file scope —
// JUCE's UnitTestRunner picks them up automatically.
//
// Build:  cmake --build build --target mu-clid-tests
// Run:    build/mu-clid-tests_artefacts/Release/mu-clid-tests.exe
// Exit:   0 on success, non-zero if any test reported a failure.
//
// Scope: DATA-LAYER tests only — no PluginProcessor / MessageManager / audio-
// thread setup. The point is to catch #430-class drift in:
//   - kRhythmParamDefs apply/push lambda pairs
//   - writeKindedProperty / readKindedPropertyAsActualV2 round-trip
//   - serialiseModulators / deserialiseModulators round-trip
//   - algorithm-name table sizes vs. dispatch-table sizes
//   - v0 / v1 legacy-preset migration
// Fast (<1s), deterministic, no host dependency.

#include <juce_core/juce_core.h>
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

int main(int, char**)
{
    StdoutLogger logger;
    juce::Logger::setCurrentLogger(&logger);

    // Data-layer tests don't need MessageManager / GUI init. If future tests
    // ever instantiate PluginProcessor or any UI Component, add
    // juce_gui_basics to the test target's link libs and bring back
    // juce::ScopedJuceInitialiser_GUI here.
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure(false);   // Print failures, don't trap into a debugger.
    runner.runAllTests();

    int totalFailures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        totalFailures += runner.getResult(i)->failures;

    std::printf("\n=== mu-clid-tests: %d failure(s) across %d test result(s) ===\n",
                totalFailures, runner.getNumResults());
    juce::Logger::setCurrentLogger(nullptr);
    return totalFailures == 0 ? 0 : 1;
}
