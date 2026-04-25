#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "ava/agent/runtime.hpp"

namespace ava::agent::runtime_detail {

[[nodiscard]] std::uint64_t derive_message_id_counter(const ava::types::SessionRecord& session);

void append_session_message(
    ava::types::SessionRecord& session,
    std::uint64_t& id_counter,
    const std::string& role,
    std::string content,
    nlohmann::json tool_calls = nlohmann::json::array(),
    nlohmann::json tool_results = nlohmann::json::array(),
    std::optional<std::string> tool_call_id = std::nullopt
);

[[nodiscard]] std::vector<ava::llm::ChatMessage> build_chat_messages(
    const ava::types::SessionRecord& session,
    const std::string& system_prompt
);

[[nodiscard]] std::size_t estimate_session_tokens(
    const ava::types::SessionRecord& session,
    const ava::llm::Provider& provider
);

[[nodiscard]] ava::types::TokenUsage estimate_turn_usage(
    const std::vector<ava::llm::ChatMessage>& chat_messages,
    const std::string& assistant_text,
    const std::vector<ava::types::ToolCall>& tool_calls,
    const ava::llm::Provider& provider
);

[[nodiscard]] std::pair<std::size_t, std::size_t> compact_session_for_agent(
    ava::types::SessionRecord& session,
    const ava::llm::Provider& provider,
    std::size_t preserve_recent_messages
);

[[nodiscard]] std::string encode_tool_result_message(const ava::types::ToolResult& result);

void emit_event(const AgentEventSink& sink, AgentEvent event);

void record_unprocessed_queue_items(ava::agent::MessageQueue* queue, BudgetTracker& budget);

void persist_completion_metadata(
    ava::types::SessionRecord& session,
    AgentCompletionReason reason,
    std::size_t turns_used,
    const BudgetTracker& budget
);

void persist_error_metadata(
    ava::types::SessionRecord& session,
    std::size_t turns_used,
    std::string_view error_message
);

[[nodiscard]] bool append_promoted_queue_messages(
    ava::types::SessionRecord& session,
    ava::agent::MessageQueue* queue,
    std::uint64_t& id_counter,
    std::size_t turn,
    const std::function<void(std::size_t, std::string)>& emit_checkpoint
);

}  // namespace ava::agent::runtime_detail
