#include "sys.h"
#include "tests/support/test_harness.h"
#include "ava/core/path.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <unistd.h>
#include <string_view>
#include "debug.h"

void run_core_mode_tests();
void run_acp_tests();
void run_session_tests();
void run_session_run_controller_tests();
void run_core_json_permission_tests();
void run_app_command_classification_tests();
void run_app_command_registry_tests();
void run_tools_tests();
void run_config_context_auth_oauth_tests();
void run_command_tests();
void run_app_compaction_tests();
void run_app_print_tests();
void run_app_event_serialization_tests();
void run_app_rpc_queue_tests();
void run_app_rpc_resolver_tests();
void run_app_rpc_tests();
void run_app_runtime_tests();
void run_app_event_bus_tests();
void run_provider_openai_tests();
void run_provider_anthropic_tests();
void run_provider_gemini_tests();
void run_provider_live_smoke_tests();
void run_agent_loop_resilience_tests();
void run_agent_loop_tests();
void run_agent_tool_dispatcher_tests();
void run_tool_scheduler_tests();
void run_lsp_tests();
void run_plugin_tests();
void run_mcp_tests();
void run_permission_rules_tests();
void run_tui_composer_tests();
void run_run_observer_tests();
void run_containment_tests();
#ifdef CWDEBUG
void run_debug_tests();
#endif

namespace {

struct TestSuite
{
  std::string_view name;
  void (*run)();
};

constexpr std::array kTestSuites{
    TestSuite{"core_mode", run_core_mode_tests},
    TestSuite{"acp", run_acp_tests},
    TestSuite{"session", run_session_tests},
    TestSuite{"session_run_controller", run_session_run_controller_tests},
    TestSuite{"core_json_permission", run_core_json_permission_tests},
    TestSuite{"app_command_classification", run_app_command_classification_tests},
    TestSuite{"app_command_registry", run_app_command_registry_tests},
    TestSuite{"tools", run_tools_tests},
    TestSuite{"config_context_auth_oauth", run_config_context_auth_oauth_tests},
    TestSuite{"command", run_command_tests},
    TestSuite{"app_compaction", run_app_compaction_tests},
    TestSuite{"app_print", run_app_print_tests},
    TestSuite{"app_event_serialization", run_app_event_serialization_tests},
    TestSuite{"app_event_bus", run_app_event_bus_tests},
    TestSuite{"app_rpc_queue", run_app_rpc_queue_tests},
    TestSuite{"app_rpc_resolver", run_app_rpc_resolver_tests},
    TestSuite{"app_rpc", run_app_rpc_tests},
    TestSuite{"app_runtime", run_app_runtime_tests},
    TestSuite{"provider_openai", run_provider_openai_tests},
    TestSuite{"provider_anthropic", run_provider_anthropic_tests},
    TestSuite{"provider_gemini", run_provider_gemini_tests},
    TestSuite{"provider_live_smoke", run_provider_live_smoke_tests},
    TestSuite{"agent_loop_resilience", run_agent_loop_resilience_tests},
    TestSuite{"agent_loop", run_agent_loop_tests},
    TestSuite{"agent_tool_dispatcher", run_agent_tool_dispatcher_tests},
    TestSuite{"tool_scheduler", run_tool_scheduler_tests},
    TestSuite{"lsp", run_lsp_tests},
    TestSuite{"plugin", run_plugin_tests},
    TestSuite{"mcp", run_mcp_tests},
    TestSuite{"permission_rules", run_permission_rules_tests},
    TestSuite{"tui_composer", run_tui_composer_tests},
    TestSuite{"run_observer", run_run_observer_tests},
    TestSuite{"containment", run_containment_tests},
#ifdef CWDEBUG
    TestSuite{"debug", run_debug_tests},
#endif
};

void run_suite(TestSuite const& suite)
{
  ava::test::clear_skip();
  suite.run();

  int const failures = ava::test::failures();
  if (failures == 0 && ava::test::skip_requested())
  {
    std::cout << suite.name << " tests skipped: " << ava::test::skip_message() << '\n';
  }
  else if (failures == 0)
  {
    std::cout << suite.name << " tests passed\n";
  }
}

int print_failures()
{
  int const failures = ava::test::failures();
  if (failures != 0)
  {
    std::cerr << failures << " test failure(s)\n";
    return 1;
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv)
{
  Debug(NAMESPACE_DEBUG::init());

  if (argc > 2)
  {
    std::cerr << "usage: ava_tests [suite]\n";
    return 2;
  }

  // CTest may change the working directory without updating $PWD, which
  // would cause logical_cwd() to throw. Verify and fix $PWD to match the
  // actual working directory (falling back to the physical path).
  try
  {
    ava::core::logical_cwd();
  }
  catch (...)
  {
    Dout(dc::warning, "For correct testing, the environment variable PWD should be set to the Working Directory that ctest is using.");
    Dout(dc::warning, "Run `ctest` as follows:");
    Dout(dc::warning, "    export PWD=\"$BUILDDIR/tests\" && ctest --test-dir \"$BUILDDIR\" --output-on-failure test \"$@\"");
    Dout(dc::warning, "where $BUILDDIR is your build directory and \"$@\" stands for any optional arguments that you want to pass.");

    // Use the physical path for now.
    char buffer[4096];
    if (::getcwd(buffer, sizeof(buffer)) != nullptr)
      ::setenv("PWD", buffer, 1);
  }

  if (argc == 2)
  {
    std::string_view const requested_suite = argv[1];
    for (auto const& suite : kTestSuites)
    {
      if (suite.name == requested_suite)
      {
        run_suite(suite);
        if (ava::test::failures() == 0 && ava::test::skip_requested())
          return 77;
        return print_failures();
      }
    }

    std::cerr << "unknown test suite: " << requested_suite << "\n";
    std::cerr << "available test suites:";
    for (auto const& suite : kTestSuites)
    {
      std::cerr << ' ' << suite.name;
    }
    std::cerr << '\n';
    return 2;
  }

  for (auto const& suite : kTestSuites)
  {
    suite.run();
  }

  int const failure_status = print_failures();
  if (failure_status == 0)
  {
    std::cout << "ava tests passed\n";
  }
  return failure_status;
}
