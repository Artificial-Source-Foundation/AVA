#pragma once

#include "ava/debug/print_members_on.h"
#include "ava/app/ContextSourceMetadata.h"
#include "ava/app/RuntimeBasePromptMetadata.h"
#include "ava/app/RuntimeFreshnessSourceMetadata.h"
#include "ava/agent/mode.h"

#include <string>
#include <vector>

namespace ava::app {

// Bundle the assembled prompt for one mode: the active agent mode, base prompt metadata, contributing context and freshness sources, and the resulting system
// prompt text.
//
// select_runtime_prompt_state builds this aggregate; apply_runtime_prompt_state copies it back into a RuntimeSession.
struct RuntimePromptState
{
  ava::agent::Mode mode = ava::agent::Mode::Build;
  RuntimeBasePromptMetadata base_prompt;
  std::vector<ContextSourceMetadata> context_sources;
  std::vector<RuntimeFreshnessSourceMetadata> freshness_sources;
  std::string system_prompt;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

}  // namespace ava::app
