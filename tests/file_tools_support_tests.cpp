#include <filesystem>
#include <fstream>
#include <string>

#include "ava/tools/file_tools_support.h"
#include "tests/support/test_harness.h"

namespace {

void write_text(std::filesystem::path const& path, std::string const& text)
{
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output << text;
}

void test_file_tool_support_reads_bounded_text()
{
  auto const root = temp_root() / "file-tool-support-read";
  auto const path = root / "sample.txt";
  write_text(path, "abcdef");
  ava::tools::ToolContext context{.workspace_dir = root};

  auto all = ava::tools::detail::read_all_text(context, path, "test_read");
  auto head = ava::tools::detail::read_head_text(context, path, 4);
  expect(all && *all == "abcdef", "file tool support reads full file text");
  expect(head && head->content == "abcd" && head->total_bytes == 6 && head->output_bytes == 4 && head->truncated,
         "file tool support reads bounded file text and reports truncation");
}

void test_file_tool_support_cancellation_and_errors()
{
  auto const root = temp_root() / "file-tool-support-cancel";
  auto const path = root / "sample.txt";
  ava::tools::ToolContext context{.workspace_dir = root, .cancel_requested = [] { return true; }};

  auto canceled = ava::tools::detail::check_canceled(context, "test_cancel", path);
  expect(!canceled && canceled.error().message() == "tool canceled" &&
             canceled.error().format().find("test_cancel") != std::string::npos,
         "file tool support reports operation context for cancellation");
  expect(ava::tools::detail::is_canceled_error(canceled.error()), "file tool support recognizes cancellation errors");
  expect(ava::tools::detail::errno_cause(0) == "stream operation failed",
         "file tool support has a default stream failure cause");
}

void test_file_tool_support_staged_write_helpers()
{
  auto const root = temp_root() / "file-tool-support-write";
  auto const path = root / "nested" / "sample.txt";
  ava::tools::ToolContext context{.workspace_dir = root};

  expect(ava::tools::detail::write_parent_path(std::filesystem::path("file.txt")) == ".",
         "file tool support resolves parent path for root-level writes");
  auto temp_path = ava::tools::detail::unique_write_temp_path(path);
  expect(temp_path.parent_path() == path.parent_path() &&
             temp_path.filename().string().find(".sample.txt.ava-write-") == 0,
         "file tool support creates staged temp paths next to the target");

  auto written = ava::tools::detail::write_file_unlocked(context, path, "hello");
  auto read_back = ava::tools::detail::read_all_text(context, path, "test_read_back");
  expect(written && written->path == path && written->bytes_written == 5 && read_back && *read_back == "hello",
         "file tool support writes through staged atomic helper");
}

void test_file_tool_support_permission_diff_preview()
{
  auto const root = temp_root() / "file-tool-support-preview";
  auto const path = root / "sample.txt";
  write_text(path, "before\n");
  ava::tools::ToolContext context{.workspace_dir = root};

  auto preview = ava::tools::detail::write_permission_diff_preview(context, path, "after\n");
  expect(preview && preview->has_value() && !(*preview)->truncated &&
             (*preview)->text.find("-before") != std::string::npos &&
             (*preview)->text.find("+after") != std::string::npos,
         "file tool support builds permission diff previews for editable workspace files");
}

}  // namespace

void run_file_tools_support_tests()
{
  test_file_tool_support_reads_bounded_text();
  test_file_tool_support_cancellation_and_errors();
  test_file_tool_support_staged_write_helpers();
  test_file_tool_support_permission_diff_preview();
}
