#include "sys.h"
#include "tests/support/session_test_support.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <ranges>
#include <string>
#include <string_view>

namespace ava::tests {
bool session_replay_has_issue(session::SessionReplayValidation const& validation, session::SessionReplayIssueKind kind)
{
  return std::ranges::any_of(validation.issues, [kind](session::SessionReplayIssue const& issue) { return issue.kind == kind; });
}

std::string session_test_tiny_png_bytes()
{
  std::string bytes;
  bytes.push_back(static_cast<char>(0x89));
  bytes += "PNG\r\n";
  bytes.push_back(static_cast<char>(0x1A));
  bytes += "\nava-image";
  return bytes;
}

void write_session_test_binary_file(std::filesystem::path const& path, std::string_view bytes)
{
  std::filesystem::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::string read_session_test_binary_file(std::filesystem::path const& path)
{
  std::ifstream file(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}
}  // namespace ava::tests
