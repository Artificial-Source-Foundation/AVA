#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ava/app/runtime.h"
#include "ava/core/result.h"

namespace ava::app::runtime {

[[nodiscard]] std::string_view trim(std::string_view value);
[[nodiscard]] std::string trimmed_copy(std::string_view value);

[[nodiscard]] std::string json_string_field(std::string_view key, std::string_view value);
[[nodiscard]] std::string json_bool_field(std::string_view key, bool value);
[[nodiscard]] std::string optional_bool_json(std::string_view key, const std::optional<bool>& value);
[[nodiscard]] std::string optional_integer_json(std::string_view key, const std::optional<long long>& value);
[[nodiscard]] std::string string_array_json(const std::vector<std::string>& values);
[[nodiscard]] std::vector<std::string> string_array_field(std::string_view object, std::string_view key);
[[nodiscard]] std::optional<bool> bool_json_field(std::string_view object, std::string_view key);

[[nodiscard]] ava::core::VoidResult append_session_start(ava::session::SessionStore& store, ava::agent::Mode mode,
                                                         const ava::config::ModelInfo& model,
                                                         const ava::config::PromptSelection& prompt,
                                                         std::size_t context_source_count);
[[nodiscard]] ava::core::VoidResult append_model_change(ava::session::SessionStore& store,
                                                        const ava::config::ModelInfo& previous,
                                                        const ava::config::ModelInfo& current);
[[nodiscard]] ava::core::VoidResult append_reasoning_change(
    ava::session::SessionStore& store, const ava::config::ModelInfo& model,
    const std::optional<RuntimeReasoningSelection>& selection);

}  // namespace ava::app::runtime
