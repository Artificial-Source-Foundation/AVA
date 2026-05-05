#include <filesystem>
#include <fstream>
#include <regex>
#include <string>

#include "ava/tools/search_tools_support.h"
#include "tests/support/test_harness.h"

namespace {

void write_bytes(std::filesystem::path const& path, std::string const& bytes)
{
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void test_search_glob_to_regex()
{
  expect(ava::tools::detail::search_regex_escape('.') == "\\.", "search support escapes regex metacharacters");
  expect(ava::tools::detail::search_regex_escape('x') == "x", "search support leaves plain characters unescaped");

  auto matcher = ava::tools::detail::search_glob_to_regex("src/**/*.cpp");
  expect(matcher && std::regex_match(std::string("src/main.cpp"), *matcher) &&
             std::regex_match(std::string("src/lib/main.cpp"), *matcher) &&
             !std::regex_match(std::string("src/lib/main.h"), *matcher),
         "search support converts simple recursive globs to regex matchers");

  auto bracket = ava::tools::detail::search_glob_to_regex("*.[ch]");
  expect(!bracket && bracket.error().format().find("bracket") != std::string::npos,
         "search support rejects unsupported bracket glob syntax");
}

void test_search_path_and_binary_helpers()
{
  auto const root = temp_root() / "search-support-paths";
  auto const path = root / "nested" / "file.txt";
  expect(ava::tools::detail::search_relative_slash_path(root, path) == "nested/file.txt",
         "search support formats relative slash paths");

  expect(!ava::tools::detail::search_looks_binary("plain text"), "search support treats text without NUL as text");
  expect(ava::tools::detail::search_looks_binary(std::string_view("a\0b", 3)),
         "search support detects NUL bytes as binary");
}

void test_search_cancellation_and_tool_name_helpers()
{
  ava::tools::ToolContext default_context{.workspace_dir = temp_root()};
  ava::tools::ToolContext named_context{.workspace_dir = temp_root(), .permission_tool_name = "custom-search"};
  expect(ava::tools::detail::search_permission_tool_name(default_context) == "search",
         "search support uses default permission tool name");
  expect(ava::tools::detail::search_permission_tool_name(named_context) == "custom-search",
         "search support preserves custom permission tool names");

  ava::tools::ToolContext canceled{.workspace_dir = temp_root(), .cancel_requested = [] { return true; }};
  auto cancel_result = ava::tools::detail::check_search_canceled(canceled, "grep");
  expect(!cancel_result && cancel_result.error().format().find("grep") != std::string::npos,
         "search support reports cancellation with tool context");
}

void test_read_limited_search_line()
{
  auto const root = temp_root() / "search-support-lines";
  auto const path = root / "sample.txt";
  write_bytes(path, "abcdef\nlast");

  std::ifstream input(path, std::ios::binary);
  std::string line;
  bool truncated = false;
  bool binary = false;
  auto first = ava::tools::detail::read_limited_search_line(input, line, path, 4, truncated, binary);
  expect(first && *first && line == "abcd" && truncated && !binary,
         "search support reads and marks overlong lines as truncated");

  auto second = ava::tools::detail::read_limited_search_line(input, line, path, 10, truncated, binary);
  expect(second && *second && line == "last" && !truncated && !binary, "search support reads final unterminated lines");

  auto end = ava::tools::detail::read_limited_search_line(input, line, path, 10, truncated, binary);
  expect(end && !*end, "search support reports EOF after the final line");
}

void test_read_limited_search_line_detects_binary_after_truncation()
{
  auto const root = temp_root() / "search-support-binary";
  auto const path = root / "binary.txt";
  write_bytes(path, std::string("abc\0def\n", 8));

  std::ifstream input(path, std::ios::binary);
  std::string line;
  bool truncated = false;
  bool binary = false;
  auto read = ava::tools::detail::read_limited_search_line(input, line, path, 3, truncated, binary);
  expect(read && *read && line == "abc" && truncated && binary,
         "search support detects NUL bytes even after the retained line cap");
}

void test_search_match_permission_helper_allows_safe_workspace_file()
{
  auto const root = temp_root() / "search-support-permission";
  auto const path = root / "sample.txt";
  write_bytes(path, "content\n");
  ava::tools::ToolContext context{.workspace_dir = root};

  auto can_read = ava::tools::detail::can_read_search_match(context, path);
  expect(can_read && *can_read, "search support allows regular readable workspace matches");
}

}  // namespace

void run_search_tools_support_tests()
{
  test_search_glob_to_regex();
  test_search_path_and_binary_helpers();
  test_search_cancellation_and_tool_name_helpers();
  test_read_limited_search_line();
  test_read_limited_search_line_detects_binary_after_truncation();
  test_search_match_permission_helper_allows_safe_workspace_file();
}
