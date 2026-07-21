#pragma once

#include "ava/app/runtime.h"
#include "ava/core/result.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ava::app::runtime {

[[nodiscard]] std::string json_string_field(std::string_view key, std::string_view value);
[[nodiscard]] std::string json_bool_field(std::string_view key, bool value);
[[nodiscard]] std::string optional_bool_json(std::string_view key, std::optional<bool> const& value);
[[nodiscard]] std::string optional_integer_json(std::string_view key, std::optional<long long> const& value);
[[nodiscard]] std::string string_array_json(std::vector<std::string> const& values);
[[nodiscard]] std::vector<std::string> string_array_field(std::string_view object, std::string_view key);
[[nodiscard]] std::optional<bool> bool_json_field(std::string_view object, std::string_view key);

[[nodiscard]] ava::core::VoidResult append_session_start(ava::session::SessionStore& store, ava::session::SessionLease const& lease,
                                                         ava::agent::Mode mode, ava::config::ModelInfo const& model,
                                                         BasePromptMetadata const& base_prompt, std::size_t context_source_count,
                                                         std::filesystem::path const& original_cwd = {});
[[nodiscard]] ava::core::VoidResult append_session_start_ephemeral(ava::session::SessionStore& store, ava::agent::Mode mode,
                                                                   ava::config::ModelInfo const& model, BasePromptMetadata const& base_prompt,
                                                                   std::size_t context_source_count, std::filesystem::path const& original_cwd = {});
[[nodiscard]] ava::session::SessionEntry make_model_change_entry(ava::config::ModelInfo const& previous, ava::config::ModelInfo const& current);
[[nodiscard]] ava::core::VoidResult append_model_change(ava::session::SessionStore& store, ava::session::SessionLease const& lease,
                                                        ava::config::ModelInfo const& previous, ava::config::ModelInfo const& current);
[[nodiscard]] ava::core::VoidResult append_model_change_ephemeral(ava::session::SessionStore& store, ava::config::ModelInfo const& previous,
                                                                  ava::config::ModelInfo const& current);
[[nodiscard]] ava::session::SessionEntry make_reasoning_change_entry(ava::config::ModelInfo const& model,
                                                                     std::optional<ReasoningSelection> const& selection);
[[nodiscard]] ava::core::VoidResult append_reasoning_change(ava::session::SessionStore& store, ava::session::SessionLease const& lease,
                                                            ava::config::ModelInfo const& model,
                                                            std::optional<ReasoningSelection> const& selection);
[[nodiscard]] ava::core::VoidResult append_reasoning_change_ephemeral(ava::session::SessionStore& store, ava::config::ModelInfo const& model,
                                                                      std::optional<ReasoningSelection> const& selection);

}  // namespace ava::app::runtime
