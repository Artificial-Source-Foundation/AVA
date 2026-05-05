#include "ava/agent/tool_arguments.h"

#include <cctype>

#include "ava/core/json.h"

namespace ava::agent {

ava::core::Result<std::string> required_string_arg(std::string_view arguments, std::string_view field,
                                                   std::string_view tool_name)
{
  auto value = ava::core::json::string_field(arguments, field);
  if (value) return *value;
  auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "tool argument is required");
  error.with_context("tool", std::string(tool_name));
  error.with_context("argument", std::string(field));
  return std::unexpected(std::move(error));
}

ava::core::VoidResult reject_nul_arg(std::string_view value, std::string_view field, std::string_view tool_name)
{
  if (value.find('\0') == std::string_view::npos) return {};
  auto error =
      ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "tool argument contains a forbidden NUL byte");
  error.with_context("tool", std::string(tool_name));
  error.with_context("argument", std::string(field));
  return std::unexpected(std::move(error));
}

ava::core::VoidResult reject_control_arg(std::string_view value, std::string_view field, std::string_view tool_name)
{
  for (char const ch : value) {
    auto const byte = static_cast<unsigned char>(ch);
    if (byte < 0x20 || byte == 0x7F) {
      auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                    "tool argument contains a forbidden control byte");
      error.with_context("tool", std::string(tool_name));
      error.with_context("argument", std::string(field));
      return std::unexpected(std::move(error));
    }
  }
  return {};
}

ava::core::VoidResult reject_control_value(std::string_view value, std::string_view field, std::string_view message)
{
  for (char const ch : value) {
    auto const byte = static_cast<unsigned char>(ch);
    if (byte < 0x20 || byte == 0x7F) {
      auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, std::string(message));
      error.with_context("field", std::string(field));
      return std::unexpected(std::move(error));
    }
  }
  return {};
}

ava::core::Result<std::string> required_text_arg(std::string_view arguments, std::string_view field,
                                                 std::string_view tool_name)
{
  auto value = required_string_arg(arguments, field, tool_name);
  if (!value) return std::unexpected(value.error());
  if (auto safe = reject_nul_arg(*value, field, tool_name); !safe) return std::unexpected(safe.error());
  return *value;
}

ava::core::Result<std::string> required_safe_string_arg(std::string_view arguments, std::string_view field,
                                                        std::string_view tool_name)
{
  auto value = required_string_arg(arguments, field, tool_name);
  if (!value) return std::unexpected(value.error());
  if (auto safe = reject_control_arg(*value, field, tool_name); !safe) return std::unexpected(safe.error());
  return *value;
}

std::filesystem::path workspace_path(ava::tools::ToolContext const& context, std::string_view path)
{
  std::filesystem::path const parsed(path);
  if (parsed.is_absolute()) return parsed;
  return context.workspace_dir / parsed;
}

std::size_t optional_size_arg(std::string_view arguments, std::string_view field, std::size_t fallback,
                              std::size_t maximum)
{
  auto const value = ava::core::json::integer_field(arguments, field);
  if (!value || *value <= 0) return fallback;
  auto const converted = static_cast<unsigned long long>(*value);
  if (converted > maximum) return maximum;
  return static_cast<std::size_t>(converted);
}

ava::core::Result<bool> optional_bool_arg(std::string_view arguments, std::string_view field, bool fallback,
                                          std::string_view tool_name)
{
  auto const start = ava::core::json::field_value_start(arguments, field);
  if (!start) return fallback;
  auto const is_value_boundary = [&arguments](std::size_t index) {
    return index >= arguments.size() || std::isspace(static_cast<unsigned char>(arguments[index])) != 0 ||
           arguments[index] == ',' || arguments[index] == '}' || arguments[index] == ']';
  };
  if (arguments.substr(*start, 4) == "true" && is_value_boundary(*start + 4)) return true;
  if (arguments.substr(*start, 5) == "false" && is_value_boundary(*start + 5)) return false;

  auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "tool argument must be a boolean");
  error.with_context("tool", std::string(tool_name));
  error.with_context("argument", std::string(field));
  return std::unexpected(std::move(error));
}

ava::core::VoidResult reject_provider_no_ignore(std::string_view arguments, std::string_view tool_name)
{
  auto no_ignore = optional_bool_arg(arguments, "no_ignore", false, tool_name);
  if (!no_ignore) return std::unexpected(no_ignore.error());
  if (!*no_ignore) return {};

  auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                "no_ignore requires explicit local control and is not available to provider tools");
  error.with_context("tool", std::string(tool_name));
  error.with_context("argument", "no_ignore");
  return std::unexpected(std::move(error));
}

}  // namespace ava::agent
