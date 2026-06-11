# MuLicenseTool — owner-side license keygen + signer (JUCE GUI app).
#
# Replaces tools/keygen.ps1 + tools/gen_license.ps1. Signs with the SAME Monocypher the
# plugin verifies with, and includes mu-core's shared LicenseCanonical.h, so the signed
# payload can never drift from the verifier. Dev tool only — never deployed to testers.
set(MU_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../mu-core")

juce_add_gui_app(MuLicenseTool PRODUCT_NAME "MuLicenseTool")

target_sources(MuLicenseTool PRIVATE
    Source/Main.cpp
    Source/LicenseToolComponent.cpp
    "${CMAKE_SOURCE_DIR}/ThirdParty/monocypher/monocypher.c"
)

target_compile_features(MuLicenseTool PRIVATE cxx_std_17)

target_include_directories(MuLicenseTool PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Source"
    "${MU_CORE_DIR}"                                  # mu_core::buildLicenseCanonical
    "${CMAKE_SOURCE_DIR}/ThirdParty/monocypher"       # <monocypher.h>
)

target_link_libraries(MuLicenseTool PRIVATE
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

target_compile_definitions(MuLicenseTool PRIVATE
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_STANDALONE_APPLICATION=1
)

# Silence MSVC C4244 in vendored monocypher (benign u64->u32 narrowing in its Argon2 path).
if(MSVC)
    set_source_files_properties("${CMAKE_SOURCE_DIR}/ThirdParty/monocypher/monocypher.c"
        PROPERTIES COMPILE_FLAGS "/wd4244")
endif()
