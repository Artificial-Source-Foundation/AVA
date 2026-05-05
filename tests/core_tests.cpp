#include "tests/support/test_harness.h"

#include <array>
#include <iostream>
#include <string_view>

void run_core_mode_tests();
void run_session_tests();
void run_core_json_permission_tests();
void run_app_command_classification_tests();
void run_tools_tests();
void run_config_context_auth_oauth_tests();
void run_app_event_serialization_tests();
void run_app_rpc_tests();
void run_app_runtime_tests();
void run_app_event_bus_tests();
void run_provider_openai_tests();
void run_provider_anthropic_tests();
void run_agent_loop_tests();
void run_agent_tool_dispatcher_tests();
void run_lsp_tests();
void run_plugin_tests();
void run_mcp_tests();
void run_tui_composer_tests();

namespace {

struct TestSuite {
  std::string_view name;
  void (*run)();
};

constexpr std::array kTestSuites{
    TestSuite{"core_mode", run_core_mode_tests},
    TestSuite{"session", run_session_tests},
    TestSuite{"core_json_permission", run_core_json_permission_tests},
    TestSuite{"app_command_classification", run_app_command_classification_tests},
    TestSuite{"tools", run_tools_tests},
    TestSuite{"config_context_auth_oauth", run_config_context_auth_oauth_tests},
    TestSuite{"app_event_serialization", run_app_event_serialization_tests},
    TestSuite{"app_event_bus", run_app_event_bus_tests},
    TestSuite{"app_rpc", run_app_rpc_tests},
    TestSuite{"app_runtime", run_app_runtime_tests},
    TestSuite{"provider_openai", run_provider_openai_tests},
    TestSuite{"provider_anthropic", run_provider_anthropic_tests},
    TestSuite{"agent_loop", run_agent_loop_tests},
    TestSuite{"agent_tool_dispatcher", run_agent_tool_dispatcher_tests},
    TestSuite{"lsp", run_lsp_tests},
    TestSuite{"plugin", run_plugin_tests},
    TestSuite{"mcp", run_mcp_tests},
    TestSuite{"tui_composer", run_tui_composer_tests},
};

void run_suite(TestSuite const& suite)
{
  suite.run();

  int const failures = ava::test::failures();
  if (failures == 0) {
    std::cout << suite.name << " tests passed\n";
  }
}

int print_failures()
{
  int const failures = ava::test::failures();
  if (failures != 0) {
    std::cerr << failures << " test failure(s)\n";
    return 1;
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv)
{
  if (argc > 2) {
    std::cerr << "usage: ava_tests [suite]\n";
    return 2;
  }

  if (argc == 2) {
    std::string_view const requested_suite = argv[1];
    for (auto const& suite : kTestSuites) {
      if (suite.name == requested_suite) {
        run_suite(suite);
        return print_failures();
      }
    }

    std::cerr << "unknown test suite: " << requested_suite << "\n";
    std::cerr << "available test suites:";
    for (auto const& suite : kTestSuites) {
      std::cerr << ' ' << suite.name;
    }
    std::cerr << '\n';
    return 2;
  }

  for (auto const& suite : kTestSuites) {
    suite.run();
  }

  int const failure_status = print_failures();
  if (failure_status == 0) {
    std::cout << "ava tests passed\n";
  }
  return failure_status;
}
