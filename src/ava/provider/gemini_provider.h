#pragma once
#include "ava/debug/print_members_on.h"
#include "ava/http/transport.h"
#include "ava/provider/provider.h"

#include <memory>
#include <optional>
#include <string>

namespace ava::provider {

class GeminiStreamParser final : public StreamParser
{
 public:
  GeminiStreamParser();
  [[nodiscard]] ava::core::Result<std::vector<StreamEvent>> append(std::string_view chunk) override;
  [[nodiscard]] ava::core::Result<std::vector<StreamEvent>> finish() override;

 private:
  std::string pending_line_;
  std::string data_;
  std::size_t scan_offset_ = 0;
  std::optional<TokenUsage> usage_ = std::nullopt;
  std::optional<ProviderFinishReason> finish_reason_ = std::nullopt;
  std::string fallback_tool_call_prefix_;
  bool saw_data_ = false;
  bool done_seen_ = false;
  bool error_seen_ = false;
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

class GeminiProvider final : public Provider
{
 public:
  using Provider::build_request;

  explicit GeminiProvider(std::string base_url = "");
  [[nodiscard]] ava::core::Result<ava::http::HttpRequest> build_request(ProviderRequest const& request, std::string_view access_token) const override;
  [[nodiscard]] std::unique_ptr<StreamParser> create_stream_parser() const override;
  [[nodiscard]] ava::core::Result<std::vector<StreamEvent>> parse_response(ava::http::HttpResponse const& response, bool stream) const override;

 private:
  std::string base_url_;
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

[[nodiscard]] ava::core::Result<std::vector<StreamEvent>> parse_gemini_sse(std::string_view sse);
[[nodiscard]] ava::core::Result<std::vector<StreamEvent>> parse_gemini_sse_response(ava::http::HttpResponse const& response);
[[nodiscard]] ava::core::Result<std::vector<StreamEvent>> parse_gemini_response(ava::http::HttpResponse const& response);
[[nodiscard]] std::optional<TokenUsage> parse_gemini_usage(std::string_view body);

}  // namespace ava::provider
