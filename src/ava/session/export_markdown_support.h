#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "ava/session/export.h"

namespace ava::session::detail {

[[nodiscard]] std::string export_json_string(std::string_view value);
[[nodiscard]] std::size_t export_longest_backtick_run(std::string_view text) noexcept;
[[nodiscard]] std::string export_fence_for(std::string_view text);
[[nodiscard]] std::string sanitize_fenced_export_content(std::string_view content);

void append_export_heading(std::string& out, std::string_view heading);
void append_export_fenced_block(std::string& out, std::string_view label, std::string_view content,
                                std::string_view language = "text");
void append_optional_export_fenced_block(std::string& out, std::string_view label,
                                         std::optional<std::string> const& content, std::string_view language = "text");

[[nodiscard]] std::string export_metadata_json(SessionEntry const& entry);
void append_export_metadata(std::string& out, SessionEntry const& entry, ExportOptions const& options);

[[nodiscard]] std::optional<std::string> export_string_field(SessionEntry const& entry, std::string_view key);
[[nodiscard]] std::optional<long long> export_integer_field(SessionEntry const& entry, std::string_view key);
[[nodiscard]] std::optional<std::string> export_object_field(SessionEntry const& entry, std::string_view key);
[[nodiscard]] bool export_bool_field_is_true(SessionEntry const& entry, std::string_view key);
[[nodiscard]] std::string export_success_text(SessionEntry const& entry);

}  // namespace ava::session::detail
