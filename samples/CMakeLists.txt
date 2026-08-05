# SPDX-License-Identifier: Apache-2.0

if(WIN32)
    add_executable(rs-runtime-sample runtime_monitor_windows.c)
else()
    add_executable(rs-runtime-sample runtime_monitor_posix.c)
endif()
target_link_libraries(rs-runtime-sample PRIVATE runtimeskeptic rs_warnings)
install(TARGETS rs-runtime-sample RUNTIME DESTINATION bin)
