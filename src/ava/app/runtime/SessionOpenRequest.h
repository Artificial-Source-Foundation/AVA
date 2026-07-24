#pragma once

#include "ava/debug/print_members_on.h"

#include <filesystem>
#include <optional>
#include <string>
#include "debug.h"

namespace ava::app::runtime {

// Carry one-open selection, initialization, and validation inputs. These are
// deliberately reset by lifecycle navigation instead of becoming session
// continuity state.
struct SessionOpenRequest
{
  std::optional<std::string> requested_session_id;
  std::optional<std::string> fork_session_id;
  std::optional<std::string> initial_session_name;
  bool continue_last_session = false;
  bool sessionless = false;
  std::optional<std::string> initial_reasoning_level = std::nullopt;
  // Strict adapters opt out of CLI prefix resolution.
  bool exact_session_id = false;
  // Strict adapters may pin the persisted cwd while retaining lease
  // acquisition inside the protocol-neutral runtime ownership boundary.
  std::optional<std::filesystem::path> expected_original_cwd = std::nullopt;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

}  // namespace ava::app::runtime
