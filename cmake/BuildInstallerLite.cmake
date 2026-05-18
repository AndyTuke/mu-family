# Builds mu-Clid-Lite-Setup-v1.0.<N>.exe using Inno Setup.
# Called via: cmake -P BuildInstallerLite.cmake

file(READ "${SOURCE_DIR}/build_number.txt" BUILD_NUM)
string(STRIP "${BUILD_NUM}" BUILD_NUM)

execute_process(
    COMMAND "${ISCC_EXE}"
            "/DBuildNum=${BUILD_NUM}"
            "${SOURCE_DIR}/installer/mu-Clid-Lite.iss"
    RESULT_VARIABLE ISCC_RESULT
)

if(NOT ISCC_RESULT EQUAL 0)
    message(FATAL_ERROR "Inno Setup failed with code ${ISCC_RESULT}")
endif()

message(STATUS "μ-Clid Lite installer built (build ${BUILD_NUM})")
