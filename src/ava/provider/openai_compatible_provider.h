#pragma once

#include <map>
#include <string>

#include "ava/provider/openai_compatible_request.h"
#include "ava/provider/provider.h"

namespace ava::provider {

class OpenAICompatibleStreamParser final : public StreamParser {
 public:
  explicit OpenAICompatibleStreamParser(std::string reasoning_format = "reasoning_content");
  [[nodiscard]] ava::core::Result<std::vector<StreamEvent>> append(std::string_view chunk) override;
  [[nodiscard]] ava::core::Result<std::vector<StreamEvent>> finish() override;

 private:
  std::string pending_line_;
  std::string data_;
  std::size_t scan_offset_ = 0;
  std::string reasoning_format_;
  bool reasoning_open_ = false;
  std::map<int, std::string> open_tool_call_ids_;
  std::optional<TokenUsage> usage_ = std::nullopt;
  std::string stop_reason_ = {};
  bool saw_data_ = false;
  bool done_seen_ = false;
  bool error_seen_ = false;
};

class OpenAICompatibleProvider final : public Provider {
 public:
  using Provider::build_request;

  explicit OpenAICompatibleProvider(OpenAICompatibleProviderOptions options = {});
  [[nodiscard]] ava::core::Result<HttpRequest> build_request(ProviderRequest const& request,
                                                             std::string_view access_token) const override;
  [[nodiscard]] std::unique_ptr<StreamParser> create_stream_parser() const override;
  [[nodiscard]] ava::core::Result<std::vector<StreamEvent>> parse_response(HttpResponse const& response,
                                                                           bool stream) const override;

 private:
  OpenAICompatibleProviderOptions options_;
};

[[nodiscard]] ava::core::Result<std::vector<StreamEvent>> parse_openai_compatible_sse(
    std::string_view sse, std::string reasoning_format = "reasoning_content");

}  // namespace ava::provider
