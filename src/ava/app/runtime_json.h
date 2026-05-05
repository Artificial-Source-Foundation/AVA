#pragma once

#include "ava/app/runtime.h"

#include "ava/core/result.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ava::app::runtime {

[[nodiscard]] std::string_view trim(std::string_view value);
[[nodiscard]] std::string trimmed_copy(std::string_view value);

[[nodiscard]] std::string json_string_field(std::string_view key, std::string_view value);
[[nodiscard]] std::string json_bool_field(std::string_view key, bool value);
[[nodiscard]] std::string optional_bool_json(std::string_view key, std::optional<bool> const& value);
[[nodiscard]] std::string optional_integer_json(std::string_view key, std::optional<long long> const& value);
[[nodiscard]] std::string string_array_json(std::vector<std::string> const& values);
[[nodiscard]] std::vector<std::string> string_array_field(std::string_view object, std::string_view key);
[[nodiscard]] std::optional<bool> bool_json_field(std::string_view object, std::string_view key);

[[nodiscard]] ava::core::VoidResult append_session_start(ava::session::SessionStore& store, ava::agent::Mode mode,
                                                         ava::config::ModelInfo const& model,
                                                         ava::config::PromptSelection const& prompt,
                                                         std::size_t context_source_count);
[[nodiscard]] ava::core::VoidResult append_model_change(ava::session::SessionStore& store,
                                                        ava::config::ModelInfo const& previous,
                                                        ava::config::ModelInfo const& current);
[[nodiscard]] ava::core::VoidResult append_reasoning_change(ava::session::SessionStore& store,
                                                            ava::config::ModelInfo const& model,
                                                            std::optional<RuntimeReasoningSelection> const& selection);

}  // namespace ava::app::runtime
