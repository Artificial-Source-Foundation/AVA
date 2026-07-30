#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ava::app {

struct CommandResult;

void add_output(CommandResult& result, std::string text);
[[nodiscard]] CommandResult handled_text(std::string text);

[[nodiscard]] std::string display_path(std::filesystem::path const& path, std::filesystem::path const& base);
[[nodiscard]] std::string sanitize_inline_text(std::string text);
[[nodiscard]] std::string joined_strings(std::vector<std::string> const& values, std::string_view separator);
[[nodiscard]] std::string missing_argument(std::string_view usage);
[[nodiscard]] std::string command_argument(std::string_view line, std::string_view command);
[[nodiscard]] std::vector<std::string> split_command_arguments(std::string_view text);

// Exact-id payload after the family prefix (text following the first '_').
// Used to derive stable short completion refs without surfacing full authority ids.
[[nodiscard]] std::string_view id_payload_after_family_prefix(std::string_view id) noexcept;

// Deterministic short unique refs for a displayed completion set. Each ref is a
// prefix of id_payload_after_family_prefix(id), starting at min_length (default 6)
// and extending only until unique within `ids`. Same id always yields the same
// ref for a given set; distinct ids never share a ref. Parallel to `ids`.
[[nodiscard]] std::vector<std::string> unique_short_id_refs(std::vector<std::string> const& ids, std::size_t min_length = 6);

}  // namespace ava::app
