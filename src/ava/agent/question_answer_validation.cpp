#include "ava/agent/question_answer_validation.h"

#include <cstddef>
#include <string>
#include <utility>

namespace ava::agent {
namespace {

constexpr std::size_t kMaxQuestionAnswerSelectedOptions = 64;
constexpr std::size_t kMaxQuestionAnswerStringBytes = 8192;

ava::core::Error question_answer_error(std::string_view tool_name, std::string_view field, std::string message)
{
  auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, std::move(message));
  error.with_context("tool", std::string(tool_name));
  error.with_context("field", std::string(field));
  return error;
}

}  // namespace

ava::core::VoidResult validate_question_answer(QuestionAnswer const& answer, std::string_view tool_name)
{
  if (answer.selected_options.size() > kMaxQuestionAnswerSelectedOptions) {
    auto error = question_answer_error(tool_name, "selected_options", "question answer has too many selected options");
    error.with_context("max_options", std::to_string(kMaxQuestionAnswerSelectedOptions));
    return std::unexpected(std::move(error));
  }
  for (std::size_t index = 0; index < answer.selected_options.size(); ++index) {
    if (answer.selected_options[index].size() > kMaxQuestionAnswerStringBytes) {
      auto error = question_answer_error(tool_name, "selected_options", "question answer selected option is too long");
      error.with_context("index", std::to_string(index));
      error.with_context("max_bytes", std::to_string(kMaxQuestionAnswerStringBytes));
      return std::unexpected(std::move(error));
    }
  }
  if (answer.custom_text.size() > kMaxQuestionAnswerStringBytes) {
    auto error = question_answer_error(tool_name, "custom_text", "question answer custom text is too long");
    error.with_context("max_bytes", std::to_string(kMaxQuestionAnswerStringBytes));
    return std::unexpected(std::move(error));
  }
  return {};
}

}  // namespace ava::agent
