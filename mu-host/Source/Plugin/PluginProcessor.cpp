#include "PluginProcessor.h"
#include "License/EngineLicensing.h"
#include "License/ProductLicensing.h"   // mu-core: ProcessorBase::initLicensing (licensed products only)

namespace mu_host
{

// No licence of its own (the identity says so); the engine the restored state holds counts at once, before any editor opens.
PluginProcessor::PluginProcessor()
    : mu_tronic::PluginProcessor({ kStateType, kAppName, false })
{
    licenseEngine(getLayerTypeId(0));
}

juce::String PluginProcessor::licenseEngine(mu_core::LayerTypeId type)
{
    const auto* product = productForEngine(type);
    if (product == nullptr) { disableLicensing(); return {}; }

    // Already licensed as this product (another engine of the same product): nothing to redo.
    if (getLicensingProductKey() != product->id)
    {
        // The product's own app keeps its key in its own folder beside this one's; a key dropped in this folder counts as well.
        const auto here = getContentDir();
        const auto productDir = here.getFileName() == kAppName ? here.getSiblingFile(product->appName) : here;
        initLicensing({ product->id, product->licenceFile, product->publicKey, product->activationFile, product->checkoutUrl,
                        product->suggestedPrice, productDir, here });
    }
    return juce::String(juce::CharPointer_UTF8(product->display));
}

} // namespace mu_host

// The JUCE plugin factory is the one entry point that stays at global scope.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new mu_host::PluginProcessor();
}
