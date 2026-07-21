#pragma once

#include "ava/session/session_store.h"

#include <string_view>

namespace ava::session {

inline constexpr std::string_view kPublicSessionErrorOmission = "[Session error diagnostics omitted from public projection.]";

// Rebuilds portable records from explicit allowlists so provider-private
// replay material and attachment-storage references never cross export/import
// boundaries.
[[nodiscard]] SessionEntry sanitize_session_entry_for_portable_jsonl_export(SessionEntry entry);

// Imported and legacy Error records may contain provider-controlled details.
// Public projections retain no diagnostic payload from those physical records.
[[nodiscard]] SessionEntry sanitize_session_error_for_public_projection(SessionEntry entry);

}  // namespace ava::session
