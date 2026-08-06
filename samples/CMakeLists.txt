# SPDX-License-Identifier: Apache-2.0

if(WIN32)
    add_executable(rs-runtime-sample runtime_monitor_windows.c)
else()
    add_executable(rs-runtime-sample runtime_monitor_posix.c)
endif()
target_link_libraries(rs-runtime-sample PRIVATE runtimeskeptic rs_warnings)
if(APPLE)
    set_target_properties(rs-runtime-sample PROPERTIES
        BUILD_RPATH "@loader_path/../src"
        BUILD_WITH_INSTALL_RPATH YES
        INSTALL_RPATH "@loader_path/../lib")
elseif(UNIX)
    set_target_properties(rs-runtime-sample PROPERTIES
        BUILD_RPATH "$ORIGIN/../src"
        BUILD_WITH_INSTALL_RPATH YES
        INSTALL_RPATH "$ORIGIN/../lib")
endif()
install(TARGETS rs-runtime-sample
        RUNTIME DESTINATION bin
        COMPONENT RuntimeSDK)
