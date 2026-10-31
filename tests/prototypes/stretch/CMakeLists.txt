# Signalsmith Stretch prototype for mu-Tilate: a standalone console program, NOT part of the product build.
#   cmake -S tests/prototypes/stretch -B build/stretch-proto
#   cmake --build build/stretch-proto --config Release
#   build/stretch-proto/Release/stretch_prototype.exe [outputBlock=256] [splitComputation=0]
cmake_minimum_required(VERSION 3.24)
project(stretch_prototype CXX)

set(MU_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
add_executable(stretch_prototype stretch_prototype.cpp)
target_compile_features(stretch_prototype PRIVATE cxx_std_17)
target_include_directories(stretch_prototype PRIVATE
    "${MU_ROOT}/ThirdParty/signalsmith-stretch"
    "${MU_ROOT}/ThirdParty/signalsmith-linear/include")
if(MSVC)
    target_compile_options(stretch_prototype PRIVATE /bigobj /EHsc)
endif()
