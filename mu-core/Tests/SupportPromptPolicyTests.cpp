#include <juce_core/juce_core.h>
#include "License/SupportPromptPolicy.h"

namespace sp = mu_core::support_prompt;

// When the licence reminder appears and who counts as a supporter.
struct SupportPromptPolicyTests : juce::UnitTest
{
    SupportPromptPolicyTests() : juce::UnitTest("Support prompt policy", "mu-core") {}

    void runTest() override
    {
        beginTest("a supporter holds a key (offline or online) in a product that has licensing");
        {
            expect(! sp::isSupporter(true,  false, false));
            expect(  sp::isSupporter(true,  true,  false));
            expect(  sp::isSupporter(true,  false, true));
            expect(  sp::isSupporter(true,  true,  true));
            expect(! sp::isSupporter(false, true,  true));   // no licensing model: never a supporter
        }

        beginTest("the reminder shows for every unlicensed instance until it is dismissed on that instance");
        {
            // hasLicensing, isSupporter, dismissedThisInstance
            expect(  sp::shouldShow(true,  false, false));
            expect(! sp::shouldShow(false, false, false));   // product has no licensing
            expect(! sp::shouldShow(true,  true,  false));   // licensed
            expect(! sp::shouldShow(true,  false, true));    // "Maybe later" on this instance
            expect(  sp::shouldShow(true,  false, false));   // another instance has its own flag and asks again
        }
    }
};

static SupportPromptPolicyTests supportPromptPolicyTests;
