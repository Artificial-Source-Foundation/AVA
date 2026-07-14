#pragma once

#include "ava/provider/provider.h"

#include <map>
#include <memory>
#include <optional>
#include <string>

namespace ava::provider {

class AnthropicStreamParser final : public StreamParser
{
 public:
  struct ToolBlock
  {
    std::string id;
    std::string name;
  };
  struct ReasoningBlock
  {
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
  std::optional<ProviderFinishReason> finish_reason_;
  bool saw_data_ = false;
  bool message_stop_seen_ = false;
  bool error_seen_ = false;
};

class AnthropicProvider final : public Provider
{
 public:
  using Provider::build_request;

  explicit AnthropicProvider(std::string base_url = "");
  [[nodiscard]] ava::core::Result<HttpRequest> build_request(ProviderRequest const& request, std::string_view access_token) const override;
  [[nodiscard]] ava::core::VoidResult apply_auth_options(HttpRequest& request, ProviderAuthContext const& auth) const override;
  [[nodiscard]] std::unique_ptr<StreamParser> create_stream_parser() const override;
  [[nodiscard]] ava::core::Result<std::vector<StreamEvent>> parse_response(HttpResponse const& response, bool stream) const override;

 private:
  std::string base_url_;
};

[[nodiscard]] ava::core::Result<std::vector<StreamEvent>> parse_anthropic_sse(std::string_view sse);
[[nodiscard]] ava::core::Result<std::vector<StreamEvent>> parse_anthropic_sse_response(HttpResponse const& response);
[[nodiscard]] ava::core::Result<std::vector<StreamEvent>> parse_anthropic_response(HttpResponse const& response);
[[nodiscard]] std::optional<TokenUsage> parse_anthropic_usage(std::string_view body);

}  // namespace ava::provider
