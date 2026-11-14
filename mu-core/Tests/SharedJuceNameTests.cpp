// The JUCE modules are compiled once for every product (mu-juce), so JUCE's accessibility application name cannot come from a
// per-product define: it must follow the name the shell registers (SharedJuceName.h).
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_core/juce_core.h>
#include <juce_gui_basics/detail/juce_AccessibilityHelpers.h>   // internal to JUCE: read only to pin the name

#include "SharedJuceName.h"

namespace
{
    struct SharedJuceNameTests : juce::UnitTest
    {
        SharedJuceNameTests() : juce::UnitTest("Shared JUCE: the accessibility name follows the product", "mu-core") {}

        void runTest() override
        {
            beginTest("the registered display name reaches JUCE's accessibility name (non-ASCII intact)");
            {
                const juce::String name = juce::String(juce::CharPointer_UTF8("\xce\xbc-Test"));
                mu_core::setSharedProductName(name.toRawUTF8());
                expectEquals(juce::detail::AccessibilityHelpers::getApplicationOrPluginName(), name);
            }
        }
    };
    static SharedJuceNameTests sharedJuceNameTests;
}
