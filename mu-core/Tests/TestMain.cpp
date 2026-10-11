// mu-core test runner - console entry that runs every juce::UnitTest registered in the
// link graph (each test file defines a `static MyTest x;` at file scope).
//
// Build:  cmake --build build --config Debug --target mu-core-tests
// Run:    build/mu-core/Tests/mu-core-tests_artefacts/Debug/mu-core-tests.exe
// Scope:  shared mu-core behaviour that needs no product: ProcessorCore (host transport
//         publishing, layer access). No ProcessorBase, mixer, FX or licensing sources are linked.

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

    std::printf("\n=== mu-core-tests: %d failure(s) across %d test result(s) ===\n",
                totalFailures, runner.getNumResults());
    juce::Logger::setCurrentLogger(nullptr);
    return totalFailures == 0 ? 0 : 1;
}
