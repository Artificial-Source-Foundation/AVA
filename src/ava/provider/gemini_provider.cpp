#include "sys.h"
#include "ava/http/transport.h"
#include "ava/provider/gemini_provider.h"
#include "ava/provider/provider_utils.h"
#include "ava/core/ids.h"
#include "ava/core/json.h"

#include <cctype>
#include <cstdlib>
#include <optional>
#include <string_view>
#include <utility>

namespace ava::provider {
namespace {

constexpr std::string_view kDefaultGeminiBaseUrl = "https://generativelanguage.googleapis.com";

struct GeminiParseState
{
  std::optional<TokenUsage> usage = std::nullopt;
  std::optional<ProviderFinishReason> finish_reason = std::nullopt;
  std::string fallback_tool_call_prefix;
  bool parsed_content = false;
  bool error_seen = false;
};

std::string configured_base_url()
{
  if (char const* value = std::getenv("GEMINI_BASE_URL"); value != nullptr && value[0] != '\0')
    return value;
  return std::string(kDefaultGeminiBaseUrl);
}

std::string trim_trailing_slashes(std::string value)
{
  while (value.size() > 1 && value.back() == '/') value.pop_back();
  return value;
}

std::string normalize_gemini_base_url(std::string base_url)
{
  return trim_trailing_slashes(base_url.empty() ? configured_base_url() : std::move(base_url));
}

bool valid_gemini_identifier(std::string_view value)
{
  if (value.empty() || value.size() > 128)
    return false;
  for (char const ch : value)
  {
    auto const byte = static_cast<unsigned char>(ch);
    if (std::isalnum(byte) != 0 || ch == '_' || ch == '-')
      continue;
    return false;
  }
  return true;
}

bool valid_gemini_model_id(std::string_view value)
{
  if (value.empty() || value.size() > 128)
    return false;
  for (char const ch : value)
  {
    auto const byte = static_cast<unsigned char>(ch);
    if (std::isalnum(byte) != 0 || ch == '_' || ch == '-' || ch == '.')
      continue;
    return false;
  }
  return true;
}

ava::core::Result<std::string> gemini_model_path(std::string_view model_id)
{
  if (model_id.starts_with("models/"))
    model_id.remove_prefix(std::string_view("models/").size());
  if (!valid_gemini_model_id(model_id))
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "Gemini model id is invalid");
    error.with_context("model", std::string(model_id));
    return std::unexpected(std::move(error));
  }
  return "models/" + std::string(model_id);
}

std::string gemini_role(std::string_view role)
{
  return role == "assistant" ? "model" : "user";
}

ava::core::Error invalid_content_part_error(std::string message, std::size_t message_index, std::size_t part_index)
{
  auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, std::move(message));
  error.with_context("provider", "gemini");
  error.with_context("message_index", std::to_string(message_index));
  error.with_context("content_part_index", std::to_string(part_index));
  return error;
}

ava::core::VoidResult validate_gemini_content_parts(ProviderRequest const& request)
{
  if (auto valid_images = validate_image_content_parts(request, true); !valid_images)
    return valid_images;
  for (std::size_t message_index = 0; message_index < request.messages.size(); ++message_index)
  {
    auto const& message = request.messages[message_index];
    auto const role = gemini_role(message.role);
    for (std::size_t part_index = 0; part_index < message.content_parts.size(); ++part_index)
    {
      auto const& part = message.content_parts[part_index];
      switch (part.type)
      {
        case ContentPartType::Text:
        case ContentPartType::Reasoning:
          break;
        case ContentPartType::Image:
          if (part.data_base64.empty())
          {
            return std::unexpected(invalid_content_part_error("Gemini image content requires verified attachment bytes", message_index, part_index));
          }
          break;
        case ContentPartType::ToolUse:
          if (role != "model")
          {
            return std::unexpected(invalid_content_part_error("Gemini functionCall content requires assistant role", message_index, part_index));
          }
          if (part.tool_call_id.empty() || !valid_gemini_identifier(part.tool_name))
          {
            return std::unexpected(invalid_content_part_error("Gemini functionCall content requires id and a valid name", message_index, part_index));
          }
          if (!is_valid_json_object(part.input_json))
          {
            return std::unexpected(invalid_content_part_error("Gemini functionCall args must be a valid JSON object", message_index, part_index));
          }
          break;
        case ContentPartType::ToolResult:
          if (role != "user")
          {
            return std::unexpected(invalid_content_part_error("Gemini functionResponse content requires user role", message_index, part_index));
          }
          if (part.tool_call_id.empty() || !valid_gemini_identifier(part.tool_name))
          {
            return std::unexpected(invalid_content_part_error("Gemini functionResponse content requires id and a valid name", message_index, part_index));
          }
          break;
      }
    }
  }
  return {};
}

std::string text_part_json(std::string_view text)
{
  return "{\"text\":\"" + ava::core::json::escape(text) + "\"}";
}

std::string image_part_json(ContentPart const& part)
{
  return "{\"inlineData\":{\"mimeType\":\"" + ava::core::json::escape(part.mime_type) + "\",\"data\":\"" + ava::core::json::escape(part.data_base64) + "\"}}";
}

std::string function_call_part_json(ContentPart const& part)
{
  auto const args = is_valid_json_object(part.input_json) ? part.input_json : std::string("{}");
  return "{\"functionCall\":{\"id\":\"" + ava::core::json::escape(part.tool_call_id) + "\",\"name\":\"" + ava::core::json::escape(part.tool_name) +
         "\",\"args\":" + args + "}}";
}

std::string function_response_payload_json(ContentPart const& part)
{
  std::string json = "{\"content\":";
  if (is_valid_json_object(part.text))
  {
    json += part.text;
  }
  else
  {
    json += "\"" + ava::core::json::escape(part.text) + "\"";
  }
  if (part.is_error)
    json += ",\"is_error\":true";
  json += '}';
  return json;
}

std::string function_response_part_json(ContentPart const& part)
{
  return "{\"functionResponse\":{\"id\":\"" + ava::core::json::escape(part.tool_call_id) + "\",\"name\":\"" + ava::core::json::escape(part.tool_name) +
         "\",\"response\":" + function_response_payload_json(part) + "}}";
}

std::string gemini_content_json(ChatMessage const& message)
{
  std::string json = "{\"role\":\"" + gemini_role(message.role) + "\",\"parts\":[";
  bool first = true;
  auto append_part = [&](std::string part_json) {
    if (!first)
      json += ',';
    first = false;
    json += std::move(part_json);
  };

  if (message.content_parts.empty())
  {
    append_part(text_part_json(message.content));
  }
  else
  {
    for (auto const& part : message.content_parts)
    {
      switch (part.type)
      {
        case ContentPartType::Text:
          if (!part.text.empty())
            append_part(text_part_json(part.text));
          break;
        case ContentPartType::Image:
          append_part(image_part_json(part));
          break;
        case ContentPartType::ToolUse:
          append_part(function_call_part_json(part));
          break;
        case ContentPartType::ToolResult:
          append_part(function_response_part_json(part));
          break;
        case ContentPartType::Reasoning:
          break;
      }
    }
    if (first)
      append_part(text_part_json(message.content));
  }

  json += "]}";
  return json;
}

ava::core::Result<std::string> function_declaration_json(std::string_view schema)
{
  if (!is_valid_json_object(schema))
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "Gemini tool JSON must be valid JSON"));
  }

  std::string function_storage;
  std::string_view scope = schema;
  if (auto const function = ava::core::json::object_field(schema, "function"))
  {
    function_storage = *function;
    scope = function_storage;
  }

  auto const name = ava::core::json::string_field(scope, "name");
  if (!name || !valid_gemini_identifier(*name))
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "Gemini tool JSON requires a valid function name"));
  }
  auto const description = ava::core::json::string_field(scope, "description").value_or("");
  auto parameters = ava::core::json::object_field(scope, "parameters");
  if (!parameters)
    parameters = ava::core::json::object_field(scope, "parametersJsonSchema");
  if (!parameters)
    parameters = std::string("{\"type\":\"object\"}");
  if (!is_valid_json_object(*parameters))
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "Gemini tool parameters must be valid JSON"));
  }

  return "{\"name\":\"" + ava::core::json::escape(*name) + "\",\"description\":\"" + ava::core::json::escape(description) + "\",\"parameters\":" + *parameters +
         "}";
}

ava::core::Result<std::string> gemini_request_body_json(ProviderRequest const& request)
{
  std::string body = "{";
  bool first_member = true;
  auto append_member = [&](std::string member) {
    if (!first_member)
      body += ',';
    first_member = false;
    body += std::move(member);
  };

  if (!request.system_prompt.empty())
  {
    append_member("\"systemInstruction\":{\"parts\":[" + text_part_json(request.system_prompt) + "]}");
  }

  std::string contents = "\"contents\":[";
  for (std::size_t index = 0; index < request.messages.size(); ++index)
  {
    if (index > 0)
      contents += ',';
    contents += gemini_content_json(request.messages[index]);
  }
  contents += ']';
  append_member(std::move(contents));

  if (!request.tools_json.empty())
  {
    std::string tools = "\"tools\":[{\"functionDeclarations\":[";
    for (std::size_t index = 0; index < request.tools_json.size(); ++index)
    {
      auto declaration = function_declaration_json(request.tools_json[index]);
      if (!declaration)
        return std::unexpected(std::move(declaration.error()));
      if (index > 0)
        tools += ',';
      tools += *declaration;
    }
    tools += "]}]";
    append_member(std::move(tools));
  }

  if (request.max_output_tokens && *request.max_output_tokens > 0)
  {
    append_member("\"generationConfig\":{\"maxOutputTokens\":" + std::to_string(*request.max_output_tokens) + "}");
  }

  body += '}';
  return body;
}

std::optional<long long> non_negative_integer_field(std::string_view object, std::string_view key)
{
  auto const value = ava::core::json::integer_field(object, key);
  if (!value || *value < 0)
    return std::nullopt;
  return value;
}

void merge_usage(TokenUsage& target, TokenUsage const& source)
{
  if (source.input_tokens)
    target.input_tokens = source.input_tokens;
  if (source.output_tokens)
    target.output_tokens = source.output_tokens;
  if (source.reasoning_tokens)
    target.reasoning_tokens = source.reasoning_tokens;
  if (source.cache_read_tokens)
    target.cache_read_tokens = source.cache_read_tokens;
  if (source.total_tokens)
    target.total_tokens = source.total_tokens;
}

ProviderFinishReason normalized_finish_reason(std::string_view reason)
{
  return normalize_provider_finish_reason(ProviderProtocol::Gemini, reason);
}

std::string prompt_block_reason(std::string_view body)
{
  auto const prompt_feedback = ava::core::json::object_field(body, "promptFeedback");
  if (!prompt_feedback)
    return {};
  return ava::core::json::string_field(*prompt_feedback, "blockReason").value_or("");
}

void append_error_event(std::vector<StreamEvent>& events, std::string message)
{
  events.push_back(
      StreamEvent{.type = StreamEventType::Error, .text = "", .tool_call_id = "", .tool_name = "", .error_message = std::move(message), .usage = std::nullopt});
}

void append_done_event(std::vector<StreamEvent>& events, std::optional<TokenUsage> usage, std::optional<ProviderFinishReason> finish_reason)
{
  events.push_back(StreamEvent{.type = StreamEventType::Done,
                               .text = "",
                               .tool_call_id = "",
                               .tool_name = "",
                               .error_message = "",
                               .usage = std::move(usage),
                               .finish_reason = finish_reason});
}

void append_function_call_events(std::vector<StreamEvent>& events, std::string_view function_call, std::size_t candidate_index, std::size_t part_index,
                                 GeminiParseState& state)
{
  auto const name = ava::core::json::string_field(function_call, "name").value_or("");
  if (name.empty())
  {
    state.error_seen = true;
    append_error_event(events, "Gemini functionCall is missing name");
    return;
  }
  auto id = ava::core::json::string_field(function_call, "id").value_or("");
  if (id.empty())
    id = state.fallback_tool_call_prefix + "_" + std::to_string(candidate_index) + "_" + std::to_string(part_index);
  auto args = ava::core::json::object_field(function_call, "args").value_or("{}");
  if (!is_valid_json_object(args))
  {
    state.error_seen = true;
    append_error_event(events, "Gemini functionCall args must be a JSON object");
    return;
  }

  state.parsed_content = true;
  events.push_back(
      StreamEvent{.type = StreamEventType::ToolCallStart, .text = "", .tool_call_id = id, .tool_name = name, .error_message = "", .usage = std::nullopt});
  events.push_back(StreamEvent{
      .type = StreamEventType::ToolCallDelta, .text = std::move(args), .tool_call_id = id, .tool_name = "", .error_message = "", .usage = std::nullopt});
  events.push_back(StreamEvent{
      .type = StreamEventType::ToolCallEnd, .text = "", .tool_call_id = std::move(id), .tool_name = "", .error_message = "", .usage = std::nullopt});
}

void append_part_events(std::vector<StreamEvent>& events, std::string_view part, std::size_t candidate_index, std::size_t part_index, GeminiParseState& state)
{
  if (auto text = ava::core::json::string_field(part, "text"); text && !text->empty())
  {
    state.parsed_content = true;
    events.push_back(
        StreamEvent{.type = StreamEventType::TextDelta, .text = *text, .tool_call_id = "", .tool_name = "", .error_message = "", .usage = std::nullopt});
  }
  if (auto const function_call = ava::core::json::object_field(part, "functionCall"))
  {
    append_function_call_events(events, *function_call, candidate_index, part_index, state);
  }
}

void append_response_events(std::vector<StreamEvent>& events, GeminiParseState& state, std::string_view body)
{
  if (ava::core::json::object_field(body, "error"))
  {
    state.error_seen = true;
    append_error_event(events, "Gemini provider reported an error");
    return;
  }
  if (auto usage = parse_gemini_usage(body))
  {
    if (!state.usage)
      state.usage = TokenUsage{};
    merge_usage(*state.usage, *usage);
  }
  if (auto block_reason = prompt_block_reason(body); !block_reason.empty())
  {
    state.finish_reason = normalize_provider_finish_reason(ProviderProtocol::Gemini, block_reason);
    if (state.finish_reason == ProviderFinishReason::Refusal)
    {
      // A prompt-level safety block is a semantic refusal even though Gemini
      // returns no candidate content.
      state.parsed_content = true;
    }
    else
    {
      state.error_seen = true;
      append_error_event(events, "Gemini prompt was blocked");
    }
    return;
  }

  auto const candidates = ava::core::json::strict_objects_in_array_field(body, "candidates", kMaxProviderParserArrayItems);
  if (ava::core::json::field_value_start(body, "candidates") && !candidates)
  {
    state.error_seen = true;
    append_error_event(events, "Gemini response parser limit exceeded");
    return;
  }
  for (std::size_t candidate_index = 0; candidates && candidate_index < candidates->size(); ++candidate_index)
  {
    auto const& candidate = candidates->at(candidate_index);
    if (auto const content = ava::core::json::object_field(candidate, "content"))
    {
      auto const parts = ava::core::json::strict_objects_in_array_field(*content, "parts", kMaxProviderParserArrayItems);
      if (ava::core::json::field_value_start(*content, "parts") && !parts)
      {
        state.error_seen = true;
        append_error_event(events, "Gemini response parser limit exceeded");
        return;
      }
      for (std::size_t part_index = 0; parts && part_index < parts->size(); ++part_index)
      {
        append_part_events(events, parts->at(part_index), candidate_index, part_index, state);
      }
    }
    if (auto const finish_reason = ava::core::json::string_field(candidate, "finishReason"); finish_reason && !finish_reason->empty())
    {
      state.finish_reason = normalized_finish_reason(*finish_reason);
    }
  }
}

void append_event_for_data(std::vector<StreamEvent>& events, GeminiParseState& state, bool& saw_data, bool& done_seen, bool& error_seen, std::string_view data)
{
  if (done_seen || error_seen)
    return;
  if (data == "[DONE]")
  {
    done_seen = true;
    append_done_event(events, std::move(state.usage), state.finish_reason);
    state.usage = std::nullopt;
    state.finish_reason = std::nullopt;
    return;
  }
  saw_data = true;
  if (!is_json_object_shape(data))
  {
    error_seen = true;
    append_error_event(events, "malformed Gemini SSE event");
    return;
  }
  append_response_events(events, state, data);
  if (state.error_seen)
    error_seen = true;
}

void append_events_for_sse_line(std::vector<StreamEvent>& events, GeminiParseState& state, bool& saw_data, bool& done_seen, bool& error_seen, std::string& data,
                                std::string line)
{
  if (!line.empty() && line.back() == '\r')
    line.pop_back();
  if (line.empty())
  {
    if (!data.empty())
    {
      append_event_for_data(events, state, saw_data, done_seen, error_seen, data);
      data.clear();
    }
    return;
  }
  if (line.starts_with("data:"))
  {
    if (!data.empty())
      data.push_back('\n');
    auto value = std::string_view(line).substr(5);
    if (!value.empty() && value.front() == ' ')
      value.remove_prefix(1);
    data.append(value);
  }
}

ava::core::Result<std::vector<StreamEvent>> gemini_http_error(ava::http::HttpResponse const& response)
{
  auto error = ava::core::Error(ava::core::ErrorCategory::Provider, "Gemini HTTP request failed with status " + std::to_string(response.status_code));
  error.with_context("status", std::to_string(response.status_code));
  error.with_context("provider_error_kind", to_string(classify_provider_error(response)));
  if (auto const retry_after = ava::http::retry_after_header(response))
    error.with_context("retry_after", *retry_after);
  return std::unexpected(std::move(error));
}

}  // namespace

GeminiProvider::GeminiProvider(std::string base_url) : base_url_(normalize_gemini_base_url(std::move(base_url)))
{
}

ava::core::Result<ava::http::HttpRequest> GeminiProvider::build_request(ProviderRequest const& request, std::string_view access_token) const
{
  auto model_path = gemini_model_path(request.model_id);
  if (!model_path)
    return std::unexpected(std::move(model_path.error()));
  if (access_token.empty())
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "Gemini API key is required"));
  }
  if (request.reasoning)
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "Gemini reasoning options are not supported"));
  }
  if (auto valid_parts = validate_gemini_content_parts(request); !valid_parts)
    return std::unexpected(std::move(valid_parts.error()));
  auto body = gemini_request_body_json(request);
  if (!body)
    return std::unexpected(std::move(body.error()));

  std::string url = base_url_ + "/v1beta/" + *model_path + (request.stream ? ":streamGenerateContent?alt=sse" : ":generateContent");
  return ava::http::HttpRequest{.method = "POST",
                                .url = std::move(url),
                                .headers = {{"x-goog-api-key", std::string(access_token)},
                                            {"Content-Type", "application/json"},
                                            {"Accept", request.stream ? "text/event-stream" : "application/json"}},
                                .body = std::move(*body),
                                .timeout_ms = 60000,
                                .follow_redirects = true,
                                .include_response_headers = false,
                                .resolve_hosts = {}};
}

std::unique_ptr<StreamParser> GeminiProvider::create_stream_parser() const
{
  return std::make_unique<GeminiStreamParser>();
}

ava::core::Result<std::vector<StreamEvent>> GeminiProvider::parse_response(ava::http::HttpResponse const& response, bool stream) const
{
  return stream ? parse_gemini_sse_response(response) : parse_gemini_response(response);
}

GeminiStreamParser::GeminiStreamParser() : fallback_tool_call_prefix_(ava::core::make_id("gemini_call"))
{
}

ava::core::Result<std::vector<StreamEvent>> GeminiStreamParser::append(std::string_view chunk)
{
  std::vector<StreamEvent> events;
  pending_line_.append(chunk);
  std::size_t line_start = 0;
  std::size_t search_from = scan_offset_;
  GeminiParseState state{.usage = usage_,
                         .finish_reason = finish_reason_,
                         .fallback_tool_call_prefix = fallback_tool_call_prefix_,
                         .parsed_content = false,
                         .error_seen = error_seen_};
  while (true)
  {
    auto const newline = pending_line_.find('\n', search_from);
    if (newline == std::string::npos)
      break;
    append_events_for_sse_line(events, state, saw_data_, done_seen_, error_seen_, data_, pending_line_.substr(line_start, newline - line_start));
    line_start = newline + 1;
    search_from = line_start;
  }
  if (line_start > 0)
    pending_line_.erase(0, line_start);
  scan_offset_ = pending_line_.size();
  usage_ = std::move(state.usage);
  finish_reason_ = state.finish_reason;
  error_seen_ = error_seen_ || state.error_seen;
  return events;
}

ava::core::Result<std::vector<StreamEvent>> GeminiStreamParser::finish()
{
  std::vector<StreamEvent> events;
  GeminiParseState state{.usage = usage_,
                         .finish_reason = finish_reason_,
                         .fallback_tool_call_prefix = fallback_tool_call_prefix_,
                         .parsed_content = false,
                         .error_seen = error_seen_};
  if (!pending_line_.empty())
  {
    append_events_for_sse_line(events, state, saw_data_, done_seen_, error_seen_, data_, std::move(pending_line_));
    pending_line_.clear();
  }
  scan_offset_ = 0;
  if (!data_.empty())
  {
    append_event_for_data(events, state, saw_data_, done_seen_, error_seen_, data_);
    data_.clear();
  }
  if (saw_data_ && !done_seen_ && !error_seen_ && !state.error_seen)
  {
    append_done_event(events, std::move(state.usage), state.finish_reason);
  }
  usage_ = std::nullopt;
  finish_reason_ = std::nullopt;
  saw_data_ = false;
  done_seen_ = false;
  error_seen_ = false;
  return events;
}

ava::core::Result<std::vector<StreamEvent>> parse_gemini_sse(std::string_view sse)
{
  GeminiStreamParser parser;
  auto events = parser.append(sse);
  if (!events)
    return std::unexpected(std::move(events.error()));
  auto final_events = parser.finish();
  if (!final_events)
    return std::unexpected(std::move(final_events.error()));
  events->insert(events->end(), final_events->begin(), final_events->end());
  return events;
}

ava::core::Result<std::vector<StreamEvent>> parse_gemini_sse_response(ava::http::HttpResponse const& response)
{
  if (response.status_code < 200 || response.status_code >= 300)
    return gemini_http_error(response);
  return parse_gemini_sse(response.body);
}

ava::core::Result<std::vector<StreamEvent>> parse_gemini_response(ava::http::HttpResponse const& response)
{
  if (response.status_code < 200 || response.status_code >= 300)
    return gemini_http_error(response);
  std::vector<StreamEvent> events;
  GeminiParseState state{.fallback_tool_call_prefix = ava::core::make_id("gemini_call")};
  if (!is_json_object_shape(response.body))
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Provider, "Gemini response was malformed JSON"));
  append_response_events(events, state, response.body);
  if (state.error_seen)
    return events;
  if (!state.parsed_content)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Provider, "Gemini response content is missing");
    if (state.finish_reason.has_value())
      error.with_context("finish_reason", std::string(to_string(*state.finish_reason)));
    return std::unexpected(std::move(error));
  }
  append_done_event(events, std::move(state.usage), state.finish_reason);
  return events;
}

std::optional<TokenUsage> parse_gemini_usage(std::string_view body)
{
  auto const usage_object = ava::core::json::object_field(body, "usageMetadata");
  auto const usage_view = usage_object ? std::string_view(*usage_object) : body;
  TokenUsage usage;
  usage.input_tokens = non_negative_integer_field(usage_view, "promptTokenCount");
  usage.output_tokens = non_negative_integer_field(usage_view, "candidatesTokenCount");
  usage.reasoning_tokens = non_negative_integer_field(usage_view, "thoughtsTokenCount");
  usage.cache_read_tokens = non_negative_integer_field(usage_view, "cachedContentTokenCount");
  usage.total_tokens = non_negative_integer_field(usage_view, "totalTokenCount");
  if (!usage.input_tokens && !usage.output_tokens && !usage.reasoning_tokens && !usage.cache_read_tokens && !usage.total_tokens)
    return std::nullopt;
  return usage;
}

}  // namespace ava::provider
