#pragma once

#include "ava/debug/print_members_on.h"
#include "ava/session/session_store.h"
#include "ava/core/result.h"

#include <string>
#include <string_view>
#include <vector>
#include "debug.h"

namespace ava::session {

// Compatibility projection for legacy public/RPC consumers. Committed v4
// turns become the established assistant/reasoning/tool records; the
// synthesized assistant message preserves legacy text/tool_calls/usage and
// adds a private-free ordered_output manifest. v0-v3 records remain intact.
[[nodiscard]] ava::core::Result<std::vector<SessionEntry>> project_logical_session_history(std::vector<SessionEntry> const& entries);

// Ordered public projection for human-facing rendering, transcript projection,
// compaction prompts, and token estimation. Each committed v4 output item
// becomes exactly one safe legacy-shaped entry at its physical sequence
// position. It is deliberately not an accounting source.
[[nodiscard]] ava::core::Result<std::vector<SessionEntry>> project_ordered_public_session_history(std::vector<SessionEntry> const& entries);

// Portable archive projection. Unlike either public projection it retains
// committed v4 assistant_output_item and assistant_turn_commit records, their
// exact order and tool-result bindings, while rebuilding their payloads from
// private-free allowlists. A valid final uncommitted staging suffix is omitted.
[[nodiscard]] ava::core::Result<std::vector<SessionEntry>> project_portable_session_history(std::vector<SessionEntry> const& entries);

// RPC uses this narrow allowlist when serializing a compatibility-projected
// assistant message. It preserves legacy fields and the safe ordered manifest
// without treating arbitrary assistant data as public.
[[nodiscard]] std::string sanitized_compatibility_assistant_message_data_json(std::string_view data_json, bool include_ordered_output = true);

}  // namespace ava::session
