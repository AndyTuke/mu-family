// mu-link test runner - console entry that runs every juce::UnitTest in the link
// graph (each test file defines a `static MyTest x;` at file scope).
//
// Build:  cmake --build build --config Debug --target mu-link-tests
// Run:    build/mu-link/mu-link-tests_artefacts/Debug/mu-link-tests.exe
// Scope:  the portable IPC foundation - lock-free SPSC AudioRing + sample-accurate
//         TransportClock. The audio server, shared-memory mapping, and GUI are not
//         linked here (they arrive in later increments).

#include <juce_core/juce_core.h>
#include <cstdio>
#include <cstring>
#include <iostream>
#include "ShmTestVectors.h"
#include "Link/MuLinkProtocol.h"
#include <cstdlib>   // _putenv_s / setenv
#include <string>

// Every shared-memory name this process uses gets a private suffix, so the tests can
// never collide with a live mu-link bus (or a running product polling to attach). It's
// passed to the --shm-child re-launch through the environment, which children inherit.
static void isolateSharedMemoryNames()
{
    static const char* kEnv = "MU_LINK_TEST_MAP_SUFFIX";
    const auto inherited = juce::SystemStats::getEnvironmentVariable(kEnv, {});
    if (inherited.isNotEmpty())
    {
        mu_link::mapNameSuffix() = inherited.toStdString();
        return;
    }
    const std::string suffix = "_test_" + juce::String::toHexString(juce::Random::getSystemRandom().nextInt64()).toStdString();
    mu_link::mapNameSuffix() = suffix;
#ifdef _WIN32
    _putenv_s(kEnv, suffix.c_str());
#else
    setenv(kEnv, suffix.c_str(), 1);
#endif
}

class StdoutLogger : public juce::Logger
{
public:
    void logMessage(const juce::String& msg) override { std::cout << msg << std::endl; }
};

int main(int argc, char** argv)
{
    isolateSharedMemoryNames();

    // Child mode: the cross-process shared-memory test re-launches this exe with
    // --shm-child to act as a real mu-link client. Do the client work and exit with a
    // status the parent reads - do NOT run the unit-test suite (would recurse).
#ifdef _WIN32
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--shm-child") == 0)
            return mu_link_test::runShmChild();
#endif

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
