#pragma once

#include "ava/provider/provider.h"

#include "ava/core/result.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ava::provider {

class OpenAIStreamParser final : public StreamParser {
 public:
  [[nodiscard]] ava::core::Result<std::vector<StreamEvent>> append(std::string_view chunk) override;
  [[nodiscard]] ava::core::Result<std::vector<StreamEvent>> finish() override;

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
  bool done_seen_ = false;
  bool error_seen_ = false;
};

}  // namespace ava::provider
