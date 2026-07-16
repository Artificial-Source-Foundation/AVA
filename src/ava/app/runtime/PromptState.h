#pragma once

#include "BasePromptMetadata.h"
#include "ContextSourceMetadata.h"
#include "FreshnessSourceMetadata.h"
#include "ava/debug/print_members_on.h"
#include "ava/agent/mode.h"

#include <string>
#include <vector>

namespace ava::app::runtime {

// Bundle the assembled prompt for one mode: the active agent mode, base prompt metadata, contributing context and freshness sources, and the resulting system
// prompt text.
//
// select_runtime_prompt_state builds this aggregate; apply_runtime_prompt_state copies it back into a Session.
struct PromptState
{
  ava::agent::Mode mode = ava::agent::Mode::Build;
  BasePromptMetadata base_prompt;
  std::vector<ContextSourceMetadata> context_sources;
  std::vector<FreshnessSourceMetadata> freshness_sources;
  std::string system_prompt;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

}  // namespace ava::app::runtime
