#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "ava/core/result.h"

namespace ava::agent {

struct QuestionOption {
  std::string value;
  std::string label;
};

struct QuestionPrompt {
  std::string header;
  std::string question;
  std::vector<QuestionOption> options;
  bool multiple = false;
  bool allow_custom = false;
  bool secret = false;
  bool modal = false;
  bool searchable = false;
};

struct QuestionAnswer {
  std::vector<std::string> selected_options;
  std::string custom_text;
};

using QuestionResolver = std::function<ava::core::Result<QuestionAnswer>(QuestionPrompt const&)>;

[[nodiscard]] ava::core::Result<QuestionPrompt> parse_question_prompt(std::string_view arguments_json,
                                                                      std::string_view tool_name);
[[nodiscard]] std::string serialize_question_answer_result(QuestionPrompt const& prompt, QuestionAnswer const& answer);

}  // namespace ava::agent
