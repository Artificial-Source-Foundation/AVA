#include <unistd.h>

#include <array>
#include <string>
#include <vector>

#include "ava/tools/bash_tool_support.h"
#include "tests/support/test_harness.h"

namespace {

void test_parse_command_argv_splits_safe_commands()
{
  auto argv = ava::tools::detail::parse_command_argv("git diff \"src/main.cpp\" docs\\ plan.md 'literal value'");
  std::vector<std::string> const expected{"git", "diff", "src/main.cpp", "docs plan.md", "literal value"};
  expect(argv && *argv == expected, "bash support parses quoted and escaped command arguments");
}

void test_parse_command_argv_rejects_unsupported_shell_syntax()
{
  expect(ava::tools::detail::is_shell_metacharacter(';'), "bash support recognizes shell metacharacters");
  expect(!ava::tools::detail::is_shell_metacharacter('x'), "bash support leaves ordinary characters unclassified");

  auto chained = ava::tools::detail::parse_command_argv("pwd; rm -rf important");
  expect(!chained && chained.error().format().find("shell metacharacters") != std::string::npos,
         "bash support rejects shell metacharacters");

  auto unterminated_quote = ava::tools::detail::parse_command_argv("printf \"unterminated");
  expect(!unterminated_quote && unterminated_quote.error().format().find("unterminated") != std::string::npos,
         "bash support rejects unterminated quoted input");

  auto trailing_escape = ava::tools::detail::parse_command_argv("printf value\\");
  expect(!trailing_escape && trailing_escape.error().format().find("unterminated") != std::string::npos,
         "bash support rejects unterminated escapes");

  auto empty = ava::tools::detail::parse_command_argv("   ");
  expect(!empty && empty.error().message() == "command must not be empty", "bash support rejects empty commands");
}

void test_append_tail_tracks_bounds_and_total_bytes()
{
  ava::tools::BashResult result;
  ava::tools::detail::append_tail(result, "abc", 5);
  ava::tools::detail::append_tail(result, "def", 5);
  expect(result.output == "bcdef" && result.total_bytes == 6 && result.truncated,
         "bash support keeps only the bounded output tail");

  ava::tools::BashResult large_chunk;
  ava::tools::detail::append_tail(large_chunk, "0123456789", 4);
  expect(large_chunk.output == "6789" && large_chunk.total_bytes == 10 && large_chunk.truncated,
         "bash support keeps the tail of a single oversized chunk");

  ava::tools::BashResult zero_cap;
  ava::tools::detail::append_tail(zero_cap, "abc", 0);
  expect(zero_cap.output.empty() && zero_cap.total_bytes == 3 && zero_cap.truncated,
         "bash support treats zero retained bytes as truncation after output");
}

void test_pipe_and_fd_helpers()
{
  auto pipe_fds = ava::tools::detail::make_pipe();
  expect(pipe_fds.has_value(), "bash support creates process pipes");
  if (!pipe_fds) {
    return;
  }

  ava::tools::detail::UniqueFd read_fd((*pipe_fds)[0]);
  ava::tools::detail::UniqueFd write_fd((*pipe_fds)[1]);
  expect(read_fd.get() >= 0 && write_fd.get() >= 0, "bash support fd wrapper owns valid descriptors");

  expect(::write(write_fd.get(), "ok", 2) == 2, "test writes through owned pipe fd");
  write_fd.reset();

  std::array<char, 4> buffer{};
  auto const bytes = ava::tools::detail::read_retry(read_fd.get(), buffer.data(), buffer.size());
  expect(bytes == 2 && std::string(buffer.data(), static_cast<std::size_t>(bytes)) == "ok",
         "bash support read retry helper reads pipe data");

  int const released = read_fd.release();
  expect(released >= 0 && read_fd.get() == -1, "bash support fd wrapper releases ownership");
  ::close(released);
}

void test_cancellation_helper()
{
  ava::tools::ToolContext not_canceled;
  ava::tools::ToolContext canceled{.workspace_dir = temp_root(), .cancel_requested = [] { return true; }};
  expect(!ava::tools::detail::is_bash_canceled(not_canceled),
         "bash support treats missing cancellation callback as active");
  expect(ava::tools::detail::is_bash_canceled(canceled), "bash support observes tool cancellation callbacks");
}

}  // namespace

void run_bash_tool_support_tests()
{
  test_parse_command_argv_splits_safe_commands();
  test_parse_command_argv_rejects_unsupported_shell_syntax();
  test_append_tail_tracks_bounds_and_total_bytes();
  test_pipe_and_fd_helpers();
  test_cancellation_helper();
}
