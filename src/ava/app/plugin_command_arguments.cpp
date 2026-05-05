#include "ava/app/plugin_command_arguments.h"

#include <cctype>
#include <utility>

#include "ava/core/json.h"

namespace ava::app::detail {

std::string trim_ascii_whitespace(std::string_view text)
{
  std::size_t start = 0;
  while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0) ++start;
  auto end = text.size();
  while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) --end;
  return std::string(text.substr(start, end - start));
}

std::string plugin_validate_argument(std::string_view plugins_argument)
{
  std::size_t index = 0;
  while (index < plugins_argument.size() && std::isspace(static_cast<unsigned char>(plugins_argument[index])) != 0) {
    ++index;
  }
  while (index < plugins_argument.size() && std::isspace(static_cast<unsigned char>(plugins_argument[index])) == 0) {
    ++index;
  }
  return trim_ascii_whitespace(plugins_argument.substr(index));
}

std::optional<std::string_view> consume_token(std::string_view& text)
{
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) text.remove_prefix(1);
  if (text.empty()) return std::nullopt;
  auto const end = text.find_first_of(" \t\r\n");
  auto const token = text.substr(0, end == std::string_view::npos ? text.size() : end);
  text.remove_prefix(token.size());
  return token;
}

ava::core::Result<PluginRunArguments> parse_plugin_run_arguments(std::string_view argument)
{
  auto const subcommand = consume_token(argument);
  auto const plugin_id = consume_token(argument);
  auto const command_name = consume_token(argument);
  while (!argument.empty() && std::isspace(static_cast<unsigned char>(argument.front())) != 0)
    argument.remove_prefix(1);
  if (!subcommand || *subcommand != "run" || !plugin_id || !command_name) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                            "usage: /plugin run <plugin_id> <command> [arguments_json]"));
  }
  auto arguments_json = argument.empty() ? std::string("{}") : std::string(argument);
  if (!ava::core::json::is_valid_object(arguments_json)) {
    return std::unexpected(
        ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "plugin command arguments must be a JSON object"));
  }
  return PluginRunArguments{.plugin_id = std::string(*plugin_id),
                            .command_name = std::string(*command_name),
                            .arguments_json = std::move(arguments_json)};
}

}  // namespace ava::app::detail
