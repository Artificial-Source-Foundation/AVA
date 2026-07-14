#pragma once

#include "ava/provider/provider.h"

#include <map>
#include <optional>
#include <string>

namespace ava::provider {

struct OpenAICompatibleProviderOptions
{
  std::string base_url = "https://api.openai.com";
  std::string chat_completions_path = "/v1/chat/completions";
  std::string provider_name = "OpenAI-compatible";
  std::string reasoning_format = "reasoning_content";
  std::string user_agent = {};
  std::optional<double> default_temperature = std::nullopt;
  std::string reasoning_request_field = "thinking";
  bool reasoning_request_effort_string = false;
  bool preserve_reasoning_content = false;
  bool include_stream_usage = false;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

class OpenAICompatibleStreamParser final : public StreamParser
{
 public:
  explicit OpenAICompatibleStreamParser(std::string reasoning_format = "reasoning_content");
  [[nodiscard]] ava::core::Result<std::vector<StreamEvent>> append(std::string_view chunk) override;
  [[nodiscard]] ava::core::Result<std::vector<StreamEvent>> finish() override;

  AVA_DEBUG_PRINT_MEMBERS_ON

 private:
  std::string pending_line_;
  std::string data_;
  std::size_t scan_offset_ = 0;
  std::string reasoning_format_;
  std::string fallback_tool_call_prefix_;
  bool reasoning_open_ = false;
  std::map<int, std::string> open_tool_call_ids_;
  std::optional<TokenUsage> usage_ = std::nullopt;
  std::optional<ProviderFinishReason> finish_reason_ = std::nullopt;
  bool saw_data_ = false;
  bool done_seen_ = false;
  bool error_seen_ = false;
};

class OpenAICompatibleProvider final : public Provider
{
 public:
  using Provider::build_request;

  explicit OpenAICompatibleProvider(OpenAICompatibleProviderOptions options = {});
  [[nodiscard]] ava::core::Result<HttpRequest> build_request(ProviderRequest const& request, std::string_view access_token) const override;
  [[nodiscard]] std::unique_ptr<StreamParser> create_stream_parser() const override;
  [[nodiscard]] ava::core::Result<std::vector<StreamEvent>> parse_response(HttpResponse const& response, bool stream) const override;

  AVA_DEBUG_PRINT_MEMBERS_ON

 private:
  OpenAICompatibleProviderOptions options_;
};

[[nodiscard]] ava::core::Result<std::vector<StreamEvent>> parse_openai_compatible_sse(std::string_view sse, std::string reasoning_format = "reasoning_content");

}  // namespace ava::provider
