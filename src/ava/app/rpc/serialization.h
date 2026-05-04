#pragma once

#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ava/agent/agent_loop.h"
#include "ava/agent/question.h"
#include "ava/app/commands.h"
#include "ava/app/runtime.h"
#include "ava/config/model_config.h"
#include "ava/core/result.h"
#include "ava/permissions/permission.h"

namespace ava::app::rpc {

[[nodiscard]] std::string string_field_json(std::string_view key, std::string_view value);
[[nodiscard]] std::string bool_field_json(std::string_view key, bool value);
[[nodiscard]] std::string number_field_json(std::string_view key, std::size_t value);
[[nodiscard]] std::string integer_field_json(std::string_view key, long long value);
[[nodiscard]] std::string output_array_json(std::vector<std::string> const& output);
[[nodiscard]] std::string string_array_json(std::vector<std::string> const& values);

[[nodiscard]] std::vector<ava::config::ModelInfo> effective_models(ava::config::ModelRegistry const& registry);

[[nodiscard]] std::string state_result_json(RuntimeSession const& session, bool cancel_requested);
[[nodiscard]] ava::core::Result<std::string> list_sessions_result_json(RuntimeSession const& session);
[[nodiscard]] ava::core::Result<std::string> list_models_result_json(RuntimeSession const& session);
[[nodiscard]] std::string command_result_json(CommandResult const& result);
[[nodiscard]] ava::core::Result<std::string> messages_result_json(RuntimeSession const& session);
[[nodiscard]] ava::core::Result<std::string> session_stats_result_json(RuntimeSession const& session);
[[nodiscard]] ava::core::Result<std::string> session_validation_result_json(RuntimeSession const& session);
[[nodiscard]] std::string permission_request_payload_json(std::string_view resolver_request_id,
                                                          ava::permissions::PermissionPrompt const& prompt);
[[nodiscard]] std::string question_request_payload_json(std::string_view resolver_request_id,
                                                        ava::agent::QuestionPrompt const& prompt);
[[nodiscard]] std::string permission_reply_payload_json(std::string_view resolver_request_id,
                                                        std::string_view decision);
[[nodiscard]] std::string question_reply_payload_json(std::string_view resolver_request_id,
                                                      std::optional<std::string> const& answer,
                                                      std::optional<std::string> const& selected);
[[nodiscard]] std::string cancel_requested_payload_json(bool active_run, std::size_t cleared_steer,
                                                        std::size_t cleared_follow_up,
                                                        std::string_view active_request_id = {});
[[nodiscard]] std::string queued_message_payload_json(std::string_view message, std::string_view reason = {});
[[nodiscard]] std::string prompt_result_json(std::string_view session_id, ava::agent::AgentLoopResult const& result);

}  // namespace ava::app::rpc
