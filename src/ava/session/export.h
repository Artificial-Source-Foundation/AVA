#pragma once

#include "ava/session/session_store.h"

#include <string>
#include <vector>

namespace ava::session {

struct ExportOptions
{
  bool include_tool_details = false;
  bool include_metadata = false;
  bool include_compactions = true;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

[[nodiscard]] SessionEntry sanitize_session_entry_for_portable_jsonl_export(SessionEntry entry);
[[nodiscard]] std::string format_session_markdown(std::vector<SessionEntry> const& entries, ExportOptions const& options = {});
[[nodiscard]] std::string format_session_html(std::vector<SessionEntry> const& entries, ExportOptions const& options = {});

}  // namespace ava::session
