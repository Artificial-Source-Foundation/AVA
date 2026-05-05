#include <string>

#include "ava/core/error.h"
#include "ava/mcp/config.h"
#include "ava/mcp/tool_list_parser.h"
#include "tests/support/test_harness.h"

namespace {

ava::mcp::McpServerConfig server_config()
{
  return ava::mcp::McpServerConfig{.id = "test_server",
                                   .name = "Test Server",
                                   .command = "test-command",
                                   .args = {},
                                   .enabled = true,
                                   .scope = ava::mcp::McpServerScope::Global,
                                   .source_path = {}};
}

bool has_context(ava::core::Error const& error, std::string const& key, std::string const& value)
{
  for (auto const& context : error.context()) {
    if (context.key == key && context.value == value) return true;
  }
  return false;
}

void test_valid_tool_descriptions()
{
  auto const server = server_config();
  auto parsed = ava::mcp::parse_mcp_tool_list_result(
      R"({"tools":[)"
      R"({"name":"read_file","description":"Read a file","inputSchema":{"type":"object","properties":{"path":{"type":"string"}}}},)"
      R"({"name":"search","description":"Search files","inputSchema":{"type":"object"}})"
      R"(]})",
      R"({"jsonrpc":"2.0","id":"1","result":{"tools":[]}})", server);

  expect(parsed.has_value(), "valid MCP tools/list result parses");
  expect(parsed && parsed->size() == 2, "valid MCP tools/list result returns all tool descriptions");
  if (parsed && parsed->size() == 2) {
    expect((*parsed)[0].name == "read_file" && (*parsed)[0].description == "Read a file",
           "MCP tool parser preserves name and description");
    expect((*parsed)[0].input_schema_json.contains("\"properties\""),
           "MCP tool parser preserves explicit input schema");
    expect((*parsed)[1].name == "search" && (*parsed)[1].input_schema_json == R"({"type":"object"})",
           "MCP tool parser preserves simple input schema");
  }
}

void test_missing_tools_returns_empty_list()
{
  auto const parsed =
      ava::mcp::parse_mcp_tool_list_result(R"({"meta":{}})", R"({"result":{"meta":{}}})", server_config());
  expect(parsed && parsed->empty(), "missing MCP tools field returns an empty tool list");
}

void test_invalid_tools_field_errors()
{
  auto const parsed =
      ava::mcp::parse_mcp_tool_list_result(R"({"tools":{}})", R"({"result":{"tools":{}}})", server_config());
  expect(!parsed.has_value(), "non-array MCP tools field is rejected");
  expect(!parsed && parsed.error().category() == ava::core::ErrorCategory::Tool,
         "non-array MCP tools field uses tool error category");
  expect(!parsed && has_context(parsed.error(), "mcp_server", "test_server"),
         "non-array MCP tools error carries server context");
}

void test_invalid_tool_name_errors()
{
  auto const parsed = ava::mcp::parse_mcp_tool_list_result(R"({"tools":[{"name":""}]})",
                                                           R"({"result":{"tools":[{"name":""}]}})", server_config());
  expect(!parsed.has_value(), "empty MCP tool name is rejected");
  expect(!parsed && parsed.error().message() == "MCP tool has invalid name",
         "empty MCP tool name reports stable parser error");
}

void test_missing_input_schema_defaults_to_object()
{
  auto const parsed = ava::mcp::parse_mcp_tool_list_result(R"({"tools":[{"name":"default_schema"}]})",
                                                           R"({"result":{"tools":[]}})", server_config());
  expect(parsed && parsed->size() == 1, "MCP tool without input schema parses");
  expect(parsed && parsed->front().input_schema_json == R"({"type":"object"})",
         "MCP tool without input schema defaults to object schema");
}

void test_invalid_input_schema_errors()
{
  auto const parsed = ava::mcp::parse_mcp_tool_list_result(R"({"tools":[{"name":"bad_schema","inputSchema":true}]})",
                                                           R"({"result":{"tools":[]}})", server_config());
  expect(!parsed.has_value(), "non-object MCP input schema is rejected");
  expect(!parsed && parsed.error().message() == "MCP tool inputSchema is invalid",
         "non-object MCP input schema reports stable parser error");
  expect(!parsed && has_context(parsed.error(), "tool", "bad_schema"),
         "non-object MCP input schema error names the tool");
}

}  // namespace

void run_mcp_tool_list_parser_tests()
{
  test_valid_tool_descriptions();
  test_missing_tools_returns_empty_list();
  test_invalid_tools_field_errors();
  test_invalid_tool_name_errors();
  test_missing_input_schema_defaults_to_object();
  test_invalid_input_schema_errors();
}
