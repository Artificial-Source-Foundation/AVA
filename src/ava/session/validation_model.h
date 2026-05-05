#pragma once

#include <cstddef>
#include <string>

#include "ava/session/session_store.h"
#include "ava/session/validation.h"

namespace ava::session {

struct SessionReplayModelState {
  std::string provider_id;
  std::string model_id;
};

void validate_session_start_entry(SessionReplayValidation& validation, SessionReplayModelState& active_model,
                                  std::size_t index, SessionEntry const& entry);
void validate_model_change_entry(SessionReplayValidation& validation, SessionReplayModelState& active_model,
                                 std::size_t index, SessionEntry const& entry);
void validate_reasoning_change_entry(SessionReplayValidation& validation, SessionReplayModelState const& active_model,
                                     std::size_t index, SessionEntry const& entry);
void validate_reasoning_block_entry(SessionReplayValidation& validation, std::size_t index, SessionEntry const& entry);

}  // namespace ava::session
