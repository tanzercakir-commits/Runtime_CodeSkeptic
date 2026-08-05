# SPDX-License-Identifier: Apache-2.0
if(NOT DEFINED RS_BUILD_DIR OR NOT DEFINED RS_INSTALL_PREFIX)
    message(FATAL_ERROR "RS_BUILD_DIR and RS_INSTALL_PREFIX are required")
endif()
file(REMOVE_RECURSE "${RS_INSTALL_PREFIX}")
set(command "${CMAKE_COMMAND}" --install "${RS_BUILD_DIR}"
    --prefix "${RS_INSTALL_PREFIX}")
if(DEFINED RS_CONFIG AND NOT RS_CONFIG STREQUAL "")
    list(APPEND command --config "${RS_CONFIG}")
endif()
execute_process(COMMAND ${command} RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "cmake --install failed: ${result}")
endif()
foreach(header runtime.h runtime_posix.h runtime_windows.h trace.hpp)
    if(NOT EXISTS "${RS_INSTALL_PREFIX}/include/runtimeskeptic/runtime/${header}")
        message(FATAL_ERROR "installed runtime header missing: ${header}")
    endif()
endforeach()
if(WIN32)
    file(GLOB runtime_libraries
        "${RS_INSTALL_PREFIX}/bin/*runtimeskeptic*.dll"
        "${RS_INSTALL_PREFIX}/lib/*runtimeskeptic*.lib")
else()
    file(GLOB runtime_libraries
        "${RS_INSTALL_PREFIX}/lib/libruntimeskeptic.so*"
        "${RS_INSTALL_PREFIX}/lib/libruntimeskeptic*.dylib")
endif()
if(NOT runtime_libraries)
    message(FATAL_ERROR "installed libruntimeskeptic artifact missing")
endif()
