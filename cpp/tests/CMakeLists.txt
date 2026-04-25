# Backward-compatible M3 aggregate kept for docs/scripts that still invoke
# `ava_cpp_unit`; `ava_m3_foundation` is the canonical milestone gate.
set(AVA_M3_FOUNDATION_TEST_SOURCES
  unit/control_plane_contracts.test.cpp
  unit/platform_filesystem.test.cpp
  unit/types_smoke.test.cpp
)

add_executable(ava_cpp_tests
  ${AVA_M3_FOUNDATION_TEST_SOURCES}
)
target_link_libraries(ava_cpp_tests
  PRIVATE
    ava_types
    ava_control_plane
    ava_platform
    Catch2::Catch2WithMain
)
ava_apply_toolchain_hygiene(ava_cpp_tests)
ava_apply_warnings(ava_cpp_tests)
ava_enable_sanitizers(ava_cpp_tests)

add_test(NAME ava_cpp_unit COMMAND ava_cpp_tests)

add_executable(ava_m3_foundation_tests
  ${AVA_M3_FOUNDATION_TEST_SOURCES}
)
target_link_libraries(ava_m3_foundation_tests
  PRIVATE
    ava_types
    ava_control_plane
    ava_platform
    Catch2::Catch2WithMain
)
ava_apply_toolchain_hygiene(ava_m3_foundation_tests)
ava_apply_warnings(ava_m3_foundation_tests)
ava_enable_sanitizers(ava_m3_foundation_tests)

add_test(NAME ava_m3_foundation COMMAND ava_m3_foundation_tests)

add_executable(ava_config_tests
  unit/config_foundation.test.cpp
)
target_link_libraries(ava_config_tests
  PRIVATE
    ava_config
    Catch2::Catch2WithMain
)
ava_apply_toolchain_hygiene(ava_config_tests)
ava_apply_warnings(ava_config_tests)
ava_enable_sanitizers(ava_config_tests)

add_test(NAME ava_config_unit COMMAND ava_config_tests)

add_executable(ava_session_tests
  unit/session_foundation.test.cpp
)
target_link_libraries(ava_session_tests
  PRIVATE
    ava_session
    Catch2::Catch2WithMain
)
ava_apply_toolchain_hygiene(ava_session_tests)
ava_apply_warnings(ava_session_tests)
ava_enable_sanitizers(ava_session_tests)

add_test(NAME ava_session_unit COMMAND ava_session_tests)

add_executable(ava_tools_tests
  unit/tools_core_tools.test.cpp
  unit/tools_registry.test.cpp
)
target_link_libraries(ava_tools_tests
  PRIVATE
    ava_tools
    Catch2::Catch2WithMain
)
ava_apply_toolchain_hygiene(ava_tools_tests)
ava_apply_warnings(ava_tools_tests)
ava_enable_sanitizers(ava_tools_tests)

add_test(NAME ava_tools_unit COMMAND ava_tools_tests)

add_executable(ava_mock_mcp_server
  helpers/mock_mcp_stdio_server.cpp
)
target_link_libraries(ava_mock_mcp_server
  PRIVATE
    nlohmann_json::nlohmann_json
)
ava_apply_toolchain_hygiene(ava_mock_mcp_server)
ava_apply_warnings(ava_mock_mcp_server)
ava_enable_sanitizers(ava_mock_mcp_server)

add_executable(ava_mcp_tests
  unit/mcp_foundation.test.cpp
)
target_link_libraries(ava_mcp_tests
  PRIVATE
    ava_mcp
    ava_config
    Catch2::Catch2WithMain
)
ava_apply_toolchain_hygiene(ava_mcp_tests)
ava_apply_warnings(ava_mcp_tests)
ava_enable_sanitizers(ava_mcp_tests)
target_compile_definitions(ava_mcp_tests
  PRIVATE
    AVA_MOCK_MCP_SERVER_PATH="$<TARGET_FILE:ava_mock_mcp_server>"
)
add_dependencies(ava_mcp_tests ava_mock_mcp_server)

add_test(NAME ava_mcp_unit COMMAND ava_mcp_tests)

add_executable(ava_llm_tests
  unit/llm_foundation.test.cpp
)
target_link_libraries(ava_llm_tests
  PRIVATE
    ava_llm
    Catch2::Catch2WithMain
)
ava_apply_toolchain_hygiene(ava_llm_tests)
ava_apply_warnings(ava_llm_tests)
ava_enable_sanitizers(ava_llm_tests)

add_test(NAME ava_llm_unit COMMAND ava_llm_tests)

add_executable(ava_agent_tests
  unit/agent_runtime.test.cpp
)
target_link_libraries(ava_agent_tests
  PRIVATE
    ava_agent
    Catch2::Catch2WithMain
)
ava_apply_toolchain_hygiene(ava_agent_tests)
ava_apply_warnings(ava_agent_tests)
ava_enable_sanitizers(ava_agent_tests)

add_test(NAME ava_agent_unit COMMAND ava_agent_tests)

add_executable(ava_orchestration_tests
  unit/orchestration_foundation.test.cpp
)
target_link_libraries(ava_orchestration_tests
  PRIVATE
    ava_orchestration
    Catch2::Catch2WithMain
)
ava_apply_toolchain_hygiene(ava_orchestration_tests)
ava_apply_warnings(ava_orchestration_tests)
ava_enable_sanitizers(ava_orchestration_tests)

add_test(NAME ava_orchestration_unit COMMAND ava_orchestration_tests)

add_executable(ava_app_tests
  unit/ava_cli_headless.test.cpp
)
target_link_libraries(ava_app_tests
  PRIVATE
    ava_headless_app
    ava_runtime
    Catch2::Catch2WithMain
)
ava_apply_toolchain_hygiene(ava_app_tests)
ava_apply_warnings(ava_app_tests)
ava_enable_sanitizers(ava_app_tests)

add_test(NAME ava_app_unit COMMAND ava_app_tests)

add_executable(ava_tui_tests
  unit/ava_tui_state.test.cpp
)
target_link_libraries(ava_tui_tests
  PRIVATE
    ava_tui_app
    ava_runtime
    Catch2::Catch2WithMain
)
ava_apply_toolchain_hygiene(ava_tui_tests)
ava_apply_warnings(ava_tui_tests)
ava_enable_sanitizers(ava_tui_tests)

add_test(NAME ava_tui_unit COMMAND ava_tui_tests)

add_executable(ava_app_integration_tests
  integration/headless_runtime_m10.test.cpp
)
target_link_libraries(ava_app_integration_tests
  PRIVATE
    ava_headless_app
    ava_runtime
    Catch2::Catch2WithMain
)
ava_apply_toolchain_hygiene(ava_app_integration_tests)
ava_apply_warnings(ava_app_integration_tests)
ava_enable_sanitizers(ava_app_integration_tests)

add_test(NAME ava_app_integration COMMAND ava_app_integration_tests)
