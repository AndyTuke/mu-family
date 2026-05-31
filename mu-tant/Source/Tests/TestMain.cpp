// mu-tant test runner — console entry that runs every juce::UnitTest registered
// in the link graph (each test file defines a `static MyTest x;` at file scope).
//
// Build:  cmake --build build --config Debug --target mu-tant-tests
// Run:    build/mu-tant/mu-tant-tests_artefacts/Debug/mu-tant-tests.exe
// Scope:  synth DSP + modulator/gate/insert/persist coverage, plus a shared
//         global-FX APVTS layout test (mu_mixfx::addGlobalFxParams behind a
//         headless AudioProcessor stub). The full mu-tant PluginProcessor is not
//         linked — its createEditor() drags in the editor/UI tree (see #721).

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

    std::printf("\n=== mu-tant-tests: %d failure(s) across %d test result(s) ===\n",
                totalFailures, runner.getNumResults());
    juce::Logger::setCurrentLogger(nullptr);
    return totalFailures == 0 ? 0 : 1;
}
