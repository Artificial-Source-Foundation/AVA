#pragma once

#include <string>
#include <vector>

#include "ava/session/session_store.h"

namespace ava::session {

struct ExportOptions {
  bool include_tool_details = false;
  bool include_metadata = false;
  bool include_compactions = true;
};

[[nodiscard]] std::string format_session_markdown(const std::vector<SessionEntry>& entries,
                                                  const ExportOptions& options = {});

}  // namespace ava::session
