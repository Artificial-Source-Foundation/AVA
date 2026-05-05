#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ava/provider/provider.h"
#include "ava/session/session_store.h"

namespace ava::agent::detail {

[[nodiscard]] std::string entry_text(ava::session::SessionEntry const& entry);
[[nodiscard]] std::string compaction_context_text(ava::session::SessionEntry const& entry);
[[nodiscard]] std::string tool_context_text(ava::session::SessionEntry const& entry);
[[nodiscard]] std::string tool_call_context_text(ava::session::SessionEntry const& entry);
[[nodiscard]] std::optional<bool> bool_field(std::string_view object, std::string_view key);
[[nodiscard]] std::size_t utf8_prefix_boundary(std::string_view text, std::size_t max_bytes);
[[nodiscard]] std::vector<ava::provider::ContentPart> tool_call_content_parts(ava::session::SessionEntry const& entry);
[[nodiscard]] std::string truncate_native_tool_result(std::string text, std::size_t max_bytes);
[[nodiscard]] std::vector<ava::provider::ContentPart> tool_result_content_parts(
    ava::session::SessionEntry const& entry, std::size_t max_tool_result_context_bytes);
[[nodiscard]] std::optional<ava::provider::ContentPart> reasoning_content_part(ava::session::SessionEntry const& entry);
void append_pending_reasoning_parts(std::vector<ava::provider::ContentPart>& target,
                                    std::vector<ava::provider::ContentPart>& pending);
[[nodiscard]] std::string truncate_tool_context(std::string text, std::size_t max_bytes);
void append_fallback_text(std::string& target, std::string text);
[[nodiscard]] std::size_t assistant_tool_call_count(ava::session::SessionEntry const& entry);
[[nodiscard]] bool contains_string(std::vector<std::string> const& values, std::string_view value);
bool erase_first_string(std::vector<std::string>& values, std::string_view value);

}  // namespace ava::agent::detail
