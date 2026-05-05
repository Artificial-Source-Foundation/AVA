#include <sys/stat.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "ava/config/auth_storage.h"
#include "ava/config/xdg_paths.h"
#include "ava/core/error.h"
#include "tests/support/test_harness.h"

namespace {

std::filesystem::path fresh_root(std::string const& name)
{
  auto const root = temp_root() / name;
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  std::filesystem::create_directories(root);
  return root;
}

int file_mode(std::filesystem::path const& path)
{
  struct stat status {};
  if (::stat(path.c_str(), &status) != 0) return -1;
  return status.st_mode & 0777;
}

void write_text(std::filesystem::path const& path, std::string const& text)
{
  std::filesystem::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file << text;
}

void test_missing_and_private_reads()
{
  auto const root = fresh_root("auth-storage-focused-read");
  auto const path = root / "auth.json";

  auto missing = ava::config::read_auth_text_if_exists(path, true);
  expect(missing && !missing->content, "auth storage focused read treats missing explicit auth file as empty");

  write_text(path, "{\"provider\":{\"type\":\"api_key\"}}\n");
  ::chmod(path.c_str(), S_IRUSR | S_IWUSR);
  auto private_read = ava::config::read_auth_text_if_exists(path, true);
  expect(private_read && private_read->content && *private_read->content == "{\"provider\":{\"type\":\"api_key\"}}\n",
         "auth storage focused read returns exact private auth file content");
}

void test_broad_permission_rejection_and_override()
{
  auto const root = fresh_root("auth-storage-focused-permissions");
  auto const path = root / "auth.json";
  write_text(path, "{\"provider\":{\"type\":\"api_key\"}}\n");
  ::chmod(path.c_str(), S_IRUSR | S_IWUSR | S_IRGRP);

  auto rejected = ava::config::read_auth_text_if_exists(path, true);
  expect(!rejected && rejected.error().category() == ava::core::ErrorCategory::PermissionDenied &&
             ava::config::auth_error_has_context(rejected.error(), "reason", "broad_permissions"),
         "auth storage focused read rejects broad explicit auth file permissions");

  auto allowed = ava::config::read_auth_text_if_exists(path, true, true);
  expect(allowed && allowed->content, "auth storage focused read allows broad permissions when requested");
}

void test_atomic_write_replaces_content_with_private_permissions()
{
  auto const root = fresh_root("auth-storage-focused-write");
  auto const path = root / "auth.json";
  std::filesystem::create_directories(path.parent_path());

  auto written = ava::config::write_auth_file_atomic(path, "{\"provider\":{\"type\":\"api_key\",\"key\":\"new\"}}\n");
  expect(written.has_value(), "auth storage focused write performs atomic replacement");
  auto read = ava::config::read_auth_text_if_exists(path, true);
  expect(read && read->content && read->content->find("\"new\"") != std::string::npos,
         "auth storage focused write persists replacement content");
  expect(file_mode(path) == 0600, "auth storage focused write creates private auth files");
}

void test_atomic_write_rejects_symlink_target()
{
  auto const root = fresh_root("auth-storage-focused-symlink");
  auto const target = root / "target.json";
  auto const link = root / "auth.json";
  write_text(target, "{}");

  std::error_code symlink_error;
  std::filesystem::create_symlink(target, link, symlink_error);
  if (symlink_error) return;

  auto written = ava::config::write_auth_file_atomic(link, "{\"provider\":{\"type\":\"api_key\"}}\n");
  expect(!written && written.error().category() == ava::core::ErrorCategory::PermissionDenied,
         "auth storage focused write rejects symlink targets");
  auto target_read = ava::config::read_auth_text_if_exists(target, true, true);
  expect(target_read && target_read->content && *target_read->content == "{}",
         "auth storage focused symlink rejection leaves target content untouched");
}

void test_directory_and_lock_helpers()
{
  auto const root = fresh_root("auth-storage-focused-lock");
  ava::config::XdgPaths paths;
  paths.auth_file = root / "config" / "auth.json";

  auto ensured = ava::config::ensure_auth_directory(paths);
  expect(ensured.has_value(), "auth storage focused setup creates auth directory");
  expect(file_mode(paths.auth_file.parent_path()) == 0700, "auth storage focused setup creates private auth directory");

  auto lock = ava::config::acquire_auth_file_lock(paths);
  expect(lock.has_value() && lock->get() >= 0, "auth storage focused setup acquires lock file");
}

}  // namespace

void run_config_auth_storage_tests()
{
  test_missing_and_private_reads();
  test_broad_permission_rejection_and_override();
  test_atomic_write_replaces_content_with_private_permissions();
  test_atomic_write_rejects_symlink_target();
  test_directory_and_lock_helpers();
}
