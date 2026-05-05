#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include "ava/app/line_shell_support.h"
#include "tests/support/test_harness.h"

namespace {

void test_compact_command_detection()
{
  expect(ava::app::line_shell::detail::is_compact_command("/compact") &&
             ava::app::line_shell::detail::is_compact_command("/compact now") &&
             !ava::app::line_shell::detail::is_compact_command("/compactly") &&
             !ava::app::line_shell::detail::is_compact_command("/compact\t"),
         "line shell support recognizes only exact slash compact commands and space-separated arguments");
}

void test_compact_token_total()
{
  ava::session::SessionStats stats;
  expect(!ava::app::line_shell::detail::compact_token_total(stats),
         "line shell support returns no compact token total when no token fields exist");

  stats.input_tokens = 100;
  stats.output_tokens = 50;
  stats.reasoning_tokens = 25;
  stats.cache_read_tokens = 10;
  stats.cache_write_tokens = 5;
  expect(ava::app::line_shell::detail::compact_token_total(stats) == 190,
         "line shell support sums fallback token components");

  stats.total_tokens = 999;
  expect(ava::app::line_shell::detail::compact_token_total(stats) == 999,
         "line shell support prefers explicit total tokens over component sums");
}

void test_compact_token_count_formatting()
{
  expect(ava::app::line_shell::detail::format_compact_token_count(999) == "999" &&
             ava::app::line_shell::detail::format_compact_token_count(1000) == "1k" &&
             ava::app::line_shell::detail::format_compact_token_count(1530) == "1.5k" &&
             ava::app::line_shell::detail::format_compact_token_count(1'250'000) == "1.2m",
         "line shell support formats compact token counts with stable suffixes");
}

void test_context_window_percent()
{
  expect(!ava::app::line_shell::detail::format_context_window_percent(100, std::nullopt) &&
             !ava::app::line_shell::detail::format_context_window_percent(100, 0),
         "line shell support omits context percentages without a positive context window");
  expect(ava::app::line_shell::detail::format_context_window_percent(0, 1000) == std::string("0.0%"),
         "line shell support formats zero-token context usage");
  expect(ava::app::line_shell::detail::format_context_window_percent(1, 2000) == std::string("<0.1%"),
         "line shell support formats tiny nonzero context usage");
  expect(ava::app::line_shell::detail::format_context_window_percent(500, 1000) == std::string("50.0%"),
         "line shell support formats regular context usage percentages");
}

void test_compact_token_status()
{
  ava::session::SessionStats stats;
  expect(!ava::app::line_shell::detail::compact_token_status(stats, 1000),
         "line shell support omits token status when no token estimate exists");

  stats.total_tokens = 1500;
  expect(ava::app::line_shell::detail::compact_token_status(stats, 3000) == std::string("1.5k (50.0%)"),
         "line shell support combines compact token counts with context percentages");
}

void test_git_branch_detection()
{
  auto const root = temp_root() / "line-shell-support";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const git_dir = root / ".git";
  std::filesystem::create_directories(git_dir);

  {
    std::ofstream head(git_dir / "HEAD", std::ios::binary | std::ios::trunc);
    head << "ref: refs/heads/feature/backend-contract\n";
  }
  expect(ava::app::line_shell::detail::git_branch_for_workspace(root) == "feature/backend-contract",
         "line shell support reads symbolic git branches");

  {
    std::ofstream head(git_dir / "HEAD", std::ios::binary | std::ios::trunc);
    head << "0123456789abcdef\n";
  }
  expect(ava::app::line_shell::detail::git_branch_for_workspace(root) == "0123456789ab",
         "line shell support truncates detached git heads");

  std::filesystem::remove_all(git_dir, remove_error);
  expect(ava::app::line_shell::detail::git_branch_for_workspace(root).empty(),
         "line shell support returns no branch when git HEAD is absent");
}

}  // namespace

void run_app_line_shell_support_tests()
{
  test_compact_command_detection();
  test_compact_token_total();
  test_compact_token_count_formatting();
  test_context_window_percent();
  test_compact_token_status();
  test_git_branch_detection();
}
