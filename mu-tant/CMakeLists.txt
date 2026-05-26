# mu-tant — μ-family plugin (scaffolding only; not yet wired into the build).
#
# This CMakeLists is a placeholder. When real source lands under Source/, copy
# the juce_add_plugin / target_sources / target_link_libraries pattern from
# mu-clid/CMakeLists.txt, then add `add_subdirectory(mu-tant)` to the family
# root CMakeLists.txt.
#
# The standard mu platform pieces (audio engine slots, mixer, FX, modulation
# matrix, shared UI components, ProcessorBase) come from mu-core via
# target_link_libraries(mu-tant PRIVATE mu-core). Product-specific bits —
# sequencer, voice/engine glue, panels, presets — live under this directory.

message(STATUS "mu-tant: scaffolding only — no targets defined yet")
