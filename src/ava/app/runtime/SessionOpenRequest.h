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

  // Clear one-navigation selections and initialization choices while keeping
  // initial_session_name plus strict adapter policy (exact_session_id and
  // expected_original_cwd) across lifecycle directory rebinding.
  void reset_for_lifecycle_navigation() noexcept
  {
    requested_session_id.reset();
    fork_session_id.reset();
    continue_last_session = false;
    sessionless = false;
    initial_reasoning_level.reset();
  }

  // Clear all selections and initialization choices before directly handing
  // an already-owned replacement store to the runtime. Strict adapter policy
  // remains in force for validation of that replacement.
  void reset_for_owned_replacement() noexcept
  {
    reset_for_lifecycle_navigation();
    initial_session_name.reset();
  }

  AVA_DEBUG_PRINT_MEMBERS_ON
};

}  // namespace ava::app::runtime
