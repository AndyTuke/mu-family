#include "License/SupportPromptPolicy.h"

namespace sp = mu_core::support_prompt;

// When the polite "support mu" prompt appears, who counts as a supporter, and the saved
// "Don't show again" flag.
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

        beginTest("the prompt shows only for an unlicensed, unhidden, not-yet-shown licensing product");
        {
            // hasLicensing, isSupporter, hiddenForever, alreadyShown
            expect(  sp::shouldShow(true,  false, false, false));
            expect(! sp::shouldShow(false, false, false, false));   // product has no licensing
            expect(! sp::shouldShow(true,  true,  false, false));   // supporter
            expect(! sp::shouldShow(true,  false, true,  false));   // "Don't show again"
            expect(! sp::shouldShow(true,  false, false, true));    // already shown this process
        }

        beginTest("once per process: markShown suppresses the prompt until reset");
        {
            sp::resetShownThisProcessForTesting();
            expect(sp::shouldShowNow(true, false, false));
            sp::markShown();
            expect(! sp::shouldShowNow(true, false, false));
            sp::resetShownThisProcessForTesting();
            expect(sp::shouldShowNow(true, false, false));
        }

        beginTest("the hidden flag round-trips through a settings file and defaults to not hidden");
        {
            juce::TemporaryFile tmp(".settings");
            juce::PropertiesFile::Options opts;
            opts.filenameSuffix = "";
            {
                juce::PropertiesFile file(tmp.getFile(), opts);
                expect(! sp::isHidden(&file));
                sp::setHidden(&file, true);
                expect(sp::isHidden(&file));
            }
            {
                juce::PropertiesFile reopened(tmp.getFile(), opts);
                expect(sp::isHidden(&reopened));   // survived a save and reload
                sp::setHidden(&reopened, false);
                expect(! sp::isHidden(&reopened));
            }
            expect(! sp::isHidden(nullptr));
            sp::setHidden(nullptr, true);   // a missing settings file is ignored, not a crash
        }

        sp::resetShownThisProcessForTesting();
    }
};

static SupportPromptPolicyTests supportPromptPolicyTests;
