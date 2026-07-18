#pragma once

#include "ava/provider/provider.h"
#include "ava/core/result.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ava::provider {

class OpenAIStreamParser final : public StreamParser
{
 public:
  // Responses identifies the streamed item with an fc_ ID while callers
  // dispatch and replay through the separate opaque logical call_id.
  struct FunctionCallState
  {
    std::string logical_call_id;
    std::string name;
    std::string arguments;
    std::size_t emitted_argument_bytes = 0;
    bool started = false;
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
  bool reasoning_open_ = false;
  bool reasoning_text_seen_ = false;
  std::string active_reasoning_item_id_;
  std::string active_reasoning_text_;
  std::vector<std::string> completed_reasoning_item_ids_;
  std::vector<std::string> completed_reasoning_texts_;
  // Documented output-item events bind these maps once. Legacy event-family
  // state remains separate so it cannot turn an item ID into a logical call ID.
  std::unordered_map<std::string, FunctionCallState> function_calls_;
  std::unordered_map<std::string, std::string> function_call_item_ids_by_logical_id_;
  std::unordered_map<std::string, FunctionCallState> legacy_function_calls_;
  bool done_seen_ = false;
  bool error_seen_ = false;
};

}  // namespace ava::provider
