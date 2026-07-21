#pragma once

#include "ava/provider/provider.h"
#include "ava/core/result.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "debug.h"

namespace ava::provider {

class OpenAIStreamParser final : public StreamParser
{
 public:
  // Responses identifies the streamed item with an fc_ ID while callers
  // dispatch and replay through the separate opaque logical call_id.
  struct FunctionCallState
  {
    std::string provider_item_id;
    std::optional<std::size_t> provider_output_index = std::nullopt;
    std::string logical_call_id;
    std::string name;
    std::string arguments;
    std::size_t emitted_argument_bytes = 0;
    bool started = false;
    bool ended = false;

    AVA_DEBUG_PRINT_MEMBERS_ON
  };

  struct MessageItemState
  {
    std::optional<std::size_t> provider_output_index = std::nullopt;
    AssistantPhase phase = AssistantPhase::Unknown;
    // Responses exposes independent output_text and refusal content parts on
    // one message item. Retain separate completion state so a refusal.done
    // never conflicts with a preceding output_text.done.
    std::string text;
    std::string output_text;
    std::string refusal_text;
    bool output_text_completed = false;
    bool refusal_completed = false;
    bool ended = false;

    AVA_DEBUG_PRINT_MEMBERS_ON
  };

  [[nodiscard]] ava::core::Result<std::vector<StreamEvent>> append(std::string_view chunk) override;
  [[nodiscard]] ava::core::Result<std::vector<StreamEvent>> finish() override;

  AVA_DEBUG_PRINT_MEMBERS_ON

 private:
  std::string pending_line_;
  std::string data_;
  std::size_t scan_offset_ = 0;
  bool saw_content_ = false;
  bool saw_refusal_ = false;
  bool reasoning_open_ = false;
  bool reasoning_text_seen_ = false;
  std::string active_reasoning_item_id_;
  std::optional<std::size_t> active_reasoning_output_index_ = std::nullopt;
  std::string active_reasoning_text_;
  std::vector<std::string> completed_reasoning_item_ids_;
  std::vector<std::string> completed_reasoning_texts_;
  // Documented output-item events bind these maps once. Legacy event-family
  // state remains separate so it cannot turn an item ID into a logical call ID.
  std::unordered_map<std::string, FunctionCallState> function_calls_;
  std::unordered_map<std::string, std::string> function_call_item_ids_by_logical_id_;
  std::unordered_map<std::string, FunctionCallState> legacy_function_calls_;
  std::unordered_map<std::string, MessageItemState> message_items_;
  std::unordered_map<std::string, std::string> documented_output_item_types_;
  std::unordered_set<std::string> documented_output_item_added_ids_;
  std::unordered_map<std::size_t, std::string> documented_output_item_ids_by_index_;
  bool done_seen_ = false;
  bool error_seen_ = false;
  bool parser_limit_exceeded_ = false;
  // Counts events handed to the caller across all append() calls for one
  // response; a peer must not evade the event budget by fragmenting chunks.
  std::size_t events_emitted_ = 0;
  std::size_t data_records_seen_ = 0;
  bool data_record_limit_exceeded_ = false;
};

}  // namespace ava::provider
