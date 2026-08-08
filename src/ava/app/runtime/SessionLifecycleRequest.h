#pragma once

#include "ava/debug/print_members_on.h"

#include <filesystem>
#include <optional>
#include <string>

namespace ava::app::runtime {

// Select one-shot session lifecycle and startup behavior for a runtime open.
//
// requested_session_id, fork_session_id, continue_last_session, and sessionless
// are mutually exclusive selectors. expected_original_cwd validates a resumed
// session and has no effect when a new session is created.
struct SessionLifecycleRequest
{
  bool sessionless = false;
  std::optional<std::string> requested_session_id = {};
  std::optional<std::string> fork_session_id = {};
  std::optional<std::string> initial_session_name = {};
  bool continue_last_session = false;
  std::optional<std::string> initial_reasoning_level = std::nullopt;
  // Strict adapters may pin the persisted cwd while retaining lease acquisition inside the protocol-neutral runtime ownership boundary.
  std::optional<std::filesystem::path> expected_original_cwd = std::nullopt;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

}  // namespace ava::app::runtime
