#include "License/EngineLicensing.h"

// Each product keeps its licence identity in its own License/LicenseKey.h (the same file name in every product), so they are reached
// from the repository root rather than through the include path.
#include "mu-clid/Source/License/LicenseKey.h"
#include "mu-tant/Source/License/LicenseKey.h"
#include "mu-toni/Source/License/LicenseKey.h"
#include "mu-on/Source/License/LicenseKey.h"
#include "mu-tilate/Source/License/LicenseKey.h"

namespace mu_host
{

const EngineProduct* productForEngine(mu_core::LayerTypeId type)
{
    static const EngineProduct clid   { "\xce\xbc-Clid",   "muClid",   mu_clid::kLicenseProductId,   mu_clid::kLicenseFilename,   mu_clid::kLicensePublicKey,   "muclid.activation",   mu_clid::kCheckoutUrl,   20 };
    static const EngineProduct tant   { "\xce\xbc-Tant",   "muTant",   mu_tant::kLicenseProductId,   mu_tant::kLicenseFilename,   mu_tant::kLicensePublicKey,   "mutant.activation",   mu_tant::kCheckoutUrl,   20 };
    static const EngineProduct toni   { "\xce\xbc-Toni",   "muToni",   mu_toni::kLicenseProductId,   mu_toni::kLicenseFilename,   mu_toni::kLicensePublicKey,   "mutoni.activation",   mu_toni::kCheckoutUrl,   20 };
    static const EngineProduct on     { "\xce\xbc-On",     "muOn",     mu_on::kLicenseProductId,     mu_on::kLicenseFilename,     mu_on::kLicensePublicKey,     "muon.activation",     mu_on::kCheckoutUrl,     20 };
    static const EngineProduct tilate { "\xce\xbc-Tilate", "muTilate", mu_tilate::kLicenseProductId, mu_tilate::kLicenseFilename, mu_tilate::kLicensePublicKey, "mutilate.activation", mu_tilate::kCheckoutUrl, mu_tilate::kSuggestedPrice };

    using T = mu_core::LayerTypeId;
    switch (type)
    {
        case T::Rhythm:  return &clid;
        case T::Pattern: return &tant;
        case T::Arp:     return &toni;
        case T::Kick: case T::Bass: case T::Hat: case T::Snare: case T::Rumble: case T::FmPerc: case T::Chord:
                         return &on;
        case T::Phrase:  return &tilate;
        default:         return nullptr;
    }
}

} // namespace mu_host
