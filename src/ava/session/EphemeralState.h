#pragma once

#include <ava/session/session_store.h>
#include <mutex>

namespace ava::session {

struct SessionStore::EphemeralState
{
  explicit EphemeralState(std::filesystem::path root) : root_dir(std::move(root)) {}

  EphemeralState(EphemeralState const&) = delete;
  EphemeralState& operator=(EphemeralState const&) = delete;

  ~EphemeralState()
  {
    std::error_code remove_error;
    std::filesystem::remove_all(root_dir, remove_error);
  }

  std::filesystem::path root_dir;
  mutable std::mutex mutex;
  std::vector<SessionEntry> entries;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

} // namespace ava::session
