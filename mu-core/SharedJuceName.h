#pragma once

#ifdef __cplusplus   // force-included into the C sources of JUCE too
#include <string>

// The one JUCE setting that changes compiled code and is per product: JUCE reads the plug-in's name (JucePlugin_Name) to name
// the accessibility application. The JUCE modules are compiled once for every product (mu-juce), so there is no per-product
// define; mu-juce defines JucePlugin_Name as this function's result instead (the header is force-included there), and each
// product's shell registers its display name. Header-only so a test app that compiles the shell needs nothing extra; the inline
// storage is one object per program.
namespace mu_core
{
    inline std::string& sharedProductNameStorage() { static std::string name = "mu"; return name; }   // message thread only

    // The product's display name as UTF-8 (copied).
    inline void setSharedProductName(const char* utf8Name)
    {
        if (utf8Name != nullptr && *utf8Name != 0) sharedProductNameStorage() = utf8Name;
    }
}

// UTF-8, valid until the next setSharedProductName.
inline const char* mu_core_shared_product_name() noexcept { return mu_core::sharedProductNameStorage().c_str(); }
#endif
