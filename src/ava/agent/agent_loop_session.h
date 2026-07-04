#pragma once

#include "ava/agent/assistant_turn.h"
#include "ava/agent/tool_types.h"
#include "ava/tools/file_tools.h"
#include "ava/session/session_store.h"
#include "ava/session/attachments.h"
#include "ava/provider/provider.h"
#include "ava/core/result.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ava::agent {

[[nodiscard]] ava::core::Result<std::string> append_user_message(ava::session::SessionStore& store, std::string const& text);
[[nodiscard]] ava::core::Result<std::string> append_user_message(ava::session::SessionStore& store, std::string const& text,
                                                                 std::vector<ava::session::ImageAttachmentRef> const& attachments);
[[nodiscard]] ava::core::VoidResult append_replay_user_message(ava::session::SessionStore& store, std::string const& text, std::string const& replay_of);
[[nodiscard]] ava::core::VoidResult append_replay_user_message(ava::session::SessionStore& store, std::string const& text,
                                                               std::vector<ava::session::ImageAttachmentRef> const& attachments,
                                                               std::string const& replay_of);
[[nodiscard]] ava::core::VoidResult append_assistant_message(ava::session::SessionStore& store, std::string const& text, std::size_t tool_call_count,
                                                             ava::provider::TokenUsage const& usage, std::optional<long double> const& cost_usd);
[[nodiscard]] ava::core::VoidResult append_reasoning_block(ava::session::SessionStore& store, ParsedReasoningBlock const& block, std::string_view provider_id,
                                                           std::string_view model_id);
[[nodiscard]] ava::core::VoidResult append_tool_call(ava::session::SessionStore& store, ProviderToolCall const& call);
[[nodiscard]] ava::core::VoidResult append_tool_result(ava::session::SessionStore& store, ToolDispatchResult const& result);
[[nodiscard]] ava::core::VoidResult append_permission_decision(ava::session::SessionStore& store, ava::tools::PermissionAuditEvent const& event);
[[nodiscard]] ava::core::VoidResult append_error(ava::session::SessionStore& store, ava::core::Error const& error);
[[nodiscard]] ava::core::VoidResult append_cancel(ava::session::SessionStore& store, std::string_view boundary);

}  // namespace ava::agent
