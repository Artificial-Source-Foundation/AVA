#include "ava/provider/default_provider_parser.h"

#include "ava/core/json.h"

namespace ava::provider::detail {
namespace {

class DefaultStreamParser final : public StreamParser {
 public:
  [[nodiscard]] ava::core::Result<std::vector<StreamEvent>> append(std::string_view chunk) override
  {
    pending_.append(chunk);
    return std::vector<StreamEvent>{};
  }

  [[nodiscard]] ava::core::Result<std::vector<StreamEvent>> finish() override
  {
    return parse_default_provider_response(HttpResponse{.status_code = 200, .headers = {}, .body = std::move(pending_)},
                                           true);
  }

 private:
  std::string pending_;
};

std::optional<std::string> default_text_from_json(std::string_view body)
{
  if (auto output = ava::core::json::string_field(body, "output_text")) return output;
  if (auto text = ava::core::json::string_field(body, "text")) return text;
  if (auto delta = ava::core::json::string_field(body, "delta")) return delta;
  return std::nullopt;
}

}  // namespace

std::unique_ptr<StreamParser> make_default_stream_parser()
{
  return std::make_unique<DefaultStreamParser>();
}

ava::core::Result<std::vector<StreamEvent>> parse_default_provider_response(HttpResponse const& response, bool stream)
{
  if (response.status_code < 200 || response.status_code >= 300) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Provider,
                                  "provider HTTP request failed with status " + std::to_string(response.status_code));
    error.with_context("status", std::to_string(response.status_code));
    error.with_context("provider_error_kind", to_string(classify_provider_error(response)));
    if (auto const retry_after = retry_after_header(response)) error.with_context("retry_after", *retry_after);
    return std::unexpected(std::move(error));
  }
  if (stream) {
    std::vector<StreamEvent> events;
    std::size_t line_start = 0;
    while (line_start <= response.body.size()) {
      auto const newline = response.body.find('\n', line_start);
      auto line = newline == std::string::npos
                      ? std::string_view(response.body).substr(line_start)
                      : std::string_view(response.body).substr(line_start, newline - line_start);
      if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
      if (line.starts_with("data:")) {
        line.remove_prefix(5);
        if (!line.empty() && line.front() == ' ') line.remove_prefix(1);
        if (line == "[DONE]") {
          events.push_back(StreamEvent{.type = StreamEventType::Done,
                                       .text = "",
                                       .tool_call_id = "",
                                       .tool_name = "",
                                       .error_message = "",
                                       .usage = std::nullopt});
        } else if (auto text = default_text_from_json(line)) {
          events.push_back(StreamEvent{.type = StreamEventType::TextDelta,
                                       .text = *text,
                                       .tool_call_id = "",
                                       .tool_name = "",
                                       .error_message = "",
                                       .usage = std::nullopt});
        }
      }
      if (newline == std::string::npos) break;
      line_start = newline + 1;
    }
    return events;
  }
  auto text = default_text_from_json(response.body);
  if (!text) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Provider, "provider response text is missing"));
  }
  return std::vector<StreamEvent>{StreamEvent{.type = StreamEventType::TextDelta,
                                              .text = *text,
                                              .tool_call_id = "",
                                              .tool_name = "",
                                              .error_message = "",
                                              .usage = std::nullopt},
                                  StreamEvent{.type = StreamEventType::Done,
                                              .text = "",
                                              .tool_call_id = "",
                                              .tool_name = "",
                                              .error_message = "",
                                              .usage = std::nullopt}};
}

}  // namespace ava::provider::detail
