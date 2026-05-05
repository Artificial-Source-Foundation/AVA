#include "ava/app/rpc/protocol.h"

#include "ava/core/json.h"

#include <cctype>
#include <istream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ava::app::rpc {
namespace {

std::string_view trim(std::string_view value)
{
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) value.remove_prefix(1);
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) value.remove_suffix(1);
  return value;
}

bool is_json_object_line(std::string_view line)
{
  line = trim(line);
  if (line.size() < 2 || line.front() != '{') return false;
  bool in_string = false;
  bool escaped = false;
  int depth = 0;
  std::size_t object_end = std::string_view::npos;
  for (std::size_t index = 0; index < line.size(); ++index) {
    char const ch = line[index];
    if (escaped) {
      escaped = false;
      continue;
    }
    if (ch == '\\' && in_string) {
      escaped = true;
      continue;
    }
    if (ch == '"') {
      in_string = !in_string;
      continue;
    }
    if (in_string) continue;
    if (ch == '{') {
      ++depth;
    } else if (ch == '}') {
      --depth;
      if (depth == 0) {
        object_end = index;
        break;
      }
      if (depth < 0) return false;
    }
  }
  if (in_string || depth != 0 || object_end == std::string_view::npos) return false;
  return trim(line.substr(object_end + 1)).empty();
}

std::string string_field_json(std::string_view key, std::string_view value)
{
  return "\"" + std::string(key) + "\":\"" + ava::core::json::escape(value) + "\"";
}

ava::core::Result<std::optional<long long>> exact_optional_integer_field(std::string_view object, std::string_view key)
{
  auto const start = ava::core::json::field_value_start(object, key);
  if (!start) return std::optional<long long>{};
  auto const field_name = std::string(key);
  std::size_t end = *start;
  if (end < object.size() && object[end] == '-') ++end;
  auto const digits_start = end;
  while (end < object.size() && std::isdigit(static_cast<unsigned char>(object[end])) != 0) ++end;
  if (end == digits_start) return std::unexpected(invalid_rpc("RPC " + field_name + " must be an integer"));
  bool const negative = object[*start] == '-';
  auto const unsigned_start = negative ? *start + 1 : *start;
  if (end - unsigned_start > 1 && object[unsigned_start] == '0') {
    return std::unexpected(invalid_rpc("RPC " + field_name + " must be an integer"));
  }
  while (end < object.size() && std::isspace(static_cast<unsigned char>(object[end])) != 0) ++end;
  if (end < object.size() && object[end] != ',' && object[end] != '}') {
    return std::unexpected(invalid_rpc("RPC " + field_name + " must be an integer"));
  }
  try {
    return std::optional<long long>{std::stoll(std::string(object.substr(*start, end - *start)))};
  } catch (...) {
    return std::unexpected(invalid_rpc("RPC " + field_name + " is out of range"));
  }
}

bool is_rpc_identifier_metacharacter(char ch)
{
  switch (ch) {
    case '"':
    case '\'':
    case '\\':
    case '`':
    case '$':
    case '&':
    case '|':
    case ';':
    case '<':
    case '>':
    case '(':
    case ')':
    case '[':
    case ']':
    case '{':
    case '}':
      return true;
    default:
      return false;
  }
}

ava::core::VoidResult validate_rpc_identifier(std::string_view value, std::string_view field_name)
{
  if (value.size() > kMaxRpcIdentifierBytes) {
    auto error = invalid_rpc("RPC identifier is too long");
    error.with_context("field", std::string(field_name));
    error.with_context("max_bytes", std::to_string(kMaxRpcIdentifierBytes));
    return std::unexpected(std::move(error));
  }

  for (char const ch : value) {
    auto const byte = static_cast<unsigned char>(ch);
    if (byte < 0x20 || byte == 0x7F || ch == ' ' || is_rpc_identifier_metacharacter(ch)) {
      auto error = invalid_rpc("RPC identifier contains invalid character");
      error.with_context("field", std::string(field_name));
      return std::unexpected(std::move(error));
    }
  }

  return {};
}

ava::core::VoidResult validate_optional_rpc_identifier(std::optional<std::string> const& value,
                                                       std::string_view field_name)
{
  if (!value) return {};
  return validate_rpc_identifier(*value, field_name);
}

ava::core::VoidResult validate_optional_rpc_text(std::optional<std::string> const& value, std::string_view field_name,
                                                 std::size_t max_bytes)
{
  if (!value) return {};
  if (value->size() > max_bytes) {
    auto error = invalid_rpc("RPC text field is too long");
    error.with_context("field", std::string(field_name));
    error.with_context("max_bytes", std::to_string(max_bytes));
    return std::unexpected(std::move(error));
  }

  for (char const ch : *value) {
    auto const byte = static_cast<unsigned char>(ch);
    if (byte < 0x20 || byte == 0x7F) {
      auto error = invalid_rpc("RPC text field contains invalid character");
      error.with_context("field", std::string(field_name));
      return std::unexpected(std::move(error));
    }
  }

  return {};
}

void skip_json_ws(std::string_view text, std::size_t& index)
{
  while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index])) != 0) ++index;
}

ava::core::Result<std::optional<std::vector<std::string>>> optional_string_array_field(std::string_view object,
                                                                                       std::string_view key)
{
  auto const start = ava::core::json::field_value_start(object, key);
  if (!start) return std::optional<std::vector<std::string>>{};
  auto const field_name = std::string(key);
  if (*start >= object.size() || object[*start] != '[') {
    return std::unexpected(invalid_rpc("RPC " + field_name + " must be an array of strings"));
  }

  std::size_t index = *start + 1;
  std::size_t string_count = 0;
  skip_json_ws(object, index);
  if (index < object.size() && object[index] != ']') {
    while (index < object.size()) {
      if (object[index] != '"') {
        return std::unexpected(invalid_rpc("RPC " + field_name + " must be an array of strings"));
      }
      bool escaped = false;
      bool closed = false;
      for (++index; index < object.size(); ++index) {
        char const ch = object[index];
        if (escaped) {
          escaped = false;
          continue;
        }
        if (ch == '\\') {
          escaped = true;
          continue;
        }
        if (ch == '"') {
          closed = true;
          ++index;
          break;
        }
      }
      if (!closed) return std::unexpected(invalid_rpc("RPC " + field_name + " must be an array of strings"));
      ++string_count;

      skip_json_ws(object, index);
      if (index >= object.size()) {
        return std::unexpected(invalid_rpc("RPC " + field_name + " must be an array of strings"));
      }
      if (object[index] == ',') {
        ++index;
        skip_json_ws(object, index);
        continue;
      }
      if (object[index] == ']') break;
      return std::unexpected(invalid_rpc("RPC " + field_name + " must be an array of strings"));
    }
  }

  if (index >= object.size() || object[index] != ']') {
    return std::unexpected(invalid_rpc("RPC " + field_name + " must be an array of strings"));
  }
  ++index;
  skip_json_ws(object, index);
  if (index < object.size() && object[index] != ',' && object[index] != '}') {
    return std::unexpected(invalid_rpc("RPC " + field_name + " must be an array of strings"));
  }

  auto values = ava::core::json::strings_in_array_field(object, key);
  if (values.size() != string_count) {
    return std::unexpected(invalid_rpc("RPC " + field_name + " must be an array of strings"));
  }
  return std::optional<std::vector<std::string>>{std::move(values)};
}

ava::core::VoidResult validate_optional_rpc_text_array(std::optional<std::vector<std::string>> const& values,
                                                       std::string_view field_name, std::size_t max_entries,
                                                       std::size_t max_bytes)
{
  if (!values) return {};
  if (values->size() > max_entries) {
    auto error = invalid_rpc("RPC text array field has too many entries");
    error.with_context("field", std::string(field_name));
    error.with_context("max_entries", std::to_string(max_entries));
    return std::unexpected(std::move(error));
  }
  for (auto const& value : *values) {
    auto wrapped = std::optional<std::string>{value};
    if (auto valid = validate_optional_rpc_text(wrapped, field_name, max_bytes); !valid) {
      return valid;
    }
  }
  return {};
}

}  // namespace

ava::core::Error invalid_rpc(std::string message)
{
  return ava::core::Error(ava::core::ErrorCategory::InvalidArgument, std::move(message));
}

ava::core::VoidResult validate_protocol_version(RpcCommand const& command)
{
  if (!command.protocol_version) return {};
  if (*command.protocol_version == kRpcProtocolVersion) return {};

  auto error = invalid_rpc("unsupported RPC protocol version");
  error.with_context("protocol_version", std::to_string(*command.protocol_version));
  error.with_context("supported_protocol_version", std::to_string(kRpcProtocolVersion));
  return std::unexpected(std::move(error));
}

std::string rpc_protocol_result_json()
{
  return "{\"protocol_version\":" + std::to_string(kRpcProtocolVersion) + ",\"supported_protocol_versions\":[" +
         std::to_string(kRpcProtocolVersion) + "]}";
}

std::string parse_error_response_id(std::string_view line)
{
  if (!is_json_object_line(line)) return "";
  auto id = ava::core::json::string_field(line, "id");
  if (!id || id->empty()) return "";
  if (auto valid = validate_rpc_identifier(*id, "id"); !valid) return "";
  return *id;
}

ava::core::Result<bool> read_rpc_line_bounded(std::istream& in, std::string& line)
{
  line.clear();
  bool oversized = false;
  while (true) {
    auto const next = in.get();
    if (next == std::istream::traits_type::eof()) {
      if (oversized) return std::unexpected(invalid_rpc("RPC request line is too large"));
      if (in.eof()) return !line.empty() || oversized;
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to read RPC stdin"));
    }
    char const ch = static_cast<char>(next);
    if (ch == '\n') break;
    if (line.size() >= kMaxRpcLineBytes) {
      oversized = true;
      continue;
    }
    if (!oversized) line.push_back(ch);
  }
  if (oversized) return std::unexpected(invalid_rpc("RPC request line is too large"));
  return true;
}

}  // namespace ava::app::rpc

namespace ava::app {

ava::core::Result<RpcCommand> parse_rpc_command_line(std::string_view line)
{
  if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
  if (line.size() > rpc::kMaxRpcLineBytes) return std::unexpected(rpc::invalid_rpc("RPC request line is too large"));
  if (!rpc::is_json_object_line(line)) return std::unexpected(rpc::invalid_rpc("malformed RPC JSON object"));

  auto id = ava::core::json::string_field(line, "id");
  if (!id || id->empty()) return std::unexpected(rpc::invalid_rpc("RPC request requires a non-empty string id"));
  if (auto valid = rpc::validate_rpc_identifier(*id, "id"); !valid) return std::unexpected(std::move(valid.error()));
  auto type = ava::core::json::string_field(line, "type");
  if (!type || type->empty()) return std::unexpected(rpc::invalid_rpc("RPC request requires a non-empty string type"));
  auto protocol_version = rpc::exact_optional_integer_field(line, "protocol_version");
  if (!protocol_version) return std::unexpected(std::move(protocol_version.error()));
  auto request_id = ava::core::json::string_field(line, "request_id");
  if (auto valid = rpc::validate_optional_rpc_identifier(request_id, "request_id"); !valid) {
    return std::unexpected(std::move(valid.error()));
  }
  auto correlation_id = ava::core::json::string_field(line, "correlation_id");
  if (auto valid = rpc::validate_optional_rpc_identifier(correlation_id, "correlation_id"); !valid) {
    return std::unexpected(std::move(valid.error()));
  }
  auto grant_id = ava::core::json::string_field(line, "grant_id");
  if (auto valid = rpc::validate_optional_rpc_identifier(grant_id, "grant_id"); !valid) {
    return std::unexpected(std::move(valid.error()));
  }
  auto provider = ava::core::json::string_field(line, "provider");
  if (auto valid = rpc::validate_optional_rpc_identifier(provider, "provider"); !valid) {
    return std::unexpected(std::move(valid.error()));
  }
  auto model = ava::core::json::string_field(line, "model");
  if (auto valid = rpc::validate_optional_rpc_identifier(model, "model"); !valid) {
    return std::unexpected(std::move(valid.error()));
  }
  auto plugin_id = ava::core::json::string_field(line, "plugin_id");
  if (auto valid = rpc::validate_optional_rpc_identifier(plugin_id, "plugin_id"); !valid) {
    return std::unexpected(std::move(valid.error()));
  }
  auto name = ava::core::json::string_field(line, "name");
  if (auto valid = rpc::validate_optional_rpc_identifier(name, "name"); !valid) {
    return std::unexpected(std::move(valid.error()));
  }
  auto server_id = ava::core::json::string_field(line, "server_id");
  if (auto valid = rpc::validate_optional_rpc_identifier(server_id, "server_id"); !valid) {
    return std::unexpected(std::move(valid.error()));
  }
  auto reasoning_budget_tokens = rpc::exact_optional_integer_field(line, "reasoning_budget_tokens");
  if (!reasoning_budget_tokens) return std::unexpected(std::move(reasoning_budget_tokens.error()));
  auto reason = ava::core::json::string_field(line, "reason");
  if (auto valid = rpc::validate_optional_rpc_text(reason, "reason", rpc::kMaxRpcReasonBytes); !valid) {
    return std::unexpected(std::move(valid.error()));
  }
  auto selected_options = rpc::optional_string_array_field(line, "selected_options");
  if (!selected_options) return std::unexpected(std::move(selected_options.error()));
  if (auto valid = rpc::validate_optional_rpc_text_array(
          *selected_options, "selected_options", rpc::kMaxRpcQuestionSelectedOptions, rpc::kMaxRpcQuestionAnswerBytes);
      !valid) {
    return std::unexpected(std::move(valid.error()));
  }
  auto answer = ava::core::json::string_field(line, "answer");
  if (auto valid = rpc::validate_optional_rpc_text(answer, "answer", rpc::kMaxRpcQuestionAnswerBytes); !valid) {
    return std::unexpected(std::move(valid.error()));
  }
  auto selected = ava::core::json::string_field(line, "selected");
  if (auto valid = rpc::validate_optional_rpc_text(selected, "selected", rpc::kMaxRpcQuestionAnswerBytes); !valid) {
    return std::unexpected(std::move(valid.error()));
  }

  return RpcCommand{.id = std::move(*id),
                    .type = std::move(*type),
                    .protocol_version = std::move(*protocol_version),
                    .message = ava::core::json::string_field(line, "message"),
                    .session_id = ava::core::json::string_field(line, "session_id"),
                    .provider = std::move(provider),
                    .model = std::move(model),
                    .instructions = ava::core::json::string_field(line, "instructions"),
                    .reasoning_level = ava::core::json::string_field(line, "reasoning_level"),
                    .reasoning_budget_tokens = std::move(*reasoning_budget_tokens),
                    .reasoning_display = ava::core::json::string_field(line, "reasoning_display"),
                    .request_id = std::move(request_id),
                    .correlation_id = std::move(correlation_id),
                    .grant_id = std::move(grant_id),
                    .decision = ava::core::json::string_field(line, "decision"),
                    .reason = std::move(reason),
                    .answer = std::move(answer),
                    .selected = std::move(selected),
                    .selected_options = std::move(*selected_options),
                    .plugin_id = std::move(plugin_id),
                    .name = std::move(name),
                    .arguments = ava::core::json::object_field(line, "arguments"),
                    .server_id = std::move(server_id),
                    .path = ava::core::json::string_field(line, "path")};
}

std::string serialize_rpc_success_jsonl(std::string_view id, std::string_view result_json)
{
  std::string json = "{\"id\":\"" + ava::core::json::escape(id) + "\",\"type\":\"response\",\"success\":true";
  if (!result_json.empty()) {
    json += ",\"result\":";
    json += result_json;
  }
  json += "}\n";
  return json;
}

std::string serialize_rpc_error_jsonl(std::string_view id, ava::core::Error const& error)
{
  std::string json =
      "{\"id\":\"" + ava::core::json::escape(id) + "\",\"type\":\"response\",\"success\":false,\"error\":{";
  json += rpc::string_field_json("category", ava::core::to_string(error.category()));
  json += ',';
  json += rpc::string_field_json("message", error.message());
  json += ',';
  json += rpc::string_field_json("details", error.format());
  json += "}}\n";
  return json;
}

}  // namespace ava::app
