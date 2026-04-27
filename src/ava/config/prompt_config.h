#pragma once

#include <string>

#include "ava/agent/mode.h"
#include "ava/config/model_config.h"
#include "ava/config/xdg_paths.h"
#include "ava/core/result.h"

namespace ava::config {

struct PromptSelection {
  std::string text;
  bool from_override = false;
};

[[nodiscard]] std::string builtin_prompt(std::string_view provider_id,
                                         std::string_view family,
                                         ava::agent::Mode mode);
[[nodiscard]] ava::core::Result<PromptSelection> select_prompt(const XdgPaths& paths,
                                                               const ModelInfo& model,
                                                               ava::agent::Mode mode);

}  // namespace ava::config
