#pragma once

#include "ava/app/runtime.h"
#include "ava/session/session_metadata.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ava::app::session_command_support {

[[nodiscard]] std::string trim_ascii(std::string_view text);
[[nodiscard]] std::string lower_ascii(std::string_view text);
[[nodiscard]] bool contains_ascii_case_insensitive(std::string_view text, std::string_view query);
[[nodiscard]] ava::core::Result<std::vector<ava::session::SessionEntry>> load_runtime_entries(runtime::Session const& session);
[[nodiscard]] std::string labels_text(std::vector<std::string> const& labels);
[[nodiscard]] std::string shorten_middle(std::string text, std::size_t max_columns);
[[nodiscard]] ava::core::Result<runtime::Session> reopen_session(runtime::Session const& current, std::string_view session_id);
[[nodiscard]] runtime::OpenOptions owned_replacement_options(runtime::Session const& current);

}  // namespace ava::app::session_command_support
