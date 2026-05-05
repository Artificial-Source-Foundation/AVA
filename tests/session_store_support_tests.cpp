#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "ava/session/session_entry_codec.h"
#include "ava/session/session_store_support.h"
#include "tests/support/test_harness.h"

namespace {

int file_mode(std::filesystem::path const& path)
{
  struct stat status {};
  if (::stat(path.c_str(), &status) != 0) return -1;
  return status.st_mode & 0777;
}

std::filesystem::path fresh_root(std::string name)
{
  auto root = temp_root() / std::move(name);
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  return root;
}

void write_text(std::filesystem::path const& path, std::string const& text)
{
  std::filesystem::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file << text;
}

void test_project_key_and_paths()
{
  auto const root = fresh_root("session-store-support-paths");
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);

  auto const key = ava::session::detail::project_key(workspace);
  expect(!key.empty() && key == ava::session::detail::project_key(workspace),
         "session store support derives stable project keys from workspace paths");

  auto const directory = ava::session::detail::session_project_directory(root / "sessions", workspace);
  auto const path = ava::session::detail::session_file_path(root / "sessions", workspace, "session_test");
  expect(path.parent_path() == directory && path.filename() == "session_test.jsonl",
         "session store support derives session file paths under project directories");
}

void test_directory_and_file_safety_helpers()
{
  auto const root = fresh_root("session-store-support-safety");
  auto const session_dir = root / "sessions" / "project";
  auto made = ava::session::detail::create_private_session_directories(root / "sessions", session_dir);
  expect(made.has_value(), "session store support creates private session directories");
  expect(file_mode(root / "sessions") == 0700 && file_mode(session_dir) == 0700,
         "session store support locks session directories to owner access");

  auto const missing_path = session_dir / "missing.jsonl";
  auto missing_allowed = ava::session::detail::inspect_session_file(missing_path, "session_missing",
                                                                    ava::session::detail::MissingSessionFile::Allow);
  auto missing_error = ava::session::detail::inspect_session_file(
      missing_path, "session_missing", ava::session::detail::MissingSessionFile::NotFoundError);
  expect(missing_allowed && !*missing_allowed, "session store support allows missing session files when requested");
  expect(!missing_error && missing_error.error().category() == ava::core::ErrorCategory::NotFound,
         "session store support can report missing session files as not found");

  auto const regular_path = session_dir / "regular.jsonl";
  write_text(regular_path, "");
  auto regular = ava::session::detail::inspect_session_file(regular_path, "session_regular",
                                                            ava::session::detail::MissingSessionFile::Allow);
  expect(regular && *regular, "session store support accepts regular session files");

  auto const directory_path = session_dir / "not-file.jsonl";
  std::filesystem::create_directories(directory_path);
  auto non_regular = ava::session::detail::inspect_session_file(directory_path, "session_dir",
                                                                ava::session::detail::MissingSessionFile::Allow);
  expect(!non_regular && non_regular.error().category() == ava::core::ErrorCategory::Session,
         "session store support rejects non-regular session paths");

  auto const symlink_path = session_dir / "link.jsonl";
  std::error_code symlink_error;
  std::filesystem::create_symlink(regular_path, symlink_path, symlink_error);
  if (!symlink_error) {
    auto symlink = ava::session::detail::inspect_session_file(symlink_path, "session_link",
                                                              ava::session::detail::MissingSessionFile::Allow);
    expect(!symlink && symlink.error().category() == ava::core::ErrorCategory::PermissionDenied,
           "session store support rejects symlink session paths");
  }

  ::chmod(regular_path.c_str(), S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
  auto permissions = ava::session::detail::set_private_session_file_permissions(regular_path);
  expect(permissions.has_value() && file_mode(regular_path) == 0600,
         "session store support locks session files to owner read/write");
}

void test_read_entries_and_list_helpers()
{
  auto const root = fresh_root("session-store-support-read-list");
  auto const session_dir = root / "sessions";
  std::filesystem::create_directories(session_dir);

  ava::session::SessionEntry const entry{.id = "entry_1",
                                         .parent_id = "",
                                         .type = ava::session::EntryType::UserMessage,
                                         .timestamp = "2026-05-05T00:00:00Z",
                                         .data_json = R"({"text":"hello"})"};
  auto line = ava::session::encode_session_entry_line(entry);
  expect(line.has_value(), "session store support test entry encodes");
  auto const session_path = session_dir / "session_test.jsonl";
  write_text(session_path, *line + "\n");

  auto entries = ava::session::detail::read_session_entries(session_path, "session_test");
  expect(entries && entries->size() == 1 && entries->front().id == "entry_1" &&
             entries->front().data_json == R"({"text":"hello"})",
         "session store support reads encoded session entries");

  auto const text_path = session_dir / "notes.txt";
  write_text(text_path, "ignore");
  auto const directory_path = session_dir / "directory.jsonl";
  std::filesystem::create_directories(directory_path);
  expect(ava::session::detail::is_listable_session_file(std::filesystem::directory_entry(session_path)) &&
             !ava::session::detail::is_listable_session_file(std::filesystem::directory_entry(text_path)) &&
             !ava::session::detail::is_listable_session_file(std::filesystem::directory_entry(directory_path)),
         "session store support filters listable session files by regular JSONL paths");

  auto const older_path = session_dir / "older.jsonl";
  auto const newer_path = session_dir / "newer.jsonl";
  write_text(older_path, "");
  write_text(newer_path, "");
  auto const now = std::filesystem::file_time_type::clock::now();
  std::filesystem::last_write_time(older_path, now - std::chrono::seconds(30));
  std::filesystem::last_write_time(newer_path, now);
  std::vector<ava::session::SessionSummary> summaries{
      ava::session::SessionSummary{.session_id = "older", .path = older_path, .last_updated = "", .entry_count = 1},
      ava::session::SessionSummary{.session_id = "newer", .path = newer_path, .last_updated = "", .entry_count = 1}};
  ava::session::detail::sort_session_summaries(summaries);
  expect(summaries.size() == 2 && summaries.front().session_id == "newer",
         "session store support sorts summaries by newest file time first");
}

}  // namespace

void run_session_store_support_tests()
{
  test_project_key_and_paths();
  test_directory_and_file_safety_helpers();
  test_read_entries_and_list_helpers();
}
