#pragma once

#include <mutex>
#include <string>
#include <vector>

#include "ava/core/result.h"
#include "ava/session/session_store.h"

namespace ava::agent {

struct ActiveTurnMessage {
  std::string id;
  std::string text;
};

class ActiveTurnMessages final {
 public:
  [[nodiscard]] std::vector<ActiveTurnMessage> const& messages() const noexcept;
  [[nodiscard]] std::vector<std::string> replayable_texts() const;

  [[nodiscard]] ava::core::VoidResult append_user_message(ava::session::SessionStore& store, std::mutex* session_mutex,
                                                          std::string const& text);
  [[nodiscard]] ava::core::VoidResult replay_user_messages(ava::session::SessionStore& store,
                                                           std::mutex* session_mutex) const;

 private:
  std::vector<ActiveTurnMessage> messages_;
};

}  // namespace ava::agent
