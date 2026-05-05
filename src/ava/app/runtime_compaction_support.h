#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "ava/core/result.h"
#include "ava/provider/provider.h"
#include "ava/session/session_store.h"

namespace ava::app::detail {

[[nodiscard]] std::string capped_entry_data(std::string_view data);
[[nodiscard]] std::string sanitized_reasoning_data_for_compaction(ava::session::SessionEntry const& entry);
[[nodiscard]] std::string compaction_entry_data(ava::session::SessionEntry const& entry);
[[nodiscard]] std::size_t utf8_suffix_start(std::string_view text, std::size_t suffix_bytes);
[[nodiscard]] std::string truncate_recent_context_to_token_budget(std::string tail, std::size_t keep_recent_tokens);
void erase_replayed_active_user_messages(std::vector<ava::provider::ChatMessage>& messages,
                                         std::vector<std::string> const& replayed_user_messages);
[[nodiscard]] ava::core::Result<std::string> build_recent_context_tail(
    std::vector<ava::session::SessionEntry> const& entries, std::size_t keep_recent_messages,
    std::size_t keep_recent_tokens, std::vector<std::string> const& replayed_user_messages);
[[nodiscard]] ava::core::Result<std::string> parse_compaction_response_text(ava::provider::Provider const& provider,
                                                                            ava::provider::HttpResponse const& response,
                                                                            bool stream);

}  // namespace ava::app::detail
