# mu-control - controller drivers, surface mapping and surface model for the mu-family.
#
# An INTERFACE library exactly like mu-core: the sources are compiled in the consuming target, where the JUCE
# modules are already set up. It uses mu-core headers and nothing else of the family; mu-core never includes it
# (tests/scripts/check-core-boundary.ps1). The library and its tests are built in every configuration; the option
# MUFAMILY_WITH_CONTROL only decides whether the standalone apps link it (mu_control_attach below).

add_library(mu-control INTERFACE)
target_compile_features(mu-control INTERFACE cxx_std_17)

target_sources(mu-control INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/Surface/SurfaceMapping.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Surface/SurfaceModel.cpp"
)

target_include_directories(mu-control INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")

# Links mu-control into a product's STANDALONE target only (never the plugin's shared-code target, so VST3 and
# CLAP carry none of it). A no-op unless MUFAMILY_WITH_CONTROL is ON.
function(mu_control_attach target)
    if(MUFAMILY_WITH_CONTROL)
        target_link_libraries(${target} PRIVATE mu-control)
    endif()
endfunction()

add_subdirectory(Tests)
