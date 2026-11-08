// mu-tant test runner - console entry that runs every juce::UnitTest registered
// in the link graph (each test file defines a `static MyTest x;` at file scope).
//
// Build:  cmake --build build --config Debug --target mu-tant-tests
// Run:    build/mu-tant/mu-tant-tests_artefacts/Debug/mu-tant-tests.exe
// Scope:  synth DSP + modulator/gate/insert/persist coverage, plus a shared
//         global-FX APVTS layout test (mu_mixfx::addGlobalFxParams behind a
//         headless AudioProcessor stub), and the real PluginProcessor built headless
//         (createEditor lives in PluginEditor.cpp, which is not linked).

#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <cstdio>
#include <iostream>

#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#include <cstdlib>

// A Debug assertion (a vector index out of range, a failed _ASSERT) would open a modal dialog and stall an unattended run:
// print the message and the stack instead, then exit with a failure.
static int assertToStderr(int reportType, char* message, int*)
{
    if (reportType != _CRT_ASSERT) return 0;
    std::fprintf(stderr, "CRT ASSERT: %s\n%s\n", message, juce::SystemStats::getStackBacktrace().toRawUTF8());
    std::fflush(stderr);
    std::_Exit(3);
}
#endif

class StdoutLogger : public juce::Logger
{
public:
    void logMessage(const juce::String& msg) override { std::cout << msg << std::endl; }
};

int main(int argc, char** argv)
{
    // ProcessorBase starts a message-thread poll timer, which needs a message manager.
    juce::ScopedJuceInitialiser_GUI juceInit;
#if defined(_MSC_VER) && defined(_DEBUG)
    _CrtSetReportHook(assertToStderr);
#endif

    StdoutLogger logger;
    juce::Logger::setCurrentLogger(&logger);

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure(false);
    if (argc > 1) runner.runTestsInCategory(argv[1]);   // optional: run only one category
    else          runner.runAllTests();

    int totalFailures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        totalFailures += runner.getResult(i)->failures;

    std::printf("\n=== mu-tant-tests: %d failure(s) across %d test result(s) ===\n",
                totalFailures, runner.getNumResults());
    juce::Logger::setCurrentLogger(nullptr);
    return totalFailures == 0 ? 0 : 1;
}
