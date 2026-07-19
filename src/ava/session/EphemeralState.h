#pragma once

#include <mutex>
#include <ava/session/session_store.h>

namespace ava::session {

struct SessionStore::EphemeralState
{
  explicit EphemeralState(std::filesystem::path root) : root_dir(std::move(root)) { }

  EphemeralState(EphemeralState const&) = delete;
  EphemeralState& operator=(EphemeralState const&) = delete;

  ~EphemeralState()
  {
    std::error_code remove_error;
    std::filesystem::remove_all(root_dir, remove_error);
  }

  std::filesystem::path root_dir;
  // Serializes mutations across all copied stores/append targets. Keep this
  // distinct from entries_mutex so readers can take a short snapshot without
  // blocking a complete append preflight and write transaction.
  mutable std::mutex mutation_mutex;
  mutable std::mutex entries_mutex;
  std::vector<SessionEntry> entries;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

} // namespace ava::session
