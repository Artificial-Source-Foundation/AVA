#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "ava/core/result.h"
#include "ava/provider/openai_stream_events.h"
#include "ava/provider/provider.h"

namespace ava::provider {

class OpenAIStreamParser final : public StreamParser {
 public:
  [[nodiscard]] ava::core::Result<std::vector<StreamEvent>> append(std::string_view chunk) override;
  [[nodiscard]] ava::core::Result<std::vector<StreamEvent>> finish() override;

 private:
  std::string pending_line_;
  std::size_t scan_offset_ = 0;
  detail::OpenAIStreamEventState event_state_;
};

}  // namespace ava::provider
