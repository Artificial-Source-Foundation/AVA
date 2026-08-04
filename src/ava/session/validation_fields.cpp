#include "sys.h"
#include "ava/session/session_store.h"
#include "ava/session/validation_fields.h"
#include "ava/core/json.h"

#include <cctype>

namespace ava::session {
namespace {

bool is_json_value_delimiter(char ch)
{
  return ch == ',' || ch == '}' || std::isspace(static_cast<unsigned char>(ch)) != 0;
}

}  // namespace

bool supported_entry_version(long long version)
{
  return version == 0 || (version >= 1 && version <= kCurrentSessionEntryVersion);
}

bool bool_field_is_true(std::string_view object, std::string_view key)
{
  auto const start = ava::core::json::field_value_start(object, key);
  if (!start)
    return false;
  auto const end = *start + std::string_view("true").size();
  if (end > object.size() || object.substr(*start, std::string_view("true").size()) != "true")
    return false;
  return end == object.size() || object[end] == ',' || object[end] == '}' || object[end] == ']' || object[end] == ' ' || object[end] == '\t' ||
         object[end] == '\n' || object[end] == '\r';
}

bool bool_field_is_false(std::string_view object, std::string_view key)
{
  auto const start = ava::core::json::field_value_start(object, key);
  if (!start)
    return false;
  auto const end = *start + std::string_view("false").size();
  if (end > object.size() || object.substr(*start, std::string_view("false").size()) != "false")
    return false;
  return end == object.size() || object[end] == ',' || object[end] == '}' || object[end] == ']' || object[end] == ' ' || object[end] == '\t' ||
         object[end] == '\n' || object[end] == '\r';
}

bool valid_status(std::string_view status)
{
  return status == "success" || status == "error" || status == "canceled";
}

bool valid_operation(std::string_view operation)
{
  return operation == "read" || operation == "search" || operation == "edit" || operation == "bash" || operation == "network.fetch" ||
         operation == "network.search" || operation == "lsp.server.launch" || operation == "lsp.query" || operation == "skill" || operation == "task" ||
         operation == "plugin.execute" || operation == "plugin.tool.call" || operation == "plugin.command.run" || operation == "plugin.ui.present" ||
         operation == "plugin.event.observe" || operation == "mcp.server.launch" || operation == "mcp.server.connect" || operation == "mcp.tool.call" ||
         operation == "mcp.resource.read";
}

bool valid_mode(std::string_view mode)
{
  return mode == "build" || mode == "plan";
}

bool valid_action(std::string_view action)
{
  return action == "allow" || action == "ask" || action == "deny";
}

bool valid_resolution(std::string_view resolution)
{
  return resolution == "allow" || resolution == "deny" || resolution == "cancel";
}

bool valid_resolution_source(std::string_view source)
{
  if (source == "policy" || source == "resolver" || source == "session_grant" || source == "no_resolver" || source == "resolver_failed" ||
      source == "persistent_rule" || source == "persistent_rule_error" || source == "client_cancel" || source == "hard_scope" || source == "session_config" ||
      source == "client")
    return true;

  // Legacy persisted ACP-specific aliases remain readable but are not emitted by current writers.
  return source == "acp_hard_policy" || source == "acp_session_mcp" || source == "acp_client" || source == "acp_session_grant" ||
         source == "acp_client_cancel" || source == "acp_client_error";
}

bool valid_risk(std::string_view risk)
{
  return risk == "low" || risk == "medium" || risk == "high" || risk == "critical";
}

bool present_non_empty_string(std::string_view object, std::string_view key)
{
  auto const start = ava::core::json::field_value_start(object, key);
  if (!start)
    return true;
  auto const value = ava::core::json::string_field(object, key);
  return value && !value->empty();
}

bool present_boolean(std::string_view object, std::string_view key)
{
  auto const start = ava::core::json::field_value_start(object, key);
  if (!start)
    return true;
  return bool_field_is_true(object, key) || bool_field_is_false(object, key);
}

bool required_boolean(std::string_view object, std::string_view key)
{
  auto const start = ava::core::json::field_value_start(object, key);
  if (!start)
    return false;
  return bool_field_is_true(object, key) || bool_field_is_false(object, key);
}

bool present_integer_matching(std::string_view object, std::string_view key, bool require_positive)
{
  auto const start = ava::core::json::field_value_start(object, key);
  if (!start)
    return true;
  std::size_t end = *start;
  if (end < object.size() && object[end] == '-')
    ++end;
  auto const digits_start = end;
  while (end < object.size() && std::isdigit(static_cast<unsigned char>(object[end])) != 0) ++end;
  if (end == digits_start)
    return false;
  while (end < object.size() && std::isspace(static_cast<unsigned char>(object[end])) != 0) ++end;
  if (end < object.size() && !is_json_value_delimiter(object[end]))
    return false;
  auto const value = ava::core::json::integer_field(object, key);
  if (!value)
    return false;
  return require_positive ? *value > 0 : *value >= 0;
}

}  // namespace ava::session
