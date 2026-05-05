#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ava/provider/provider.h"

namespace ava::provider {

class AnthropicStreamParser final : public StreamParser {
 public:
  struct ToolBlock {
    std::string id;
    std::string name;
  };
  struct ReasoningBlock {
    std::string signature;
    std::string redacted_data;
    bool redacted = false;
  };

  [[nodiscard]] ava::core::Result<std::vector<StreamEvent>> append(std::string_view chunk) override;
  [[nodiscard]] ava::core::Result<std::vector<StreamEvent>> finish() override;

 private:
  std::string pending_line_;
  std::string data_;
  std::size_t scan_offset_ = 0;
  std::map<long long, ToolBlock> tool_blocks_;
  std::map<long long, ReasoningBlock> reasoning_blocks_;
  std::optional<TokenUsage> usage_;
  std::string stop_reason_;
  bool saw_data_ = false;
  bool message_stop_seen_ = false;
  bool error_seen_ = false;
};

[[nodiscard]] ava::core::Result<std::vector<StreamEvent>> parse_anthropic_sse(std::string_view sse);
[[nodiscard]] ava::core::Result<std::vector<StreamEvent>> parse_anthropic_sse_response(HttpResponse const& response);
[[nodiscard]] ava::core::Result<std::vector<StreamEvent>> parse_anthropic_response(HttpResponse const& response);
[[nodiscard]] std::optional<TokenUsage> parse_anthropic_usage(std::string_view body);

}  // namespace ava::provider
