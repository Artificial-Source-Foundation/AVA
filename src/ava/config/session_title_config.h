#pragma once

#include "ava/debug/print_members_on.h"
#include "ava/config/xdg_paths.h"
#include "ava/core/result.h"

#include <optional>
#include <string>
#include <string_view>

namespace ava::config {

struct SessionTitleConfig
{
  bool enabled = true;
  std::optional<std::string> provider_id = std::nullopt;
  std::optional<std::string> model_id = std::nullopt;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

[[nodiscard]] SessionTitleConfig default_session_title_config();
[[nodiscard]] ava::core::Result<SessionTitleConfig> parse_session_title_config(std::string_view content);
[[nodiscard]] ava::core::Result<SessionTitleConfig> load_session_title_config(XdgPaths const& paths);

}  // namespace ava::config
