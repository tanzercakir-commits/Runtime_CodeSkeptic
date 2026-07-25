# SPDX-License-Identifier: Apache-2.0

add_executable(rs-env-probe rs-env-probe/main.cpp)
target_link_libraries(rs-env-probe PRIVATE rs_probe rs_reports rs_warnings)

add_executable(rs-check rs-check/main.cpp)
target_link_libraries(rs-check PRIVATE rs_reports rs_warnings)

add_executable(rs-profile rs-profile/main.cpp)
target_link_libraries(rs-profile PRIVATE rs_reports rs_warnings)

add_executable(rs-mcp rs-mcp/main.cpp)
target_link_libraries(rs-mcp PRIVATE rs_server rs_warnings)

install(TARGETS rs-env-probe rs-check rs-profile rs-mcp RUNTIME DESTINATION bin)
