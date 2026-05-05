#pragma once

#include <cstddef>
#include <filesystem>
#include <iosfwd>
#include <string>
#include <string_view>

#include "ava/core/error.h"
#include "ava/core/result.h"
#include "ava/session/session_store.h"

namespace ava::session {

inline constexpr std::size_t kMaxSessionLineBytes = 1024 * 1024;

[[nodiscard]] ava::core::VoidResult validate_session_id(std::string_view session_id);
[[nodiscard]] ava::core::VoidResult validate_parent_id(std::string_view parent_id, std::string_view entry_id);
[[nodiscard]] ava::core::Result<bool> read_limited_session_line(std::istream& file, std::string& line);
[[nodiscard]] ava::core::Result<long long> read_supported_entry_version(std::string_view line,
                                                                        std::filesystem::path const& path);
[[nodiscard]] bool is_unsupported_session_version_error(ava::core::Error const& error);
[[nodiscard]] ava::core::Result<SessionEntry> decode_session_entry_line(std::string_view line,
                                                                        std::filesystem::path const& path);
[[nodiscard]] ava::core::Result<std::string> encode_session_entry_line(SessionEntry const& entry);

}  // namespace ava::session
