#pragma once

#include <string_view>

#include "ava/agent/question.h"
#include "ava/core/result.h"

namespace ava::agent {

[[nodiscard]] ava::core::VoidResult validate_question_answer(QuestionAnswer const& answer, std::string_view tool_name);

}  // namespace ava::agent
