#include <iostream>

#include "tests/support/test_harness.h"

void run_core_mode_tests();
void run_session_tests();
void run_core_json_permission_tests();
void run_app_command_classification_tests();
void run_tools_tests();
void run_config_context_auth_oauth_tests();
void run_app_event_serialization_tests();
void run_app_runtime_tests();
void run_app_event_bus_tests();
void run_provider_openai_tests();
void run_agent_tool_dispatcher_tests();
void run_tui_composer_tests();

int main() {
  run_core_mode_tests();
  run_session_tests();
  run_core_json_permission_tests();
  run_app_command_classification_tests();
  run_tools_tests();
  run_config_context_auth_oauth_tests();
  run_app_event_serialization_tests();
  run_app_event_bus_tests();
  run_app_runtime_tests();
  run_provider_openai_tests();
  run_agent_tool_dispatcher_tests();
  run_tui_composer_tests();

  const int failures = ava::test::failures();
  if (failures != 0) {
    std::cerr << failures << " test failure(s)\n";
    return 1;
  }
  std::cout << "core tests passed\n";
  return 0;
}
