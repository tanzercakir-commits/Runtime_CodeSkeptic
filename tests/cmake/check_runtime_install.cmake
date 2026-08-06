# SPDX-License-Identifier: Apache-2.0
if(NOT DEFINED RS_BUILD_DIR OR NOT DEFINED RS_INSTALL_PREFIX)
    message(FATAL_ERROR "RS_BUILD_DIR and RS_INSTALL_PREFIX are required")
endif()
file(REMOVE_RECURSE "${RS_INSTALL_PREFIX}")
set(command "${CMAKE_COMMAND}" --install "${RS_BUILD_DIR}"
    --prefix "${RS_INSTALL_PREFIX}" --component RuntimeSDK)
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
file(GLOB trace_libraries
    "${RS_INSTALL_PREFIX}/lib/*rs_trace*"
    "${RS_INSTALL_PREFIX}/lib/*trace.lib")
if(NOT trace_libraries)
    message(FATAL_ERROR "installed trace replay library missing")
endif()
if(NOT EXISTS
   "${RS_INSTALL_PREFIX}/lib/cmake/RuntimeSkeptic/RuntimeSkepticConfig.cmake")
    message(FATAL_ERROR "installed RuntimeSkeptic CMake package missing")
endif()

set(consumer_dir "${RS_INSTALL_PREFIX}-consumer")
file(REMOVE_RECURSE "${consumer_dir}")
file(MAKE_DIRECTORY "${consumer_dir}")
file(WRITE "${consumer_dir}/CMakeLists.txt"
"cmake_minimum_required(VERSION 3.20)\n"
"project(RuntimeSkepticConsumer LANGUAGES C CXX)\n"
"find_package(RuntimeSkeptic CONFIG REQUIRED "
"PATHS \"${RS_INSTALL_PREFIX}/lib/cmake/RuntimeSkeptic\" NO_DEFAULT_PATH)\n"
"add_executable(consumer main.cpp)\n"
"target_link_libraries(consumer PRIVATE "
"RuntimeSkeptic::runtime RuntimeSkeptic::trace)\n")
file(WRITE "${consumer_dir}/main.cpp"
"#include <string>\n"
"#include \"runtimeskeptic/runtime/runtime.h\"\n"
"#include \"runtimeskeptic/runtime/trace.hpp\"\n"
"int main() {\n"
"  rs::runtime::trace::Trace trace;\n"
"  std::string error;\n"
"  auto result = rs::runtime::trace::replay(trace, error);\n"
"  return rs_runtime_is_enabled_v1() + (result ? 0 : 1);\n"
"}\n")
set(configure_command "${CMAKE_COMMAND}" -S "${consumer_dir}"
    -B "${consumer_dir}/build")
if(DEFINED RS_OSX_ARCHITECTURES AND
   NOT RS_OSX_ARCHITECTURES STREQUAL "")
    list(APPEND configure_command
        "-DCMAKE_OSX_ARCHITECTURES=${RS_OSX_ARCHITECTURES}")
endif()
execute_process(COMMAND ${configure_command}
                RESULT_VARIABLE configure_result)
if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR "installed-package consumer configure failed")
endif()
set(build_command "${CMAKE_COMMAND}" --build "${consumer_dir}/build")
if(DEFINED RS_CONFIG AND NOT RS_CONFIG STREQUAL "")
    list(APPEND build_command --config "${RS_CONFIG}")
endif()
execute_process(COMMAND ${build_command} RESULT_VARIABLE build_result)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "installed-package consumer link failed")
endif()
if(WIN32)
    if(DEFINED RS_CONFIG AND NOT RS_CONFIG STREQUAL "")
        set(consumer_executable
            "${consumer_dir}/build/${RS_CONFIG}/consumer.exe")
    else()
        set(consumer_executable "${consumer_dir}/build/consumer.exe")
    endif()
    file(GLOB runtime_dlls "${RS_INSTALL_PREFIX}/bin/*runtimeskeptic*.dll")
    get_filename_component(consumer_directory "${consumer_executable}" DIRECTORY)
    file(COPY ${runtime_dlls} DESTINATION "${consumer_directory}")
else()
    set(consumer_executable "${consumer_dir}/build/consumer")
endif()
execute_process(COMMAND "${consumer_executable}"
                RESULT_VARIABLE consumer_result)
if(NOT consumer_result EQUAL 0)
    message(FATAL_ERROR "installed-package consumer execution failed: ${consumer_result}")
endif()
