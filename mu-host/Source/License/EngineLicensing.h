#pragma once

#include "Sequencer/LayerTypeInfo.h"   // mu-core: mu_core::LayerTypeId

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

} // namespace mu_host
