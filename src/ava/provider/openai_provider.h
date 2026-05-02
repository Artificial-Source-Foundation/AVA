#pragma once

#include <optional>
#include <string>

#include "ava/config/auth.h"
#include "ava/provider/provider.h"

namespace ava::provider {

class OpenAIStreamParser final : public StreamParser {
 public:
  [[nodiscard]] ava::core::Result<std::vector<StreamEvent>> append(std::string_view chunk) override;
  [[nodiscard]] ava::core::Result<std::vector<StreamEvent>> finish() override;

 private:
  std::string pending_line_;
  std::string data_;
  std::size_t scan_offset_ = 0;
};

class OpenAIProvider final : public Provider {
 public:
  using Provider::build_request;

  explicit OpenAIProvider(std::string base_url = "https://api.openai.com");
  [[nodiscard]] ava::core::Result<HttpRequest> build_request(const ProviderRequest& request,
                                                              std::string_view access_token) const override;
  [[nodiscard]] ava::core::VoidResult apply_auth_options(HttpRequest& request,
                                                         const ProviderAuthContext& auth) const override;
  [[nodiscard]] std::unique_ptr<StreamParser> create_stream_parser() const override;
  [[nodiscard]] ava::core::Result<std::vector<StreamEvent>> parse_response(const HttpResponse& response,
                                                                           bool stream) const override;
  [[nodiscard]] ava::core::Result<HttpRequest> build_request(const ProviderRequest& request,
                                                              const ava::config::OpenAICredential& credential,
                                                              long long now_seconds) const;
  [[nodiscard]] ava::core::Result<HttpRequest> build_request(const ProviderRequest& request,
                                                             const ava::config::OpenAICredential& credential) const;

 private:
  std::string base_url_;
};

[[nodiscard]] ava::core::Result<std::vector<StreamEvent>> parse_openai_sse(std::string_view sse);
[[nodiscard]] ava::core::Result<std::vector<StreamEvent>> parse_openai_sse_response(const HttpResponse& response);
[[nodiscard]] ava::core::Result<std::string> parse_openai_response_text(std::string_view body);
[[nodiscard]] std::optional<TokenUsage> parse_openai_usage(std::string_view body);
[[nodiscard]] bool is_retryable_status(int status_code) noexcept;
[[nodiscard]] bool is_auth_status(int status_code) noexcept;

}  // namespace ava::provider
