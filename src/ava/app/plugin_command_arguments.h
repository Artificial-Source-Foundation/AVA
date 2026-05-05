#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "ava/core/result.h"

namespace ava::app::detail {

struct PluginRunArguments {
  std::string plugin_id;
  std::string command_name;
  std::string arguments_json = "{}";
};

[[nodiscard]] std::string trim_ascii_whitespace(std::string_view text);
[[nodiscard]] std::string plugin_validate_argument(std::string_view plugins_argument);
[[nodiscard]] std::optional<std::string_view> consume_token(std::string_view& text);
[[nodiscard]] ava::core::Result<PluginRunArguments> parse_plugin_run_arguments(std::string_view argument);

}  // namespace ava::app::detail
