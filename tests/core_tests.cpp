#include <sys/stat.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "ava/agent/agent_loop.h"
#include "ava/agent/mode.h"
#include "ava/agent/tool_dispatcher.h"
#include "ava/config/auth.h"
#include "ava/config/model_config.h"
#include "ava/config/prompt_config.h"
#include "ava/config/xdg_paths.h"
#include "ava/core/json.h"
#include "ava/permissions/permission.h"
#include "ava/provider/openai_provider.h"
#include "ava/session/session_store.h"
#include "ava/tools/bash_tool.h"
#include "ava/tools/file_tools.h"
#include "ava/tools/search_tools.h"
#include "ava/tui/composer.h"
#include "ava/tui/terminal.h"

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

std::filesystem::path temp_root() {
  const auto build_name = std::filesystem::current_path().filename();
  return std::filesystem::temp_directory_path() / ("ava_core_tests_" + build_name.string());
}

void test_mode_parsing() {
  const auto build = ava::agent::parse_mode("build");
  const auto plan = ava::agent::parse_mode("plan");
  const auto bad = ava::agent::parse_mode("other");

  expect(build && *build == ava::agent::Mode::Build, "build mode parses");
  expect(plan && *plan == ava::agent::Mode::Plan, "plan mode parses");
  expect(!bad, "unknown mode fails");
  expect(ava::agent::toggle_mode(ava::agent::Mode::Build) == ava::agent::Mode::Plan, "build toggles to plan");
}

void test_session_store_round_trip() {
  std::error_code remove_error;
  std::filesystem::remove_all(temp_root(), remove_error);

  auto store = ava::session::SessionStore::create(std::filesystem::current_path(), temp_root());
  expect(store.has_value(), "session store creates");
  if (!store) return;

  auto append = store->append(ava::session::SessionEntry{
      .id = "entry_1",
      .parent_id = "",
      .type = ava::session::EntryType::SessionStart,
      .timestamp = "2026-04-27T00:00:00Z",
      .data_json = "{\"mode\":\"build\"}",
  });
  expect(append.has_value(), "session entry appends");

  struct stat session_stat {};
  if (stat(store->session_path().c_str(), &session_stat) == 0) {
    expect((session_stat.st_mode & 0777) == 0600, "session file is owner read/write only");
  }
  struct stat session_dir_stat {};
  const auto session_dir = store->session_path().parent_path();
  if (stat(session_dir.c_str(), &session_dir_stat) == 0) {
    expect((session_dir_stat.st_mode & 0777) == 0700, "session directory is owner-only");
  }

  auto loaded = store->load();
  expect(loaded.has_value(), "session entries load");
  if (!loaded) return;

  expect(loaded->size() == 1, "one entry loaded");
  expect((*loaded)[0].id == "entry_1", "entry id round trips");
  expect((*loaded)[0].type == ava::session::EntryType::SessionStart, "entry type round trips");

  auto text_store = ava::session::SessionStore::create(std::filesystem::current_path(), temp_root());
  expect(text_store.has_value(), "text session store creates");
  if (!text_store) return;
  const auto text_append = text_store->append(ava::session::SessionEntry{
      .id = "entry_2\n",
      .parent_id = "",
      .type = ava::session::EntryType::UserMessage,
      .timestamp = "2026-04-27T00:00:00Z",
      .data_json = "{\"text\":\"hello\"}",
  });
  expect(text_append.has_value(), "session entry with escaped newline appends");
  auto text_loaded = text_store->load();
  expect(text_loaded && !text_loaded->empty() && (*text_loaded)[0].id == "entry_2\n",
         "session escaped strings round trip");

  const auto raw_newline_append = text_store->append(ava::session::SessionEntry{
      .id = "entry_raw_newline",
      .parent_id = "",
      .type = ava::session::EntryType::UserMessage,
      .timestamp = "2026-04-27T00:00:00Z",
      .data_json = "{\"text\":\"bad\nsplit\"}",
  });
  expect(!raw_newline_append, "session data_json rejects raw newlines to preserve JSONL entries");

  const auto raw_carriage_return_append = text_store->append(ava::session::SessionEntry{
      .id = "entry_raw_carriage_return",
      .parent_id = "",
      .type = ava::session::EntryType::UserMessage,
      .timestamp = "2026-04-27T00:00:00Z",
      .data_json = "{\"text\":\"bad\rsplit\"}",
  });
  expect(!raw_carriage_return_append, "session data_json rejects raw carriage returns to preserve JSONL entries");

  const std::vector<std::string> bad_session_ids = {"",
                                                    ".",
                                                    "..",
                                                    "/",
                                                    "\\",
                                                    "../escape",
                                                    "with/slash",
                                                    "with\\slash",
                                                    std::string("bad\0id", 6),
                                                    std::string("bad\x1Fid", 6)};
  for (const auto& bad_session_id : bad_session_ids) {
    ava::session::SessionStore bad_store(ava::session::SessionStoreOptions{
        .root_dir = temp_root(),
        .workspace_dir = std::filesystem::current_path(),
        .session_id = bad_session_id,
    });
    const auto bad_append = bad_store.append(ava::session::SessionEntry{
        .id = "entry_bad_session",
        .parent_id = "",
        .type = ava::session::EntryType::UserMessage,
        .timestamp = "2026-04-27T00:00:00Z",
        .data_json = "{\"text\":\"hello\"}",
    });
    expect(!bad_append, "session append rejects invalid externally supplied session ids");
    expect(!bad_store.load(), "session load rejects invalid externally supplied session ids");
  }

  ava::session::SessionStore traversal_store(ava::session::SessionStoreOptions{
      .root_dir = temp_root(),
      .workspace_dir = std::filesystem::current_path(),
      .session_id = "../escape",
  });
  const auto attempted_traversal_path = traversal_store.session_path().lexically_normal();
  std::error_code cleanup_error;
  std::filesystem::remove(attempted_traversal_path, cleanup_error);
  const auto traversal_append = traversal_store.append(ava::session::SessionEntry{
      .id = "entry_traversal",
      .parent_id = "",
      .type = ava::session::EntryType::UserMessage,
      .timestamp = "2026-04-27T00:00:00Z",
      .data_json = "{\"text\":\"hello\"}",
  });
  expect(!traversal_append && !std::filesystem::exists(attempted_traversal_path),
         "session traversal id append is rejected before creating attempted path");

  ava::session::SessionStore unicode_store(ava::session::SessionStoreOptions{
      .root_dir = temp_root(),
      .workspace_dir = std::filesystem::current_path(),
      .session_id = "unicode",
  });
  std::filesystem::create_directories(unicode_store.session_path().parent_path());
  {
    std::ofstream file(unicode_store.session_path(), std::ios::binary | std::ios::trunc);
    file << "{\"version\":1,\"id\":\"entry_\\u00e9\",\"parent_id\":\"\",\"type\":\"user_message\","
            "\"timestamp\":\"2026-04-27T00:00:00Z\",\"data\":{\"text\":\"hello\"}}\n";
  }
  auto unicode_loaded = unicode_store.load();
  expect(unicode_loaded && !unicode_loaded->empty() && (*unicode_loaded)[0].id == std::string("entry_\xC3\xA9"),
         "session unicode escapes decode as utf-8");

  ava::session::SessionStore surrogate_store(ava::session::SessionStoreOptions{
      .root_dir = temp_root(),
      .workspace_dir = std::filesystem::current_path(),
      .session_id = "surrogate",
  });
  std::filesystem::create_directories(surrogate_store.session_path().parent_path());
  {
    std::ofstream file(surrogate_store.session_path(), std::ios::binary | std::ios::trunc);
    file << "{\"version\":1,\"id\":\"entry_\\uD834\\uDD1E\",\"parent_id\":\"\",\"type\":\"user_message\","
            "\"timestamp\":\"2026-04-27T00:00:00Z\",\"data\":{\"text\":\"hello\"}}\n";
  }
  auto surrogate_loaded = surrogate_store.load();
  expect(surrogate_loaded && !surrogate_loaded->empty() &&
             (*surrogate_loaded)[0].id == std::string("entry_\xF0\x9D\x84\x9E"),
         "session surrogate pairs decode as utf-8");

  ava::session::SessionStore bad_surrogate_store(ava::session::SessionStoreOptions{
      .root_dir = temp_root(),
      .workspace_dir = std::filesystem::current_path(),
      .session_id = "bad-surrogate",
  });
  std::filesystem::create_directories(bad_surrogate_store.session_path().parent_path());
  {
    std::ofstream file(bad_surrogate_store.session_path(), std::ios::binary | std::ios::trunc);
    file << "{\"version\":1,\"id\":\"entry_\\uD834\",\"parent_id\":\"\",\"type\":\"user_message\","
            "\"timestamp\":\"2026-04-27T00:00:00Z\",\"data\":{\"text\":\"hello\"}}\n";
  }
  auto bad_surrogate_loaded = bad_surrogate_store.load();
  expect(bad_surrogate_loaded && !bad_surrogate_loaded->empty() &&
             (*bad_surrogate_loaded)[0].id == std::string("entry_\xEF\xBF\xBD"),
         "session lone surrogate decodes as replacement character");

  ava::session::SessionStore nested_key_store(ava::session::SessionStoreOptions{
      .root_dir = temp_root(),
      .workspace_dir = std::filesystem::current_path(),
      .session_id = "nested-key",
  });
  std::filesystem::create_directories(nested_key_store.session_path().parent_path());
  {
    std::ofstream file(nested_key_store.session_path(), std::ios::binary | std::ios::trunc);
    file << "{\"version\":1,\"data\":{\"id\":\"bad\",\"parent_id\":\"bad\",\"type\":\"bad\","
            "\"timestamp\":\"bad\",\"text\":\"contains \\\"type\\\":\\\"bad\\\"\"},"
            "\"id\":\"entry_safe\",\"parent_id\":\"parent_safe\",\"type\":\"user_message\","
            "\"timestamp\":\"2026-04-27T00:00:00Z\"}\n";
  }
  auto nested_key_loaded = nested_key_store.load();
  expect(nested_key_loaded && !nested_key_loaded->empty() && (*nested_key_loaded)[0].id == "entry_safe" &&
             (*nested_key_loaded)[0].parent_id == "parent_safe" &&
             (*nested_key_loaded)[0].type == ava::session::EntryType::UserMessage &&
             (*nested_key_loaded)[0].timestamp == "2026-04-27T00:00:00Z",
         "session load ignores key-looking strings and nested data keys");
}

void test_session_resume_and_listing() {
  const auto root = temp_root() / "session-resume";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  const auto session_root = root / "sessions";

  auto first = ava::session::SessionStore::create(workspace, session_root);
  auto second = ava::session::SessionStore::create(workspace, session_root);
  expect(first && second, "resume test sessions create");
  if (!first || !second) return;

  expect(first->append(ava::session::SessionEntry{.id = "entry_first",
                                                  .parent_id = "",
                                                  .type = ava::session::EntryType::UserMessage,
                                                  .timestamp = "2026-04-27T00:00:00Z",
                                                  .data_json = "{\"text\":\"first\"}"})
             .has_value(),
         "first resume test session appends");
  expect(second->append(ava::session::SessionEntry{.id = "entry_second",
                                                   .parent_id = "",
                                                   .type = ava::session::EntryType::UserMessage,
                                                   .timestamp = "2026-04-27T00:01:00Z",
                                                   .data_json = "{\"text\":\"second\"}"})
             .has_value(),
         "second resume test session appends");

  const auto old_time = std::filesystem::file_time_type::clock::now() - std::chrono::minutes(2);
  const auto new_time = std::filesystem::file_time_type::clock::now();
  std::filesystem::last_write_time(first->session_path(), old_time);
  std::filesystem::last_write_time(second->session_path(), new_time);

  auto reopened = ava::session::SessionStore::open(workspace, first->session_id(), session_root);
  expect(reopened && reopened->session_id() == first->session_id(), "session opens by exact id");
  if (reopened) {
    auto reopened_entries = reopened->load();
    expect(reopened_entries && reopened_entries->size() == 1 && (*reopened_entries)[0].id == "entry_first",
           "opened session loads original history");
  }

  auto listed = ava::session::SessionStore::list_sessions(workspace, session_root);
  expect(listed && listed->size() == 2, "sessions list for workspace");
  expect(listed && (*listed)[0].session_id == second->session_id(), "sessions list newest first");
  expect(listed && (*listed)[0].entry_count == 1 && (*listed)[0].last_updated == "2026-04-27T00:01:00Z",
         "session summary includes entry count and last timestamp");

  ava::session::SessionStore corrupt_store(ava::session::SessionStoreOptions{
      .root_dir = session_root,
      .workspace_dir = workspace,
      .session_id = "corrupt",
  });
  std::filesystem::create_directories(corrupt_store.session_path().parent_path());
  {
    std::ofstream file(corrupt_store.session_path(), std::ios::binary | std::ios::trunc);
    file << "{\"version\":1,\"id\":\"bad\",\"parent_id\":\"\",\"type\":\"not_real\","
            "\"timestamp\":\"2026-04-27T00:02:00Z\",\"data\":{}}\n";
  }
  listed = ava::session::SessionStore::list_sessions(workspace, session_root);
  expect(listed && listed->size() == 2, "session listing skips one corrupt session file");

  ava::session::SessionStore crlf_store(ava::session::SessionStoreOptions{
      .root_dir = session_root,
      .workspace_dir = workspace,
      .session_id = "crlf",
  });
  std::filesystem::create_directories(crlf_store.session_path().parent_path());
  {
    std::ofstream file(crlf_store.session_path(), std::ios::binary | std::ios::trunc);
    file << "{\"version\":1,\"id\":\"entry_crlf\",\"parent_id\":\"\",\"type\":\"user_message\","
            "\"timestamp\":\"2026-04-27T00:03:00Z\",\"data\":{\"text\":\"hello\"}}\r\n";
  }
  auto crlf_loaded = crlf_store.load();
  expect(crlf_loaded && crlf_loaded->size() == 1 && (*crlf_loaded)[0].id == "entry_crlf",
         "session loader accepts CRLF line endings");

  ava::session::SessionStore symlink_store(ava::session::SessionStoreOptions{
      .root_dir = session_root,
      .workspace_dir = workspace,
      .session_id = "link",
  });
  std::filesystem::create_directories(symlink_store.session_path().parent_path());
  std::error_code symlink_error;
  std::filesystem::create_symlink(first->session_path(), symlink_store.session_path(), symlink_error);
  if (!symlink_error) {
    auto symlink_open = ava::session::SessionStore::open(workspace, "link", session_root);
    expect(!symlink_open && symlink_open.error().category() == ava::core::ErrorCategory::PermissionDenied,
           "session open rejects symlink session files");
    auto symlink_load = symlink_store.load();
    expect(!symlink_load && symlink_load.error().category() == ava::core::ErrorCategory::PermissionDenied,
           "session load rejects symlink session files");
    auto symlink_append = symlink_store.append(ava::session::SessionEntry{.id = "entry_symlink",
                                                                          .parent_id = "",
                                                                          .type = ava::session::EntryType::UserMessage,
                                                                          .timestamp = "2026-04-27T00:04:00Z",
                                                                          .data_json = "{\"text\":\"bad\"}"});
    expect(!symlink_append && symlink_append.error().category() == ava::core::ErrorCategory::PermissionDenied,
           "session append rejects symlink session files");
  }

  auto missing = ava::session::SessionStore::open(workspace, "missing-session", session_root);
  expect(!missing && missing.error().category() == ava::core::ErrorCategory::NotFound, "missing session open fails");
  auto bad = ava::session::SessionStore::open(workspace, "../escape", session_root);
  expect(!bad && bad.error().category() == ava::core::ErrorCategory::InvalidArgument,
         "resume rejects invalid session id");
}

void test_json_escape_control_characters() {
  const auto escaped = ava::session::json_escape(std::string("a\x01\b\f", 4));
  expect(escaped == "a\\u0001\\b\\f", "json_escape escapes all JSON control characters");
}

void test_core_json_top_level_lookup() {
  const std::string document =
      "{\"data\":{\"type\":\"bad\"},\"items\":[{\"type\":\"array_bad\"}],"
      "\"text\":\"contains \\\"type\\\":\\\"string_bad\\\"\",\"type\":\"good\"}";
  const auto type = ava::core::json::string_field(document, "type");
  expect(type && *type == "good", "JSON string_field reads only top-level object keys");
  expect(!ava::core::json::string_field(document, "items.type"), "JSON lookup does not invent nested paths");

  const std::string arrays_first = "{\"items\":[{\"models\":[{\"id\":\"bad\"}]}],\"models\":[{\"id\":\"ok\"}]}";
  const auto models = ava::core::json::objects_in_array_field(arrays_first, "models");
  const auto model_id = models.empty() ? std::optional<std::string>{} : ava::core::json::string_field(models[0], "id");
  expect(models.size() == 1 && model_id && *model_id == "ok",
         "JSON array lookup ignores nested arrays before top-level field");

  const auto surrogate_pair = ava::core::json::string_field("{\"text\":\"\\uD834\\uDD1E\"}", "text");
  expect(surrogate_pair && *surrogate_pair == std::string("\xF0\x9D\x84\x9E"),
         "JSON string_field decodes UTF-16 surrogate pairs");
  const auto lone_high_surrogate = ava::core::json::string_field("{\"text\":\"\\uD834x\"}", "text");
  expect(lone_high_surrogate && *lone_high_surrogate == std::string("\xEF\xBF\xBDx"),
         "JSON string_field replaces lone high surrogates");
  const auto lone_low_surrogate = ava::core::json::string_field("{\"text\":\"\\uDD1E\"}", "text");
  expect(lone_low_surrogate && *lone_low_surrogate == std::string("\xEF\xBF\xBD"),
         "JSON string_field replaces lone low surrogates");
  const auto high_then_non_low = ava::core::json::string_field("{\"text\":\"\\uD834\\u0061\"}", "text");
  expect(high_then_non_low && *high_then_non_low == std::string("\xEF\xBF\xBD") + "a",
         "JSON string_field leaves non-low escape after replacing high surrogate");
  expect(!ava::core::json::string_field("{\"text\":\"\\u12xz\"}", "text"),
         "JSON string_field rejects malformed unicode escapes");
  expect(!ava::core::json::string_field("{\"text\":\"\\q\"}", "text"),
         "JSON string_field rejects invalid escapes");
}

void test_permission_defaults() {
  const auto workspace = std::filesystem::current_path();

  const auto normal_edit = ava::permissions::decide(ava::permissions::PermissionRequest{
      .operation = ava::permissions::Operation::EditFile,
      .mode = ava::agent::Mode::Build,
      .workspace_dir = workspace,
      .target_path = workspace / "src/main.cpp",
      .command = "",
  });
  expect(normal_edit.action == ava::permissions::PermissionAction::Allow, "build mode allows workspace edits");

  const auto plan_source_edit = ava::permissions::decide(ava::permissions::PermissionRequest{
      .operation = ava::permissions::Operation::EditFile,
      .mode = ava::agent::Mode::Plan,
      .workspace_dir = workspace,
      .target_path = workspace / "src/main.cpp",
      .command = "",
  });
  expect(plan_source_edit.action == ava::permissions::PermissionAction::Deny, "plan mode denies source edits");

  const auto plan_doc_edit = ava::permissions::decide(ava::permissions::PermissionRequest{
      .operation = ava::permissions::Operation::EditFile,
      .mode = ava::agent::Mode::Plan,
      .workspace_dir = workspace,
      .target_path = workspace / "docs/versions/0.1.md",
      .command = "",
  });
  expect(plan_doc_edit.action == ava::permissions::PermissionAction::Allow, "plan mode allows planning markdown");

  const auto secret_read = ava::permissions::decide(ava::permissions::PermissionRequest{
      .operation = ava::permissions::Operation::ReadFile,
      .mode = ava::agent::Mode::Build,
      .workspace_dir = workspace,
      .target_path = workspace / ".env",
      .command = "",
  });
  expect(secret_read.action == ava::permissions::PermissionAction::Deny, "secret files are denied");

  const auto npmrc_read = ava::permissions::decide(ava::permissions::PermissionRequest{
      .operation = ava::permissions::Operation::ReadFile,
      .mode = ava::agent::Mode::Build,
      .workspace_dir = workspace,
      .target_path = workspace / ".npmrc",
      .command = "",
  });
  expect(npmrc_read.action == ava::permissions::PermissionAction::Deny, "common credential files are denied");

  const auto ssh_read = ava::permissions::decide(ava::permissions::PermissionRequest{
      .operation = ava::permissions::Operation::ReadFile,
      .mode = ava::agent::Mode::Build,
      .workspace_dir = workspace,
      .target_path = workspace / ".ssh/work_key",
      .command = "",
  });
  expect(ssh_read.action == ava::permissions::PermissionAction::Deny, "credential directories are denied");

  const auto external_read = ava::permissions::decide(ava::permissions::PermissionRequest{
      .operation = ava::permissions::Operation::ReadFile,
      .mode = ava::agent::Mode::Build,
      .workspace_dir = workspace,
      .target_path = workspace.parent_path() / "outside.txt",
      .command = "",
  });
  expect(external_read.action == ava::permissions::PermissionAction::Ask, "external paths ask");

  const auto symlink_workspace = temp_root() / "symlink-workspace";
  const auto outside = temp_root() / "outside";
  std::filesystem::create_directories(symlink_workspace);
  std::filesystem::create_directories(outside);
  std::error_code symlink_error;
  const auto link = symlink_workspace / "link-outside";
  std::filesystem::remove(link, symlink_error);
  std::filesystem::create_directory_symlink(outside, link, symlink_error);
  if (!symlink_error) {
    const auto symlink_escape = ava::permissions::decide(ava::permissions::PermissionRequest{
        .operation = ava::permissions::Operation::ReadFile,
        .mode = ava::agent::Mode::Build,
        .workspace_dir = symlink_workspace,
        .target_path = link / "secret.txt",
        .command = "",
    });
    expect(symlink_escape.action == ava::permissions::PermissionAction::Ask, "symlink workspace escape asks");
  }
}

void test_command_classification() {
  expect(ava::permissions::classify_command("git status --short").action == ava::permissions::PermissionAction::Allow,
         "git status is allowed");
  expect(ava::permissions::classify_command("rm -rf build").action == ava::permissions::PermissionAction::Deny,
         "rm -rf is denied");
  expect(ava::permissions::classify_command("git push origin main").action == ava::permissions::PermissionAction::Ask,
         "git push asks");
  expect(ava::permissions::classify_command("git diff --output=/tmp/ava-owned").action ==
             ava::permissions::PermissionAction::Ask,
         "git diff output paths are not auto-allowed");
  expect(ava::permissions::classify_command("git diff --output out.diff").action ==
             ava::permissions::PermissionAction::Ask,
         "git diff output option is not auto-allowed");
  expect(ava::permissions::classify_command("git diff --no-index empty .ssh/work_key").action ==
             ava::permissions::PermissionAction::Ask,
         "relative credential paths are not auto-allowed");
  expect(ava::permissions::classify_command("cmake --build build").action == ava::permissions::PermissionAction::Allow,
         "cmake build is allowed");
  expect(ava::permissions::classify_command("ctest --test-dir build").action == ava::permissions::PermissionAction::Allow,
         "ctest is allowed with safe arguments");
  expect(ava::permissions::classify_command("cmake -E cat ~/.config/ava/auth.json").action ==
             ava::permissions::PermissionAction::Deny,
         "cmake -E helper access is denied");
  expect(ava::permissions::classify_command("cmake -P docs/plan.md").action == ava::permissions::PermissionAction::Deny,
         "cmake -P script execution is denied");
  expect(ava::permissions::classify_command("cmake -E copy docs/plan.md src/new.cpp").action ==
             ava::permissions::PermissionAction::Deny,
         "cmake -E copy mutation is denied");
  expect(ava::permissions::classify_command("python3 scripts/run.py").action == ava::permissions::PermissionAction::Deny,
         "interpreters are denied");
}

void test_file_tools() {
  std::error_code remove_error;
  std::filesystem::remove_all(temp_root(), remove_error);
  std::filesystem::create_directories(temp_root());

  const auto workspace = temp_root() / "workspace";
  std::filesystem::create_directories(workspace / "docs");
  const auto source_path = workspace / "src.txt";
  const ava::tools::ToolContext build_context{.workspace_dir = workspace, .mode = ava::agent::Mode::Build};
  const ava::tools::ToolContext plan_context{.workspace_dir = workspace, .mode = ava::agent::Mode::Plan};

  auto write = ava::tools::write_file(build_context, source_path, "hello world");
  expect(write.has_value(), "write_file writes in build mode");

  auto read = ava::tools::read_file(build_context, source_path, ava::tools::ReadOptions{.max_bytes = 5});
  expect(read.has_value(), "read_file reads content");
  if (read) {
    expect(read->content == "hello", "read_file truncates head");
    expect(read->truncated, "read_file reports truncation");
  }

  auto edit = ava::tools::edit_file(build_context, source_path, "world", "ava");
  expect(edit.has_value(), "edit_file edits unique text");

  auto edited = ava::tools::read_file(build_context, source_path);
  expect(edited && edited->content == "hello ava", "edit_file result is persisted");

  auto denied = ava::tools::write_file(plan_context, workspace / "main.cpp", "int main() {}\n");
  expect(!denied, "plan mode source write is denied");

  auto denied_edit = ava::tools::edit_file(build_context, workspace / ".env", "secret", "other");
  expect(!denied_edit, "edit_file checks read permission before reading");

  auto plan_denied_edit = ava::tools::edit_file(plan_context, workspace / "missing.cpp", "old", "new");
  expect(!plan_denied_edit && plan_denied_edit.error().category() == ava::core::ErrorCategory::PermissionDenied,
         "edit_file checks edit permission before reading target content");

  {
    std::ofstream secret_file(workspace / ".env", std::ios::binary | std::ios::trunc);
    secret_file << "secret";
  }
  auto direct_secret = ava::tools::read_file(build_context, workspace / ".env");
  expect(!direct_secret, "read_file denies direct .env reads");
  std::error_code symlink_error;
  const auto secret_link = workspace / "safe.txt";
  std::filesystem::create_symlink(workspace / ".env", secret_link, symlink_error);
  if (!symlink_error) {
    auto linked_secret = ava::tools::read_file(build_context, secret_link);
    expect(!linked_secret, "read_file denies symlink to secret file");
  }

  const auto source_link = workspace / "docs" / "plan-link.md";
  symlink_error.clear();
  std::filesystem::create_symlink(source_path, source_link, symlink_error);
  if (!symlink_error) {
    auto linked_source_edit = ava::tools::write_file(plan_context, source_link, "bad");
    expect(!linked_source_edit, "plan mode denies symlink edit to source file");
  }

  auto plan_doc = ava::tools::write_file(plan_context, workspace / "docs" / "plan.md", "# Plan\n");
  expect(plan_doc.has_value(), "plan mode markdown plan write is allowed");

  const auto large_path = workspace / "large.txt";
  {
    std::ofstream large(large_path, std::ios::binary | std::ios::trunc);
    large << std::string(8192, 'x');
  }
  auto large_read = ava::tools::read_file(build_context, large_path, ava::tools::ReadOptions{.max_bytes = 16});
  expect(large_read && large_read->content.size() == 16 && large_read->total_bytes == 8192,
         "read_file bounds output while counting bytes");
}

void test_search_tools() {
  std::error_code remove_error;
  std::filesystem::remove_all(temp_root(), remove_error);

  const auto workspace = temp_root() / "workspace";
  const ava::tools::ToolContext context{.workspace_dir = workspace, .mode = ava::agent::Mode::Build};

  expect(ava::tools::write_file(context, workspace / "src" / "main.cpp", "int main() { return 0; }\n").has_value(),
         "search setup writes source");
  expect(ava::tools::write_file(context, workspace / "root.cpp", "int root() { return 0; }\n").has_value(),
         "search setup writes root source");
  expect(ava::tools::write_file(context, workspace / "docs" / "plan.md", "hello ava\nhello again\n").has_value(),
         "search setup writes docs");
  expect(ava::tools::write_file(context, workspace / "build" / "ignored.txt", "hello hidden\n").has_value(),
         "search setup writes ignored file");
  {
    std::ofstream secret_file(workspace / ".env", std::ios::binary | std::ios::trunc);
    secret_file << "hello secret\n";
  }
  std::filesystem::create_directories(workspace / ".ssh");
  {
    std::ofstream key_file(workspace / ".ssh" / "id_rsa", std::ios::binary | std::ios::trunc);
    key_file << "hello key\n";
  }

  auto glob = ava::tools::glob_files(context, "**/*.cpp");
  expect(glob.has_value(), "glob_files succeeds");
  if (glob) {
    expect(glob->paths.size() == 2, "glob_files returns nested and root source files");
  }

  auto capped = ava::tools::glob_files(context, "**/*", ava::tools::GlobOptions{.max_results = 2000, .max_visited = 1});
  expect(capped && capped->truncated, "glob_files reports traversal cap truncation");

  auto grep = ava::tools::grep_files(context, "hello", "**/*.md");
  expect(grep.has_value(), "grep_files succeeds");
  if (grep) {
    expect(grep->matches.size() == 2, "grep_files returns matching markdown lines");
    expect(grep->matches[0].line_number == 1, "grep_files records line numbers");
  }

  auto punctuation = ava::tools::grep_files(context, "main()", "**/*.cpp");
  expect(punctuation && !punctuation->matches.empty(), "grep_files literal search accepts punctuation");

  auto truncated = ava::tools::grep_files(context, "int", "**/*.cpp", ava::tools::GrepOptions{.max_line_length = 5});
  expect(truncated && !truncated->matches.empty() && truncated->matches[0].line_truncated,
         "grep_files reports line truncation metadata");

  auto ignored = ava::tools::grep_files(context, "hidden", "**/*");
  expect(ignored && ignored->matches.empty(), "grep_files skips generated folders");

  auto glob_secrets = ava::tools::glob_files(context, "**/*");
  expect(glob_secrets &&
             std::ranges::none_of(glob_secrets->paths,
                                  [](const std::filesystem::path& path) { return path.filename() == "id_rsa"; }),
         "glob_files skips files denied by read policy");

  auto secret = ava::tools::grep_files(context, "secret", "**/*");
  expect(secret && secret->matches.empty(), "grep_files skips files denied by read policy");
}

void test_bash_tool() {
  std::error_code remove_error;
  std::filesystem::remove_all(temp_root(), remove_error);
  std::filesystem::create_directories(temp_root());

  const ava::tools::ToolContext context{.workspace_dir = temp_root(), .mode = ava::agent::Mode::Build};

  auto pwd = ava::tools::run_bash(context, "pwd", ava::tools::BashOptions{.timeout = std::chrono::milliseconds(1000)});
  expect(pwd.has_value(), "run_bash allows safe command");
  if (pwd) {
    expect(pwd->exit_code == 0, "run_bash records exit code");
    expect(pwd->output.find(temp_root().string()) != std::string::npos, "run_bash uses workspace directory");
  }

  auto denied = ava::tools::run_bash(context, "rm -rf important");
  expect(!denied, "run_bash denies destructive command");

  auto chained = ava::tools::run_bash(context, "pwd; rm -rf important");
  expect(!chained, "run_bash rejects shell metacharacters");
  auto allowed_prefix_chain = ava::tools::run_bash(context, "pwd; whoami");
  expect(!allowed_prefix_chain, "run_bash rejects shell metacharacters after allowed prefix");

  expect(!ava::tools::run_bash(context, "cmake -E cat ~/.config/ava/auth.json"),
         "run_bash denies cmake -E file helper before execution");
  expect(!ava::tools::run_bash(context, "cmake -P docs/plan.md"),
         "run_bash denies cmake script execution before execution");
  expect(!ava::tools::run_bash(context, "cmake -E copy docs/plan.md src/new.cpp"),
         "run_bash denies cmake copy helper before execution");

  auto timeout =
      ava::tools::run_bash(context, "sleep 2", ava::tools::BashOptions{.timeout = std::chrono::milliseconds(50)});
  expect(timeout && timeout->timed_out, "run_bash times out long command");
}

void test_xdg_paths() {
  const auto root = temp_root() / "xdg";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  std::filesystem::create_directories(root);

  setenv("HOME", (root / "home").c_str(), 1);
  unsetenv("XDG_CONFIG_HOME");
  unsetenv("XDG_STATE_HOME");
  unsetenv("XDG_DATA_HOME");
  auto fallback = ava::config::xdg_paths();
  expect(fallback.ava_config_dir == root / "home" / ".config" / "ava", "XDG config falls back to ~/.config/ava");
  expect(fallback.ava_state_dir == root / "home" / ".local" / "state" / "ava",
         "XDG state falls back to ~/.local/state/ava");
  expect(fallback.auth_file == fallback.ava_config_dir / "auth.json", "auth file is in XDG config dir");
  expect(fallback.sessions_dir == fallback.ava_state_dir / "sessions", "sessions are in XDG state dir");

  setenv("XDG_CONFIG_HOME", (root / "config").c_str(), 1);
  setenv("XDG_STATE_HOME", (root / "state").c_str(), 1);
  setenv("XDG_DATA_HOME", (root / "data").c_str(), 1);
  auto overridden = ava::config::xdg_paths();
  expect(overridden.ava_config_dir == root / "config" / "ava", "XDG config override is honored");
  expect(overridden.ava_state_dir == root / "state" / "ava", "XDG state override is honored");
  expect(ava::config::opencode_auth_path() == root / "data" / "opencode" / "auth.json",
         "opencode auth path follows XDG data home");

  setenv("XDG_CONFIG_HOME", "relative-config", 1);
  auto relative_ignored = ava::config::xdg_paths();
  expect(relative_ignored.config_home == root / "home" / ".config", "relative XDG config path is ignored safely");

  unsetenv("HOME");
  unsetenv("XDG_CONFIG_HOME");
  unsetenv("XDG_STATE_HOME");
  unsetenv("XDG_DATA_HOME");
  auto no_home = ava::config::xdg_paths();
  expect(no_home.config_home.is_absolute(), "XDG config fallback remains absolute when HOME is unset");
  expect(no_home.config_home != std::filesystem::current_path() / ".config",
         "XDG fallback does not use current directory");

  setenv("HOME", "", 1);
  auto empty_home = ava::config::xdg_paths();
  expect(empty_home.state_home.is_absolute(), "XDG state fallback remains absolute when HOME is empty");
}

void test_auth_load_and_store() {
  const auto root = temp_root() / "auth";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  std::filesystem::create_directories(root / "config" / "ava");
  setenv("HOME", (root / "home").c_str(), 1);
  setenv("XDG_CONFIG_HOME", (root / "config").c_str(), 1);
  setenv("XDG_STATE_HOME", (root / "state").c_str(), 1);
  setenv("XDG_DATA_HOME", (root / "data").c_str(), 1);

  const auto paths = ava::config::xdg_paths();
  {
    std::ofstream file(paths.auth_file, std::ios::binary | std::ios::trunc);
    file << "{\"openai\":{\"type\":\"oauth\",\"access_token\":\"secret-token\",\"refresh_token\":\"refresh\",\"expires_"
            "at\":42}}";
  }
  auto loaded = ava::config::load_openai_credential(paths);
  expect(loaded && loaded->has_value(), "OpenAI OAuth credential loads from AVA XDG auth file");
  if (loaded && *loaded) {
    expect((*loaded)->access_token == "secret-token", "OpenAI access token parses");
    expect((*loaded)->source_path == paths.auth_file, "credential source path records location only");
  }

  std::filesystem::remove(paths.auth_file, remove_error);
  std::filesystem::create_directories(root / "data" / "opencode");
  {
    std::ofstream file(root / "data" / "opencode" / "auth.json", std::ios::binary | std::ios::trunc);
    file << "{\"openai\":{\"type\":\"oauth\",\"access\":\"opencode-token\",\"refresh\":\"r\",\"expires\":7}}";
  }
  auto imported = ava::config::load_openai_credential(paths);
  expect(imported && imported->has_value() && (*imported)->access_token == "opencode-token",
         "OpenAI OAuth credential is recognized from opencode auth path");

  {
    std::ofstream file(paths.auth_file, std::ios::binary | std::ios::trunc);
    file << "{\"openai\":{\"type\":\"api\",\"key\":\"ava-api-key\"}}";
  }
  auto oauth_preferred = ava::config::load_openai_credential(paths);
  expect(oauth_preferred && oauth_preferred->has_value() && (*oauth_preferred)->access_token == "opencode-token" &&
             (*oauth_preferred)->type == ava::config::OpenAICredentialType::OAuth,
         "OpenAI OAuth credential is preferred over API key fallback");

  std::filesystem::remove(paths.auth_file, remove_error);
  std::filesystem::remove_all(root / "home" / ".ava", remove_error);
  std::filesystem::create_directories(root / "home" / ".ava" / "credentials.json");
  {
    std::ofstream file(root / "data" / "opencode" / "auth.json", std::ios::binary | std::ios::trunc);
    file << "{\"openai\":{\"type\":\"api\",\"key\":\"opencode-api-key\"}}";
  }
  auto api_key = ava::config::load_openai_credential(paths);
  expect(api_key && api_key->has_value() && (*api_key)->access_token == "opencode-api-key" &&
             (*api_key)->type == ava::config::OpenAICredentialType::ApiKey,
         "OpenAI API key auth shape loads after skipping non-regular legacy candidate");

  auto stored = ava::config::store_openai_credential(
      paths, ava::config::OpenAICredential{.type = ava::config::OpenAICredentialType::OAuth,
                                           .access_token = "stored-token",
                                           .refresh_token = "stored-refresh",
                                           .expires_at = 99,
                                           .source_path = {}});
  expect(stored.has_value(), "OpenAI OAuth credential stores to AVA XDG auth file");
  auto stored_oauth = ava::config::load_openai_credential(paths);
  expect(stored_oauth && stored_oauth->has_value() &&
             (*stored_oauth)->type == ava::config::OpenAICredentialType::OAuth &&
             (*stored_oauth)->access_token == "stored-token" && (*stored_oauth)->refresh_token == "stored-refresh" &&
             (*stored_oauth)->expires_at == 99,
         "OpenAI OAuth credential store/load round trips type and token fields");

  auto stored_api = ava::config::store_openai_credential(
      paths, ava::config::OpenAICredential{.type = ava::config::OpenAICredentialType::ApiKey,
                                           .access_token = "stored-api-key",
                                           .refresh_token = "ignored-refresh",
                                           .expires_at = 1,
                                           .source_path = {}});
  expect(stored_api.has_value(), "OpenAI API key credential stores to AVA XDG auth file");
  auto loaded_api = ava::config::load_openai_credential(paths);
  expect(loaded_api && loaded_api->has_value() && (*loaded_api)->type == ava::config::OpenAICredentialType::ApiKey &&
             (*loaded_api)->access_token == "stored-api-key" && (*loaded_api)->refresh_token.empty() &&
             (*loaded_api)->expires_at == 0,
         "OpenAI API key credential store/load round trips without OAuth expiry");

  expect(!ava::config::parse_openai_credential("{\"openai\":{\"type\":\"oauth\",\"api_key\":\"wrong\"}}"),
         "OpenAI credential parser rejects typed OAuth without OAuth token");
  expect(!ava::config::parse_openai_credential("{\"openai\":{\"type\":\"api_key\",\"access_token\":\"wrong\"}}"),
         "OpenAI credential parser rejects typed API key without key field");
  expect(!ava::config::parse_openai_credential("{\"openai\":{\"type\":\"unknown\",\"key\":\"wrong\"}}"),
         "OpenAI credential parser rejects unknown auth type");
  auto expired_oauth = ava::config::openai_access_token_for_request(
      ava::config::OpenAICredential{.type = ava::config::OpenAICredentialType::OAuth,
                                    .access_token = "expired-token",
                                    .refresh_token = "",
                                    .expires_at = 10,
                                    .source_path = paths.auth_file},
      11);
  expect(!expired_oauth && expired_oauth.error().category() == ava::core::ErrorCategory::PermissionDenied,
         "OpenAI expired OAuth credential is not usable for requests");
  auto api_key_not_expired = ava::config::openai_access_token_for_request(
      ava::config::OpenAICredential{.type = ava::config::OpenAICredentialType::ApiKey,
                                    .access_token = "api-token",
                                    .refresh_token = "",
                                    .expires_at = 10,
                                    .source_path = {}},
      11);
  expect(api_key_not_expired && *api_key_not_expired == "api-token", "OpenAI API key ignores OAuth expiry field");
  struct stat st {};
  if (::stat(paths.auth_file.c_str(), &st) == 0) {
    expect((st.st_mode & 0777) == 0600, "auth file is owner-only");
  } else {
    expect(false, "auth file stat succeeds");
  }
  struct stat dir_st {};
  if (::stat(paths.auth_file.parent_path().c_str(), &dir_st) == 0) {
    expect((dir_st.st_mode & 0777) == 0700, "auth directory is owner-only");
  } else {
    expect(false, "auth directory stat succeeds");
  }

  const auto symlink_target = root / "symlink-target.json";
  {
    std::ofstream file(symlink_target, std::ios::binary | std::ios::trunc);
    file << "unchanged";
  }
  std::filesystem::remove(paths.auth_file, remove_error);
  std::error_code symlink_error;
  std::filesystem::create_symlink(symlink_target, paths.auth_file, symlink_error);
  if (!symlink_error) {
    auto symlink_store = ava::config::store_openai_credential(
        paths, ava::config::OpenAICredential{.type = ava::config::OpenAICredentialType::ApiKey,
                                             .access_token = "must-not-write-through-symlink",
                                             .refresh_token = "",
                                             .expires_at = 0,
                                             .source_path = {}});
    expect(!symlink_store, "OpenAI credential store rejects symlink auth file");
    std::ifstream target_read(symlink_target, std::ios::binary);
    std::string target_content;
    target_read >> target_content;
    expect(target_content == "unchanged", "OpenAI credential store does not write through symlink");
  }
}

void test_model_and_prompt_config() {
  const auto root = temp_root() / "model";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  setenv("HOME", (root / "home").c_str(), 1);
  setenv("XDG_CONFIG_HOME", (root / "config").c_str(), 1);
  setenv("XDG_STATE_HOME", (root / "state").c_str(), 1);
  setenv("XDG_DATA_HOME", (root / "data").c_str(), 1);
  const auto paths = ava::config::xdg_paths();

  const auto builtin = ava::config::builtin_model_registry();
  auto selected = ava::config::select_default_model(builtin);
  expect(selected.provider_id == "openai" && selected.model_id == "gpt-5.5", "default model is OpenAI GPT-5.5");

  std::filesystem::create_directories(paths.ava_config_dir);
  {
    std::ofstream file(paths.models_file, std::ios::binary | std::ios::trunc);
    file << "{\"default_provider\":\"openai\",\"default_model\":\"gpt-5.5-mini\","
            "\"models\":[{\"provider\":\"openai\",\"id\":\"gpt-5.5-mini\",\"name\":\"Mini\",\"family\":\"gpt-5\"}]}";
  }
  auto registry = ava::config::load_model_registry(paths);
  expect(registry.has_value(), "model registry loads XDG override");
  if (registry) {
    selected = ava::config::select_default_model(*registry);
    expect(selected.model_id == "gpt-5.5-mini" && selected.display_name == "Mini",
           "default model override selects user model");
  }

  auto inferred_family = ava::config::select_default_model(
      ava::config::ModelRegistry{.default_provider_id = "openai", .default_model_id = "gpt-5.5", .models = {}});
  expect(inferred_family.family == "gpt-5", "GPT-5.5 model id infers GPT-5 prompt family");

  auto prompt = ava::config::select_prompt(paths, selected, ava::agent::Mode::Build);
  expect(prompt && !prompt->from_override && prompt->text.find("Provider=openai") != std::string::npos,
         "builtin prompt selects by provider and family");
  std::filesystem::create_directories(paths.prompts_dir / "openai" / "gpt-5");
  {
    std::ofstream file(paths.prompts_dir / "openai" / "gpt-5" / "plan.txt", std::ios::binary | std::ios::trunc);
    file << "custom plan prompt";
  }
  auto override = ava::config::select_prompt(paths, selected, ava::agent::Mode::Plan);
  expect(override && override->from_override && override->text == "custom plan prompt",
         "prompt override loads from XDG config");

  {
    std::ofstream file(paths.models_file, std::ios::binary | std::ios::trunc);
    file << std::string((1024 * 1024) + 1, 'x');
  }
  auto oversized_registry = ava::config::load_model_registry(paths);
  expect(!oversized_registry, "oversized model config is rejected");

  {
    std::ofstream file(paths.prompts_dir / "openai" / "gpt-5" / "plan.txt", std::ios::binary | std::ios::trunc);
    file << std::string((256 * 1024) + 1, 'x');
  }
  auto oversized_prompt = ava::config::select_prompt(paths, selected, ava::agent::Mode::Plan);
  expect(!oversized_prompt, "oversized prompt override is rejected");
}

void test_openai_provider_contract() {
  const ava::provider::OpenAIProvider provider("https://api.example.test");
  const auto request = provider.build_request(
      ava::provider::ProviderRequest{
          .provider_id = "openai",
          .model_id = "gpt-5.5",
          .system_prompt = "system",
          .messages = {ava::provider::ChatMessage{.role = "user", .content = "hello \"ava\""}},
          .tools_json = {"{\"type\":\"function\",\"name\":\"read_file\"}"}},
      "oauth-token");
  expect(request.has_value(), "OpenAI request builds with OAuth token");
  if (request) {
    expect(request->method == "POST" && request->url == "https://api.example.test/v1/responses",
           "OpenAI request targets responses endpoint");
    expect(request->headers.at("Authorization") == "Bearer oauth-token", "OpenAI request uses OAuth bearer header");
    expect(request->body.find("\"model\":\"gpt-5.5\"") != std::string::npos, "OpenAI request includes model id");
    expect(request->body.find("\"stream\":true") != std::string::npos, "OpenAI request defaults to streaming");
    expect(request->body.find("hello \\\"ava\\\"") != std::string::npos, "OpenAI request JSON escapes message content");
    expect(request->body.find("\"tools\":[{\"type\":\"function\",\"name\":\"read_file\"}]") != std::string::npos,
           "OpenAI request includes tools array");
    expect(request->timeout_ms == 60000, "OpenAI request carries default HTTP timeout");
  }

  const auto non_stream_request = provider.build_request(ava::provider::ProviderRequest{.provider_id = "openai",
                                                                                        .model_id = "gpt-5.5",
                                                                                        .system_prompt = "system",
                                                                                        .messages = {},
                                                                                        .tools_json = {},
                                                                                        .stream = false},
                                                         "oauth-token");
  expect(non_stream_request && non_stream_request->body.find("\"stream\":false") != std::string::npos,
         "OpenAI request preserves stream=false body field");

  const auto expired_credential_request = provider.build_request(
      ava::provider::ProviderRequest{
          .provider_id = "openai", .model_id = "gpt-5.5", .system_prompt = "system", .messages = {}, .tools_json = {}},
      ava::config::OpenAICredential{.type = ava::config::OpenAICredentialType::OAuth,
                                    .access_token = "expired-token",
                                    .refresh_token = "",
                                    .expires_at = 10,
                                    .source_path = {}},
      11);
  expect(
      !expired_credential_request && expired_credential_request.error().message().find("expired") != std::string::npos,
      "OpenAI provider rejects expired OAuth before building request");

  const auto invalid_tool = provider.build_request(ava::provider::ProviderRequest{.provider_id = "openai",
                                                                                  .model_id = "gpt-5.5",
                                                                                  .system_prompt = "system",
                                                                                  .messages = {},
                                                                                  .tools_json = {"not an object"}},
                                                   "oauth-token");
  expect(!invalid_tool && invalid_tool.error().category() == ava::core::ErrorCategory::InvalidArgument,
         "OpenAI request rejects malformed tool JSON before embedding");

  const auto missing_model = provider.build_request(
      ava::provider::ProviderRequest{
          .provider_id = "openai", .model_id = "", .system_prompt = "system", .messages = {}, .tools_json = {}},
      "oauth-token");
  expect(!missing_model && missing_model.error().category() == ava::core::ErrorCategory::InvalidArgument,
         "OpenAI request rejects empty model");
  const auto missing_token = provider.build_request(
      ava::provider::ProviderRequest{
          .provider_id = "openai", .model_id = "gpt-5.5", .system_prompt = "system", .messages = {}, .tools_json = {}},
      "");
  expect(!missing_token && missing_token.error().category() == ava::core::ErrorCategory::PermissionDenied,
         "OpenAI request rejects empty token");

  const std::string sse =
      "data: {\"type\":\"response.output_text.delta\",\"delta\":\"hi\"}\r\n\r\n"
      "data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_1\",\"name\":\"read_file\"}\r\n\r\n"
      "data: {\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_1\",\"delta\":\"{}\"}\n\n"
      "data: {\"type\":\"response.function_call.done\",\"item_id\":\"call_1\"}\n\n"
      "data: [DONE]\n\n";
  auto events = ava::provider::parse_openai_sse(sse);
  expect(events.has_value(), "OpenAI SSE parses");
  if (events) {
    expect(events->size() == 5, "OpenAI SSE produces expected events");
    expect((*events)[0].type == ava::provider::StreamEventType::TextDelta && (*events)[0].text == "hi",
           "OpenAI SSE text delta parses");
    expect((*events)[1].type == ava::provider::StreamEventType::ToolCallStart && (*events)[1].tool_name == "read_file",
           "OpenAI SSE tool start parses");
    expect((*events)[4].type == ava::provider::StreamEventType::Done, "OpenAI SSE done parses");
  }
  auto http_error = ava::provider::parse_openai_sse_response(ava::provider::HttpResponse{
      .status_code = 401, .headers = {}, .body = "{\"error\":\"bad auth\",\"Authorization\":\"Bearer secret\"}"});
  expect(!http_error && http_error.error().message().find("401") != std::string::npos,
         "OpenAI non-200 response error includes status context");
  if (!http_error) {
    const auto formatted = http_error.error().format();
    expect(formatted.find("body_snippet") != std::string::npos && formatted.find("bad auth") != std::string::npos &&
               formatted.find("Bearer secret") == std::string::npos,
           "OpenAI non-200 response includes sanitized body snippet context");
  }
  expect(
      ava::provider::is_auth_status(401) && ava::provider::is_auth_status(403) && !ava::provider::is_auth_status(429),
      "OpenAI auth status helper classifies auth failures");
  expect(ava::provider::is_retryable_status(429) && ava::provider::is_retryable_status(500) &&
             !ava::provider::is_retryable_status(401),
         "OpenAI retryable status helper classifies transient failures");

  auto completed = ava::provider::parse_openai_sse("data: {\"type\":\"response.completed\"}\n\n");
  expect(completed && completed->size() == 1 && (*completed)[0].type == ava::provider::StreamEventType::Done,
         "OpenAI response.completed event produces done event");
  auto completed_tool = ava::provider::parse_openai_sse(
      "data: {\"type\":\"response.function_call.completed\",\"call_id\":\"call_fallback\"}\n\n");
  expect(completed_tool && completed_tool->size() == 1 &&
             (*completed_tool)[0].type == ava::provider::StreamEventType::ToolCallEnd &&
             (*completed_tool)[0].tool_call_id == "call_fallback",
         "OpenAI function_call.completed uses call_id fallback");
  auto text_fallback =
      ava::provider::parse_openai_sse("data: {\"type\":\"response.text.delta\",\"text\":\"fallback\"}\n\n");
  expect(text_fallback && text_fallback->size() == 1 &&
             (*text_fallback)[0].type == ava::provider::StreamEventType::TextDelta &&
             (*text_fallback)[0].text == "fallback",
         "OpenAI response.text.delta uses text fallback");

  auto unknown = ava::provider::parse_openai_sse("data: {\"type\":\"response.unexpected\"}\n\n");
  expect(unknown && unknown->size() == 1 && (*unknown)[0].type == ava::provider::StreamEventType::Error,
         "OpenAI unknown SSE event produces error event");
  auto malformed = ava::provider::parse_openai_sse("data: {not-json}\n\n");
  expect(malformed && malformed->size() == 1 && (*malformed)[0].type == ava::provider::StreamEventType::Error,
         "OpenAI malformed SSE data produces error event");
  auto api_error = ava::provider::parse_openai_sse(
      "data: {\"type\":\"response.error\",\"error\":{\"message\":\"bad request\"}}\n\n");
  expect(api_error && api_error->size() == 1 && (*api_error)[0].type == ava::provider::StreamEventType::Error &&
             (*api_error)[0].error_message == "bad request",
         "OpenAI SSE error event preserves error message");
  auto text = ava::provider::parse_openai_response_text("{\"output_text\":\"done\"}");
  expect(text && *text == "done", "OpenAI non-stream response text parses");
  auto missing_text = ava::provider::parse_openai_response_text("{\"id\":\"resp_1\"}");
  expect(!missing_text, "OpenAI non-stream response requires expected text field");

  if (request) {
    ava::provider::FakeTransport transport(
        {ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = "ok"}});
    auto fake_response = transport.send(*request);
    expect(fake_response && fake_response->body == "ok" && transport.requests().size() == 1 &&
               transport.requests()[0].timeout_ms == request->timeout_ms,
           "fake transport records offline provider request and preserves timeout");
  }
}

void test_tool_dispatcher() {
  const auto root = temp_root() / "dispatcher";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  {
    std::ofstream file(workspace / "note.txt", std::ios::binary | std::ios::trunc);
    file << "hello dispatcher";
  }

  const ava::agent::ToolDispatcher dispatcher(
      ava::tools::ToolContext{.workspace_dir = workspace, .mode = ava::agent::Mode::Build});
  auto read = dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_read", .name = "read_file", .arguments_json = "{\"path\":\"note.txt\",\"max_bytes\":5}"});
  expect(read && read->success && read->result_text.find("hello") != std::string::npos,
         "tool dispatcher maps read_file provider call to file tool");

  auto nul_path = dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_nul_path", .name = "read_file", .arguments_json = "{\"path\":\"note\\u0000.txt\"}"});
  expect(nul_path && !nul_path->success && nul_path->result_text.find("control byte") != std::string::npos,
         "tool dispatcher rejects NUL bytes decoded into file paths");

  auto nul_command = dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_nul_command", .name = "bash", .arguments_json = "{\"command\":\"pwd\\u0000whoami\"}"});
  expect(nul_command && !nul_command->success && nul_command->result_text.find("control byte") != std::string::npos,
         "tool dispatcher rejects NUL bytes decoded into commands");

  auto malformed_args = dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_bad_args", .name = "read_file", .arguments_json = "{not-json}"});
  expect(malformed_args && !malformed_args->success && malformed_args->result_text.find("required") != std::string::npos,
         "tool dispatcher returns structured errors for malformed tool arguments");

  auto patch = dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_patch",
      .name = "apply_patch",
      .arguments_json = "{\"edits\":[{\"path\":\"note.txt\",\"old_text\":\"dispatcher\",\"new_text\":\"patch\"}]}"});
  expect(patch && patch->success && patch->result_text.find("apply_patch") != std::string::npos,
         "tool dispatcher applies exact patch edits");
  auto patched_read = dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_patched_read", .name = "read_file", .arguments_json = "{\"path\":\"note.txt\"}"});
  expect(patched_read && patched_read->result_text.find("hello patch") != std::string::npos,
         "apply_patch updates file content through file tools");

  auto question = dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_question", .name = "question", .arguments_json = "{\"question\":\"Which approach?\"}"});
  expect(question && question->success && question->result_text.find("Which approach?") != std::string::npos,
         "tool dispatcher exposes question tool response");

  auto unknown = dispatcher.dispatch(
      ava::agent::ProviderToolCall{.id = "call_unknown", .name = "missing_tool", .arguments_json = "{}"});
  expect(unknown && !unknown->success && unknown->result_text.find("unknown tool") != std::string::npos,
         "tool dispatcher returns structured unknown tool errors");

  const auto schemas = ava::agent::ToolDispatcher::tool_schemas_json();
  bool has_apply_patch = false;
  bool has_question = false;
  for (const auto& schema : schemas) {
    has_apply_patch = has_apply_patch || schema.find("apply_patch") != std::string::npos;
    has_question = has_question || schema.find("question") != std::string::npos;
  }
  expect(!schemas.empty() && schemas[0].find("read_file") != std::string::npos && has_apply_patch && has_question,
         "tool dispatcher exposes provider tool schemas");
}

void test_tool_dispatcher_plan_mode_denies_mutation() {
  const auto root = temp_root() / "dispatcher-plan";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  std::filesystem::create_directories(workspace);

  const ava::agent::ToolDispatcher dispatcher(
      ava::tools::ToolContext{.workspace_dir = workspace, .mode = ava::agent::Mode::Plan});
  auto denied = dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_write", .name = "write_file", .arguments_json = "{\"path\":\"main.cpp\",\"content\":\"bad\"}"});
  expect(denied && !denied->success && denied->result_text.find("permission_denied") != std::string::npos,
         "tool dispatcher keeps plan mode source mutation denied inside tools");
  expect(!std::filesystem::exists(workspace / "main.cpp"), "denied plan mode write does not create source file");
}

ava::provider::HttpResponse sse_response(const std::string& body) {
  return ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = body};
}

void test_agent_loop_text_only_turn() {
  const auto root = temp_root() / "agent-text";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  ava::session::SessionStore store(ava::session::SessionStoreOptions{
      .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "text"});
  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::provider::FakeTransport transport(
      {sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"hello user\"}\n\n"
                    "data: [DONE]\n\n")});
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                                .mode = ava::agent::Mode::Build,
                                                                .provider_id = "openai",
                                                                .model_id = "gpt-5.5",
                                                                .system_prompt = "system prompt",
                                                                .access_token = "token"});
  auto result = loop.run_turn("hi", store, provider, transport);
  expect(result && result->final_text == "hello user" && result->tool_calls == 0,
         "agent loop returns text-only provider response");
  expect(transport.requests().size() == 1 && transport.requests()[0].body.find("read_file") != std::string::npos,
         "agent loop includes tool schemas in provider request");
  auto entries = store.load();
  expect(entries && entries->size() == 2 && (*entries)[0].type == ava::session::EntryType::UserMessage &&
             (*entries)[1].type == ava::session::EntryType::AssistantMessage,
         "agent loop persists user and assistant entries for text-only turn");
}

void test_agent_loop_tool_turn_and_continuation() {
  const auto root = temp_root() / "agent-tool";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  {
    std::ofstream file(workspace / "note.txt", std::ios::binary | std::ios::trunc);
    file << "tool content";
  }
  ava::session::SessionStore store(ava::session::SessionStoreOptions{
      .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "tool"});
  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::provider::FakeTransport transport(
      {sse_response(
           "data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_1\",\"name\":\"read_file\"}\n\n"
           "data: "
           "{\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_1\",\"delta\":\"{\\\"path\\\":"
           "\\\"note.txt\\\"}\"}\n\n"
           "data: {\"type\":\"response.function_call.done\",\"item_id\":\"call_1\"}\n\n"
           "data: [DONE]\n\n"),
       sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"read it\"}\n\n"
                    "data: [DONE]\n\n")});
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                                .mode = ava::agent::Mode::Build,
                                                                .provider_id = "openai",
                                                                .model_id = "gpt-5.5",
                                                                .system_prompt = "system prompt",
                                                                .access_token = "token"});
  auto result = loop.run_turn("read note", store, provider, transport);
  expect(result && result->final_text == "read it" && result->tool_calls == 1 && result->provider_iterations == 2,
         "agent loop runs one sequential tool call then continues to final answer");
  expect(transport.requests().size() == 2 && transport.requests()[1].body.find("tool content") != std::string::npos,
         "agent loop sends persisted tool result as continuation context");

  auto entries = store.load();
  expect(entries.has_value(), "agent tool turn session loads");
  if (!entries) return;
  bool saw_tool_call = false;
  bool saw_tool_result = false;
  bool saw_final_assistant = false;
  for (const auto& entry : *entries) {
    saw_tool_call = saw_tool_call || entry.type == ava::session::EntryType::ToolCall;
    saw_tool_result = saw_tool_result || entry.type == ava::session::EntryType::ToolResult;
    saw_final_assistant = saw_final_assistant || (entry.type == ava::session::EntryType::AssistantMessage &&
                                                  entry.data_json.find("read it") != std::string::npos);
  }
  expect(saw_tool_call && saw_tool_result && saw_final_assistant,
         "agent loop persists assistant, tool call, and tool result entries");
}

void test_agent_loop_non_stream_response() {
  const auto root = temp_root() / "agent-non-stream";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  ava::session::SessionStore store(ava::session::SessionStoreOptions{
      .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "nonstream"});
  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::provider::FakeTransport transport({ava::provider::HttpResponse{
      .status_code = 200, .headers = {}, .body = "{\"output_text\":\"plain response with data: literal\"}"}});
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                          .mode = ava::agent::Mode::Build,
                                                          .provider_id = "openai",
                                                          .model_id = "gpt-5.5",
                                                          .system_prompt = "system prompt",
                                                          .access_token = "token",
                                                          .stream = false});
  auto result = loop.run_turn("hi", store, provider, transport);
  expect(result && result->final_text == "plain response with data: literal",
         "agent loop parses non-stream response without sniffing data text");
  expect(!transport.requests().empty() && transport.requests()[0].body.find("\"stream\":false") != std::string::npos,
         "agent loop passes explicit non-stream request expectation");
}

void test_agent_loop_error_paths_and_bounds() {
  const ava::provider::OpenAIProvider provider("https://api.example.test");

  {
    const auto root = temp_root() / "agent-provider-error";
    std::error_code remove_error;
    std::filesystem::remove_all(root, remove_error);
    const auto workspace = root / "workspace";
    std::filesystem::create_directories(workspace);
    ava::session::SessionStore store(ava::session::SessionStoreOptions{
        .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "provider-error"});
    ava::provider::FakeTransport transport({sse_response(
        "data: {\"type\":\"response.error\",\"error\":{\"message\":\"bad request\"}}\n\n")});
    ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                            .mode = ava::agent::Mode::Build,
                                                            .provider_id = "openai",
                                                            .model_id = "gpt-5.5",
                                                            .system_prompt = "system prompt",
                                                            .access_token = "token"});
    auto result = loop.run_turn("hi", store, provider, transport);
    expect(!result && result.error().message().find("provider stream error") != std::string::npos,
           "agent loop returns provider error events");
  }

  {
    const auto root = temp_root() / "agent-empty-transport";
    std::error_code remove_error;
    std::filesystem::remove_all(root, remove_error);
    const auto workspace = root / "workspace";
    std::filesystem::create_directories(workspace);
    ava::session::SessionStore store(ava::session::SessionStoreOptions{
        .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "empty-transport"});
    ava::provider::FakeTransport transport({});
    ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                            .mode = ava::agent::Mode::Build,
                                                            .provider_id = "openai",
                                                            .model_id = "gpt-5.5",
                                                            .system_prompt = "system prompt",
                                                            .access_token = "token"});
    auto result = loop.run_turn("hi", store, provider, transport);
    expect(!result && result.error().message().find("fake transport has no response") != std::string::npos,
           "agent loop returns transport failures");
  }

  {
    const auto root = temp_root() / "agent-empty-response";
    std::error_code remove_error;
    std::filesystem::remove_all(root, remove_error);
    const auto workspace = root / "workspace";
    std::filesystem::create_directories(workspace);
    ava::session::SessionStore store(ava::session::SessionStoreOptions{
        .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "empty-response"});
    ava::provider::FakeTransport transport({sse_response("")});
    ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                            .mode = ava::agent::Mode::Build,
                                                            .provider_id = "openai",
                                                            .model_id = "gpt-5.5",
                                                            .system_prompt = "system prompt",
                                                            .access_token = "token"});
    auto result = loop.run_turn("hi", store, provider, transport);
    expect(!result && result.error().message().find("empty") != std::string::npos,
           "agent loop returns empty provider responses");
  }

  {
    const auto root = temp_root() / "agent-event-bound";
    std::error_code remove_error;
    std::filesystem::remove_all(root, remove_error);
    const auto workspace = root / "workspace";
    std::filesystem::create_directories(workspace);
    ava::session::SessionStore store(ava::session::SessionStoreOptions{
        .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "event-bound"});
    ava::provider::FakeTransport transport({sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"a\"}\n\n"
                                                         "data: [DONE]\n\n")});
    ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                            .mode = ava::agent::Mode::Build,
                                                            .provider_id = "openai",
                                                            .model_id = "gpt-5.5",
                                                            .system_prompt = "system prompt",
                                                            .access_token = "token",
                                                            .max_provider_events = 1});
    auto result = loop.run_turn("hi", store, provider, transport);
    expect(!result && result.error().message().find("event limit") != std::string::npos,
           "agent loop enforces provider event bounds");
  }

  {
    const auto root = temp_root() / "agent-text-bound";
    std::error_code remove_error;
    std::filesystem::remove_all(root, remove_error);
    const auto workspace = root / "workspace";
    std::filesystem::create_directories(workspace);
    ava::session::SessionStore store(ava::session::SessionStoreOptions{
        .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "text-bound"});
    ava::provider::FakeTransport transport({sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"hello\"}\n\n"
                                                         "data: [DONE]\n\n")});
    ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                            .mode = ava::agent::Mode::Build,
                                                            .provider_id = "openai",
                                                            .model_id = "gpt-5.5",
                                                            .system_prompt = "system prompt",
                                                            .access_token = "token",
                                                            .max_assistant_text_bytes = 3});
    auto result = loop.run_turn("hi", store, provider, transport);
    expect(!result && result.error().message().find("text byte limit") != std::string::npos,
           "agent loop enforces assistant text byte bounds");
  }

  {
    const auto root = temp_root() / "agent-arg-bound";
    std::error_code remove_error;
    std::filesystem::remove_all(root, remove_error);
    const auto workspace = root / "workspace";
    std::filesystem::create_directories(workspace);
    ava::session::SessionStore store(ava::session::SessionStoreOptions{
        .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "arg-bound"});
    ava::provider::FakeTransport transport({sse_response(
        "data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_1\",\"name\":\"read_file\"}\n\n"
        "data: {\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_1\",\"delta\":\"{\\\"path\\\":\\\"note.txt\\\"}\"}\n\n"
        "data: [DONE]\n\n")});
    ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                            .mode = ava::agent::Mode::Build,
                                                            .provider_id = "openai",
                                                            .model_id = "gpt-5.5",
                                                            .system_prompt = "system prompt",
                                                            .access_token = "token",
                                                            .max_tool_argument_bytes = 5});
    auto result = loop.run_turn("hi", store, provider, transport);
    expect(!result && result.error().message().find("argument byte limit") != std::string::npos,
           "agent loop enforces tool argument byte bounds");
  }
}

void test_agent_loop_multiple_tools_and_denied_continuation() {
  const auto root = temp_root() / "agent-multi-tools";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  {
    std::ofstream one(workspace / "one.txt", std::ios::binary | std::ios::trunc);
    one << "one";
    std::ofstream two(workspace / "two.txt", std::ios::binary | std::ios::trunc);
    two << "two";
  }
  ava::session::SessionStore store(ava::session::SessionStoreOptions{
      .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "multi"});
  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::provider::FakeTransport transport(
      {sse_response(
           "data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_1\",\"name\":\"read_file\"}\n\n"
           "data: {\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_1\",\"delta\":\"{\\\"path\\\":\\\"one.txt\\\"}\"}\n\n"
           "data: {\"type\":\"response.function_call.done\",\"item_id\":\"call_1\"}\n\n"
           "data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_2\",\"name\":\"read_file\"}\n\n"
           "data: {\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_2\",\"delta\":\"{\\\"path\\\":\\\"two.txt\\\"}\"}\n\n"
           "data: {\"type\":\"response.function_call.done\",\"item_id\":\"call_2\"}\n\n"
           "data: [DONE]\n\n"),
       sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"done\"}\n\n"
                    "data: [DONE]\n\n")});
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                          .mode = ava::agent::Mode::Build,
                                                          .provider_id = "openai",
                                                          .model_id = "gpt-5.5",
                                                          .system_prompt = "system prompt",
                                                          .access_token = "token"});
  auto result = loop.run_turn("read both", store, provider, transport);
  expect(result && result->tool_calls == 2 && result->final_text == "done",
         "agent loop handles multiple tool calls before continuation");

  const auto denied_root = temp_root() / "agent-denied-continuation";
  std::filesystem::remove_all(denied_root, remove_error);
  const auto denied_workspace = denied_root / "workspace";
  std::filesystem::create_directories(denied_workspace);
  ava::session::SessionStore denied_store(ava::session::SessionStoreOptions{
      .root_dir = denied_root / "sessions", .workspace_dir = denied_workspace, .session_id = "denied"});
  ava::provider::FakeTransport denied_transport(
      {sse_response(
           "data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_write\",\"name\":\"write_file\"}\n\n"
           "data: {\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_write\",\"delta\":\"{\\\"path\\\":\\\"src/new.cpp\\\",\\\"content\\\":\\\"bad\\\"}\"}\n\n"
           "data: [DONE]\n\n"),
       sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"permission explained\"}\n\n"
                    "data: [DONE]\n\n")});
  ava::agent::AgentLoop denied_loop(ava::agent::AgentLoopOptions{.workspace_dir = denied_workspace,
                                                                 .mode = ava::agent::Mode::Plan,
                                                                 .provider_id = "openai",
                                                                 .model_id = "gpt-5.5",
                                                                 .system_prompt = "system prompt",
                                                                 .access_token = "token"});
  auto denied_result = denied_loop.run_turn("write source", denied_store, provider, denied_transport);
  expect(denied_result && denied_result->final_text == "permission explained" && denied_result->provider_iterations == 2,
         "agent loop continues after permission-denied tool results");
  expect(denied_transport.requests().size() == 2 &&
             denied_transport.requests()[1].body.find("permission_denied") != std::string::npos,
         "permission-denied tool result is framed into continuation context");
}

void test_agent_loop_truncates_tool_context() {
  const auto root = temp_root() / "agent-tool-truncate";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  {
    std::ofstream large(workspace / "large.txt", std::ios::binary | std::ios::trunc);
    large << std::string(12 * 1024, 'x');
  }
  ava::session::SessionStore store(ava::session::SessionStoreOptions{
      .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "truncate"});
  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::provider::FakeTransport transport(
      {sse_response(
           "data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_large\",\"name\":\"read_file\"}\n\n"
           "data: {\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_large\",\"delta\":\"{\\\"path\\\":\\\"large.txt\\\"}\"}\n\n"
           "data: [DONE]\n\n"),
       sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"ok\"}\n\n"
                    "data: [DONE]\n\n")});
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                          .mode = ava::agent::Mode::Build,
                                                          .provider_id = "openai",
                                                          .model_id = "gpt-5.5",
                                                          .system_prompt = "system prompt",
                                                          .access_token = "token",
                                                          .max_tool_result_context_bytes = 8 * 1024});
  auto result = loop.run_turn("read large", store, provider, transport);
  expect(result && transport.requests().size() == 2 &&
             transport.requests()[1].body.find("tool result context truncated") != std::string::npos,
         "agent loop truncates tool results before provider continuation context");
}

void test_agent_loop_max_iteration_guard() {
  const auto root = temp_root() / "agent-max";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  ava::session::SessionStore store(ava::session::SessionStoreOptions{
      .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "max"});
  const std::string tool_sse =
      "data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_glob\",\"name\":\"glob\"}\n\n"
      "data: "
      "{\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_glob\",\"delta\":\"{\\\"pattern\\\":"
      "\\\"**/*\\\"}\"}\n\n"
      "data: {\"type\":\"response.function_call.done\",\"item_id\":\"call_glob\"}\n\n"
      "data: [DONE]\n\n";
  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::provider::FakeTransport transport({sse_response(tool_sse), sse_response(tool_sse)});
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                                .mode = ava::agent::Mode::Build,
                                                                .provider_id = "openai",
                                                                .model_id = "gpt-5.5",
                                                                .system_prompt = "system prompt",
                                                                .access_token = "token",
                                                                .max_tool_iterations = 2});
  auto result = loop.run_turn("loop", store, provider, transport);
  expect(!result && result.error().message().find("maximum tool iterations") != std::string::npos,
         "agent loop stops repeated tool use at max iteration guard");
  auto entries = store.load();
  expect(entries && std::ranges::any_of(*entries,
                                        [](const ava::session::SessionEntry& entry) {
                                          return entry.type == ava::session::EntryType::Error &&
                                                 entry.data_json.find("maximum tool iterations") != std::string::npos;
                                        }),
         "agent loop persists max iteration guard as error event");
}

void test_tui_composer_rendering_and_input() {
  expect(ava::tui::parse_input_byte('a').key == ava::tui::Key::Character, "tui parses characters");
  expect(ava::tui::parse_input_byte('\n').key == ava::tui::Key::Enter, "tui parses enter");
  expect(ava::tui::parse_input_byte('\r').key == ava::tui::Key::Enter, "tui parses carriage-return enter");
  expect(ava::tui::parse_input_byte('\t').key == ava::tui::Key::Tab, "tui parses tab");
  expect(ava::tui::parse_input_byte(0x7F).key == ava::tui::Key::Backspace, "tui parses backspace");
  expect(ava::tui::parse_input_byte(0x08).key == ava::tui::Key::Backspace, "tui parses alternate backspace");
  expect(ava::tui::parse_input_byte(0x03).key == ava::tui::Key::CtrlC, "tui parses ctrl-c");
  expect(ava::tui::parse_input_byte(0x04).key == ava::tui::Key::CtrlD, "tui parses ctrl-d");
  expect(ava::tui::parse_input_byte(0x1B).key == ava::tui::Key::Escape, "tui parses escape");
  expect(ava::tui::parse_input_byte(0x01).key == ava::tui::Key::Unknown, "tui treats unknown controls as unknown");
  std::string utf8_input = std::string("ab") + "\xC3\xA9";
  ava::tui::erase_last_utf8_codepoint(utf8_input);
  expect(utf8_input == "ab", "tui backspace erases a complete utf-8 codepoint");

  const auto split_empty = ava::tui::split_lines("");
  expect(split_empty.size() == 1 && split_empty.front().empty(), "tui split keeps empty input as one line");
  const auto split_trailing = ava::tui::split_lines("a\n");
  expect(split_trailing.size() == 2 && split_trailing[0] == "a" && split_trailing[1].empty(),
         "tui split preserves trailing empty line");

  const auto lines = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "/help",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{.label = "you", .text = "hello"},
                     ava::tui::TranscriptItem{.label = "ava", .text = "world"}},
      .width = 80,
      .height = 10});
  expect(lines.size() >= 5, "tui renders a bounded screen");
  expect(!lines.empty() && lines.front().find("AVA 0.1") != std::string::npos, "tui renders title");
  expect(std::ranges::any_of(lines, [](const std::string& line) { return line.find("[build] ava> /help") != std::string::npos; }),
         "tui renders composer input");

  const auto sanitized = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "bad\x1b[31mred"}},
      .width = 80,
      .height = 8});
  expect(std::ranges::none_of(sanitized, [](const std::string& line) { return line.find('\x1b') != std::string::npos; }),
         "tui render sanitizes transcript escape bytes");
  expect(ava::tui::sanitize_terminal_text(std::string("osc") + static_cast<char>(0x9D) + "payload") == "osc?payload",
         "tui sanitizes raw c1 terminal control bytes");
  expect(ava::tui::sanitize_terminal_text(std::string("ok ") + "\xC3\xA9") == std::string("ok ") + "\xC3\xA9",
         "tui sanitizer preserves valid utf-8 text");

  const auto screen = ava::tui::render_screen(ava::tui::ComposerSnapshot{.mode = "plan",
                                                                         .provider = "openai",
                                                                         .model = "gpt-5.5",
                                                                         .session_id = "session_test",
                                                                         .input = "hello",
                                                                         .status = "ready",
                                                                         .transcript = {},
                                                                         .width = 40,
                                                                         .height = 8});
  expect(screen.find("\x1b[?25l") != std::string::npos && screen.find("\x1b[H") != std::string::npos &&
             screen.find("\x1b[2K") != std::string::npos && screen.find("\x1b[?25h") != std::string::npos,
         "tui render_screen emits expected terminal control envelope");

  std::vector<ava::tui::TranscriptItem> many_items;
  for (int index = 0; index < 20; ++index) {
    many_items.push_back(ava::tui::TranscriptItem{.label = "line", .text = "item " + std::to_string(index)});
  }
  const auto scrolled = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                             .provider = "openai",
                                                                             .model = "gpt-5.5",
                                                                             .session_id = "session_test",
                                                                             .input = "",
                                                                             .status = "ready",
                                                                             .transcript = many_items,
                                                                             .width = 40,
                                                                             .height = 8});
  expect(std::ranges::any_of(scrolled, [](const std::string& line) { return line.find("item 19") != std::string::npos; }) &&
             std::ranges::none_of(scrolled, [](const std::string& line) { return line.find("item 0") != std::string::npos; }),
         "tui transcript viewport keeps newest lines");

  const auto multiline = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "one\ntwo"}},
      .width = 80,
      .height = 10});
  expect(std::ranges::any_of(multiline, [](const std::string& line) { return line.find("ava: one") != std::string::npos; }) &&
             std::ranges::any_of(multiline, [](const std::string& line) { return line.find("ava: two") != std::string::npos; }),
         "tui renders each transcript line with a label");

  const auto utf8 = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = std::string(12, 'x') + "\xC3\xA9" + "zzz"}},
      .width = 20,
      .height = 8});
  expect(std::ranges::none_of(utf8,
                              [](const std::string& line) {
                                return !line.empty() &&
                                       (static_cast<unsigned char>(line.back()) & 0xC0U) == 0xC0U;
                              }),
         "tui truncation does not leave a trailing utf-8 starter byte");
}

}  // namespace

int main() {
  test_mode_parsing();
  test_session_store_round_trip();
  test_session_resume_and_listing();
  test_json_escape_control_characters();
  test_core_json_top_level_lookup();
  test_permission_defaults();
  test_command_classification();
  test_file_tools();
  test_search_tools();
  test_bash_tool();
  test_xdg_paths();
  test_auth_load_and_store();
  test_model_and_prompt_config();
  test_openai_provider_contract();
  test_tool_dispatcher();
  test_tool_dispatcher_plan_mode_denies_mutation();
  test_agent_loop_text_only_turn();
  test_agent_loop_tool_turn_and_continuation();
  test_agent_loop_non_stream_response();
  test_agent_loop_error_paths_and_bounds();
  test_agent_loop_multiple_tools_and_denied_continuation();
  test_agent_loop_truncates_tool_context();
  test_agent_loop_max_iteration_guard();
  test_tui_composer_rendering_and_input();

  if (failures != 0) {
    std::cerr << failures << " test failure(s)\n";
    return 1;
  }
  std::cout << "core tests passed\n";
  return 0;
}
