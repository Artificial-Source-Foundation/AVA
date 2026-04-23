add_library(ava_types STATIC
  types/build_info.cpp
  types/message.cpp
  types/thinking.cpp
  types/attachment.cpp
)
target_include_directories(ava_types
  PUBLIC
    "${AVA_CPP_INCLUDE_DIR}"
  PRIVATE
    "${AVA_GENERATED_INCLUDE_DIR}"
)
target_link_libraries(ava_types
  PUBLIC
    nlohmann_json::nlohmann_json
)
ava_apply_warnings(ava_types)
ava_enable_sanitizers(ava_types)

add_library(ava_control_plane STATIC
  control_plane/commands.cpp
  control_plane/events.cpp
)
target_include_directories(ava_control_plane
  PUBLIC
    "${AVA_CPP_INCLUDE_DIR}"
)
target_link_libraries(ava_control_plane
  PUBLIC
    ava_types
)
ava_apply_warnings(ava_control_plane)
ava_enable_sanitizers(ava_control_plane)

add_library(ava_platform STATIC
  platform/filesystem.cpp
  platform/platform.cpp
)
target_include_directories(ava_platform
  PUBLIC
    "${AVA_CPP_INCLUDE_DIR}"
)
ava_apply_warnings(ava_platform)
ava_enable_sanitizers(ava_platform)

add_library(ava_config STATIC
  config/agents.cpp
  config/config.cpp
  config/paths.cpp
  config/trust.cpp
  config/credentials.cpp
  config/model_spec.cpp
  config/model_registry.cpp
)
target_include_directories(ava_config
  PUBLIC
    "${AVA_CPP_INCLUDE_DIR}"
)
target_link_libraries(ava_config
  PUBLIC
    ava_platform
    nlohmann_json::nlohmann_json
)
ava_apply_warnings(ava_config)
ava_enable_sanitizers(ava_config)

add_library(ava_session STATIC
  session/session.cpp
)
target_include_directories(ava_session
  PUBLIC
    "${AVA_CPP_INCLUDE_DIR}"
)
target_link_libraries(ava_session
  PUBLIC
    ava_types
    nlohmann_json::nlohmann_json
    SQLite::SQLite3
)
ava_apply_warnings(ava_session)
ava_enable_sanitizers(ava_session)

add_library(ava_llm STATIC
  llm/provider.cpp
  llm/message_transform.cpp
  llm/retry.cpp
  llm/circuit_breaker.cpp
  llm/pricing.cpp
  llm/shared_provider.cpp
  llm/factory.cpp
  llm/providers/mock_provider.cpp
  llm/providers/openai_protocol.cpp
  llm/providers/openai_provider.cpp
)
target_include_directories(ava_llm
  PUBLIC
    "${AVA_CPP_INCLUDE_DIR}"
)
target_link_libraries(ava_llm
  PUBLIC
    ava_types
    ava_config
    nlohmann_json::nlohmann_json
    ava_optional_cpr
)
ava_apply_warnings(ava_llm)
ava_enable_sanitizers(ava_llm)

add_library(ava_tools STATIC
  tools/registry.cpp
  tools/retry.cpp
  tools/permission_middleware.cpp
  tools/path_guard.cpp
  tools/file_backup.cpp
  tools/output_fallback.cpp
  tools/core_tools.cpp
)
target_include_directories(ava_tools
  PUBLIC
    "${AVA_CPP_INCLUDE_DIR}"
)
target_link_libraries(ava_tools
  PUBLIC
    ava_types
    ava_platform
    nlohmann_json::nlohmann_json
)
ava_apply_warnings(ava_tools)
ava_enable_sanitizers(ava_tools)

add_library(ava_agent STATIC
  agent/message_queue.cpp
  agent/stuck_detector.cpp
  agent/response.cpp
  agent/runtime.cpp
)
target_include_directories(ava_agent
  PUBLIC
    "${AVA_CPP_INCLUDE_DIR}"
)
target_link_libraries(ava_agent
  PUBLIC
    ava_types
    ava_config
    ava_session
    ava_llm
    ava_tools
    nlohmann_json::nlohmann_json
)
ava_apply_warnings(ava_agent)
ava_enable_sanitizers(ava_agent)

add_library(ava_orchestration STATIC
  orchestration/subagents.cpp
)
target_include_directories(ava_orchestration
  PUBLIC
    "${AVA_CPP_INCLUDE_DIR}"
)
target_link_libraries(ava_orchestration
  PUBLIC
    ava_types
    ava_config
    ava_tools
  PRIVATE
)
ava_apply_warnings(ava_orchestration)
ava_enable_sanitizers(ava_orchestration)

# Thin composition target so app targets don't assemble leaves directly.
add_library(ava_runtime INTERFACE)
target_include_directories(ava_runtime
  INTERFACE
    "${AVA_CPP_INCLUDE_DIR}"
    "${AVA_GENERATED_INCLUDE_DIR}"
)
target_link_libraries(ava_runtime
  INTERFACE
    ava_types
    ava_control_plane
    ava_platform
    ava_config
    ava_session
    ava_llm
    ava_tools
    ava_agent
    ava_orchestration
    ava_optional_ftxui
    ava_optional_cpr
)
