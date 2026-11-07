#include "Plugin/PluginProcessor.h"

// The JUCE plugin factory is the one entry point that stays at global scope. It is its own file so a product built on this shell
// (mu-Host) compiles the shell's sources without it and supplies its own.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new mu_tronic::PluginProcessor();
}
