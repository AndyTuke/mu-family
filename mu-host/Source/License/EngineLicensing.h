#pragma once

#include "Sequencer/LayerTypeInfo.h"   // mu-core: mu_core::LayerTypeId

#include <juce_core/juce_core.h>

#include <cstdint>

// mu-Host has no licence of its own: an engine is licensed through the product it belongs to, and a product's key covers all its
// engines. This table says which product each engine belongs to and holds that product's licence identity (the same values the
// product's own app passes to initLicensing).
namespace mu_host
{

struct EngineProduct
{
    const char*    display;      // UTF-8, with the Greek mu: what the support prompt calls the product
    const char*    appName;      // the product's own content folder under Documents/TDP (where its key and activation live)
    const char*    id;           // the .lic's product= field
    const char*    licenceFile;
    const uint8_t* publicKey;
    const char*    activationFile;
    const char*    checkoutUrl;
    int            suggestedPrice;
};

// The product `type` belongs to, or null for an Empty slot and any type no product claims.
const EngineProduct* productForEngine(mu_core::LayerTypeId type);

// Does the user hold a valid key for `product`: a signed .lic or a saved online activation in the product's own content folder
// (a sibling of `hostDir`, mu-Host's own folder) or in mu-Host's? Reads files only, so it can be asked of every product without
// opening an engine. Message thread.
bool isProductLicensed(const EngineProduct& product, const juce::File& hostDir, const char* hostAppName);

// True when the engine `type` belongs to a product the user holds a key for. An Empty slot and a type no product claims are not.
bool isEngineLicensed(mu_core::LayerTypeId type, const juce::File& hostDir, const char* hostAppName);

} // namespace mu_host
