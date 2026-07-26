#pragma once

#include "ava/session/validation.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace ava::test {
bool session_replay_has_issue(session::SessionReplayValidation const& validation, session::SessionReplayIssueKind kind);
std::string session_test_tiny_png_bytes();
void write_session_test_binary_file(std::filesystem::path const& path, std::string_view bytes);
std::string read_session_test_binary_file(std::filesystem::path const& path);
}  // namespace ava::test
