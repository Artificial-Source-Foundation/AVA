#pragma once

#include "ava/provider/openai_stream_parser.h"
#include "ava/core/result.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ava::provider::openai_stream_parser_internal {

enum class MessageContentKind
{
  OutputText,
  Refusal,
};

bool is_ignored_lifecycle_event(std::string_view type);
bool contains_string(std::vector<std::string> const& values, std::string_view value);
void remember_string(std::vector<std::string>& values, std::string value);
void append_stream_error(std::vector<StreamEvent>& events, bool& error_seen, std::string message);
void append_parser_limit_error(std::vector<StreamEvent>& events, bool& error_seen, bool& parser_limit_exceeded);
ava::core::Result<std::optional<std::size_t>> documented_output_index(std::string_view data, std::string_view item);
ava::core::Result<AssistantPhase> documented_message_phase(std::string_view data, std::string_view item);
ava::core::VoidResult documented_output_item_metadata(std::string_view data, std::string_view item, bool is_message, std::string& id,
                                                      std::optional<std::size_t>& output_index, AssistantPhase& phase);
bool register_documented_output_item(std::vector<StreamEvent>& events, bool& error_seen, std::string_view id, std::string_view type,
                                     std::optional<std::size_t> output_index, bool added, std::unordered_map<std::string, std::string>& item_types,
                                     std::unordered_set<std::string>& added_item_ids, std::unordered_map<std::size_t, std::string>& item_ids_by_output_index);
bool is_valid_function_call_arguments_object(std::string_view arguments);
bool reject_unended_documented_function_calls(std::vector<StreamEvent>& events, bool& error_seen,
                                              std::unordered_map<std::string, OpenAIStreamParser::FunctionCallState> const& function_calls);
bool reject_unended_documented_message_items(std::vector<StreamEvent>& events, bool& error_seen,
                                             std::unordered_map<std::string, OpenAIStreamParser::MessageItemState> const& message_items);
ava::core::Result<bool> completed_message_content_from_item(std::string_view item, std::string& output_text, std::string& refusal_text);
OpenAIStreamParser::MessageItemState* documented_message_item_for_event(std::vector<StreamEvent>& events, bool& error_seen, std::string_view data,
                                                                        std::unordered_map<std::string, OpenAIStreamParser::MessageItemState>& message_items,
                                                                        bool& documented);
void append_documented_message_delta(std::vector<StreamEvent>& events, OpenAIStreamParser::MessageItemState& state, std::string_view item_id,
                                     std::string_view text, MessageContentKind kind);
bool reconcile_complete_message_text(std::vector<StreamEvent>& events, OpenAIStreamParser::MessageItemState& state, std::string_view item_id,
                                     std::string_view complete_text, MessageContentKind kind, bool& error_seen);

std::string function_call_item_id_from_event(std::string_view data, std::optional<std::string_view> item = std::nullopt);
std::string logical_function_call_id_from_event(std::string_view data, std::optional<std::string_view> item = std::nullopt);
std::string function_call_name_from_event(std::string_view data, std::optional<std::string_view> item = std::nullopt);
std::optional<std::string> function_call_arguments_from_event(std::string_view data, std::optional<std::string_view> item = std::nullopt);
OpenAIStreamParser::FunctionCallState& legacy_function_call_state_for(std::string_view data,
                                                                      std::unordered_map<std::string, OpenAIStreamParser::FunctionCallState>& calls,
                                                                      std::optional<std::string_view> item = std::nullopt);
OpenAIStreamParser::FunctionCallState* legacy_function_call_state_for_existing_event(
    std::string_view data, std::unordered_map<std::string, OpenAIStreamParser::FunctionCallState>& calls);
bool has_documented_function_call_mapping(std::string_view data, std::unordered_map<std::string, std::string> const& item_ids_by_logical_id);
bool has_valid_legacy_function_call_identity(std::string_view data);
OpenAIStreamParser::FunctionCallState* bind_documented_function_call_item(std::vector<StreamEvent>& events, bool& error_seen, std::string_view item,
                                                                          std::optional<std::size_t> provider_output_index,
                                                                          std::unordered_map<std::string, OpenAIStreamParser::FunctionCallState>& calls,
                                                                          std::unordered_map<std::string, std::string>& item_ids_by_logical_id);
OpenAIStreamParser::FunctionCallState* documented_function_call_state_for_event(std::vector<StreamEvent>& events, bool& error_seen, std::string_view data,
                                                                                std::unordered_map<std::string, OpenAIStreamParser::FunctionCallState>& calls,
                                                                                std::unordered_map<std::string, std::string> const& item_ids_by_logical_id);
bool append_function_call_start_if_ready(std::vector<StreamEvent>& events, OpenAIStreamParser::FunctionCallState& state);
void append_unemitted_function_call_arguments(std::vector<StreamEvent>& events, OpenAIStreamParser::FunctionCallState& state);
bool append_function_call_argument_fragment(std::vector<StreamEvent>& events, OpenAIStreamParser::FunctionCallState& state, std::string_view fragment);
bool reconcile_complete_function_call_arguments(std::vector<StreamEvent>& events, OpenAIStreamParser::FunctionCallState& state,
                                                std::optional<std::string> const& complete_arguments, bool& error_seen);
bool append_function_call_end(std::vector<StreamEvent>& events, OpenAIStreamParser::FunctionCallState& state, bool require_named_start, bool& error_seen);

std::string reasoning_item_id_from_event(std::string_view data, std::optional<std::string_view> item = std::nullopt);
void set_active_reasoning_item_id(std::string& active_item_id, std::string_view item_id);
void append_start_reasoning_if_needed(std::vector<StreamEvent>& events, bool& reasoning_open, std::string_view active_reasoning_item_id,
                                      std::optional<std::size_t> active_reasoning_output_index);
void append_reasoning_delta(std::vector<StreamEvent>& events, bool& reasoning_open, bool& reasoning_text_seen, std::string const& active_reasoning_item_id,
                            std::optional<std::size_t> active_reasoning_output_index, std::string& active_reasoning_text, std::string_view text);
bool reasoning_event_has_authoritative_complete_text(std::string_view object);
bool reconcile_complete_reasoning_text(std::vector<StreamEvent>& events, bool& reasoning_open, bool& reasoning_text_seen,
                                       std::string const& active_reasoning_item_id, std::optional<std::size_t> active_reasoning_output_index,
                                       std::string& active_reasoning_text, std::string_view complete_text, bool& error_seen);
void append_finish_reasoning_if_open(std::vector<StreamEvent>& events, bool& reasoning_open, bool& reasoning_text_seen, std::string& active_reasoning_item_id,
                                     std::optional<std::size_t>& active_reasoning_output_index, std::string& active_reasoning_text,
                                     std::vector<std::string>& completed_reasoning_item_ids, std::vector<std::string>& completed_reasoning_texts,
                                     std::string native_item_json = {});
bool bind_documented_reasoning_event(std::vector<StreamEvent>& events, bool& error_seen, std::string_view data, std::string& active_reasoning_item_id,
                                     std::optional<std::size_t>& active_reasoning_output_index,
                                     std::unordered_map<std::string, std::string> const& documented_output_item_types,
                                     std::unordered_map<std::size_t, std::string> const& documented_output_item_ids_by_index);

}  // namespace ava::provider::openai_stream_parser_internal
