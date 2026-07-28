#include "sys.h"
#include "ava/agent/question.h"
#include "ava/agent/tool_dispatch_common.h"
#include "ava/agent/tool_dispatch_question.h"

#include <string>
#include <string_view>

namespace ava::agent {
namespace {

using namespace ava::agent::tool_dispatch;

constexpr std::size_t kMaxQuestionAnswerSelectedOptions = 64;
constexpr std::size_t kMaxQuestionAnswerStringBytes = 8192;

ava::core::Error question_answer_error(std::string_view tool_name, std::string_view field, std::string message)
{
  auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, std::move(message));
  error.with_context("tool", std::string(tool_name));
  error.with_context("field", std::string(field));
  return error;
}

ava::core::VoidResult validate_question_answer(QuestionAnswer const& answer, std::string_view tool_name)
{
  if (answer.selected_options.size() > kMaxQuestionAnswerSelectedOptions)
  {
    auto error = question_answer_error(tool_name, "selected_options", "question answer has too many selected options");
    error.with_context("max_options", std::to_string(kMaxQuestionAnswerSelectedOptions));
    return std::unexpected(std::move(error));
  }
  for (std::size_t index = 0; index < answer.selected_options.size(); ++index)
  {
    if (answer.selected_options[index].size() > kMaxQuestionAnswerStringBytes)
    {
      auto error = question_answer_error(tool_name, "selected_options", "question answer selected option is too long");
      error.with_context("index", std::to_string(index));
      error.with_context("max_bytes", std::to_string(kMaxQuestionAnswerStringBytes));
      return std::unexpected(std::move(error));
    }
  }
  if (answer.custom_text.size() > kMaxQuestionAnswerStringBytes)
  {
    auto error = question_answer_error(tool_name, "custom_text", "question answer custom text is too long");
    error.with_context("max_bytes", std::to_string(kMaxQuestionAnswerStringBytes));
    return std::unexpected(std::move(error));
  }
  return {};
}

}  // namespace

ToolDispatchResult question_result(ava::tools::ToolContext const&, ToolDispatchServices const& services, ProviderToolCall const& call)
{
  auto prompt = parse_question_prompt(call.arguments_json, call.name);
  if (!prompt)
    return tool_error_result(call, prompt.error());
  if (!services.question_resolver)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Tool, "question resolver is unavailable");
    error.with_context("tool", call.name);
    return tool_error_result(call, error);
  }
  auto answer = services.question_resolver(*prompt);
  if (!answer)
    return tool_error_result(call, answer.error());
  if (auto valid_answer = validate_question_answer(*answer, call.name); !valid_answer)
  {
    return tool_error_result(call, valid_answer.error());
  }
  return ToolDispatchResult{.call_id = call.id, .name = call.name, .success = true, .result_text = serialize_question_answer_result(*prompt, *answer)};
}

}  // namespace ava::agent
