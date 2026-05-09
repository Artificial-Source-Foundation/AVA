#pragma once

#include "ava/agent/mode.h"
#include "ava/config/model_config.h"
#include "ava/config/xdg_paths.h"
#include "ava/core/result.h"

#include <string>

namespace ava::config {

struct PromptSelection
{
  std::string text;
  bool from_override = false;
};

[[nodiscard]] std::string builtin_prompt(std::string_view provider_id, std::string_view family, ava::agent::Mode mode);
[[nodiscard]] ava::core::Result<PromptSelection> select_prompt(XdgPaths const& paths, ModelInfo const& model, ava::agent::Mode mode);

}  // namespace ava::config
