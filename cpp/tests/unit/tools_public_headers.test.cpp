#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <type_traits>

#include "ava/tools/mcp_bridge.hpp"
#include "ava/tools/tools.hpp"
#include "ava/tools/web_tools.hpp"

TEST_CASE("tools public headers expose expected surfaces", "[ava_tools][headers]") {
  static_assert(std::is_class_v<ava::tools::ToolRegistry>);
  static_assert(std::is_class_v<ava::tools::ReadTool>);
  static_assert(std::is_class_v<ava::tools::WriteTool>);
  static_assert(std::is_class_v<ava::tools::EditTool>);
  static_assert(std::is_class_v<ava::tools::BashTool>);
  static_assert(std::is_class_v<ava::tools::GlobTool>);
  static_assert(std::is_class_v<ava::tools::GrepTool>);
  static_assert(std::is_class_v<ava::tools::GitReadTool>);
  static_assert(std::is_class_v<ava::tools::GitReadAliasTool>);
  static_assert(std::is_class_v<ava::tools::TodoWriteTool>);
  static_assert(std::is_class_v<ava::tools::TodoReadTool>);
  static_assert(std::is_class_v<ava::tools::ToolSearchTool>);

  static_assert(std::is_class_v<ava::tools::WebFetchTool>);
  static_assert(std::is_class_v<ava::tools::WebSearchTool>);
  static_assert(std::is_class_v<ava::tools::McpBridgeTool>);

  using RegistrationFn = ava::tools::DefaultToolRegistration (*)(
      ava::tools::ToolRegistry&,
      const std::filesystem::path&
  );
  const RegistrationFn register_fn = &ava::tools::register_default_tools;
  REQUIRE(register_fn != nullptr);
}
