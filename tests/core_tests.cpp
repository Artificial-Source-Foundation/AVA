#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "ava/agent/agent_loop.h"
#include "ava/agent/mode.h"
#include "ava/agent/tool_dispatcher.h"
#include "ava/app/commands.h"
#include "ava/app/events.h"
#include "ava/app/headless_policy.h"
#include "ava/app/print_mode.h"
#include "ava/app/rpc_mode.h"
#include "ava/app/runtime.h"
#include "ava/config/auth.h"
#include "ava/config/model_config.h"
#include "ava/config/openai_oauth.h"
#include "ava/config/prompt_config.h"
#include "ava/config/xdg_paths.h"
#include "ava/context/context_loader.h"
#include "ava/core/ids.h"
#include "ava/core/json.h"
#include "ava/permissions/permission.h"
#include "ava/provider/openai_provider.h"
#include "ava/session/compaction.h"
#include "ava/session/export.h"
#include "ava/session/session_store.h"
#include "ava/tools/bash_tool.h"
#include "ava/tools/file_tools.h"
#include "ava/tools/search_tools.h"
#include "ava/tui/composer.h"
#include "ava/tui/terminal.h"
#include "tests/support/fake_transport.h"

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

class FailingStreambuf final : public std::streambuf {
 protected:
  int overflow(int ch) override {
    static_cast<void>(ch);
    return traits_type::eof();
  }

  std::streamsize xsputn(const char* s, std::streamsize count) override {
    static_cast<void>(s);
    static_cast<void>(count);
    return 0;
  }
};

std::filesystem::path temp_root() {
  const auto build_name = std::filesystem::current_path().filename();
  return std::filesystem::temp_directory_path() / ("ava_core_tests_" + build_name.string());
}

class ScopedEnvVar {
 public:
  ScopedEnvVar(std::string name, std::string value) : name_(std::move(name)) {
    if (const char* current = std::getenv(name_.c_str())) {
      previous_ = current;
    }
    setenv(name_.c_str(), value.c_str(), 1);
  }

  ScopedEnvVar(const ScopedEnvVar&) = delete;
  ScopedEnvVar& operator=(const ScopedEnvVar&) = delete;
  ScopedEnvVar(ScopedEnvVar&&) = delete;
  ScopedEnvVar& operator=(ScopedEnvVar&&) = delete;

  ~ScopedEnvVar() {
    if (previous_) {
      setenv(name_.c_str(), previous_->c_str(), 1);
    } else {
      unsetenv(name_.c_str());
    }
  }

 private:
  std::string name_;
  std::optional<std::string> previous_ = std::nullopt;
};

std::string strip_sgr(std::string_view text) {
  std::string stripped;
  stripped.reserve(text.size());
  for (std::size_t index = 0; index < text.size();) {
    if (text[index] == '\x1b' && index + 1 < text.size() && text[index + 1] == '[') {
      auto end = index + 2;
      while (end < text.size() && text[end] != 'm') {
        ++end;
      }
      if (end < text.size()) {
        index = end + 1;
        continue;
      }
    }
    stripped.push_back(text[index]);
    ++index;
  }
  return stripped;
}

bool has_active_sgr_at_text(std::string_view line, std::string_view text, std::string_view sgr) {
  const auto text_pos = line.find(text);
  if (text_pos == std::string_view::npos) return false;
  const auto sgr_pos = line.rfind(sgr, text_pos);
  if (sgr_pos == std::string_view::npos) return false;
  const auto reset_pos = line.rfind("\x1b[0m", text_pos);
  return reset_pos == std::string_view::npos || reset_pos < sgr_pos;
}

ava::core::VoidResult append_permission_audit_for_test(ava::session::SessionStore& store,
                                                       const ava::tools::PermissionAuditEvent& event) {
  return store.append(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                 .parent_id = "",
                                                 .type = ava::session::EntryType::PermissionDecision,
                                                 .timestamp = ava::session::now_timestamp(),
                                                 .data_json = ava::tools::permission_audit_data_json(event)});
}

std::vector<ava::session::SessionEntry> permission_entries(const std::vector<ava::session::SessionEntry>& entries) {
  std::vector<ava::session::SessionEntry> filtered;
  for (const auto& entry : entries) {
    if (entry.type == ava::session::EntryType::PermissionDecision) filtered.push_back(entry);
  }
  return filtered;
}

std::size_t visible_columns(std::string_view text) {
  const auto stripped = strip_sgr(text);
  std::size_t columns = 0;
  for (std::size_t index = 0; index < stripped.size();) {
    const auto byte = static_cast<unsigned char>(stripped[index]);
    char32_t codepoint = 0;
    std::size_t length = 1;
    if ((byte & 0x80U) == 0) {
      codepoint = byte;
    } else if (byte >= 0xC2U && byte <= 0xDFU) {
      codepoint = byte & 0x1FU;
      length = 2;
    } else if ((byte & 0xF0U) == 0xE0U) {
      codepoint = byte & 0x0FU;
      length = 3;
    } else if (byte >= 0xF0U && byte <= 0xF4U) {
      codepoint = byte & 0x07U;
      length = 4;
    }
    if (index + length > stripped.size()) {
      ++columns;
      break;
    }
    bool valid = length == 1;
    for (std::size_t offset = 1; offset < length; ++offset) {
      const auto continuation = static_cast<unsigned char>(stripped[index + offset]);
      valid = (continuation & 0xC0U) == 0x80U;
      if (!valid) break;
      codepoint = (codepoint << 6U) | (continuation & 0x3FU);
    }
    if (!valid) {
      ++columns;
      ++index;
      continue;
    }
    const auto width = codepoint <= static_cast<char32_t>(WCHAR_MAX) ? ::wcwidth(static_cast<wchar_t>(codepoint)) : 1;
    const auto fallback_wide =
        (codepoint >= 0x1100 && codepoint <= 0x115F) || (codepoint >= 0x2329 && codepoint <= 0x232A) ||
        (codepoint >= 0x2E80 && codepoint <= 0xA4CF) || (codepoint >= 0xAC00 && codepoint <= 0xD7A3) ||
        (codepoint >= 0xF900 && codepoint <= 0xFAFF) || (codepoint >= 0xFE10 && codepoint <= 0xFE19) ||
        (codepoint >= 0xFE30 && codepoint <= 0xFE6F) || (codepoint >= 0xFF00 && codepoint <= 0xFF60) ||
        (codepoint >= 0xFFE0 && codepoint <= 0xFFE6) || (codepoint >= 0x1F300 && codepoint <= 0x1FAFF) ||
        (codepoint >= 0x20000 && codepoint <= 0x3FFFD);
    columns += width > 0 ? static_cast<std::size_t>(width) : (fallback_wide ? std::size_t{2} : std::size_t{1});
    index += length;
  }
  return columns;
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

  expect(first
             ->append(ava::session::SessionEntry{.id = "entry_first",
                                                 .parent_id = "",
                                                 .type = ava::session::EntryType::UserMessage,
                                                 .timestamp = "2026-04-27T00:00:00Z",
                                                 .data_json = "{\"text\":\"first\"}"})
             .has_value(),
         "first resume test session appends");
  expect(second
             ->append(ava::session::SessionEntry{.id = "entry_second",
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

void test_session_compaction_entry_round_trip() {
  const auto root = temp_root() / "compaction-round-trip";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  std::filesystem::create_directories(workspace);

  ava::session::SessionStore store(ava::session::SessionStoreOptions{
      .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "compact"});
  auto config = ava::session::default_compaction_config();
  config.auto_threshold_tokens = 1234;
  config.keep_recent_tokens = 321;
  config.keep_recent_messages = 6;
  config.model_id = "gpt-5.5-mini";
  config.max_summary_bytes = 1024;

  auto appended = ava::session::append_manual_compaction(
      store, ava::session::ManualCompactionRequest{.summary = "Prior work summary",
                                                   .instructions = "Keep the recent plan.",
                                                   .config = config,
                                                   .estimated_tokens = 1300});
  expect(appended.has_value(), "manual compaction entry appends");

  auto loaded = store.load();
  expect(loaded && loaded->size() == 1 && (*loaded)[0].type == ava::session::EntryType::Compaction,
         "compaction entry type round trips through session store");
  if (loaded && !loaded->empty()) {
    expect(ava::core::json::string_field((*loaded)[0].data_json, "summary") == "Prior work summary",
           "compaction summary round trips");
    expect(ava::core::json::string_field((*loaded)[0].data_json, "instructions") == "Keep the recent plan.",
           "compaction instructions round trips");
    expect(ava::core::json::string_field((*loaded)[0].data_json, "model") == "gpt-5.5-mini",
           "compaction model metadata round trips");
    expect(ava::core::json::integer_field((*loaded)[0].data_json, "threshold_tokens") == 1234,
           "compaction threshold metadata round trips");
  }

  ava::session::SessionStore unavailable_store(ava::session::SessionStoreOptions{
      .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "compact-unavailable"});
  auto unavailable = ava::session::append_manual_compaction(
      unavailable_store, ava::session::ManualCompactionRequest{.summary = "", .instructions = "", .config = config});
  auto unavailable_loaded = unavailable_store.load();
  std::optional<std::string> unavailable_summary;
  if (unavailable_loaded && !unavailable_loaded->empty()) {
    unavailable_summary = ava::core::json::string_field((*unavailable_loaded)[0].data_json, "summary");
  }
  expect(unavailable && unavailable_summary && unavailable_summary->find("unavailable") != std::string::npos,
         "manual compaction records deterministic unavailable summary when no provider summary exists");

  auto tiny_config = config;
  tiny_config.max_summary_bytes = 1;
  ava::session::SessionStore tiny_unavailable_store(ava::session::SessionStoreOptions{
      .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "compact-tiny-unavailable"});
  auto tiny_unavailable = ava::session::append_manual_compaction(
      tiny_unavailable_store,
      ava::session::ManualCompactionRequest{.summary = "", .instructions = "", .config = tiny_config});
  auto tiny_loaded = tiny_unavailable_store.load();
  std::optional<std::string> tiny_summary;
  if (tiny_loaded && !tiny_loaded->empty()) {
    tiny_summary = ava::core::json::string_field((*tiny_loaded)[0].data_json, "summary");
  }
  expect(tiny_unavailable && tiny_summary && tiny_summary->find("unavailable") != std::string::npos,
         "empty manual compaction succeeds with tiny summary limit");

  ava::session::SessionStore oversized_summary_store(ava::session::SessionStoreOptions{
      .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "compact-oversized-summary"});
  auto oversized_summary = ava::session::append_manual_compaction(
      oversized_summary_store,
      ava::session::ManualCompactionRequest{.summary = "xx", .instructions = "", .config = tiny_config});
  expect(!oversized_summary && oversized_summary.error().category() == ava::core::ErrorCategory::InvalidArgument,
         "manual compaction rejects oversized user summary with tiny summary limit");
}

void test_session_markdown_export() {
  const std::vector<ava::session::SessionEntry> entries = {
      ava::session::SessionEntry{.id = "start_1",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::SessionStart,
                                 .timestamp = "2026-04-29T00:00:00Z",
                                 .data_json = "{\"mode\":\"build\",\"provider\":\"openai\",\"model\":\"gpt-5.5\"}"},
      ava::session::SessionEntry{.id = "user_1",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::UserMessage,
                                 .timestamp = "2026-04-29T00:00:01Z",
                                 .data_json = "{\"text\":\"Hello AVA\"}"},
      ava::session::SessionEntry{.id = "assistant_1",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::AssistantMessage,
                                 .timestamp = "2026-04-29T00:00:02Z",
                                 .data_json = "{\"text\":\"Hello human\",\"tool_calls\":1}"},
      ava::session::SessionEntry{
          .id = "tool_call_1",
          .parent_id = "",
          .type = ava::session::EntryType::ToolCall,
          .timestamp = "2026-04-29T00:00:03Z",
          .data_json =
              "{\"call_id\":\"call_1\",\"name\":\"read_file\",\"arguments\":\"{\\\"path\\\":\\\"README.md\\\"}\"}"},
      ava::session::SessionEntry{.id = "tool_result_1",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::ToolResult,
                                 .timestamp = "2026-04-29T00:00:04Z",
                                 .data_json = "{\"call_id\":\"call_1\",\"name\":\"read_file\",\"success\":true,"
                                              "\"result\":\"tool output\"}"},
      ava::session::SessionEntry{.id = "compact_1",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::Compaction,
                                 .timestamp = "2026-04-29T00:00:05Z",
                                 .data_json = "{\"trigger\":\"manual\",\"status\":\"recorded\","
                                              "\"summary_unavailable\":false,\"summary\":\"Prior summary\","
                                              "\"instructions\":\"Keep this constraint\",\"model\":\"gpt-5.5-mini\","
                                              "\"threshold_tokens\":100,\"estimated_tokens\":125,"
                                              "\"keep_recent_tokens\":64,\"keep_recent_messages\":4}"},
      ava::session::SessionEntry{.id = "mode_1",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::ModeChange,
                                 .timestamp = "2026-04-29T00:00:06Z",
                                 .data_json = "{\"mode\":\"plan\"}"},
      ava::session::SessionEntry{
          .id = "error_1",
          .parent_id = "",
          .type = ava::session::EntryType::Error,
          .timestamp = "2026-04-29T00:00:07Z",
          .data_json = "{\"category\":\"tool\",\"message\":\"failed\",\"details\":\"tool: failed\"}"},
      ava::session::SessionEntry{.id = "backticks_1",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::UserMessage,
                                 .timestamp = "2026-04-29T00:00:08Z",
                                 .data_json = R"json({"text":"before ``` after ```` done"})json"},
      ava::session::SessionEntry{.id = "control_1",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::UserMessage,
                                 .timestamp = "2026-04-29T00:00:09Z",
                                 .data_json = R"json({"text":"first\nsecond\tindent\u0000\u001B\u007F\r"})json"},
  };

  const auto basic = ava::session::format_session_markdown(entries);
  expect(basic.find("# AVA Session Export") != std::string::npos, "markdown export has deterministic title");
  expect(basic.find("## User") != std::string::npos && basic.find("Hello AVA") != std::string::npos,
         "markdown export renders user messages");
  expect(basic.find("## Assistant") != std::string::npos && basic.find("Hello human") != std::string::npos,
         "markdown export renders assistant messages");
  expect(basic.find("## Tool Call") == std::string::npos && basic.find("README.md") == std::string::npos,
         "markdown export omits tool details by default");
  expect(basic.find("## Compaction") != std::string::npos && basic.find("Prior summary") != std::string::npos &&
             basic.find("Keep this constraint") != std::string::npos,
         "markdown export renders compactions by default");
  expect(basic.find("## Mode Change") != std::string::npos && basic.find("## Error") != std::string::npos,
         "markdown export renders mode changes and errors");
  expect(basic.find("Metadata:") == std::string::npos && basic.find("\"id\":\"user_1\"") == std::string::npos,
         "markdown export omits metadata by default");
  expect(basic.find("`````text\nbefore ``` after ```` done\n`````") != std::string::npos,
         "markdown export expands fences around backtick content");
  const std::string escaped_control_markdown = std::string("first\nsecond\tindent") + "\\u0000\\u001B\\u007F\\u000D";
  expect(basic.find(escaped_control_markdown) != std::string::npos,
         "markdown export escapes decoded fenced control bytes while preserving newlines and tabs");
  expect(basic.find('\0') == std::string::npos && basic.find('\x1B') == std::string::npos &&
             basic.find('\x7F') == std::string::npos && basic.find('\r') == std::string::npos,
         "markdown export does not emit raw NUL, escape, DEL, or carriage return bytes");

  const auto with_tools = ava::session::format_session_markdown(
      entries, ava::session::ExportOptions{.include_tool_details = true, .include_metadata = false});
  expect(with_tools.find("## Tool Call") != std::string::npos && with_tools.find("README.md") != std::string::npos &&
             with_tools.find("## Tool Result") != std::string::npos &&
             with_tools.find("tool output") != std::string::npos,
         "markdown export includes tool calls and results when requested");

  const auto without_compactions = ava::session::format_session_markdown(
      entries, ava::session::ExportOptions{
                   .include_tool_details = false, .include_metadata = false, .include_compactions = false});
  expect(without_compactions.find("## Compaction") == std::string::npos &&
             without_compactions.find("Prior summary") == std::string::npos,
         "markdown export can omit compaction entries");

  const auto with_metadata = ava::session::format_session_markdown(
      entries, ava::session::ExportOptions{
                   .include_tool_details = false, .include_metadata = true, .include_compactions = true});
  expect(with_metadata.find("Metadata:") != std::string::npos &&
             with_metadata.find("\"id\":\"user_1\"") != std::string::npos &&
             with_metadata.find("Estimated tokens") != std::string::npos &&
             with_metadata.find("gpt-5.5-mini") != std::string::npos,
         "markdown export includes entry and compaction metadata when requested");
}

void test_compaction_config_and_thresholds() {
  const auto root = temp_root() / "compaction-config";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  setenv("HOME", (root / "home").c_str(), 1);
  setenv("XDG_CONFIG_HOME", (root / "config").c_str(), 1);
  setenv("XDG_STATE_HOME", (root / "state").c_str(), 1);
  setenv("XDG_DATA_HOME", (root / "data").c_str(), 1);
  const auto paths = ava::config::xdg_paths();

  expect(paths.compaction_file == root / "config" / "ava" / "compaction.json",
         "compaction config path follows XDG config home");
  auto missing = ava::session::load_compaction_config(paths);
  expect(missing && missing->model_id == "gpt-5.5" && missing->auto_threshold_tokens == 0,
         "missing compaction config uses safe defaults");

  std::filesystem::create_directories(paths.ava_config_dir);
  {
    std::ofstream file(paths.compaction_file, std::ios::binary | std::ios::trunc);
    file << "{\"auto_threshold_tokens\":4096,\"keep_recent_tokens\":512,"
            "\"keep_recent_messages\":7,\"compaction_model\":\"gpt-5.5-compact\","
            "\"max_summary_bytes\":2048}";
  }
  auto loaded = ava::session::load_compaction_config(paths);
  expect(loaded && loaded->auto_threshold_tokens == 4096 && loaded->keep_recent_tokens == 512 &&
             loaded->keep_recent_messages == 7 && loaded->model_id == "gpt-5.5-compact" &&
             loaded->max_summary_bytes == 2048,
         "compaction config parses token budgets and model id from XDG file");

  expect(ava::session::estimate_tokens("") == 0 && ava::session::estimate_tokens("abcd") == 1 &&
             ava::session::estimate_tokens("abcde") == 2,
         "compaction token estimate uses deterministic chars over four heuristic");
  const std::vector<ava::session::SessionEntry> entries = {
      ava::session::SessionEntry{.id = "u",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::UserMessage,
                                 .timestamp = "2026-04-27T00:00:00Z",
                                 .data_json = "{\"text\":\"abcdefgh\"}"},
      ava::session::SessionEntry{.id = "ignored",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::SessionStart,
                                 .timestamp = "2026-04-27T00:00:00Z",
                                 .data_json = "{\"mode\":\"build\"}"}};
  auto config = ava::session::default_compaction_config();
  config.auto_threshold_tokens = ava::session::estimate_session_tokens(entries);
  const auto decision = ava::session::should_auto_compact(entries, config);
  expect(decision.should_compact && decision.estimated_tokens == config.auto_threshold_tokens,
         "auto compaction triggers when estimated tokens reach threshold");
  config.auto_threshold_tokens = decision.estimated_tokens + 1;
  expect(!ava::session::should_auto_compact(entries, config).should_compact,
         "auto compaction does not trigger below threshold");
  config.auto_threshold_tokens = 0;
  expect(!ava::session::should_auto_compact(entries, config).should_compact,
         "auto compaction threshold zero disables automatic compaction");
}

void test_compaction_context_reconstruction() {
  const std::vector<ava::session::SessionEntry> entries = {
      ava::session::SessionEntry{.id = "old_user",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::UserMessage,
                                 .timestamp = "2026-04-27T00:00:00Z",
                                 .data_json = "{\"text\":\"old raw user\"}"},
      ava::session::SessionEntry{.id = "old_assistant",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::AssistantMessage,
                                 .timestamp = "2026-04-27T00:00:01Z",
                                 .data_json = "{\"text\":\"old raw assistant\"}"},
      ava::session::SessionEntry{
          .id = "old_tool",
          .parent_id = "",
          .type = ava::session::EntryType::ToolResult,
          .timestamp = "2026-04-27T00:00:02Z",
          .data_json = "{\"call_id\":\"call_old\",\"name\":\"read_file\",\"result\":\"old raw tool\"}"},
      ava::session::SessionEntry{.id = "compact_1",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::Compaction,
                                 .timestamp = "2026-04-27T00:00:03Z",
                                 .data_json = "{\"summary\":\"first summary\",\"instructions\":\"ignored later\"}"},
      ava::session::SessionEntry{.id = "middle_user",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::UserMessage,
                                 .timestamp = "2026-04-27T00:00:04Z",
                                 .data_json = "{\"text\":\"middle raw user\"}"},
      ava::session::SessionEntry{.id = "compact_2",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::Compaction,
                                 .timestamp = "2026-04-27T00:00:05Z",
                                 .data_json = "{\"summary\":\"latest summary\",\"instructions\":\"carry this\"}"},
      ava::session::SessionEntry{.id = "new_user",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::UserMessage,
                                 .timestamp = "2026-04-27T00:00:06Z",
                                 .data_json = "{\"text\":\"new user\"}"},
      ava::session::SessionEntry{.id = "new_assistant",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::AssistantMessage,
                                 .timestamp = "2026-04-27T00:00:07Z",
                                 .data_json = "{\"text\":\"new assistant\"}"}};

  auto messages = ava::agent::build_provider_messages_from_entries(entries);
  expect(messages && messages->size() == 3, "compacted context reconstructs summary plus post-compaction turns");
  if (!messages) return;
  expect((*messages)[0].role == "user" && (*messages)[0].content.find("latest summary") != std::string::npos &&
             (*messages)[0].content.find("carry this") != std::string::npos,
         "latest compaction summary becomes provider-visible context");
  const std::string joined = (*messages)[0].content + (*messages)[1].content + (*messages)[2].content;
  expect(joined.find("old raw user") == std::string::npos && joined.find("old raw assistant") == std::string::npos &&
             joined.find("old raw tool") == std::string::npos && joined.find("middle raw user") == std::string::npos &&
             joined.find("first summary") == std::string::npos,
         "context reconstruction omits raw messages and tool results before latest compaction");
  expect((*messages)[1].role == "user" && (*messages)[1].content == "new user" && (*messages)[2].role == "assistant" &&
             (*messages)[2].content == "new assistant",
         "post-compaction entries remain normal provider messages");
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
  expect(!ava::core::json::string_field("{\"text\":\"\\q\"}", "text"), "JSON string_field rejects invalid escapes");
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
        .target_path = link / "outside.txt",
        .command = "",
    });
    expect(symlink_escape.action == ava::permissions::PermissionAction::Ask, "symlink workspace escape asks");
  }
}

void test_command_classification() {
  expect(ava::permissions::classify_command("git status --short").action == ava::permissions::PermissionAction::Allow,
         "git status is allowed for non-TTY line shell inspection");
  expect(ava::permissions::classify_command("git diff").action == ava::permissions::PermissionAction::Allow,
         "git diff is allowed for non-TTY line shell inspection");
  expect(ava::permissions::classify_command("git log --oneline").action == ava::permissions::PermissionAction::Allow,
         "git log is allowed for non-TTY line shell inspection");
  expect(ava::permissions::classify_command("pwd").action == ava::permissions::PermissionAction::Allow,
         "pwd remains allowed as inert local inspection");
  expect(ava::permissions::classify_command("ls src").action == ava::permissions::PermissionAction::Allow,
         "ls remains allowed for safe relative paths");
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
         "cmake build is allowed for non-TTY line shell verification");
  expect(
      ava::permissions::classify_command("ctest --test-dir build").action == ava::permissions::PermissionAction::Allow,
      "ctest is allowed for non-TTY line shell verification");
  expect(ava::permissions::classify_command("rg hello src").action == ava::permissions::PermissionAction::Allow,
         "rg is allowed for non-TTY line shell inspection");
  expect(ava::permissions::classify_command("rg --pre ./filter hello src").action ==
             ava::permissions::PermissionAction::Deny,
         "rg preprocessors remain denied because they execute commands");
  expect(
      ava::permissions::decide(ava::permissions::PermissionRequest{.operation = ava::permissions::Operation::RunCommand,
                                                                   .mode = ava::agent::Mode::Plan,
                                                                   .workspace_dir = std::filesystem::current_path(),
                                                                   .target_path = {},
                                                                   .command = "git status --short"})
              .action == ava::permissions::PermissionAction::Allow,
      "run-command decisions preserve safe command allows");
  expect(ava::permissions::classify_command("cmake -E cat ~/.config/ava/auth.json").action ==
             ava::permissions::PermissionAction::Deny,
         "cmake -E helper access is denied");
  expect(ava::permissions::classify_command("cmake -P docs/plan.md").action == ava::permissions::PermissionAction::Deny,
         "cmake -P script execution is denied");
  expect(ava::permissions::classify_command("cmake -E copy docs/plan.md src/new.cpp").action ==
             ava::permissions::PermissionAction::Deny,
         "cmake -E copy mutation is denied");
  expect(
      ava::permissions::classify_command("python3 scripts/run.py").action == ava::permissions::PermissionAction::Deny,
      "interpreters are denied");
  expect(ava::permissions::classify_command("bash -lc ls").action == ava::permissions::PermissionAction::Deny,
         "shell interpreters remain denied");
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
  const auto has_write_temp = [](const std::filesystem::path& directory) {
    std::error_code iter_error;
    for (std::filesystem::directory_iterator it(directory, iter_error), end; !iter_error && it != end;
         it.increment(iter_error)) {
      if (it->path().filename().string().find(".ava-write-") != std::string::npos) return true;
    }
    return false;
  };
  const auto permission_bits = [](const std::filesystem::path& permission_path) {
    constexpr auto mask =
        std::filesystem::perms::owner_all | std::filesystem::perms::group_all | std::filesystem::perms::others_all;
    std::error_code status_error;
    return std::filesystem::status(permission_path, status_error).permissions() & mask;
  };

  auto write = ava::tools::write_file(build_context, source_path, "hello world");
  expect(write.has_value(), "write_file writes in build mode");
  expect(write && permission_bits(source_path) == (std::filesystem::perms::owner_read |
                                                   std::filesystem::perms::owner_write),
         "write_file creates new files with 0600 permissions");

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

  const auto atomic_path = workspace / "atomic.txt";
  {
    std::ofstream atomic_file(atomic_path, std::ios::binary | std::ios::trunc);
    atomic_file << "original content";
  }
  auto atomic_write = ava::tools::write_file(build_context, atomic_path, "replacement content");
  auto atomic_read = ava::tools::read_file(build_context, atomic_path);
  expect(atomic_write && atomic_read && atomic_read->content == "replacement content" && !has_write_temp(workspace),
         "write_file atomically replaces existing content and cleans the staging file on success");

  const auto protected_path = workspace / "protected.txt";
  {
    std::ofstream protected_file(protected_path, std::ios::binary | std::ios::trunc);
    protected_file << "private original";
  }
  std::error_code chmod_error;
  std::filesystem::permissions(protected_path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::replace, chmod_error);
  expect(!chmod_error, "test can set private file permissions");
  auto protected_write = ava::tools::write_file(build_context, protected_path, "private replacement");
  auto protected_read = ava::tools::read_file(build_context, protected_path);
  expect(
      protected_write && protected_read && protected_read->content == "private replacement" &&
          permission_bits(protected_path) == (std::filesystem::perms::owner_read | std::filesystem::perms::owner_write),
      "write_file preserves 0600 permissions when overwriting a file");

  const auto edit_private_path = workspace / "edit-private.txt";
  {
    std::ofstream edit_private_file(edit_private_path, std::ios::binary | std::ios::trunc);
    edit_private_file << "alpha beta";
  }
  chmod_error.clear();
  std::filesystem::permissions(edit_private_path,
                               std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::replace, chmod_error);
  expect(!chmod_error, "test can set private edit file permissions");
  auto edit_private = ava::tools::edit_file(build_context, edit_private_path, "beta", "gamma");
  auto edit_private_read = ava::tools::read_file(build_context, edit_private_path);
  expect(edit_private && edit_private_read && edit_private_read->content == "alpha gamma" &&
             permission_bits(edit_private_path) ==
                 (std::filesystem::perms::owner_read | std::filesystem::perms::owner_write),
         "edit_file preserves 0600 permissions through atomic write_file");

  const auto audit_edit_path = workspace / "audit-edit.txt";
  {
    std::ofstream audit_edit_file(audit_edit_path, std::ios::binary | std::ios::trunc);
    audit_edit_file << "read then edit";
  }
  std::vector<ava::tools::PermissionAuditEvent> edit_audits;
  const ava::tools::ToolContext audit_edit_context{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_audit_sink = [&edit_audits](const ava::tools::PermissionAuditEvent& event) -> ava::core::VoidResult {
        edit_audits.push_back(event);
        return {};
      }};
  auto audited_edit = ava::tools::edit_file(audit_edit_context, audit_edit_path, "then", "before");
  expect(audited_edit && edit_audits.size() == 2 &&
             edit_audits[0].operation == ava::permissions::Operation::ReadFile &&
             edit_audits[1].operation == ava::permissions::Operation::EditFile,
         "edit_file audits read permission before edit permission");

  const auto rename_failure_path = workspace / "rename-failure-target";
  std::filesystem::create_directories(rename_failure_path);
  {
    std::ofstream sentinel(rename_failure_path / "sentinel.txt", std::ios::binary | std::ios::trunc);
    sentinel << "original sentinel";
  }
  auto failed_write = ava::tools::write_file(build_context, rename_failure_path, "replacement");
  auto sentinel_read = ava::tools::read_file(build_context, rename_failure_path / "sentinel.txt");
  expect(!failed_write && sentinel_read && sentinel_read->content == "original sentinel" && !has_write_temp(workspace),
         "write_file cleans the staging file and leaves existing data unchanged when commit rename fails");

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
  auto external_ssh_secret = ava::permissions::decide(ava::permissions::PermissionRequest{
      .operation = ava::permissions::Operation::ReadFile,
      .mode = ava::agent::Mode::Build,
      .workspace_dir = workspace,
      .target_path = "/home/user/.ssh/id_rsa",
      .command = "",
  });
  expect(external_ssh_secret.action == ava::permissions::PermissionAction::Deny,
         "external ssh key reads deny before outside-workspace ask");
  auto external_npmrc_secret = ava::permissions::decide(ava::permissions::PermissionRequest{
      .operation = ava::permissions::Operation::EditFile,
      .mode = ava::agent::Mode::Build,
      .workspace_dir = workspace,
      .target_path = "/home/user/.npmrc",
      .command = "",
  });
  expect(external_npmrc_secret.action == ava::permissions::PermissionAction::Deny,
         "external npm credential edits deny before outside-workspace ask");
  auto external_ava_auth = ava::permissions::decide(ava::permissions::PermissionRequest{
      .operation = ava::permissions::Operation::ReadFile,
      .mode = ava::agent::Mode::Build,
      .workspace_dir = workspace,
      .target_path = "/home/user/.config/ava/auth.json",
      .command = "",
  });
  expect(external_ava_auth.action == ava::permissions::PermissionAction::Deny,
         "external ava auth reads deny before outside-workspace ask");
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

  const auto outside_path = temp_root() / "outside.txt";
  {
    std::ofstream outside_file(outside_path, std::ios::binary | std::ios::trunc);
    outside_file << "outside content";
  }
  auto outside_without_resolver = ava::tools::read_file(build_context, outside_path);
  expect(!outside_without_resolver &&
             outside_without_resolver.error().format().find("resolution: no_resolver") != std::string::npos,
         "read_file fails closed for ask decisions without a resolver");

  int allow_prompts = 0;
  ava::tools::ToolContext allow_context{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [&allow_prompts, &outside_path](const ava::permissions::PermissionPrompt& prompt)
          -> ava::core::Result<ava::permissions::PermissionResolution> {
        ++allow_prompts;
        expect(prompt.operation == ava::permissions::Operation::ReadFile, "file resolver receives read operation");
        expect(prompt.target_path == outside_path, "file resolver receives target path");
        return ava::permissions::PermissionResolution::Allow;
      }};
  auto outside_allowed = ava::tools::read_file(allow_context, outside_path);
  expect(outside_allowed && outside_allowed->content == "outside content" && allow_prompts == 1,
         "read_file allows ask decisions when resolver allows once");

  ava::tools::ToolContext deny_context{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver =
          [](const ava::permissions::PermissionPrompt&) -> ava::core::Result<ava::permissions::PermissionResolution> {
        return ava::permissions::PermissionResolution::Deny;
      }};
  auto outside_denied = ava::tools::read_file(deny_context, outside_path);
  expect(!outside_denied && outside_denied.error().format().find("resolution: deny") != std::string::npos,
         "read_file fails closed when resolver denies ask decisions");

  ava::tools::ToolContext failing_context{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver =
          [](const ava::permissions::PermissionPrompt&) -> ava::core::Result<ava::permissions::PermissionResolution> {
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "resolver failed"));
      }};
  auto outside_failed = ava::tools::read_file(failing_context, outside_path);
  expect(!outside_failed && outside_failed.error().format().find("resolution: resolver_failed") != std::string::npos,
         "read_file fails closed when resolver fails");

  const auto outside_write_path = temp_root() / "outside-write.txt";
  auto outside_write_without_resolver = ava::tools::write_file(build_context, outside_write_path, "bad");
  expect(!outside_write_without_resolver && !std::filesystem::exists(outside_write_path) &&
             outside_write_without_resolver.error().format().find("resolution: no_resolver") != std::string::npos,
         "write_file fails closed for external ask decisions without writing");

  int write_denials = 0;
  ava::tools::ToolContext write_deny_context{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [&write_denials](const ava::permissions::PermissionPrompt&)
          -> ava::core::Result<ava::permissions::PermissionResolution> {
        ++write_denials;
        return ava::permissions::PermissionResolution::Deny;
      }};
  auto outside_write_denied = ava::tools::write_file(write_deny_context, outside_write_path, "bad");
  expect(!outside_write_denied && write_denials == 1 && !std::filesystem::exists(outside_write_path) &&
             outside_write_denied.error().format().find("resolution: deny") != std::string::npos,
         "write_file fails closed when resolver denies external writes");

  int write_fail_prompts = 0;
  ava::tools::ToolContext write_fail_context{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [&write_fail_prompts](const ava::permissions::PermissionPrompt&)
          -> ava::core::Result<ava::permissions::PermissionResolution> {
        ++write_fail_prompts;
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "resolver failed"));
      }};
  auto outside_write_failed = ava::tools::write_file(write_fail_context, outside_write_path, "bad");
  expect(!outside_write_failed && write_fail_prompts == 1 && !std::filesystem::exists(outside_write_path) &&
             outside_write_failed.error().format().find("resolution: resolver_failed") != std::string::npos,
         "write_file fails closed when resolver fails external writes");

  auto outside_edit_without_resolver = ava::tools::edit_file(build_context, outside_path, "outside", "bad");
  auto outside_after_no_resolver_edit = ava::tools::read_file(allow_context, outside_path);
  expect(!outside_edit_without_resolver && outside_after_no_resolver_edit &&
             outside_after_no_resolver_edit->content == "outside content" &&
             outside_edit_without_resolver.error().format().find("resolution: no_resolver") != std::string::npos,
         "edit_file fails closed for external ask decisions without editing");

  int edit_prompts = 0;
  ava::tools::ToolContext edit_deny_context{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [&edit_prompts](const ava::permissions::PermissionPrompt& prompt)
          -> ava::core::Result<ava::permissions::PermissionResolution> {
        ++edit_prompts;
        expect(prompt.operation == ava::permissions::Operation::ReadFile,
               "edit_file resolver sees read operation before denied external edit");
        return ava::permissions::PermissionResolution::Deny;
      }};
  auto outside_edit_denied = ava::tools::edit_file(edit_deny_context, outside_path, "outside", "bad");
  auto outside_after_denied_edit = ava::tools::read_file(allow_context, outside_path);
  expect(!outside_edit_denied && edit_prompts == 1 && outside_after_denied_edit &&
             outside_after_denied_edit->content == "outside content" &&
             outside_edit_denied.error().format().find("resolution: deny") != std::string::npos,
         "edit_file leaves content unchanged when external read permission is denied");

  int edit_fail_prompts = 0;
  ava::tools::ToolContext edit_fail_context{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [&edit_fail_prompts](const ava::permissions::PermissionPrompt& prompt)
          -> ava::core::Result<ava::permissions::PermissionResolution> {
        ++edit_fail_prompts;
        expect(prompt.operation == ava::permissions::Operation::ReadFile,
               "edit_file failure resolver sees read operation before external edit");
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "resolver failed"));
      }};
  auto outside_edit_failed = ava::tools::edit_file(edit_fail_context, outside_path, "outside", "bad");
  auto outside_after_failed_edit = ava::tools::read_file(allow_context, outside_path);
  expect(!outside_edit_failed && edit_fail_prompts == 1 && outside_after_failed_edit &&
             outside_after_failed_edit->content == "outside content" &&
             outside_edit_failed.error().format().find("resolution: resolver_failed") != std::string::npos,
         "edit_file fails closed when resolver fails and leaves content unchanged");

  std::vector<ava::permissions::Operation> edit_allow_prompts;
  ava::tools::ToolContext edit_allow_context{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [&edit_allow_prompts](const ava::permissions::PermissionPrompt& prompt)
          -> ava::core::Result<ava::permissions::PermissionResolution> {
        edit_allow_prompts.push_back(prompt.operation);
        return ava::permissions::PermissionResolution::Allow;
      }};
  auto outside_edit_allowed = ava::tools::edit_file(edit_allow_context, outside_path, "outside", "external");
  auto outside_after_allowed_edit = ava::tools::read_file(allow_context, outside_path);
  expect(outside_edit_allowed && edit_allow_prompts.size() == 2 &&
             edit_allow_prompts[0] == ava::permissions::Operation::ReadFile &&
             edit_allow_prompts[1] == ava::permissions::Operation::EditFile && outside_after_allowed_edit &&
             outside_after_allowed_edit->content == "external content",
         "edit_file resolves external read permission before edit permission");
}

void test_permission_audit_persistence() {
  const auto root = temp_root() / "permission-audit";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  std::filesystem::create_directories(workspace);

  const auto allowed_path = workspace / "allowed.txt";
  {
    std::ofstream file(allowed_path, std::ios::binary | std::ios::trunc);
    file << "allowed";
  }
  const auto secret_path = workspace / ".env";
  {
    std::ofstream file(secret_path, std::ios::binary | std::ios::trunc);
    file << "secret";
  }
  const auto outside_path = root / "outside.txt";
  {
    std::ofstream file(outside_path, std::ios::binary | std::ios::trunc);
    file << "outside";
  }

  ava::session::SessionStore store(ava::session::SessionStoreOptions{
      .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "audit"});
  auto sink = [&store](const ava::tools::PermissionAuditEvent& event) -> ava::core::VoidResult {
    return append_permission_audit_for_test(store, event);
  };
  const ava::tools::ToolContext context{
      .workspace_dir = workspace, .mode = ava::agent::Mode::Build, .permission_audit_sink = sink};

  auto allowed = ava::tools::read_file(context, allowed_path);
  expect(allowed && allowed->content == "allowed", "permission audit allows normal read");
  auto loaded = store.load();
  auto audits = loaded ? permission_entries(*loaded) : std::vector<ava::session::SessionEntry>{};
  expect(audits.size() == 1, "allowed policy decision appends one audit entry");
  if (!audits.empty()) {
    expect(ava::core::json::string_field(audits[0].data_json, "operation") == "read" &&
               ava::core::json::string_field(audits[0].data_json, "action") == "allow" &&
               ava::core::json::string_field(audits[0].data_json, "resolution") == "allow" &&
               ava::core::json::string_field(audits[0].data_json, "resolution_source") == "policy" &&
               ava::core::json::string_field(audits[0].data_json, "target_path") == allowed_path.string(),
           "allowed audit records policy resolution and target path");
  }

  auto denied = ava::tools::read_file(context, secret_path);
  expect(!denied, "permission audit denied read still fails closed");
  loaded = store.load();
  audits = loaded ? permission_entries(*loaded) : std::vector<ava::session::SessionEntry>{};
  expect(audits.size() == 2, "denied policy decision appends an audit entry");
  if (audits.size() >= 2) {
    expect(ava::core::json::string_field(audits[1].data_json, "action") == "deny" &&
               ava::core::json::string_field(audits[1].data_json, "resolution") == "deny" &&
               ava::core::json::string_field(audits[1].data_json, "resolution_source") == "policy",
           "denied audit records policy denial without resolver");
  }

  int prompts = 0;
  const ava::tools::ToolContext resolving_context{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [&prompts](const ava::permissions::PermissionPrompt&)
          -> ava::core::Result<ava::permissions::PermissionResolution> {
        ++prompts;
        return ava::permissions::PermissionResolution::Allow;
      },
      .permission_audit_sink = sink};
  auto outside = ava::tools::read_file(resolving_context, outside_path);
  expect(outside && outside->content == "outside" && prompts == 1,
         "permission audit preserves resolver-approved read behavior");
  loaded = store.load();
  audits = loaded ? permission_entries(*loaded) : std::vector<ava::session::SessionEntry>{};
  expect(audits.size() == 4, "ask policy and resolver outcome append separate audit entries");
  if (audits.size() >= 4) {
    expect(ava::core::json::string_field(audits[2].data_json, "action") == "ask" &&
               !ava::core::json::string_field(audits[2].data_json, "resolution") &&
               ava::core::json::string_field(audits[2].data_json, "resolution_source") == "policy",
           "ask audit records policy request before resolver outcome");
    expect(ava::core::json::string_field(audits[3].data_json, "action") == "ask" &&
               ava::core::json::string_field(audits[3].data_json, "resolution") == "allow" &&
               ava::core::json::string_field(audits[3].data_json, "resolution_source") == "resolver",
           "ask audit records resolver allow outcome");
  }

  auto bash_denied = ava::tools::run_bash(context, "rm -rf important");
  expect(!bash_denied, "permission audit preserves bash policy denial");
  loaded = store.load();
  audits = loaded ? permission_entries(*loaded) : std::vector<ava::session::SessionEntry>{};
  expect(audits.size() == 5, "bash policy decision appends command audit entry");
  if (audits.size() >= 5) {
    expect(ava::core::json::string_field(audits[4].data_json, "operation") == "bash" &&
               ava::core::json::string_field(audits[4].data_json, "command") == "rm -rf important" &&
               !ava::core::json::string_field(audits[4].data_json, "target_path"),
           "bash audit records command without path-only target field");
  }

  const auto exported = ava::session::format_session_markdown(audits);
  expect(exported.find("## Permission Decision") != std::string::npos &&
             exported.find("\"operation\":\"read\"") != std::string::npos &&
             exported.find("\"resolution_source\":\"resolver\"") != std::string::npos,
         "session export includes permission decision audit data");
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
    expect(glob->paths.size() < 2 || glob->paths[0].generic_string() < glob->paths[1].generic_string(),
           "glob_files sorts results deterministically by generic path string");
  }

  auto bracket_glob = ava::tools::glob_files(context, "*.[ch]");
  expect(!bracket_glob && bracket_glob.error().category() == ava::core::ErrorCategory::InvalidArgument &&
             bracket_glob.error().message().find("bracket") != std::string::npos,
         "glob_files rejects unsupported bracket character classes instead of silently mis-matching them");

  auto result_capped = ava::tools::glob_files(context, "**/*.cpp", ava::tools::GlobOptions{.max_results = 1});
  expect(result_capped && result_capped->paths.size() == 1 && result_capped->total_matches == 2 &&
             result_capped->truncated,
         "glob_files reports result-count truncation while counting all matches");

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

  const auto outside_search_path = temp_root() / "outside-search.txt";
  {
    std::ofstream outside_file(outside_search_path, std::ios::binary | std::ios::trunc);
    outside_file << "outside hello\n";
  }
  std::error_code symlink_error;
  const auto outside_search_link = workspace / "outside-link.txt";
  std::filesystem::create_symlink(outside_search_path, outside_search_link, symlink_error);
  if (!symlink_error) {
    int search_prompts = 0;
    std::vector<ava::tools::PermissionAuditEvent> search_audits;
    const ava::tools::ToolContext resolving_search_context{
        .workspace_dir = workspace,
        .mode = ava::agent::Mode::Build,
        .permission_resolver = [&search_prompts, &outside_search_link](const ava::permissions::PermissionPrompt& prompt)
            -> ava::core::Result<ava::permissions::PermissionResolution> {
          ++search_prompts;
          expect(prompt.operation == ava::permissions::Operation::ReadFile,
                 "search resolver receives read operation for symlink escapes");
          expect(prompt.target_path == outside_search_link, "search resolver receives the matching symlink path");
          return ava::permissions::PermissionResolution::Allow;
        },
        .permission_audit_sink =
            [&search_audits](const ava::tools::PermissionAuditEvent& event) -> ava::core::VoidResult {
          search_audits.push_back(event);
          return {};
        }};
    auto resolved_glob = ava::tools::glob_files(resolving_search_context, "**/*");
    const bool resolved_includes_link =
        resolved_glob && std::ranges::any_of(resolved_glob->paths, [&outside_search_link](const auto& path) {
          return path == outside_search_link;
        });
    expect(resolved_includes_link && search_prompts == 1,
           "glob_files resolves ask decisions for symlinked matches instead of silently skipping them");
    expect(search_audits.size() == 3 && search_audits[0].operation == ava::permissions::Operation::SearchFiles &&
               search_audits[0].action == ava::permissions::PermissionAction::Allow &&
               search_audits[1].operation == ava::permissions::Operation::ReadFile &&
               search_audits[1].action == ava::permissions::PermissionAction::Ask &&
               search_audits[2].resolution == "allow" && search_audits[2].resolution_source == "resolver",
           "glob_files audits the search root and ask resolver outcome without per-file allow audits");

    int denied_search_prompts = 0;
    const ava::tools::ToolContext denying_search_context{
        .workspace_dir = workspace,
        .mode = ava::agent::Mode::Build,
        .permission_resolver = [&denied_search_prompts](const ava::permissions::PermissionPrompt&)
            -> ava::core::Result<ava::permissions::PermissionResolution> {
          ++denied_search_prompts;
          return ava::permissions::PermissionResolution::Deny;
        }};
    auto denied_glob = ava::tools::glob_files(denying_search_context, "**/*");
    const bool denied_excludes_link =
        denied_glob && std::ranges::none_of(denied_glob->paths, [&outside_search_link](const auto& path) {
          return path == outside_search_link;
        });
    expect(denied_excludes_link && denied_search_prompts == 1, "glob_files keeps resolver-denied ask matches excluded");

    int failing_search_prompts = 0;
    const ava::tools::ToolContext failing_search_context{
        .workspace_dir = workspace,
        .mode = ava::agent::Mode::Build,
        .permission_resolver = [&failing_search_prompts](const ava::permissions::PermissionPrompt&)
            -> ava::core::Result<ava::permissions::PermissionResolution> {
          ++failing_search_prompts;
          return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "search resolver failed"));
        }};
    auto failing_glob = ava::tools::glob_files(failing_search_context, "**/*");
    const bool failing_skips_link =
        failing_glob && std::ranges::none_of(failing_glob->paths, [&outside_search_link](const auto& path) {
          return path == outside_search_link;
        });
    const bool failing_keeps_readable_match =
        failing_glob && std::ranges::any_of(failing_glob->paths, [&workspace](const auto& path) {
          return path == workspace / "docs" / "plan.md";
        });
    expect(failing_skips_link && failing_keeps_readable_match && failing_search_prompts == 1,
           "glob_files skips ask matches with resolver errors while continuing search");
  }

  {
    std::ofstream binary_file(workspace / "binary.bin", std::ios::binary | std::ios::trunc);
    binary_file << std::string("binary", 6) << '\0' << " marker\nhello from binary\n";
  }
  auto binary = ava::tools::grep_files(context, "hello", "**/*.bin");
  expect(binary && binary->matches.empty() && binary->total_matches == 0,
         "grep_files skips an entire file after detecting binary content");

  {
    std::ofstream binary_file(workspace / "overlong-binary.bin", std::ios::binary | std::ios::trunc);
    binary_file << std::string(32, 'x') << '\0' << " hello after nul\n";
  }
  auto overlong_binary =
      ava::tools::grep_files(context, "hello", "**/overlong-binary.bin", ava::tools::GrepOptions{.max_line_length = 5});
  expect(overlong_binary && overlong_binary->matches.empty() && overlong_binary->total_matches == 0,
         "grep_files treats NUL after an overlong truncation point as binary content");
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

  auto capped_output = ava::tools::run_bash(
      context, "pwd", ava::tools::BashOptions{.timeout = std::chrono::milliseconds(1000), .max_bytes = 4});
  expect(capped_output && capped_output->exit_code == 0 && capped_output->truncated &&
             capped_output->output.size() == 4 && capped_output->total_bytes > capped_output->output.size(),
         "run_bash bounds retained output while reporting total bytes");

  const auto hijack_path = temp_root() / "pwd";
  {
    std::ofstream hijack(hijack_path, std::ios::binary | std::ios::trunc);
    hijack << "#!/bin/sh\nprintf hijacked-path\n";
  }
  expect(chmod(hijack_path.c_str(), 0700) == 0, "test can create executable PATH hijack fixture");
  {
    const ScopedEnvVar path_guard("PATH", temp_root().string() + ":.:relative");
    auto sanitized_pwd =
        ava::tools::run_bash(context, "pwd", ava::tools::BashOptions{.timeout = std::chrono::milliseconds(1000)});
    expect(sanitized_pwd && sanitized_pwd->exit_code == 0 &&
               sanitized_pwd->output.find("hijacked-path") == std::string::npos &&
               sanitized_pwd->output.find(temp_root().string()) != std::string::npos,
           "run_bash does not inherit unsafe PATH entries for auto-allowed commands");
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

  auto ask_without_resolver = ava::tools::run_bash(context, "true");
  expect(!ask_without_resolver &&
             ask_without_resolver.error().format().find("resolution: no_resolver") != std::string::npos,
         "run_bash fails closed for ask decisions without a resolver");

  int bash_prompts = 0;
  ava::tools::ToolContext allow_context{
      .workspace_dir = temp_root(),
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [&bash_prompts](const ava::permissions::PermissionPrompt& prompt)
          -> ava::core::Result<ava::permissions::PermissionResolution> {
        ++bash_prompts;
        expect(prompt.operation == ava::permissions::Operation::RunCommand, "bash resolver receives run operation");
        expect(prompt.command == "true", "bash resolver receives command text");
        return ava::permissions::PermissionResolution::Allow;
      }};
  auto ask_allowed = ava::tools::run_bash(allow_context, "true");
  expect(ask_allowed && ask_allowed->exit_code == 0 && bash_prompts == 1,
         "run_bash allows ask decisions when resolver allows once");

  ava::tools::ToolContext deny_context{
      .workspace_dir = temp_root(),
      .mode = ava::agent::Mode::Build,
      .permission_resolver =
          [](const ava::permissions::PermissionPrompt&) -> ava::core::Result<ava::permissions::PermissionResolution> {
        return ava::permissions::PermissionResolution::Deny;
      }};
  auto ask_denied = ava::tools::run_bash(deny_context, "true");
  expect(!ask_denied && ask_denied.error().format().find("resolution: deny") != std::string::npos,
         "run_bash fails closed when resolver denies ask decisions");

  ava::tools::ToolContext failing_context{
      .workspace_dir = temp_root(),
      .mode = ava::agent::Mode::Build,
      .permission_resolver =
          [](const ava::permissions::PermissionPrompt&) -> ava::core::Result<ava::permissions::PermissionResolution> {
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "resolver failed"));
      }};
  auto ask_failed = ava::tools::run_bash(failing_context, "true");
  expect(!ask_failed && ask_failed.error().format().find("resolution: resolver_failed") != std::string::npos,
         "run_bash fails closed when resolver fails");
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
  expect(fallback.compaction_file == fallback.ava_config_dir / "compaction.json",
         "compaction file is in XDG config dir");
  expect(fallback.global_agents_file == fallback.ava_config_dir / "AGENTS.md",
         "global AGENTS.md file is in XDG config dir");
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

void test_context_loader() {
  const auto root = temp_root() / "context";
  const auto workspace = root / "workspace";
  const auto nested = workspace / "src" / "feature";
  const auto config = root / "config" / "ava";
  const auto outside = root / "outside";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  std::filesystem::create_directories(nested);
  std::filesystem::create_directories(config);
  std::filesystem::create_directories(outside);

  const auto root_agents = workspace / "AGENTS.md";
  const auto src_agents = workspace / "src" / "AGENTS.md";
  const auto nested_agents = nested / "AGENTS.md";
  const auto global_agents = config / "AGENTS.md";
  {
    std::ofstream file(root_agents, std::ios::binary | std::ios::trunc);
    file << "root instructions\n";
  }
  {
    std::ofstream file(src_agents, std::ios::binary | std::ios::trunc);
    file << "src instructions\n";
  }
  {
    std::ofstream file(nested_agents, std::ios::binary | std::ios::trunc);
    file << "nested instructions";
  }
  {
    std::ofstream file(global_agents, std::ios::binary | std::ios::trunc);
    file << "global instructions\n";
  }

  auto loaded = ava::context::load_context_files(ava::context::ContextLoadOptions{
      .workspace_root = workspace,
      .current_dir = nested,
      .global_agents_file = {},
      .max_file_bytes = 1024,
  });
  expect(loaded.has_value(), "context loader loads workspace AGENTS.md files");
  if (loaded) {
    expect(loaded->size() == 3, "context loader finds root-to-current workspace contexts");
    if (loaded->size() == 3) {
      expect((*loaded)[0].path == std::filesystem::weakly_canonical(root_agents), "root AGENTS.md loads first");
      expect((*loaded)[1].path == std::filesystem::weakly_canonical(src_agents), "intermediate AGENTS.md loads second");
      expect((*loaded)[2].path == std::filesystem::weakly_canonical(nested_agents), "nested AGENTS.md loads third");
      expect((*loaded)[0].source_type == ava::context::ContextSourceType::Workspace,
             "workspace context records workspace source type");
    }
  }

  auto outside_loaded = ava::context::load_context_files(ava::context::ContextLoadOptions{
      .workspace_root = workspace,
      .current_dir = outside,
      .global_agents_file = {},
      .max_file_bytes = 1024,
  });
  expect(outside_loaded.has_value(), "context loader handles current_dir outside workspace");
  if (outside_loaded) {
    expect(outside_loaded->size() == 1 && (*outside_loaded)[0].path == std::filesystem::weakly_canonical(root_agents),
           "outside current_dir only loads workspace root AGENTS.md");
  }

  auto with_global = ava::context::load_context_files(ava::context::ContextLoadOptions{
      .workspace_root = workspace,
      .current_dir = nested,
      .global_agents_file = global_agents,
      .max_file_bytes = 1024,
  });
  expect(with_global.has_value(), "context loader includes global AGENTS.md");
  if (with_global) {
    expect(with_global->size() == 4, "global AGENTS.md is appended to workspace contexts");
    if (with_global->size() == 4) {
      expect((*with_global)[3].path == std::filesystem::weakly_canonical(global_agents),
             "global AGENTS.md path is recorded");
      expect((*with_global)[3].source_type == ava::context::ContextSourceType::Global,
             "global AGENTS.md records global source type");

      const auto formatted = ava::context::format_context_for_prompt(*with_global);
      expect(formatted.find("# Loaded Project Instructions") != std::string::npos,
             "formatted context includes a section title");
      expect(formatted.find("## workspace: " + std::filesystem::weakly_canonical(root_agents).string()) !=
                 std::string::npos,
             "formatted context includes workspace source/path header");
      expect(formatted.find("## global: " + std::filesystem::weakly_canonical(global_agents).string()) !=
                 std::string::npos,
             "formatted context includes global source/path header");
      expect(formatted.find("root instructions") != std::string::npos &&
                 formatted.find("global instructions") != std::string::npos,
             "formatted context includes loaded content");
    }
  }

  auto deduped_global = ava::context::load_context_files(ava::context::ContextLoadOptions{
      .workspace_root = workspace,
      .current_dir = nested,
      .global_agents_file = root_agents,
      .max_file_bytes = 1024,
  });
  expect(deduped_global.has_value(), "context loader handles duplicate global path");
  if (deduped_global) {
    expect(deduped_global->size() == 3, "duplicate global AGENTS.md is not loaded twice");
  }

  const auto oversized_workspace = root / "oversized";
  std::filesystem::create_directories(oversized_workspace);
  {
    std::ofstream file(oversized_workspace / "AGENTS.md", std::ios::binary | std::ios::trunc);
    file << std::string(6, 'x');
  }
  auto oversized = ava::context::load_context_files(ava::context::ContextLoadOptions{
      .workspace_root = oversized_workspace,
      .current_dir = oversized_workspace,
      .global_agents_file = {},
      .max_file_bytes = 5,
  });
  expect(!oversized && oversized.error().category() == ava::core::ErrorCategory::Io,
         "oversized context files fail safely");

  const auto symlink_workspace = root / "symlink-workspace";
  const auto symlink_target = root / "outside-secret.md";
  std::filesystem::create_directories(symlink_workspace);
  {
    std::ofstream file(symlink_target, std::ios::binary | std::ios::trunc);
    file << "secret instructions\n";
  }
  std::error_code symlink_error;
  std::filesystem::create_symlink(symlink_target, symlink_workspace / "AGENTS.md", symlink_error);
  expect(!symlink_error, "test creates symlinked AGENTS.md");
  if (!symlink_error) {
    auto symlinked = ava::context::load_context_files(ava::context::ContextLoadOptions{
        .workspace_root = symlink_workspace,
        .current_dir = symlink_workspace,
        .global_agents_file = {},
        .max_file_bytes = 1024,
    });
    expect(!symlinked && symlinked.error().category() == ava::core::ErrorCategory::Io,
           "context loader rejects symlinked AGENTS.md files");
  }
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
                                           .account_id = "acct_stored",
                                           .source_path = {}});
  expect(stored.has_value(), "OpenAI OAuth credential stores to AVA XDG auth file");
  auto stored_oauth = ava::config::load_openai_credential(paths);
  expect(stored_oauth && stored_oauth->has_value() &&
             (*stored_oauth)->type == ava::config::OpenAICredentialType::OAuth &&
             (*stored_oauth)->access_token == "stored-token" && (*stored_oauth)->refresh_token == "stored-refresh" &&
             (*stored_oauth)->expires_at == 99 && (*stored_oauth)->account_id == "acct_stored",
         "OpenAI OAuth credential store/load round trips type, token, and account fields");

  auto stored_api = ava::config::store_openai_credential(
      paths, ava::config::OpenAICredential{.type = ava::config::OpenAICredentialType::ApiKey,
                                           .access_token = "stored-api-key",
                                           .refresh_token = "ignored-refresh",
                                           .expires_at = 1,
                                           .account_id = "",
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
                                    .account_id = "",
                                    .source_path = paths.auth_file},
      11);
  expect(!expired_oauth && expired_oauth.error().category() == ava::core::ErrorCategory::PermissionDenied,
         "OpenAI expired OAuth credential is not usable for requests");
  auto api_key_not_expired = ava::config::openai_access_token_for_request(
      ava::config::OpenAICredential{.type = ava::config::OpenAICredentialType::ApiKey,
                                    .access_token = "api-token",
                                    .refresh_token = "",
                                    .expires_at = 10,
                                    .account_id = "",
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
                                             .account_id = "",
                                             .source_path = {}});
    expect(!symlink_store, "OpenAI credential store rejects symlink auth file");
    std::ifstream target_read(symlink_target, std::ios::binary);
    std::string target_content;
    target_read >> target_content;
    expect(target_content == "unchanged", "OpenAI credential store does not write through symlink");
  }
}

void test_openai_oauth_helpers() {
  const std::string verifier = "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk";
  expect(ava::config::openai_oauth_code_challenge(verifier) == "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM",
         "OpenAI OAuth PKCE challenge matches RFC 7636 test vector");
  expect(ava::config::openai_oauth_account_id_from_token(
             "header.eyJodHRwczovL2FwaS5vcGVuYWkuY29tL2F1dGgiOnsiY2hhdGdwdF9hY2NvdW50X2lkIjoiYWNjdF8xMjMifX0.sig") ==
             "acct_123",
         "OpenAI OAuth account id extracts from Codex JWT claim");

  auto session = ava::config::make_openai_oauth_session(verifier, "state value");
  expect(session.has_value(), "OpenAI OAuth deterministic session builds");
  if (session) {
    expect(session->authorization_url.find("https://auth.openai.com/oauth/authorize?") == 0,
           "OpenAI OAuth authorization URL uses auth.openai.com");
    expect(session->authorization_url.find("client_id=app_EMoamEEZ73f0CkXaXp7hrann") != std::string::npos,
           "OpenAI OAuth authorization URL includes Codex client id");
    expect(session->authorization_url.find("redirect_uri=http%3A%2F%2Flocalhost%3A1455%2Fauth%2Fcallback") !=
               std::string::npos,
           "OpenAI OAuth authorization URL includes local callback redirect");
    expect(session->authorization_url.find("code_challenge=E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM") !=
               std::string::npos,
           "OpenAI OAuth authorization URL includes S256 code challenge");
    expect(session->authorization_url.find("state=state%20value") != std::string::npos,
           "OpenAI OAuth authorization URL percent-encodes state");
    expect(session->authorization_url.find("originator=ava") != std::string::npos,
           "OpenAI OAuth authorization URL identifies AVA originator");
  }

  ava::tests::FakeTransport transport({ava::provider::HttpResponse{
      .status_code = 200,
      .headers = {},
      .body = "{\"access_token\":\"access\",\"refresh_token\":\"refresh\",\"expires_in\":120,"
              "\"id_token\":\"header."
              "eyJodHRwczovL2FwaS5vcGVuYWkuY29tL2F1dGgiOnsiY2hhdGdwdF9hY2NvdW50X2lkIjoiYWNjdF8xMjMifX0.sig\"}",
  }});
  auto credential = ava::config::exchange_openai_oauth_code("code value", verifier, transport, 1000);
  expect(credential.has_value(), "OpenAI OAuth code exchange parses token response");
  if (credential) {
    expect(credential->type == ava::config::OpenAICredentialType::OAuth && credential->access_token == "access" &&
               credential->refresh_token == "refresh" && credential->expires_at == 1120 &&
               credential->account_id == "acct_123",
           "OpenAI OAuth code exchange returns OAuth credential with absolute expiry and account id");
  }
  const auto& requests = transport.requests();
  expect(requests.size() == 1 && requests.front().url == "https://auth.openai.com/oauth/token" &&
             requests.front().method == "POST",
         "OpenAI OAuth code exchange posts to token endpoint");
  if (!requests.empty()) {
    expect(requests.front().body.find("grant_type=authorization_code") != std::string::npos &&
               requests.front().body.find("code=code%20value") != std::string::npos &&
               requests.front().body.find("code_verifier=" + verifier) != std::string::npos,
           "OpenAI OAuth code exchange form-encodes authorization code and verifier");
  }
}

void test_openai_oauth_refresh() {
  ava::tests::FakeTransport refresh_transport({ava::provider::HttpResponse{
      .status_code = 200,
      .headers = {},
      .body = "{\"access_token\":\"refreshed-access\",\"refresh_token\":\"rotated-refresh\","
              "\"expires_in\":120,\"account_id\":\"acct_rotated\"}",
  }});
  const auto refreshed = ava::config::refresh_openai_oauth_credential(
      ava::config::OpenAICredential{.type = ava::config::OpenAICredentialType::OAuth,
                                    .access_token = "old-access",
                                    .refresh_token = "old refresh/token",
                                    .expires_at = 900,
                                    .account_id = "acct_old",
                                    .source_path = {}},
      refresh_transport, 1000);
  expect(refreshed && refreshed->access_token == "refreshed-access" && refreshed->refresh_token == "rotated-refresh" &&
             refreshed->expires_at == 1120 && refreshed->account_id == "acct_rotated",
         "OpenAI OAuth refresh parses rotated token response");
  const auto& refresh_requests = refresh_transport.requests();
  expect(refresh_requests.size() == 1 && refresh_requests.front().url == "https://auth.openai.com/oauth/token" &&
             refresh_requests.front().method == "POST",
         "OpenAI OAuth refresh posts to token endpoint");
  if (!refresh_requests.empty()) {
    expect(refresh_requests.front().body.find("grant_type=refresh_token") != std::string::npos &&
               refresh_requests.front().body.find("refresh_token=old%20refresh%2Ftoken") != std::string::npos &&
               refresh_requests.front().body.find("client_id=app_EMoamEEZ73f0CkXaXp7hrann") != std::string::npos,
           "OpenAI OAuth refresh form-encodes refresh token and client id");
  }

  ava::tests::FakeTransport preserved_transport({ava::provider::HttpResponse{
      .status_code = 200,
      .headers = {},
      .body = "{\"access_token\":\"preserved-access\",\"expires_at\":2222}",
  }});
  const auto preserved = ava::config::refresh_openai_oauth_credential(
      ava::config::OpenAICredential{.type = ava::config::OpenAICredentialType::OAuth,
                                    .access_token = "old-access",
                                    .refresh_token = "stable-refresh",
                                    .expires_at = 900,
                                    .account_id = "acct_stable",
                                    .source_path = {}},
      preserved_transport, 1000);
  expect(preserved && preserved->access_token == "preserved-access" && preserved->refresh_token == "stable-refresh" &&
             preserved->expires_at == 2222 && preserved->account_id == "acct_stable",
         "OpenAI OAuth refresh preserves existing refresh token when response omits rotation");

  ava::tests::FakeTransport id_token_transport({ava::provider::HttpResponse{
      .status_code = 200,
      .headers = {},
      .body = "{\"access_token\":\"id-token-access\",\"refresh_token\":\"id-token-refresh\","
              "\"expires_in\":120,\"id_token\":\"header."
              "eyJodHRwczovL2FwaS5vcGVuYWkuY29tL2F1dGgiOnsiY2hhdGdwdF9hY2NvdW50X2lkIjoiYWNjdF8xMjMifX0.sig\"}",
  }});
  const auto id_token_refreshed = ava::config::refresh_openai_oauth_credential(
      ava::config::OpenAICredential{.type = ava::config::OpenAICredentialType::OAuth,
                                    .access_token = "old-access",
                                    .refresh_token = "id-token-refresh-input",
                                    .expires_at = 900,
                                    .account_id = "",
                                    .source_path = {}},
      id_token_transport, 1000);
  expect(id_token_refreshed && id_token_refreshed->account_id == "acct_123",
         "OpenAI OAuth refresh falls back to id_token account id when account_id is absent");

  ava::tests::FakeTransport malformed_transport({ava::provider::HttpResponse{
      .status_code = 200,
      .headers = {},
      .body = "not json",
  }});
  const auto malformed = ava::config::refresh_openai_oauth_credential(
      ava::config::OpenAICredential{.type = ava::config::OpenAICredentialType::OAuth,
                                    .access_token = "old-access",
                                    .refresh_token = "refresh",
                                    .expires_at = 900,
                                    .account_id = "",
                                    .source_path = {}},
      malformed_transport, 1000);
  expect(!malformed && malformed.error().message().find("malformed JSON") != std::string::npos,
         "OpenAI OAuth refresh reports malformed JSON responses clearly");

  const auto root = temp_root() / "oauth-refresh";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  setenv("HOME", (root / "home").c_str(), 1);
  setenv("XDG_CONFIG_HOME", (root / "config").c_str(), 1);
  setenv("XDG_STATE_HOME", (root / "state").c_str(), 1);
  setenv("XDG_DATA_HOME", (root / "data").c_str(), 1);
  const auto paths = ava::config::xdg_paths();
  auto stored = ava::config::store_openai_credential(
      paths, ava::config::OpenAICredential{.type = ava::config::OpenAICredentialType::OAuth,
                                           .access_token = "expired-access",
                                           .refresh_token = "persist-refresh",
                                           .expires_at = 100,
                                           .account_id = "acct_old",
                                           .source_path = {}});
  expect(stored.has_value(), "OpenAI OAuth refresh persistence test stores expired credential");
  auto loaded = ava::config::load_openai_credential(paths);
  expect(loaded && loaded->has_value(), "OpenAI OAuth refresh persistence test loads expired credential");
  if (loaded && *loaded) {
    ava::tests::FakeTransport persist_transport({ava::provider::HttpResponse{
        .status_code = 200,
        .headers = {},
        .body = "{\"access_token\":\"persisted-access\",\"refresh_token\":\"persisted-refresh\","
                "\"expires_in\":60,\"account_id\":\"acct_persisted\"}",
    }});
    auto usable = ava::config::openai_credential_for_request(paths, **loaded, persist_transport, 1000);
    expect(usable && usable->access_token == "persisted-access" && usable->refresh_token == "persisted-refresh" &&
               usable->expires_at == 1060 && usable->account_id == "acct_persisted",
           "OpenAI OAuth credential preflight refreshes expired credential before use");
    auto persisted = ava::config::load_openai_credential(paths);
    expect(persisted && persisted->has_value() && (*persisted)->access_token == "persisted-access" &&
               (*persisted)->refresh_token == "persisted-refresh",
           "OpenAI OAuth credential preflight persists refreshed token rotation");
  }

  auto failure_store = ava::config::store_openai_credential(
      paths, ava::config::OpenAICredential{.type = ava::config::OpenAICredentialType::OAuth,
                                           .access_token = "failure-access",
                                           .refresh_token = "failure-refresh",
                                           .expires_at = 100,
                                           .account_id = "acct_failure",
                                           .source_path = {}});
  expect(failure_store.has_value(), "OpenAI OAuth refresh failure test stores expired credential");
  loaded = ava::config::load_openai_credential(paths);
  if (loaded && *loaded) {
    ava::tests::FakeTransport failure_transport({ava::provider::HttpResponse{
        .status_code = 400,
        .headers = {},
        .body = "{\"error\":\"invalid_grant\"}",
    }});
    auto failed = ava::config::openai_credential_for_request(paths, **loaded, failure_transport, 1000);
    expect(!failed && failed.error().message().find("ava connect openai") != std::string::npos,
           "OpenAI OAuth refresh failure suggests reconnecting");
    auto unchanged = ava::config::load_openai_credential(paths);
    expect(unchanged && unchanged->has_value() && (*unchanged)->access_token == "failure-access" &&
               (*unchanged)->refresh_token == "failure-refresh" && (*unchanged)->expires_at == 100,
           "OpenAI OAuth refresh failure does not overwrite existing credential");
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

ava::config::XdgPaths app_test_paths(const std::filesystem::path& root) {
  const auto config_home = root / "config";
  const auto state_home = root / "state";
  const auto data_home = root / "data";
  const auto ava_config = config_home / "ava";
  const auto ava_state = state_home / "ava";
  return ava::config::XdgPaths{.config_home = config_home,
                               .state_home = state_home,
                               .data_home = data_home,
                               .ava_config_dir = ava_config,
                               .ava_state_dir = ava_state,
                               .auth_file = ava_config / "auth.json",
                               .compaction_file = ava_config / "compaction.json",
                               .global_agents_file = ava_config / "AGENTS.md",
                               .models_file = ava_config / "models.json",
                               .prompts_dir = ava_config / "prompts",
                               .sessions_dir = ava_state / "sessions"};
}

void test_app_event_serialization() {
  ava::app::RuntimeEvent session_event;
  session_event.type = ava::app::RuntimeEventType::SessionStart;
  session_event.timestamp = "2026-04-29T00:00:00Z";
  session_event.session_id = "session_1";
  session_event.mode = ava::agent::Mode::Plan;
  session_event.provider_id = "openai";
  session_event.model_id = "gpt-5.5";
  const auto jsonl = ava::app::serialize_event_jsonl(session_event);
  expect(jsonl ==
             "{\"type\":\"session_start\",\"timestamp\":\"2026-04-29T00:00:00Z\","
             "\"session_id\":\"session_1\",\"mode\":\"plan\",\"provider\":\"openai\","
             "\"model\":\"gpt-5.5\"}\n",
         "runtime event JSONL serialization is deterministic");

  ava::app::RuntimeEvent message_event;
  message_event.type = ava::app::RuntimeEventType::UserMessage;
  message_event.timestamp = "2026-04-29T00:00:01Z";
  message_event.session_id = "session_1";
  message_event.text = "hello\n\"ava\"";
  const auto message_jsonl = ava::app::serialize_event_jsonl(message_event);
  expect(message_jsonl.find("hello\\n\\\"ava\\\"") != std::string::npos, "runtime event JSONL escapes message text");
  expect(message_jsonl.ends_with('\n') &&
             message_jsonl.substr(0, message_jsonl.size() - 1).find('\n') == std::string::npos,
         "runtime event JSONL contains one terminating newline only");
}

void test_app_runtime_open_session_and_context_prompt() {
  const auto root = temp_root() / "app-runtime-open";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto current = workspace / "src";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(current);
  std::filesystem::create_directories(paths.ava_config_dir);
  {
    std::ofstream file(workspace / "AGENTS.md", std::ios::binary | std::ios::trunc);
    file << "workspace runtime instructions\n";
  }
  {
    std::ofstream file(current / "AGENTS.md", std::ios::binary | std::ios::trunc);
    file << "nested runtime instructions\n";
  }
  {
    std::ofstream file(paths.global_agents_file, std::ios::binary | std::ios::trunc);
    file << "global runtime instructions\n";
  }

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = current;
  open_options.mode = ava::agent::Mode::Plan;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "runtime session opens with selected model, prompt, and context");
  if (!session) return;

  expect(session->created && session->mode == ava::agent::Mode::Plan && session->model.model_id == "gpt-5.5",
         "runtime session records created state, mode, and model");
  expect(session->context_sources.size() == 3, "runtime session records workspace and global context metadata");
  expect(session->system_prompt.find("Plan before changing files") != std::string::npos &&
             session->system_prompt.find("workspace runtime instructions") != std::string::npos &&
             session->system_prompt.find("nested runtime instructions") != std::string::npos &&
             session->system_prompt.find("global runtime instructions") != std::string::npos,
         "runtime session system prompt combines selected prompt and formatted context");
  auto entries = session->store.load();
  expect(entries && entries->size() == 1 && (*entries)[0].type == ava::session::EntryType::SessionStart &&
             (*entries)[0].data_json.find("\"context_sources\":3") != std::string::npos,
         "runtime session appends session_start on creation");

  const auto session_id = session->store.session_id();
  ava::app::RuntimeOpenOptions reopen_options;
  reopen_options.workspace_dir = workspace;
  reopen_options.current_dir = current;
  reopen_options.requested_session_id = session_id.substr(0, 12);
  reopen_options.mode = ava::agent::Mode::Plan;
  reopen_options.paths = paths;
  auto reopened = ava::app::open_runtime_session(reopen_options);
  expect(reopened && !reopened->created && reopened->store.session_id() == session_id,
         "runtime session resolves requested session id prefixes without creating a new session");
  if (reopened) {
    auto reopened_entries = reopened->store.load();
    expect(reopened_entries && reopened_entries->size() == 1,
           "runtime reopened session does not append another session_start");
  }
}

void test_app_run_prompt_emits_events() {
  const auto root = temp_root() / "app-runtime-run";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  {
    std::ofstream file(workspace / "AGENTS.md", std::ios::binary | std::ios::trunc);
    file << "runtime run context\n";
  }

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Build;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "runtime run test opens session");
  if (!session) return;

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{
      .status_code = 200,
      .headers = {},
      .body = "data: {\"type\":\"response.output_text.delta\",\"delta\":\"runtime answer\"}\n\n"
              "data: [DONE]\n\n",
  }});
  std::vector<ava::app::RuntimeEvent> events;
  ava::app::RuntimeRunOptions run_options;
  run_options.access_token = "token";
  run_options.event_sink = [&events](const ava::app::RuntimeEvent& event) {
    events.push_back(event);
    return ava::core::VoidResult{};
  };
  auto result = ava::app::run_prompt(*session, "hello runtime", provider, transport, run_options);
  expect(result && result->final_text == "runtime answer", "runtime run_prompt returns agent loop result");
  expect(events.size() == 4 && events[0].type == ava::app::RuntimeEventType::SessionStart &&
             events[1].type == ava::app::RuntimeEventType::UserMessage &&
             events[2].type == ava::app::RuntimeEventType::AssistantMessage &&
             events[3].type == ava::app::RuntimeEventType::Done,
         "runtime run_prompt emits session, user, assistant, and done events");
  expect(events.size() == 4 && events[2].text == "runtime answer" && events[3].provider_iterations == 1,
         "runtime run_prompt events include final text and completion counters");
  expect(
      transport.requests().size() == 1 && transport.requests()[0].body.find("runtime run context") != std::string::npos,
      "runtime run_prompt sends context-augmented system prompt to provider");
  auto entries = session->store.load();
  expect(entries && entries->size() == 3 && (*entries)[1].type == ava::session::EntryType::UserMessage &&
             (*entries)[2].type == ava::session::EntryType::AssistantMessage,
         "runtime run_prompt persists user and assistant entries in the runtime session");
}

void test_app_run_prompt_event_sink_failure_cancels_before_next_provider_call() {
  const auto root = temp_root() / "app-runtime-event-sink-cancel";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  {
    std::ofstream file(workspace / "note.txt", std::ios::binary | std::ios::trunc);
    file << "event sink cancel data";
  }

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Build;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "runtime event sink failure test opens session");
  if (!session) return;

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{
                                           .status_code = 200,
                                           .headers = {},
                                           .body = "data: {\"type\":\"response.function_call.added\",\"item_id\":"
                                                   "\"call_read\",\"name\":\"read_file\"}\n\n"
                                                   "data: "
                                                   "{\"type\":\"response.function_call_arguments.delta\","
                                                   "\"item_id\":\"call_read\",\"delta\":\"{\\\"path\\\":"
                                                   "\\\"note.txt\\\"}\"}\n\n"
                                                   "data: [DONE]\n\n",
                                       },
                                       ava::provider::HttpResponse{
                                           .status_code = 200,
                                           .headers = {},
                                           .body = "data: {\"type\":\"response.output_text.delta\",\"delta\":"
                                                   "\"should not request\"}\n\n"
                                                   "data: [DONE]\n\n",
                                       }});
  ava::app::RuntimeRunOptions run_options;
  run_options.access_token = "token";
  run_options.event_sink = [](const ava::app::RuntimeEvent& event) {
    if (event.type == ava::app::RuntimeEventType::ToolStart) {
      return ava::core::VoidResult{
          std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "event sink failed"))};
    }
    return ava::core::VoidResult{};
  };

  auto result = ava::app::run_prompt(*session, "read with failing sink", provider, transport, run_options);
  expect(!result && result.error().category() == ava::core::ErrorCategory::Io &&
             result.error().message() == "event sink failed",
         "runtime returns the event sink write failure");
  expect(transport.requests().size() == 1, "event sink failure cancels before the next provider request");
}

void test_app_print_prompt_merging() {
  auto explicit_only = ava::app::merge_print_prompt(
      ava::app::PrintPromptInputs{.explicit_prompt = std::string("explicit"), .stdin_prompt = std::nullopt});
  expect(explicit_only && *explicit_only == "explicit", "print prompt uses explicit prompt when stdin is absent");

  auto stdin_only = ava::app::merge_print_prompt(
      ava::app::PrintPromptInputs{.explicit_prompt = std::nullopt, .stdin_prompt = std::string("stdin")});
  expect(stdin_only && *stdin_only == "stdin", "print prompt uses stdin when explicit prompt is absent");

  auto merged = ava::app::merge_print_prompt(
      ava::app::PrintPromptInputs{.explicit_prompt = std::string("explicit"), .stdin_prompt = std::string("stdin")});
  expect(merged && *merged == "explicit\n\nstdin", "print prompt merges explicit and stdin prompts deterministically");

  auto missing = ava::app::merge_print_prompt(
      ava::app::PrintPromptInputs{.explicit_prompt = std::nullopt, .stdin_prompt = std::nullopt});
  expect(!missing && missing.error().message().find("requires a prompt") != std::string::npos,
         "print prompt rejects missing prompt input");
}

void test_headless_permission_policy() {
  const auto workspace = temp_root() / "headless-policy" / "workspace";
  const auto outside = temp_root() / "headless-policy" / "outside.txt";

  const ava::permissions::PermissionPrompt read_prompt{.operation = ava::permissions::Operation::ReadFile,
                                                       .mode = ava::agent::Mode::Build,
                                                       .workspace_dir = workspace,
                                                       .target_path = outside,
                                                       .command = "",
                                                       .tool_name = "read_file",
                                                       .reason = "target is outside the workspace"};
  const ava::permissions::PermissionPrompt search_prompt{.operation = ava::permissions::Operation::SearchFiles,
                                                         .mode = ava::agent::Mode::Build,
                                                         .workspace_dir = workspace,
                                                         .target_path = workspace,
                                                         .command = "",
                                                         .tool_name = "glob",
                                                         .reason = "search requires approval"};
  const ava::permissions::PermissionPrompt write_prompt{.operation = ava::permissions::Operation::EditFile,
                                                        .mode = ava::agent::Mode::Build,
                                                        .workspace_dir = workspace,
                                                        .target_path = outside,
                                                        .command = "",
                                                        .tool_name = "write_file",
                                                        .reason = "target is outside the workspace"};
  const ava::permissions::PermissionPrompt bash_prompt{.operation = ava::permissions::Operation::RunCommand,
                                                       .mode = ava::agent::Mode::Build,
                                                       .workspace_dir = workspace,
                                                       .target_path = workspace,
                                                       .command = "true",
                                                       .tool_name = "bash",
                                                       .reason = "command risk is unknown"};

  auto default_resolver = ava::app::build_headless_permission_resolver(ava::app::HeadlessPermissionPolicyOptions{});
  auto default_read = default_resolver(read_prompt);
  expect(default_read && *default_read == ava::permissions::PermissionResolution::Deny,
         "headless default resolver denies Ask prompts");

  ava::app::HeadlessPermissionPolicyOptions read_only_options;
  auto read_only_added = ava::app::add_headless_allow_policy(read_only_options, "read-only");
  expect(read_only_added.has_value(), "headless read-only allow value parses");
  auto read_only_resolver = ava::app::build_headless_permission_resolver(read_only_options);
  auto read_only_read = read_only_resolver(read_prompt);
  auto read_only_search = read_only_resolver(search_prompt);
  auto read_only_write = read_only_resolver(write_prompt);
  auto read_only_bash = read_only_resolver(bash_prompt);
  expect(read_only_read && *read_only_read == ava::permissions::PermissionResolution::Allow,
         "headless read-only policy allows read prompts");
  expect(read_only_search && *read_only_search == ava::permissions::PermissionResolution::Allow,
         "headless read-only policy allows search prompts");
  expect(read_only_write && *read_only_write == ava::permissions::PermissionResolution::Deny,
         "headless read-only policy denies write prompts");
  expect(read_only_bash && *read_only_bash == ava::permissions::PermissionResolution::Deny,
         "headless read-only policy denies bash prompts");

  ava::app::HeadlessPermissionPolicyOptions tool_options;
  auto tools_added = ava::app::add_headless_allowed_tools(tool_options, "glob,grep,read_file");
  expect(tools_added.has_value() && tool_options.allowed_tools.size() == 3,
         "headless allow-tool parses supported comma-separated tool names");
  auto tool_resolver = ava::app::build_headless_permission_resolver(tool_options);
  const auto tool_read = tool_resolver(read_prompt);
  const auto tool_search = tool_resolver(search_prompt);
  const ava::permissions::PermissionPrompt lower_layer_read_prompt{.operation = ava::permissions::Operation::ReadFile,
                                                                   .mode = ava::agent::Mode::Build,
                                                                   .workspace_dir = workspace,
                                                                   .target_path = outside,
                                                                   .command = "",
                                                                   .tool_name = "read",
                                                                   .reason = "target is outside the workspace"};
  const ava::permissions::PermissionPrompt mismatched_tool_prompt{.operation = ava::permissions::Operation::EditFile,
                                                                  .mode = ava::agent::Mode::Build,
                                                                  .workspace_dir = workspace,
                                                                  .target_path = outside,
                                                                  .command = "",
                                                                  .tool_name = "read_file",
                                                                  .reason = "target is outside the workspace"};
  const auto lower_layer_read = tool_resolver(lower_layer_read_prompt);
  const auto mismatched_tool = tool_resolver(mismatched_tool_prompt);
  expect(tool_read && *tool_read == ava::permissions::PermissionResolution::Allow,
         "headless allow-tool allows exact read_file prompts");
  expect(tool_search && *tool_search == ava::permissions::PermissionResolution::Allow,
         "headless allow-tool allows exact glob search prompts");
  expect(lower_layer_read && *lower_layer_read == ava::permissions::PermissionResolution::Deny,
         "headless allow-tool requires exact tool names");
  expect(mismatched_tool && *mismatched_tool == ava::permissions::PermissionResolution::Deny,
         "headless allow-tool does not allow unsafe operations with a safe tool name");

  auto invalid_allow = ava::app::add_headless_allow_policy(tool_options, "nope");
  auto invalid_tool = ava::app::add_headless_allowed_tools(tool_options, "glob,nope");
  auto empty_tool = ava::app::add_headless_allowed_tools(tool_options, "glob,");
  expect(!invalid_allow && invalid_allow.error().category() == ava::core::ErrorCategory::InvalidArgument,
         "headless --allow rejects unsupported values");
  expect(!invalid_tool && invalid_tool.error().category() == ava::core::ErrorCategory::InvalidArgument,
         "headless --allow-tool rejects unsupported values");
  expect(!empty_tool && empty_tool.error().category() == ava::core::ErrorCategory::InvalidArgument,
         "headless --allow-tool rejects empty tool names");
}

void test_app_print_text_mode_outputs_final_text_only() {
  const auto root = temp_root() / "app-print-text";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Build;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "print text test opens runtime session");
  if (!session) return;

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{
      .status_code = 200,
      .headers = {},
      .body = "data: {\"type\":\"response.output_text.delta\",\"delta\":\"print answer\"}\n\n"
              "data: [DONE]\n\n",
  }});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  const ava::app::PrintModeRunOptions run_options{.output_format = ava::app::PrintOutputFormat::Text,
                                                  .runtime_options = runtime_options};
  std::ostringstream out;
  std::ostringstream err;
  auto result = ava::app::run_print_prompt(*session, "hello print", provider, transport, run_options, out, err);
  expect(result && result->final_text == "print answer", "print text mode returns agent result");
  expect(out.str() == "print answer" && err.str().empty(), "print text mode writes only final text to stdout");
  expect(transport.requests().size() == 1 && transport.requests()[0].body.find("hello print") != std::string::npos,
         "print text mode sends prompt through shared runtime");
}

void test_app_print_text_mode_reports_stdout_write_failure() {
  const auto root = temp_root() / "app-print-text-write-failure";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Build;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "print text stdout failure test opens runtime session");
  if (!session) return;

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{
      .status_code = 200,
      .headers = {},
      .body = "data: {\"type\":\"response.output_text.delta\",\"delta\":\"print answer\"}\n\n"
              "data: [DONE]\n\n",
  }});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  const ava::app::PrintModeRunOptions run_options{.output_format = ava::app::PrintOutputFormat::Text,
                                                  .runtime_options = runtime_options};
  FailingStreambuf failing_buffer;
  std::ostream out(&failing_buffer);
  std::ostringstream err;
  auto result = ava::app::run_print_prompt(*session, "hello print", provider, transport, run_options, out, err);
  expect(!result && result.error().category() == ava::core::ErrorCategory::Io &&
             result.error().message() == "failed to write print output",
         "print text mode reports stdout write failures");
}

void test_app_print_mode_uses_headless_permission_policy() {
  const auto root = temp_root() / "app-print-policy";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto outside_path = root / "outside.txt";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  {
    std::ofstream file(outside_path, std::ios::binary | std::ios::trunc);
    file << "outside print policy";
  }

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Build;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "print policy test opens runtime session");
  if (!session) return;

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{
                                           .status_code = 200,
                                           .headers = {},
                                           .body = "data: {\"type\":\"response.function_call.added\",\"item_id\":"
                                                   "\"call_outside\",\"name\":\"read_file\"}\n\n"
                                                   "data: "
                                                   "{\"type\":\"response.function_call_arguments.delta\","
                                                   "\"item_id\":\"call_outside\",\"delta\":\"{"
                                                   "\\\"path\\\":\\\"" +
                                                   ava::core::json::escape(outside_path.generic_string()) +
                                                   "\\\"}\"}\n\n"
                                                   "data: [DONE]\n\n",
                                       },
                                       ava::provider::HttpResponse{
                                           .status_code = 200,
                                           .headers = {},
                                           .body = "data: {\"type\":\"response.output_text.delta\",\"delta\":"
                                                   "\"policy allowed\"}\n\n"
                                                   "data: [DONE]\n\n",
                                       }});

  ava::app::HeadlessPermissionPolicyOptions policy_options;
  auto allowed_tool = ava::app::add_headless_allowed_tools(policy_options, "read_file");
  expect(allowed_tool.has_value(), "print policy test configures read_file allow-tool");
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  runtime_options.permission_resolver = ava::app::build_headless_permission_resolver(policy_options);
  const ava::app::PrintModeRunOptions run_options{.output_format = ava::app::PrintOutputFormat::Text,
                                                  .runtime_options = std::move(runtime_options)};
  std::ostringstream out;
  std::ostringstream err;
  auto result =
      ava::app::run_print_prompt(*session, "read outside in print", provider, transport, run_options, out, err);
  expect(result && result->final_text == "policy allowed" && result->tool_calls == 1,
         "print mode uses supplied headless permission resolver for tool asks");
  expect(transport.requests().size() == 2 &&
             transport.requests()[1].body.find("outside print policy") != std::string::npos,
         "print mode continuation includes allow-tool-approved read_file result");
}

void test_app_print_mode_refreshes_expired_oauth_before_provider_request() {
  const auto root = temp_root() / "app-print-oauth-refresh";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  auto stored = ava::config::store_openai_credential(
      paths, ava::config::OpenAICredential{.type = ava::config::OpenAICredentialType::OAuth,
                                           .access_token = "expired-print-access",
                                           .refresh_token = "print-refresh",
                                           .expires_at = 100,
                                           .account_id = "acct_old",
                                           .source_path = {}});
  expect(stored.has_value(), "print OAuth refresh test stores expired credential");

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{
                                           .status_code = 200,
                                           .headers = {},
                                           .body = "{\"access_token\":\"print-refreshed-access\","
                                                   "\"refresh_token\":\"print-rotated-refresh\","
                                                   "\"expires_in\":3600,\"account_id\":\"acct_print\"}",
                                       },
                                       ava::provider::HttpResponse{
                                           .status_code = 200,
                                           .headers = {},
                                           .body = "data: {\"type\":\"response.output_text.delta\",\"delta\":"
                                                   "\"print refreshed answer\"}\n\n"
                                                   "data: [DONE]\n\n",
                                       }});

  ava::app::PrintModeOptions options;
  options.open_options.workspace_dir = workspace;
  options.open_options.current_dir = workspace;
  options.open_options.mode = ava::agent::Mode::Build;
  options.open_options.paths = paths;
  options.explicit_prompt = "hello refreshed print";
  options.provider_override = std::cref(provider);
  options.transport_override = std::ref(transport);

  std::istringstream in;
  std::ostringstream out;
  std::ostringstream err;
  const auto exit_code = ava::app::run_print_mode(options, in, out, err);
  expect(exit_code == 0 && out.str() == "print refreshed answer" && err.str().empty(),
         "print mode completes after refreshing expired OAuth credentials");
  expect(transport.requests().size() == 2 && transport.requests()[0].url == "https://auth.openai.com/oauth/token" &&
             transport.requests()[1].headers.at("Authorization") == "Bearer print-refreshed-access" &&
             transport.requests()[1].headers.at("ChatGPT-Account-Id") == "acct_print" &&
             transport.requests()[1].body.find("hello refreshed print") != std::string::npos,
         "print mode refreshes OAuth before sending provider request");
  auto persisted = ava::config::load_openai_credential(paths);
  expect(persisted && persisted->has_value() && (*persisted)->access_token == "print-refreshed-access" &&
             (*persisted)->refresh_token == "print-rotated-refresh",
         "print mode OAuth preflight persists refreshed credential before provider startup");
}

void test_app_print_json_mode_outputs_runtime_events() {
  const auto root = temp_root() / "app-print-json";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Plan;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "print json test opens runtime session");
  if (!session) return;

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{
      .status_code = 200,
      .headers = {},
      .body = "data: {\"type\":\"response.output_text.delta\",\"delta\":\"json answer\"}\n\n"
              "data: [DONE]\n\n",
  }});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  const ava::app::PrintModeRunOptions run_options{.output_format = ava::app::PrintOutputFormat::Json,
                                                  .runtime_options = runtime_options};
  std::ostringstream out;
  std::ostringstream err;
  auto result = ava::app::run_print_prompt(*session, "json prompt", provider, transport, run_options, out, err);
  const auto jsonl = out.str();
  const auto last_break = jsonl.size() > 1 ? jsonl.rfind('\n', jsonl.size() - 2) : std::string::npos;
  const auto last_line = jsonl.substr(last_break == std::string::npos ? 0 : last_break + 1);
  expect(result && result->final_text == "json answer", "print json mode returns agent result");
  expect(err.str().empty(), "print json mode leaves diagnostics on stderr only when needed");
  expect(std::count(jsonl.begin(), jsonl.end(), '\n') == 4 &&
             jsonl.find("\"type\":\"session_start\"") != std::string::npos &&
             jsonl.find("\"type\":\"user_message\"") != std::string::npos &&
             jsonl.find("\"type\":\"assistant_message\"") != std::string::npos &&
             last_line.find("\"type\":\"done\"") != std::string::npos,
         "print json mode writes JSONL runtime events ending in done");

  auto error_session = ava::app::open_runtime_session(open_options);
  expect(error_session.has_value(), "print json error test opens runtime session");
  if (!error_session) return;
  ava::tests::FakeTransport error_transport({ava::provider::HttpResponse{
      .status_code = 500,
      .headers = {},
      .body = "upstream failure",
  }});
  std::ostringstream error_out;
  std::ostringstream error_err;
  auto error_result = ava::app::run_print_prompt(*error_session, "json error", provider, error_transport, run_options,
                                                 error_out, error_err);
  const auto error_jsonl = error_out.str();
  const auto error_last_break =
      error_jsonl.size() > 1 ? error_jsonl.rfind('\n', error_jsonl.size() - 2) : std::string::npos;
  const auto error_last_line = error_jsonl.substr(error_last_break == std::string::npos ? 0 : error_last_break + 1);
  expect(!error_result && error_err.str().empty() && error_last_line.find("\"type\":\"error\"") != std::string::npos,
         "print json mode writes failed turns as JSONL ending in error");
}

void test_app_command_dispatcher() {
  const auto root = temp_root() / "app-command-dispatcher";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace / "src");
  std::filesystem::create_directories(paths.ava_config_dir);
  {
    std::ofstream file(workspace / "AGENTS.md", std::ios::binary | std::ios::trunc);
    file << "dispatcher context\n";
  }
  {
    std::ofstream file(workspace / "src" / "main.cpp", std::ios::binary | std::ios::trunc);
    file << "int main() { return 0; }\n";
  }

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Plan;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "command dispatcher test opens runtime session");
  if (!session) return;
  const auto plan_system_prompt = session->system_prompt;

  auto context = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/context"});
  expect(context && context->handled && !context->output.empty() &&
             context->output[0].find("workspace") != std::string::npos &&
             context->output[0].find("AGENTS.md") != std::string::npos,
         "command dispatcher /context reports loaded context metadata");

  auto mode = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/mode"});
  expect(mode && mode->handled && session->mode == ava::agent::Mode::Build && !mode->output.empty() &&
             mode->output[0].find("build") != std::string::npos,
         "command dispatcher /mode toggles runtime mode");
  expect(session->system_prompt != plan_system_prompt &&
             session->system_prompt.find("Implement changes directly") != std::string::npos &&
             session->system_prompt.find("dispatcher context") != std::string::npos,
         "command dispatcher /mode rebuilds the mode-specific system prompt with context");

  auto glob = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/glob **/*.cpp"});
  expect(glob && glob->handled && !glob->output.empty() && glob->output[0].find("src/main.cpp") != std::string::npos,
         "command dispatcher /glob runs existing safe file search command");
  expect(glob && glob->tool_timeline.size() == 2 &&
             glob->tool_timeline[0].status == ava::agent::ToolTimelineStatus::Running &&
             glob->tool_timeline[1].status == ava::agent::ToolTimelineStatus::Success,
         "command dispatcher records running and completed timeline entries");

  auto compact = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/compact Keep key facts"});
  expect(compact && compact->handled && !compact->output.empty() &&
             compact->output[0].find("manual compaction recorded") != std::string::npos,
         "command dispatcher /compact records manual compaction");
  auto compact_empty = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/compact"});
  expect(compact_empty && compact_empty->handled && !compact_empty->output.empty() &&
             compact_empty->output[0].find("manual compaction recorded") != std::string::npos,
         "command dispatcher /compact without instructions records manual compaction");
  auto compact_trailing = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/compact "});
  expect(compact_trailing && compact_trailing->handled && !compact_trailing->output.empty() &&
             compact_trailing->output[0].find("manual compaction recorded") != std::string::npos,
         "command dispatcher /compact with trailing space records manual compaction");

  auto entries = session->store.load();
  expect(entries && std::ranges::any_of(*entries,
                                        [](const ava::session::SessionEntry& entry) {
                                          return entry.type == ava::session::EntryType::Compaction &&
                                                 entry.data_json.find("Keep key facts") != std::string::npos &&
                                                 entry.data_json.find("unavailable") != std::string::npos;
                                        }),
         "command dispatcher /compact persists deterministic unavailable summary and instructions");

  auto exported = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/export"});
  expect(exported && exported->handled && !exported->output.empty() &&
             exported->output[0].find("# AVA Session Export") != std::string::npos &&
             exported->output[0].find("## Compaction") != std::string::npos,
         "command dispatcher /export returns markdown for loaded session entries");

  auto quit = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/quit"});
  expect(quit && quit->handled && quit->quit, "command dispatcher /quit requests shell exit");
}

void test_app_rpc_parsing_and_response_serialization() {
  auto command = ava::app::parse_rpc_command_line(
      "{\"id\":\"1\",\"type\":\"prompt\",\"message\":\"hello\\nava\",\"instructions\":\"keep\"}");
  expect(command && command->id == "1" && command->type == "prompt" && command->message &&
             *command->message == "hello\nava" && command->instructions && *command->instructions == "keep",
         "RPC parser extracts string envelope fields and unescapes JSON strings");

  auto malformed = ava::app::parse_rpc_command_line("{\"id\":\"bad\",\"type\":\"prompt\"");
  expect(!malformed && malformed.error().category() == ava::core::ErrorCategory::InvalidArgument,
         "RPC parser rejects malformed JSON object lines");

  const auto success = ava::app::serialize_rpc_success_jsonl("a\"b", "{\"value\":1}");
  expect(success == "{\"id\":\"a\\\"b\",\"type\":\"response\",\"success\":true,\"result\":{\"value\":1}}\n",
         "RPC success response serializes deterministic JSONL with escaped id");

  const auto error = ava::app::serialize_rpc_error_jsonl(
      "e1", ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "bad \"request\""));
  expect(error.find("\"success\":false") != std::string::npos &&
             error.find("bad \\\"request\\\"") != std::string::npos && error.ends_with('\n'),
         "RPC error response serializes JSONL error details");
}

void test_app_rpc_prompt_with_fake_transport_streams_events() {
  const auto root = temp_root() / "app-rpc-prompt";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Build;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC prompt test opens runtime session");
  if (!session) return;

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{
      .status_code = 200,
      .headers = {},
      .body = "data: {\"type\":\"response.output_text.delta\",\"delta\":\"rpc answer\"}\n\n"
              "data: [DONE]\n\n",
  }});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  std::istringstream in("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"hello rpc\"}\n");
  std::ostringstream out;
  auto result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out);
  const auto jsonl = out.str();
  expect(result.has_value(), "RPC prompt loop completes successfully");
  expect(transport.requests().size() == 1 && transport.requests()[0].body.find("hello rpc") != std::string::npos,
         "RPC prompt sends command message through shared runtime");
  expect(jsonl.find("\"type\":\"session_start\"") != std::string::npos &&
             jsonl.find("\"type\":\"assistant_message\"") != std::string::npos &&
             jsonl.find("\"id\":\"p1\"") != std::string::npos && jsonl.find("\"success\":true") != std::string::npos &&
             jsonl.find("rpc answer") != std::string::npos,
         "RPC prompt streams runtime events and ends with a successful response");
}

void test_app_rpc_prompt_refreshes_expired_oauth_before_provider_request() {
  const auto root = temp_root() / "app-rpc-oauth-refresh";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  auto stored = ava::config::store_openai_credential(
      paths, ava::config::OpenAICredential{.type = ava::config::OpenAICredentialType::OAuth,
                                           .access_token = "expired-rpc-access",
                                           .refresh_token = "rpc-refresh",
                                           .expires_at = 100,
                                           .account_id = "acct_old",
                                           .source_path = {}});
  expect(stored.has_value(), "RPC OAuth refresh test stores expired credential");

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Build;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC OAuth refresh test opens runtime session");
  if (!session) return;

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{
                                           .status_code = 200,
                                           .headers = {},
                                           .body = "{\"access_token\":\"rpc-refreshed-access\","
                                                   "\"refresh_token\":\"rpc-rotated-refresh\","
                                                   "\"expires_in\":3600,\"account_id\":\"acct_rpc\"}",
                                       },
                                       ava::provider::HttpResponse{
                                           .status_code = 200,
                                           .headers = {},
                                           .body = "data: {\"type\":\"response.output_text.delta\",\"delta\":"
                                                   "\"rpc refreshed answer\"}\n\n"
                                                   "data: [DONE]\n\n",
                                       }});
  std::istringstream in("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"hello refreshed rpc\"}\n");
  std::ostringstream out;
  auto result =
      ava::app::run_rpc_loop(*session, open_options, provider, transport, ava::app::RuntimeRunOptions{}, in, out);
  const auto jsonl = out.str();
  expect(result.has_value(), "RPC prompt with expired OAuth completes after refresh");
  expect(transport.requests().size() == 2 && transport.requests()[0].url == "https://auth.openai.com/oauth/token" &&
             transport.requests()[1].headers.at("Authorization") == "Bearer rpc-refreshed-access" &&
             transport.requests()[1].headers.at("ChatGPT-Account-Id") == "acct_rpc",
         "RPC prompt refreshes OAuth before sending provider request");
  expect(jsonl.find("rpc refreshed answer") != std::string::npos && jsonl.find("\"success\":true") != std::string::npos,
         "RPC prompt returns refreshed OAuth provider response");
  auto persisted = ava::config::load_openai_credential(paths);
  expect(persisted && persisted->has_value() && (*persisted)->access_token == "rpc-refreshed-access" &&
             (*persisted)->refresh_token == "rpc-rotated-refresh",
         "RPC OAuth preflight persists refreshed credential before provider startup");
}

void test_app_rpc_malformed_line_recovery_and_unknown_command() {
  const auto root = temp_root() / "app-rpc-recovery";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC recovery test opens runtime session");
  if (!session) return;

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  std::istringstream in(
      "not json\n{\"id\":\"s1\",\"type\":\"get_state\"}\n"
      "{\"id\":\"u1\",\"type\":\"unknown\"}\n");
  std::ostringstream out;
  auto result =
      ava::app::run_rpc_loop(*session, open_options, provider, transport, ava::app::RuntimeRunOptions{}, in, out);
  const auto jsonl = out.str();
  expect(result.has_value(), "RPC loop continues after malformed and unknown commands");
  expect(std::count(jsonl.begin(), jsonl.end(), '\n') == 3 && jsonl.find("\"id\":\"\"") != std::string::npos &&
             jsonl.find("malformed RPC JSON object") != std::string::npos &&
             jsonl.find("\"id\":\"s1\"") != std::string::npos && jsonl.find("\"session_id\":\"") != std::string::npos &&
             jsonl.find("\"id\":\"u1\"") != std::string::npos &&
             jsonl.find("unknown RPC command type") != std::string::npos,
         "RPC loop writes error responses and recovers for subsequent JSONL records");
}

void test_app_rpc_state_list_sessions_and_open_session() {
  const auto root = temp_root() / "app-rpc-state";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto first = ava::app::open_runtime_session(open_options);
  auto second = ava::app::open_runtime_session(open_options);
  expect(first.has_value() && second.has_value(), "RPC state test opens multiple sessions");
  if (!first || !second) return;
  const auto first_id = first->store.session_id();
  const auto second_id = second->store.session_id();

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  std::istringstream in(
      "{\"id\":\"state\",\"type\":\"get_state\"}\n"
      "{\"id\":\"list\",\"type\":\"list_sessions\"}\n"
      "{\"id\":\"open\",\"type\":\"open_session\",\"session_id\":\"" +
      first_id + "\"}\n");
  std::ostringstream out;
  auto result =
      ava::app::run_rpc_loop(*second, open_options, provider, transport, ava::app::RuntimeRunOptions{}, in, out);
  const auto jsonl = out.str();
  expect(result.has_value(), "RPC state/list/open loop completes successfully");
  expect(jsonl.find("\"id\":\"state\"") != std::string::npos && jsonl.find(second_id) != std::string::npos &&
             jsonl.find("\"id\":\"list\"") != std::string::npos && jsonl.find(first_id) != std::string::npos &&
             jsonl.find("\"id\":\"open\"") != std::string::npos,
         "RPC state, list_sessions, and open_session return session metadata");
  expect(second->store.session_id() == first_id, "RPC open_session switches the active runtime session");
}

void test_app_rpc_command_responses_for_context_compact_export() {
  const auto root = temp_root() / "app-rpc-commands";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  {
    std::ofstream file(workspace / "AGENTS.md", std::ios::binary | std::ios::trunc);
    file << "rpc command context\n";
  }

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC command test opens runtime session");
  if (!session) return;

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  std::istringstream in(
      "{\"id\":\"ctx\",\"type\":\"context\"}\n"
      "{\"id\":\"cmp\",\"type\":\"compact\",\"instructions\":\"remember rpc facts\"}\n"
      "{\"id\":\"exp\",\"type\":\"export\"}\n");
  std::ostringstream out;
  auto result =
      ava::app::run_rpc_loop(*session, open_options, provider, transport, ava::app::RuntimeRunOptions{}, in, out);
  const auto jsonl = out.str();
  expect(result.has_value(), "RPC context/compact/export loop completes successfully");
  expect(jsonl.find("\"id\":\"ctx\"") != std::string::npos && jsonl.find("AGENTS.md") != std::string::npos &&
             jsonl.find("\"id\":\"cmp\"") != std::string::npos &&
             jsonl.find("manual compaction recorded") != std::string::npos &&
             jsonl.find("\"id\":\"exp\"") != std::string::npos &&
             jsonl.find("# AVA Session Export") != std::string::npos &&
             jsonl.find("remember rpc facts") != std::string::npos,
         "RPC command responses expose command dispatcher output as JSONL protocol records");
}

void test_app_rpc_cancel_affects_subsequent_prompt() {
  const auto root = temp_root() / "app-rpc-cancel";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC cancel test opens runtime session");
  if (!session) return;

  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  std::istringstream in(
      "{\"id\":\"cancel\",\"type\":\"cancel\"}\n"
      "{\"id\":\"state\",\"type\":\"get_state\"}\n"
      "{\"id\":\"prompt\",\"type\":\"prompt\",\"message\":\"should cancel\"}\n");
  std::ostringstream out;
  auto result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out);
  const auto jsonl = out.str();
  expect(result.has_value(), "RPC cancel loop completes after canceled prompt response");
  expect(transport.requests().empty(), "RPC cancel flag prevents subsequent prompt provider request");
  expect(jsonl.find("\"id\":\"cancel\"") != std::string::npos &&
             jsonl.find("\"cancel_requested\":true") != std::string::npos &&
             jsonl.find("\"id\":\"prompt\"") != std::string::npos &&
             jsonl.find("agent loop canceled") != std::string::npos &&
             jsonl.find("\"success\":false") != std::string::npos,
         "RPC cancel response updates state and canceled prompts return protocol errors");
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
    expect(request->body.find("\"store\":false") == std::string::npos,
           "OpenAI API-key request does not force Codex store flag");
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

  const auto oauth_credential_request = provider.build_request(
      ava::provider::ProviderRequest{
          .provider_id = "openai", .model_id = "gpt-5.5", .system_prompt = "system", .messages = {}, .tools_json = {}},
      ava::config::OpenAICredential{.type = ava::config::OpenAICredentialType::OAuth,
                                    .access_token = "codex-token",
                                    .refresh_token = "refresh",
                                    .expires_at = 120,
                                    .account_id = "acct_123",
                                    .source_path = {}},
      11);
  expect(oauth_credential_request && oauth_credential_request->url == "https://chatgpt.com/backend-api/codex/responses",
         "OpenAI OAuth request targets ChatGPT Codex responses endpoint");
  if (oauth_credential_request) {
    expect(oauth_credential_request->headers.at("ChatGPT-Account-Id") == "acct_123" &&
               oauth_credential_request->headers.at("OpenAI-Beta") == "responses=experimental" &&
               oauth_credential_request->headers.at("originator") == "ava",
           "OpenAI OAuth request carries Codex account and beta headers");
    expect(oauth_credential_request->body.find("\"store\":false") != std::string::npos,
           "OpenAI OAuth request disables Codex response storage");
  }

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
  auto lifecycle = ava::provider::parse_openai_sse(
      "data: {\"type\":\"response.created\"}\n\n"
      "data: {\"type\":\"response.output_text.delta\",\"delta\":\"hi\"}\n\n"
      "data: {\"type\":\"response.output_text.done\"}\n\n"
      "data: {\"type\":\"response.function_call_arguments.done\",\"item_id\":\"call_1\"}\n\n"
      "data: [DONE]\n\n");
  expect(lifecycle && lifecycle->size() == 2 && (*lifecycle)[0].type == ava::provider::StreamEventType::TextDelta &&
             (*lifecycle)[0].text == "hi" && (*lifecycle)[1].type == ava::provider::StreamEventType::Done,
         "OpenAI SSE parser ignores non-content lifecycle events");
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
    ava::tests::FakeTransport transport({ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = "ok"}});
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
  const auto permission_bits = [](const std::filesystem::path& permission_path) {
    constexpr auto mask =
        std::filesystem::perms::owner_all | std::filesystem::perms::group_all | std::filesystem::perms::others_all;
    std::error_code status_error;
    return std::filesystem::status(permission_path, status_error).permissions() & mask;
  };
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

  auto control_call_id = dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = std::string("call_") + '\x01' + "bad", .name = "read_file", .arguments_json = "{\"path\":\"note.txt\"}"});
  expect(!control_call_id && control_call_id.error().message().find("control byte") != std::string::npos,
         "tool dispatcher rejects provider call ids with control bytes before tool use");

  auto long_call_id = dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = std::string(300, 'a'), .name = "read_file", .arguments_json = "{\"path\":\"note.txt\"}"});
  expect(!long_call_id && long_call_id.error().message().find("too long") != std::string::npos,
         "tool dispatcher rejects overlong provider call ids before tool use");

  auto nul_path = dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_nul_path", .name = "read_file", .arguments_json = "{\"path\":\"note\\u0000.txt\"}"});
  expect(nul_path && !nul_path->success && nul_path->result_text.find("control byte") != std::string::npos,
         "tool dispatcher rejects NUL bytes decoded into file paths");

  auto nul_command = dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_nul_command", .name = "bash", .arguments_json = "{\"command\":\"pwd\\u0000whoami\"}"});
  expect(nul_command && !nul_command->success && nul_command->result_text.find("control byte") != std::string::npos,
         "tool dispatcher rejects NUL bytes decoded into commands");

  auto nul_content = dispatcher.dispatch(
      ava::agent::ProviderToolCall{.id = "call_nul_content",
                                   .name = "write_file",
                                   .arguments_json = "{\"path\":\"nul.txt\",\"content\":\"bad\\u0000text\"}"});
  expect(nul_content && !nul_content->success && nul_content->result_text.find("NUL byte") != std::string::npos &&
             !std::filesystem::exists(workspace / "nul.txt"),
         "tool dispatcher rejects NUL bytes in text arguments before writing");

  auto nul_include = dispatcher.dispatch(
      ava::agent::ProviderToolCall{.id = "call_nul_include",
                                   .name = "grep",
                                   .arguments_json = "{\"pattern\":\"hello\",\"include\":\"**/*\\u0000\"}"});
  expect(nul_include && !nul_include->success && nul_include->result_text.find("control byte") != std::string::npos,
         "tool dispatcher rejects NUL bytes decoded into grep include globs");

  auto malformed_args = dispatcher.dispatch(
      ava::agent::ProviderToolCall{.id = "call_bad_args", .name = "read_file", .arguments_json = "{not-json}"});
  expect(
      malformed_args && !malformed_args->success && malformed_args->result_text.find("required") != std::string::npos,
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

  const auto private_patch_path = workspace / "private-patch.txt";
  {
    std::ofstream file(private_patch_path, std::ios::binary | std::ios::trunc);
    file << "private old";
  }
  std::error_code chmod_error;
  std::filesystem::permissions(private_patch_path,
                               std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::replace, chmod_error);
  expect(!chmod_error, "test can set private patch file permissions");
  auto private_patch = dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_private_patch",
      .name = "apply_patch",
      .arguments_json = "{\"edits\":[{\"path\":\"private-patch.txt\",\"old_text\":\"old\",\"new_text\":\"new\"}]}"});
  auto private_patch_read = ava::tools::read_file(
      ava::tools::ToolContext{.workspace_dir = workspace, .mode = ava::agent::Mode::Build}, private_patch_path);
  expect(private_patch && private_patch->success && private_patch_read && private_patch_read->content == "private new" &&
             permission_bits(private_patch_path) ==
                 (std::filesystem::perms::owner_read | std::filesystem::perms::owner_write),
         "apply_patch preserves 0600 permissions when replacing an existing file");

  const auto audit_patch_path = workspace / "audit-patch.txt";
  {
    std::ofstream file(audit_patch_path, std::ios::binary | std::ios::trunc);
    file << "audit old";
  }
  std::vector<ava::tools::PermissionAuditEvent> patch_audits;
  const ava::agent::ToolDispatcher audit_patch_dispatcher(ava::tools::ToolContext{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_audit_sink = [&patch_audits](const ava::tools::PermissionAuditEvent& event) -> ava::core::VoidResult {
        patch_audits.push_back(event);
        return {};
      }});
  auto audited_patch = audit_patch_dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_audit_patch",
      .name = "apply_patch",
      .arguments_json = "{\"edits\":[{\"path\":\"audit-patch.txt\",\"old_text\":\"old\",\"new_text\":\"new\"}]}"});
  expect(audited_patch && audited_patch->success && patch_audits.size() == 2 &&
             patch_audits[0].operation == ava::permissions::Operation::ReadFile &&
             patch_audits[0].tool_name == "apply_patch" &&
             patch_audits[1].operation == ava::permissions::Operation::EditFile &&
             patch_audits[1].tool_name == "apply_patch",
         "apply_patch audits read permission before edit permission");

  {
    std::ofstream file(workspace / "sequential.txt", std::ios::binary | std::ios::trunc);
    file << "one two";
  }
  auto sequential_patch = dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_sequential_patch",
      .name = "apply_patch",
      .arguments_json = "{\"edits\":[{\"path\":\"sequential.txt\",\"old_text\":\"one\",\"new_text\":\"two\"},"
                        "{\"path\":\"sequential.txt\",\"old_text\":\"two\",\"new_text\":\"three\"}]}"});
  auto sequential_read =
      ava::tools::read_file(ava::tools::ToolContext{.workspace_dir = workspace, .mode = ava::agent::Mode::Build},
                            workspace / "sequential.txt");
  expect(sequential_patch && sequential_patch->success && sequential_read && sequential_read->content == "two three",
         "apply_patch validates same-file edits against original content before applying replacements");

  {
    std::ofstream file(workspace / "overlap.txt", std::ios::binary | std::ios::trunc);
    file << "abcde";
  }
  auto overlap_patch = dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_overlap_patch",
      .name = "apply_patch",
      .arguments_json = "{\"edits\":[{\"path\":\"overlap.txt\",\"old_text\":\"abc\",\"new_text\":\"x\"},"
                        "{\"path\":\"overlap.txt\",\"old_text\":\"cde\",\"new_text\":\"y\"}]}"});
  auto overlap_read = ava::tools::read_file(
      ava::tools::ToolContext{.workspace_dir = workspace, .mode = ava::agent::Mode::Build}, workspace / "overlap.txt");
  expect(overlap_patch && !overlap_patch->success && overlap_read && overlap_read->content == "abcde" &&
             overlap_patch->result_text.find("patch edits overlap") != std::string::npos,
         "apply_patch rejects overlapping same-file edits before writing");

  auto empty_old_patch = dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_empty_old_patch",
      .name = "apply_patch",
      .arguments_json = "{\"edits\":[{\"path\":\"note.txt\",\"old_text\":\"\",\"new_text\":\"bad\"}]}"});
  expect(empty_old_patch && !empty_old_patch->success &&
             empty_old_patch->result_text.find("old_text must not be empty") != std::string::npos,
         "apply_patch rejects empty old_text before attempting a match");

  const auto outside_path = root / "outside.txt";
  {
    std::ofstream outside_file(outside_path, std::ios::binary | std::ios::trunc);
    outside_file << "dispatcher outside";
  }
  int dispatcher_prompts = 0;
  const ava::agent::ToolDispatcher resolving_dispatcher(ava::tools::ToolContext{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [&dispatcher_prompts](const ava::permissions::PermissionPrompt& prompt)
          -> ava::core::Result<ava::permissions::PermissionResolution> {
        ++dispatcher_prompts;
        expect(prompt.tool_name == "read_file", "dispatcher threads provider tool prompt metadata");
        return ava::permissions::PermissionResolution::Allow;
      }});
  auto outside_read = resolving_dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_outside_read",
      .name = "read_file",
      .arguments_json = "{\"path\":\"" + ava::core::json::escape(outside_path.generic_string()) + "\"}"});
  expect(outside_read && outside_read->success &&
             outside_read->result_text.find("dispatcher outside") != std::string::npos && dispatcher_prompts == 1,
         "tool dispatcher threads resolver into file tools");

  const auto outside_patch_path = root / "outside-patch.txt";
  {
    std::ofstream outside_patch_file(outside_patch_path, std::ios::binary | std::ios::trunc);
    outside_patch_file << "outside old";
  }
  std::vector<ava::permissions::Operation> apply_patch_prompts;
  const ava::agent::ToolDispatcher patch_resolving_dispatcher(ava::tools::ToolContext{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [&apply_patch_prompts](const ava::permissions::PermissionPrompt& prompt)
          -> ava::core::Result<ava::permissions::PermissionResolution> {
        apply_patch_prompts.push_back(prompt.operation);
        expect(prompt.tool_name == "apply_patch", "apply_patch resolver receives tool name");
        return ava::permissions::PermissionResolution::Allow;
      }});
  auto outside_patch = patch_resolving_dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_outside_patch",
      .name = "apply_patch",
      .arguments_json = "{\"edits\":[{\"path\":\"" + ava::core::json::escape(outside_patch_path.generic_string()) +
                        "\",\"old_text\":\"old\",\"new_text\":\"new\"},{\"path\":\"" +
                        ava::core::json::escape(outside_patch_path.generic_string()) +
                        "\",\"old_text\":\"outside\",\"new_text\":\"inside\"}]}"});
  auto outside_patch_read = ava::tools::read_file(
      ava::tools::ToolContext{.workspace_dir = root, .mode = ava::agent::Mode::Build}, outside_patch_path);
  expect(outside_patch && outside_patch->success && outside_patch_read && outside_patch_read->content == "inside new" &&
             apply_patch_prompts.size() == 2 && apply_patch_prompts[0] == ava::permissions::Operation::ReadFile &&
             apply_patch_prompts[1] == ava::permissions::Operation::EditFile,
         "apply_patch resolves external read permission before edit permission");

  const auto outside_no_resolver_path = root / "outside-patch-no-resolver.txt";
  {
    std::ofstream file(outside_no_resolver_path, std::ios::binary | std::ios::trunc);
    file << "keep old";
  }
  auto outside_patch_no_resolver = dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_outside_patch_no_resolver",
      .name = "apply_patch",
      .arguments_json = "{\"edits\":[{\"path\":\"" +
                        ava::core::json::escape(outside_no_resolver_path.generic_string()) +
                        "\",\"old_text\":\"old\",\"new_text\":\"new\"}]}"});
  auto outside_no_resolver_read = ava::tools::read_file(
      ava::tools::ToolContext{.workspace_dir = root, .mode = ava::agent::Mode::Build}, outside_no_resolver_path);
  expect(outside_patch_no_resolver && !outside_patch_no_resolver->success && outside_no_resolver_read &&
             outside_no_resolver_read->content == "keep old" &&
             outside_patch_no_resolver->result_text.find("no_resolver") != std::string::npos,
         "apply_patch fails closed without resolver and does not write external targets");

  const auto outside_denied_patch_path = root / "outside-patch-denied.txt";
  {
    std::ofstream file(outside_denied_patch_path, std::ios::binary | std::ios::trunc);
    file << "keep old";
  }
  int denied_patch_prompts = 0;
  const ava::agent::ToolDispatcher patch_denying_dispatcher(
      ava::tools::ToolContext{.workspace_dir = workspace,
                              .mode = ava::agent::Mode::Build,
                              .permission_resolver = [&denied_patch_prompts](
                                                         const ava::permissions::PermissionPrompt& prompt)
                                  -> ava::core::Result<ava::permissions::PermissionResolution> {
                                ++denied_patch_prompts;
                                expect(prompt.operation == ava::permissions::Operation::ReadFile,
                                       "apply_patch resolver sees read operation before denied external patch");
                                return ava::permissions::PermissionResolution::Deny;
                              }});
  auto outside_patch_denied = patch_denying_dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_outside_patch_denied",
      .name = "apply_patch",
      .arguments_json = "{\"edits\":[{\"path\":\"" +
                        ava::core::json::escape(outside_denied_patch_path.generic_string()) +
                        "\",\"old_text\":\"old\",\"new_text\":\"new\"}]}"});
  auto outside_denied_patch_read = ava::tools::read_file(
      ava::tools::ToolContext{.workspace_dir = root, .mode = ava::agent::Mode::Build}, outside_denied_patch_path);
  expect(outside_patch_denied && !outside_patch_denied->success && denied_patch_prompts == 1 &&
             outside_denied_patch_read && outside_denied_patch_read->content == "keep old" &&
             outside_patch_denied->result_text.find("resolution: deny") != std::string::npos,
         "apply_patch resolver read denial prevents all external writes");

  const auto outside_failed_patch_path = root / "outside-patch-failed.txt";
  {
    std::ofstream file(outside_failed_patch_path, std::ios::binary | std::ios::trunc);
    file << "keep old";
  }
  const ava::agent::ToolDispatcher patch_failing_dispatcher(ava::tools::ToolContext{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver =
          [](const ava::permissions::PermissionPrompt&) -> ava::core::Result<ava::permissions::PermissionResolution> {
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "resolver failed"));
      }});
  auto outside_patch_failed = patch_failing_dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_outside_patch_failed",
      .name = "apply_patch",
      .arguments_json = "{\"edits\":[{\"path\":\"" +
                        ava::core::json::escape(outside_failed_patch_path.generic_string()) +
                        "\",\"old_text\":\"old\",\"new_text\":\"new\"}]}"});
  auto outside_failed_patch_read = ava::tools::read_file(
      ava::tools::ToolContext{.workspace_dir = root, .mode = ava::agent::Mode::Build}, outside_failed_patch_path);
  expect(outside_patch_failed && !outside_patch_failed->success && outside_failed_patch_read &&
             outside_failed_patch_read->content == "keep old" &&
             outside_patch_failed->result_text.find("resolver_failed") != std::string::npos,
         "apply_patch resolver failure prevents all external writes");

  const auto partial_a = workspace / "partial-a.txt";
  const auto partial_b = workspace / "partial-b.txt";
  {
    std::ofstream a(partial_a, std::ios::binary | std::ios::trunc);
    a << "alpha old";
    std::ofstream b(partial_b, std::ios::binary | std::ios::trunc);
    b << "beta stays";
  }
  auto partial_patch = dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_partial_patch",
      .name = "apply_patch",
      .arguments_json = "{\"edits\":[{\"path\":\"partial-a.txt\",\"old_text\":\"old\",\"new_text\":\"new\"},"
                        "{\"path\":\"partial-b.txt\",\"old_text\":\"missing\",\"new_text\":\"new\"}]}"});
  auto partial_a_read = ava::tools::read_file(
      ava::tools::ToolContext{.workspace_dir = workspace, .mode = ava::agent::Mode::Build}, partial_a);
  auto partial_b_read = ava::tools::read_file(
      ava::tools::ToolContext{.workspace_dir = workspace, .mode = ava::agent::Mode::Build}, partial_b);
  expect(partial_patch && !partial_patch->success && partial_a_read && partial_b_read &&
             partial_a_read->content == "alpha old" && partial_b_read->content == "beta stays",
         "apply_patch validates all edits before writing so failures do not partially write");

  const auto staged_a = workspace / "staged-a.txt";
  {
    std::ofstream a(staged_a, std::ios::binary | std::ios::trunc);
    a << "stage alpha old";
  }
  const long name_max = pathconf(workspace.c_str(), _PC_NAME_MAX);
  if (name_max > 64 && name_max < 10000) {
    const std::string long_patch_name(static_cast<std::size_t>(name_max) - 4, 'l');
    const auto staged_long = workspace / (long_patch_name + ".txt");
    auto long_setup =
        ava::tools::write_file(ava::tools::ToolContext{.workspace_dir = workspace, .mode = ava::agent::Mode::Build},
                               staged_long, "stage beta old");
    if (long_setup) {
      auto staged_patch = dispatcher.dispatch(ava::agent::ProviderToolCall{
          .id = "call_staged_patch_failure",
          .name = "apply_patch",
          .arguments_json = "{\"edits\":[{\"path\":\"staged-a.txt\",\"old_text\":\"old\",\"new_text\":\"new\"},"
                            "{\"path\":\"" +
                            ava::core::json::escape(staged_long.filename().generic_string()) +
                            "\",\"old_text\":\"old\",\"new_text\":\"new\"}]}"});
      auto staged_a_read = ava::tools::read_file(
          ava::tools::ToolContext{.workspace_dir = workspace, .mode = ava::agent::Mode::Build}, staged_a);
      auto staged_long_read = ava::tools::read_file(
          ava::tools::ToolContext{.workspace_dir = workspace, .mode = ava::agent::Mode::Build}, staged_long);
      bool has_leftover_stage_temp = false;
      std::error_code iter_error;
      for (std::filesystem::directory_iterator it(workspace, iter_error), end; !iter_error && it != end;
           it.increment(iter_error)) {
        has_leftover_stage_temp =
            has_leftover_stage_temp || it->path().filename().string().find(".ava-patch-") != std::string::npos;
      }
      expect(
          staged_patch && !staged_patch->success && staged_a_read && staged_long_read &&
              staged_a_read->content == "stage alpha old" && staged_long_read->content == "stage beta old" &&
              !has_leftover_stage_temp && staged_patch->result_text.find("temporary_patch_write") != std::string::npos,
          "apply_patch stages all writes before commit and leaves originals unchanged when staging later files fails");
    }
  }

  const auto too_large_patch_path = workspace / "too-large-patch.txt";
  {
    std::ofstream large_patch_file(too_large_patch_path, std::ios::binary | std::ios::trunc);
    large_patch_file << "old" << std::string((10 * 1024 * 1024) + 1, 'x');
  }
  auto too_large_patch = dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_too_large_patch",
      .name = "apply_patch",
      .arguments_json = "{\"edits\":[{\"path\":\"too-large-patch.txt\",\"old_text\":\"old\",\"new_text\":\"new\"}]}"});
  std::ifstream too_large_patch_read(too_large_patch_path, std::ios::binary);
  std::string too_large_prefix(3, '\0');
  too_large_patch_read.read(too_large_prefix.data(), static_cast<std::streamsize>(too_large_prefix.size()));
  expect(too_large_patch && !too_large_patch->success &&
             too_large_patch->result_text.find("too large") != std::string::npos && too_large_prefix == "old",
         "apply_patch rejects files that exceed its full-read bound before writing");

  auto unavailable_question = dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_question_unavailable", .name = "question", .arguments_json = "{\"question\":\"Which approach?\"}"});
  expect(unavailable_question && !unavailable_question->success &&
             unavailable_question->result_text.find("unavailable") != std::string::npos,
         "question tool fails closed when no backend resolver is supplied");

  int question_prompts = 0;
  const ava::agent::ToolDispatcher question_dispatcher(ava::tools::ToolContext{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .question_resolver = [&question_prompts](const ava::agent::QuestionPrompt& prompt)
          -> ava::core::Result<ava::agent::QuestionAnswer> {
        ++question_prompts;
        expect(prompt.header == "Choose" && prompt.question == "Which approach?",
               "question resolver receives prompt text");
        expect(prompt.options.size() == 2 && prompt.options[0].value == "safe" && prompt.options[0].label == "Safe",
               "question resolver receives structured options");
        expect(!prompt.multiple && !prompt.allow_custom, "question resolver receives default selection flags");
        return ava::agent::QuestionAnswer{.selected_options = {"safe"}, .custom_text = ""};
      }});
  auto question = question_dispatcher.dispatch(
      ava::agent::ProviderToolCall{.id = "call_question",
                                   .name = "question",
                                   .arguments_json = "{\"header\":\"Choose\",\"question\":\"Which "
                                                     "approach?\",\"options\":[{\"value\":\"safe\",\"label\":\"Safe\"},"
                                                     "{\"value\":\"fast\",\"label\":\"Fast\"}]}"});
  expect(question && question->success && question_prompts == 1 &&
             question->result_text.find("\"selected_options\":[\"safe\"]") != std::string::npos,
         "question tool calls resolver and serializes selected answer");

  const ava::agent::ToolDispatcher multi_question_dispatcher(ava::tools::ToolContext{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .question_resolver =
          [](const ava::agent::QuestionPrompt& prompt) -> ava::core::Result<ava::agent::QuestionAnswer> {
        expect(prompt.multiple && prompt.allow_custom, "question resolver receives multi/custom flags");
        expect(prompt.options.size() == 2 && prompt.options[1].value == "Beta",
               "question resolver accepts string options");
        return ava::agent::QuestionAnswer{.selected_options = {"alpha", "Beta"}, .custom_text = "Use both"};
      }});
  auto multi_question = multi_question_dispatcher.dispatch(
      ava::agent::ProviderToolCall{.id = "call_question_multi",
                                   .name = "question",
                                   .arguments_json = "{\"question\":\"Pick "
                                                     "options\",\"options\":[{\"value\":\"alpha\",\"label\":\"Alpha\"},"
                                                     "\"Beta\"],\"multiple\":true,\"custom\":true}"});
  expect(multi_question && multi_question->success &&
             multi_question->result_text.find("\"selected_options\":[\"alpha\",\"Beta\"]") != std::string::npos &&
             multi_question->result_text.find("\"custom_text\":\"Use both\"") != std::string::npos,
         "question tool serializes multi-select and custom resolver answers");

  const ava::agent::ToolDispatcher too_many_answers_dispatcher(ava::tools::ToolContext{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .question_resolver = [](const ava::agent::QuestionPrompt&) -> ava::core::Result<ava::agent::QuestionAnswer> {
        return ava::agent::QuestionAnswer{.selected_options = std::vector<std::string>(65, "option"),
                                          .custom_text = ""};
      }});
  auto too_many_answers = too_many_answers_dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_question_too_many_answers", .name = "question", .arguments_json = "{\"question\":\"Pick\"}"});
  expect(too_many_answers && !too_many_answers->success &&
             too_many_answers->result_text.find("too many selected options") != std::string::npos,
         "question tool rejects resolver answers with too many selected options");

  const std::string oversized_answer_text(9000, 'x');
  const ava::agent::ToolDispatcher oversized_selected_dispatcher(ava::tools::ToolContext{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .question_resolver =
          [&oversized_answer_text](const ava::agent::QuestionPrompt&) -> ava::core::Result<ava::agent::QuestionAnswer> {
        return ava::agent::QuestionAnswer{.selected_options = {oversized_answer_text}, .custom_text = ""};
      }});
  auto oversized_selected = oversized_selected_dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_question_oversized_selected", .name = "question", .arguments_json = "{\"question\":\"Pick\"}"});
  expect(oversized_selected && !oversized_selected->success &&
             oversized_selected->result_text.find("selected option is too long") != std::string::npos,
         "question tool rejects oversized resolver selected option strings");

  const ava::agent::ToolDispatcher oversized_custom_dispatcher(ava::tools::ToolContext{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .question_resolver =
          [&oversized_answer_text](const ava::agent::QuestionPrompt&) -> ava::core::Result<ava::agent::QuestionAnswer> {
        return ava::agent::QuestionAnswer{.selected_options = {}, .custom_text = oversized_answer_text};
      }});
  auto oversized_custom = oversized_custom_dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_question_oversized_custom", .name = "question", .arguments_json = "{\"question\":\"Pick\"}"});
  expect(oversized_custom && !oversized_custom->success &&
             oversized_custom->result_text.find("custom text is too long") != std::string::npos,
         "question tool rejects oversized resolver custom text");

  const ava::agent::ToolDispatcher failing_question_dispatcher(ava::tools::ToolContext{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .question_resolver = [](const ava::agent::QuestionPrompt&) -> ava::core::Result<ava::agent::QuestionAnswer> {
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "question UI unavailable"));
      }});
  auto failed_question = failing_question_dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_question_failed", .name = "question", .arguments_json = "{\"question\":\"Continue?\"}"});
  expect(failed_question && !failed_question->success &&
             failed_question->result_text.find("question UI unavailable") != std::string::npos,
         "question tool returns resolver errors as backend failures");

  auto nul_question = dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_nul_question", .name = "question", .arguments_json = "{\"question\":\"bad\\u0000question\"}"});
  expect(nul_question && !nul_question->success && nul_question->result_text.find("control byte") != std::string::npos,
         "question tool rejects NUL bytes in question text as control bytes");

  auto control_header = dispatcher.dispatch(
      ava::agent::ProviderToolCall{.id = "call_control_header",
                                   .name = "question",
                                   .arguments_json = "{\"header\":\"bad\\u001B\",\"question\":\"Ok?\"}"});
  expect(control_header && !control_header->success &&
             control_header->result_text.find("control byte") != std::string::npos,
         "question tool rejects control bytes in header text");

  auto control_option_value = dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_control_option_value",
      .name = "question",
      .arguments_json = "{\"question\":\"Pick\",\"options\":[{\"value\":\"bad\\u001F\",\"label\":\"Bad\"}]}"});
  expect(control_option_value && !control_option_value->success &&
             control_option_value->result_text.find("control byte") != std::string::npos,
         "question tool rejects control bytes in option values");

  auto control_option_label = dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_control_option_label",
      .name = "question",
      .arguments_json = "{\"question\":\"Pick\",\"options\":[{\"value\":\"bad\",\"label\":\"Bad\\u007F\"}]}"});
  expect(control_option_label && !control_option_label->success &&
             control_option_label->result_text.find("control byte") != std::string::npos,
         "question tool rejects control bytes in option labels");

  auto trailing_comma_question = dispatcher.dispatch(
      ava::agent::ProviderToolCall{.id = "call_question_trailing_comma",
                                   .name = "question",
                                   .arguments_json = "{\"question\":\"Pick\",\"options\":[\"A\",]}"});
  expect(trailing_comma_question && !trailing_comma_question->success &&
             trailing_comma_question->result_text.find("options array is malformed") != std::string::npos,
         "question tool rejects trailing commas in options arrays");

  auto malformed_question = question_dispatcher.dispatch(
      ava::agent::ProviderToolCall{.id = "call_question_malformed",
                                   .name = "question",
                                   .arguments_json = "{\"question\":\"Bad options\",\"options\":\"not-an-array\"}"});
  expect(malformed_question && !malformed_question->success &&
             malformed_question->result_text.find("options must be an array") != std::string::npos,
         "question tool rejects malformed option arguments before resolver dispatch");

  auto unknown = dispatcher.dispatch(
      ava::agent::ProviderToolCall{.id = "call_unknown", .name = "missing_tool", .arguments_json = "{}"});
  expect(unknown && !unknown->success && unknown->result_text.find("unknown tool") != std::string::npos,
         "tool dispatcher returns structured unknown tool errors");

  const auto schemas = ava::agent::ToolDispatcher::tool_schemas_json();
  bool has_apply_patch = false;
  bool has_question = false;
  bool question_has_allow_multiple = false;
  for (const auto& schema : schemas) {
    has_apply_patch = has_apply_patch || schema.find("apply_patch") != std::string::npos;
    const bool is_question_schema = schema.find("\"name\":\"question\"") != std::string::npos;
    has_question = has_question || is_question_schema;
    question_has_allow_multiple =
        question_has_allow_multiple || (is_question_schema && schema.find("allow_multiple") != std::string::npos);
  }
  expect(!schemas.empty() && schemas[0].find("read_file") != std::string::npos && has_apply_patch && has_question,
         "tool dispatcher exposes provider tool schemas");
  expect(question_has_allow_multiple, "question tool schema exposes the allow_multiple alias");
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

class CallbackTransport final : public ava::provider::Transport {
 public:
  CallbackTransport(std::vector<ava::provider::HttpResponse> responses, std::function<void()> after_send)
      : responses_(std::move(responses)), after_send_(std::move(after_send)) {}

  [[nodiscard]] ava::core::Result<ava::provider::HttpResponse> send(
      const ava::provider::HttpRequest& request) override {
    requests_.push_back(request);
    if (responses_.empty()) {
      return std::unexpected(
          ava::core::Error(ava::core::ErrorCategory::Provider, "callback transport has no response"));
    }
    auto response = responses_.front();
    responses_.erase(responses_.begin());
    if (after_send_) after_send_();
    return response;
  }

  [[nodiscard]] const std::vector<ava::provider::HttpRequest>& requests() const noexcept { return requests_; }

 private:
  std::vector<ava::provider::HttpResponse> responses_;
  std::function<void()> after_send_;
  std::vector<ava::provider::HttpRequest> requests_;
};

void test_agent_loop_text_only_turn() {
  const auto root = temp_root() / "agent-text";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  ava::session::SessionStore store(ava::session::SessionStoreOptions{
      .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "text"});
  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"hello user\"}\n\n"
                    "data: [DONE]\n\n")});
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                          .mode = ava::agent::Mode::Build,
                                                          .provider_id = "openai",
                                                          .model_id = "gpt-5.5",
                                                          .system_prompt = "system prompt",
                                                          .access_token = "token",
                                                          .openai_oauth = true,
                                                          .openai_account_id = "acct_123"});
  auto result = loop.run_turn("hi", store, provider, transport);
  expect(result && result->final_text == "hello user" && result->tool_calls == 0 &&
             result->initial_context_messages == 1 && !result->used_compacted_context && result->tool_iterations == 0 &&
             result->stop_reason == "completed",
         "agent loop returns text-only provider response with status metadata");
  expect(transport.requests().size() == 1 && transport.requests()[0].body.find("read_file") != std::string::npos,
         "agent loop includes tool schemas in provider request");
  expect(transport.requests().size() == 1 &&
             transport.requests()[0].url == "https://chatgpt.com/backend-api/codex/responses" &&
             transport.requests()[0].headers.at("ChatGPT-Account-Id") == "acct_123",
         "agent loop routes OpenAI OAuth turns through Codex endpoint");
  expect(transport.requests().size() == 1 && transport.requests()[0].body.find("\"store\":false") != std::string::npos,
         "agent loop disables Codex response storage for OpenAI OAuth turns");
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
  ava::tests::FakeTransport transport(
      {sse_response(
           "data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_1\",\"name\":\"read_file\"}\n\n"
           "data: "
           "{\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_1\",\"delta\":\"{\\\"path\\\":"
           "\\\"note.txt\\\"}\"}\n\n"
           "data: {\"type\":\"response.function_call.done\",\"item_id\":\"call_1\"}\n\n"
           "data: [DONE]\n\n"),
       sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"read it\"}\n\n"
                    "data: [DONE]\n\n")});
  std::vector<ava::agent::ToolTimelineEntry> tool_events;
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .provider_id = "openai",
      .model_id = "gpt-5.5",
      .system_prompt = "system prompt",
      .access_token = "token",
      .on_tool_event = [&tool_events](const auto& entry) { tool_events.push_back(entry); }});
  auto result = loop.run_turn("read note", store, provider, transport);
  expect(result && result->final_text == "read it" && result->tool_calls == 1 && result->provider_iterations == 2 &&
             result->initial_context_messages == 1 && result->tool_iterations == 1 &&
             result->stop_reason == "completed",
         "agent loop runs one sequential tool call then continues to final answer with status metadata");
  expect(transport.requests().size() == 2 && transport.requests()[1].body.find("tool content") != std::string::npos,
         "agent loop sends persisted tool result as continuation context");
  expect(result && result->tool_timeline.size() == 1 &&
             result->tool_timeline.front().status == ava::agent::ToolTimelineStatus::Success &&
             result->tool_timeline.front().name == "read_file" &&
             result->tool_timeline.front().argument_summary.find("path=note.txt") != std::string::npos &&
             result->tool_timeline.front().argument_summary.find('{') == std::string::npos &&
             result->tool_timeline.front().result_summary.find("tool content") == std::string::npos &&
             result->tool_timeline.front().result_summary.find("bytes") != std::string::npos,
         "agent loop returns safe compact tool timeline summaries");
  expect(tool_events.size() == 2 && tool_events.front().status == ava::agent::ToolTimelineStatus::Running &&
             tool_events.back().status == ava::agent::ToolTimelineStatus::Success,
         "agent loop publishes running and completed tool timeline events");

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

void test_agent_loop_permission_resolver_threads_to_tools() {
  const auto root = temp_root() / "agent-permission-resolver";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  const auto outside_path = root / "outside.txt";
  {
    std::ofstream file(outside_path, std::ios::binary | std::ios::trunc);
    file << "outside via agent";
  }
  ava::session::SessionStore store(ava::session::SessionStoreOptions{
      .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "resolver"});
  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {sse_response(
           "data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_outside\",\"name\":\"read_file\"}\n\n"
           "data: "
           "{\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_outside\",\"delta\":\"{"
           "\\\"path\\\":\\\"" +
           ava::core::json::escape(outside_path.generic_string()) +
           "\\\"}\"}\n\n"
           "data: [DONE]\n\n"),
       sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"used resolver\"}\n\n"
                    "data: [DONE]\n\n")});
  int prompts = 0;
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .provider_id = "openai",
      .model_id = "gpt-5.5",
      .system_prompt = "system prompt",
      .access_token = "token",
      .permission_resolver = [&prompts, &outside_path](const ava::permissions::PermissionPrompt& prompt)
          -> ava::core::Result<ava::permissions::PermissionResolution> {
        ++prompts;
        expect(prompt.target_path == outside_path, "agent loop resolver sees tool target path");
        return ava::permissions::PermissionResolution::Allow;
      }});
  auto result = loop.run_turn("read outside", store, provider, transport);
  expect(result && result->final_text == "used resolver" && prompts == 1 && result->tool_timeline.size() == 1 &&
             result->tool_timeline.front().status == ava::agent::ToolTimelineStatus::Success,
         "agent loop threads permission resolver into tool dispatcher");
  expect(
      transport.requests().size() == 2 && transport.requests()[1].body.find("outside via agent") != std::string::npos,
      "agent loop continuation includes resolver-approved tool result");
  auto resolver_entries = store.load();
  auto resolver_audits =
      resolver_entries ? permission_entries(*resolver_entries) : std::vector<ava::session::SessionEntry>{};
  expect(resolver_audits.size() == 2 &&
             ava::core::json::string_field(resolver_audits[0].data_json, "action") == "ask" &&
             ava::core::json::string_field(resolver_audits[0].data_json, "resolution_source") == "policy" &&
             ava::core::json::string_field(resolver_audits[1].data_json, "resolution") == "allow" &&
             ava::core::json::string_field(resolver_audits[1].data_json, "resolution_source") == "resolver",
         "agent loop persists ask and resolver permission audit entries");

  {
    const auto bash_root = temp_root() / "agent-bash-ask-allow";
    std::filesystem::remove_all(bash_root, remove_error);
    const auto bash_workspace = bash_root / "workspace";
    std::filesystem::create_directories(bash_workspace);
    ava::session::SessionStore bash_store(ava::session::SessionStoreOptions{
        .root_dir = bash_root / "sessions", .workspace_dir = bash_workspace, .session_id = "bash-allow"});
    ava::tests::FakeTransport bash_transport(
        {sse_response(
             "data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_bash\",\"name\":\"bash\"}\n\n"
             "data: "
             "{\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_bash\",\"delta\":\"{"
             "\\\"command\\\":\\\"true\\\"}\"}\n\n"
             "data: [DONE]\n\n"),
         sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"bash allowed\"}\n\n"
                      "data: [DONE]\n\n")});
    int bash_allow_prompts = 0;
    ava::agent::AgentLoop bash_loop(ava::agent::AgentLoopOptions{
        .workspace_dir = bash_workspace,
        .mode = ava::agent::Mode::Build,
        .provider_id = "openai",
        .model_id = "gpt-5.5",
        .system_prompt = "system prompt",
        .access_token = "token",
        .permission_resolver = [&bash_allow_prompts](const ava::permissions::PermissionPrompt& prompt)
            -> ava::core::Result<ava::permissions::PermissionResolution> {
          ++bash_allow_prompts;
          expect(prompt.operation == ava::permissions::Operation::RunCommand,
                 "agent bash allow resolver sees run command");
          expect(prompt.command == "true", "agent bash allow resolver sees command text");
          return ava::permissions::PermissionResolution::Allow;
        }});
    auto bash_result = bash_loop.run_turn("run true", bash_store, provider, bash_transport);
    expect(bash_result && bash_result->final_text == "bash allowed" && bash_allow_prompts == 1 &&
               bash_result->tool_timeline.size() == 1 &&
               bash_result->tool_timeline.front().status == ava::agent::ToolTimelineStatus::Success,
           "agent loop allows bash Ask decisions when resolver allows once");
  }

  {
    const auto bash_root = temp_root() / "agent-bash-ask-deny";
    std::filesystem::remove_all(bash_root, remove_error);
    const auto bash_workspace = bash_root / "workspace";
    std::filesystem::create_directories(bash_workspace);
    ava::session::SessionStore bash_store(ava::session::SessionStoreOptions{
        .root_dir = bash_root / "sessions", .workspace_dir = bash_workspace, .session_id = "bash-deny"});
    ava::tests::FakeTransport bash_transport(
        {sse_response(
             "data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_bash\",\"name\":\"bash\"}\n\n"
             "data: "
             "{\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_bash\",\"delta\":\"{"
             "\\\"command\\\":\\\"true\\\"}\"}\n\n"
             "data: [DONE]\n\n"),
         sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"bash denied\"}\n\n"
                      "data: [DONE]\n\n")});
    int bash_deny_prompts = 0;
    ava::agent::AgentLoop bash_loop(ava::agent::AgentLoopOptions{
        .workspace_dir = bash_workspace,
        .mode = ava::agent::Mode::Build,
        .provider_id = "openai",
        .model_id = "gpt-5.5",
        .system_prompt = "system prompt",
        .access_token = "token",
        .permission_resolver = [&bash_deny_prompts](const ava::permissions::PermissionPrompt& prompt)
            -> ava::core::Result<ava::permissions::PermissionResolution> {
          ++bash_deny_prompts;
          expect(prompt.operation == ava::permissions::Operation::RunCommand,
                 "agent bash deny resolver sees run command");
          return ava::permissions::PermissionResolution::Deny;
        }});
    auto bash_result = bash_loop.run_turn("run true", bash_store, provider, bash_transport);
    expect(bash_result && bash_result->final_text == "bash denied" && bash_deny_prompts == 1 &&
               bash_result->tool_timeline.size() == 1 &&
               bash_result->tool_timeline.front().status == ava::agent::ToolTimelineStatus::Error,
           "agent loop records denied bash Ask decisions as failed tool results and continues");
    auto bash_entries = bash_store.load();
    auto bash_audits = bash_entries ? permission_entries(*bash_entries) : std::vector<ava::session::SessionEntry>{};
    expect(bash_audits.size() == 2 && ava::core::json::string_field(bash_audits[1].data_json, "command") == "true" &&
               ava::core::json::string_field(bash_audits[1].data_json, "resolution") == "deny" &&
               ava::core::json::string_field(bash_audits[1].data_json, "resolution_source") == "resolver",
           "agent loop persists resolver-denied command permission audit entries");
  }

  {
    const auto bash_root = temp_root() / "agent-bash-ask-fail";
    std::filesystem::remove_all(bash_root, remove_error);
    const auto bash_workspace = bash_root / "workspace";
    std::filesystem::create_directories(bash_workspace);
    ava::session::SessionStore bash_store(ava::session::SessionStoreOptions{
        .root_dir = bash_root / "sessions", .workspace_dir = bash_workspace, .session_id = "bash-fail"});
    ava::tests::FakeTransport bash_transport(
        {sse_response(
             "data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_bash\",\"name\":\"bash\"}\n\n"
             "data: "
             "{\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_bash\",\"delta\":\"{"
             "\\\"command\\\":\\\"true\\\"}\"}\n\n"
             "data: [DONE]\n\n"),
         sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"bash resolver failed\"}\n\n"
                      "data: [DONE]\n\n")});
    int bash_fail_prompts = 0;
    ava::agent::AgentLoop bash_loop(ava::agent::AgentLoopOptions{
        .workspace_dir = bash_workspace,
        .mode = ava::agent::Mode::Build,
        .provider_id = "openai",
        .model_id = "gpt-5.5",
        .system_prompt = "system prompt",
        .access_token = "token",
        .permission_resolver = [&bash_fail_prompts](const ava::permissions::PermissionPrompt& prompt)
            -> ava::core::Result<ava::permissions::PermissionResolution> {
          ++bash_fail_prompts;
          expect(prompt.operation == ava::permissions::Operation::RunCommand,
                 "agent bash fail resolver sees run command");
          return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "resolver failed"));
        }});
    auto bash_result = bash_loop.run_turn("run true", bash_store, provider, bash_transport);
    expect(bash_result && bash_result->final_text == "bash resolver failed" && bash_fail_prompts == 1 &&
               bash_result->tool_timeline.size() == 1 &&
               bash_result->tool_timeline.front().status == ava::agent::ToolTimelineStatus::Error &&
               bash_transport.requests().size() == 2,
           "agent loop records failed bash Ask resolver as failed tool result and continues");
  }
}

void test_agent_loop_question_resolver_threads_to_tools() {
  const auto root = temp_root() / "agent-question-resolver";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  ava::session::SessionStore store(ava::session::SessionStoreOptions{
      .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "question-resolver"});
  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {sse_response(
           "data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_question\",\"name\":\"question\"}\n\n"
           "data: "
           "{\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_question\",\"delta\":\"{"
           "\\\"question\\\":\\\"Pick one?\\\",\\\"options\\\":[\\\"A\\\",\\\"B\\\"]}\"}\n\n"
           "data: [DONE]\n\n"),
       sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"question answered\"}\n\n"
                    "data: [DONE]\n\n")});
  int prompts = 0;
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .provider_id = "openai",
      .model_id = "gpt-5.5",
      .system_prompt = "system prompt",
      .access_token = "token",
      .question_resolver =
          [&prompts](const ava::agent::QuestionPrompt& prompt) -> ava::core::Result<ava::agent::QuestionAnswer> {
        ++prompts;
        expect(prompt.question == "Pick one?" && prompt.options.size() == 2,
               "agent loop question resolver receives provider prompt");
        return ava::agent::QuestionAnswer{.selected_options = {"B"}, .custom_text = ""};
      }});
  auto result = loop.run_turn("ask", store, provider, transport);
  expect(result && result->final_text == "question answered" && prompts == 1 && result->tool_timeline.size() == 1 &&
             result->tool_timeline.front().status == ava::agent::ToolTimelineStatus::Success,
         "agent loop threads question resolver into tool dispatcher");
  expect(transport.requests().size() == 2 &&
             transport.requests()[1].body.find("\\\"selected_options\\\":[\\\"B\\\"]") != std::string::npos,
         "agent loop continuation includes serialized question answer");
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
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{
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

void test_agent_loop_compaction_status_metadata() {
  const auto root = temp_root() / "agent-compaction-status";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  ava::session::SessionStore store(ava::session::SessionStoreOptions{
      .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "compaction-status"});
  auto appended = store.append(ava::session::SessionEntry{.id = "entry_compaction_status",
                                                          .parent_id = "",
                                                          .type = ava::session::EntryType::Compaction,
                                                          .timestamp = ava::session::now_timestamp(),
                                                          .data_json = "{\"summary\":\"older context\"}"});
  expect(appended.has_value(), "agent loop compaction metadata test seeds compaction entry");
  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"after compaction\"}\n\n"
                    "data: [DONE]\n\n")});
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                          .mode = ava::agent::Mode::Build,
                                                          .provider_id = "openai",
                                                          .model_id = "gpt-5.5",
                                                          .system_prompt = "system prompt",
                                                          .access_token = "token"});
  auto result = loop.run_turn("continue", store, provider, transport);
  expect(result && result->used_compacted_context && result->initial_context_messages == 2 &&
             result->stop_reason == "completed",
         "agent loop status metadata reports compacted initial provider context");
  expect(transport.requests().size() == 1 &&
             transport.requests()[0].body.find("Compacted prior conversation summary") != std::string::npos,
         "agent loop sends compacted context in initial provider request");
}

void test_agent_loop_cancellation_boundaries() {
  const ava::provider::OpenAIProvider provider("https://api.example.test");

  {
    const auto root = temp_root() / "agent-cancel-before-turn-start";
    std::error_code remove_error;
    std::filesystem::remove_all(root, remove_error);
    const auto workspace = root / "workspace";
    std::filesystem::create_directories(workspace);
    ava::session::SessionStore store(ava::session::SessionStoreOptions{
        .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "cancel-before-turn-start"});
    ava::tests::FakeTransport transport(
        {sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"should not send\"}\n\n"
                      "data: [DONE]\n\n")});
    ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                            .mode = ava::agent::Mode::Build,
                                                            .provider_id = "openai",
                                                            .model_id = "gpt-5.5",
                                                            .system_prompt = "system prompt",
                                                            .access_token = "token",
                                                            .cancel_requested = [] { return true; }});
    auto result = loop.run_turn("cancel now", store, provider, transport);
    auto entries = store.load();
    bool saw_user_message = false;
    bool saw_cancel = false;
    if (entries) {
      for (const auto& entry : *entries) {
        saw_user_message = saw_user_message || entry.type == ava::session::EntryType::UserMessage;
        saw_cancel = saw_cancel || (entry.type == ava::session::EntryType::Cancel &&
                                    entry.data_json.find("before_turn_start") != std::string::npos);
      }
    }
    expect(!result && result.error().message() == "agent loop canceled" && transport.requests().empty() && entries &&
               saw_cancel && !saw_user_message,
           "agent loop cancellation before turn start avoids persisting the user message");
  }

  {
    const auto root = temp_root() / "agent-cancel-before-provider";
    std::error_code remove_error;
    std::filesystem::remove_all(root, remove_error);
    const auto workspace = root / "workspace";
    std::filesystem::create_directories(workspace);
    ava::session::SessionStore store(ava::session::SessionStoreOptions{
        .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "cancel-before-provider"});
    ava::tests::FakeTransport transport(
        {sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"should not send\"}\n\n"
                      "data: [DONE]\n\n")});
    int cancel_checks = 0;
    ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                            .mode = ava::agent::Mode::Build,
                                                            .provider_id = "openai",
                                                            .model_id = "gpt-5.5",
                                                            .system_prompt = "system prompt",
                                                            .access_token = "token",
                                                            .cancel_requested = [&cancel_checks] {
                                                              ++cancel_checks;
                                                              return cancel_checks >= 2;
                                                            }});
    auto result = loop.run_turn("cancel before provider", store, provider, transport);
    auto entries = store.load();
    const bool saw_cancel = entries && std::ranges::any_of(*entries, [](const ava::session::SessionEntry& entry) {
                              return entry.type == ava::session::EntryType::Cancel &&
                                     entry.data_json.find("before_provider_call") != std::string::npos;
                            });
    const bool saw_user_message = entries && std::ranges::any_of(*entries, [](const ava::session::SessionEntry& entry) {
                                    return entry.type == ava::session::EntryType::UserMessage;
                                  });
    expect(!result && result.error().message() == "agent loop canceled" && transport.requests().empty() && saw_cancel &&
               saw_user_message,
           "agent loop cancellation before provider call avoids transport send and records cancel boundary");
  }

  {
    const auto root = temp_root() / "agent-cancel-before-tool";
    std::error_code remove_error;
    std::filesystem::remove_all(root, remove_error);
    const auto workspace = root / "workspace";
    std::filesystem::create_directories(workspace);
    {
      std::ofstream file(workspace / "note.txt", std::ios::binary | std::ios::trunc);
      file << "must not read";
    }
    ava::session::SessionStore store(ava::session::SessionStoreOptions{
        .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "cancel-before-tool"});
    bool cancel = false;
    CallbackTransport transport(
        {sse_response(
            "data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_read\",\"name\":\"read_file\"}\n\n"
            "data: "
            "{\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_read\",\"delta\":\"{\\\"path\\\":"
            "\\\"note.txt\\\"}\"}\n\n"
            "data: [DONE]\n\n")},
        [&cancel] { cancel = true; });
    ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                            .mode = ava::agent::Mode::Build,
                                                            .provider_id = "openai",
                                                            .model_id = "gpt-5.5",
                                                            .system_prompt = "system prompt",
                                                            .access_token = "token",
                                                            .cancel_requested = [&cancel] { return cancel; }});
    auto result = loop.run_turn("read then cancel", store, provider, transport);
    auto entries = store.load();
    bool saw_tool_entry = false;
    bool saw_cancel = false;
    if (entries) {
      for (const auto& entry : *entries) {
        saw_tool_entry = saw_tool_entry || entry.type == ava::session::EntryType::ToolCall ||
                         entry.type == ava::session::EntryType::ToolResult;
        saw_cancel = saw_cancel || (entry.type == ava::session::EntryType::Cancel &&
                                    entry.data_json.find("before_tool_dispatch") != std::string::npos);
      }
    }
    expect(!result && result.error().message() == "agent loop canceled" && transport.requests().size() == 1 &&
               entries && saw_cancel && !saw_tool_entry,
           "agent loop cancellation before tool dispatch avoids tool call/result entries");
  }
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
    ava::tests::FakeTransport transport(
        {sse_response("data: {\"type\":\"response.error\",\"error\":{\"message\":\"bad request\"}}\n\n")});
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
    ava::tests::FakeTransport transport({});
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
    ava::tests::FakeTransport transport({sse_response("")});
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
    ava::tests::FakeTransport transport(
        {sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"a\"}\n\n"
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
    ava::tests::FakeTransport transport(
        {sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"hello\"}\n\n"
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
    ava::tests::FakeTransport transport({sse_response(
        "data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_1\",\"name\":\"read_file\"}\n\n"
        "data: "
        "{\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_1\",\"delta\":\"{\\\"path\\\":"
        "\\\"note.txt\\\"}\"}\n\n"
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

  {
    const auto root = temp_root() / "agent-control-call-id";
    std::error_code remove_error;
    std::filesystem::remove_all(root, remove_error);
    const auto workspace = root / "workspace";
    std::filesystem::create_directories(workspace);
    ava::session::SessionStore store(ava::session::SessionStoreOptions{
        .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "control-call-id"});
    ava::tests::FakeTransport transport({sse_response(
        "data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_\\u0001bad\",\"name\":\"read_file\"}\n\n"
        "data: [DONE]\n\n")});
    ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                            .mode = ava::agent::Mode::Build,
                                                            .provider_id = "openai",
                                                            .model_id = "gpt-5.5",
                                                            .system_prompt = "system prompt",
                                                            .access_token = "token"});
    auto result = loop.run_turn("bad id", store, provider, transport);
    auto entries = store.load();
    bool saw_tool_entry = false;
    if (entries) {
      saw_tool_entry = std::ranges::any_of(*entries, [](const ava::session::SessionEntry& entry) {
        return entry.type == ava::session::EntryType::ToolCall || entry.type == ava::session::EntryType::ToolResult;
      });
    }
    expect(!result && result.error().message().find("control byte") != std::string::npos && entries && !saw_tool_entry,
           "agent loop rejects provider tool call ids with control bytes before session or timeline use");
  }

  {
    const auto root = temp_root() / "agent-long-call-id";
    std::error_code remove_error;
    std::filesystem::remove_all(root, remove_error);
    const auto workspace = root / "workspace";
    std::filesystem::create_directories(workspace);
    ava::session::SessionStore store(ava::session::SessionStoreOptions{
        .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "long-call-id"});
    const std::string long_call_id(300, 'a');
    ava::tests::FakeTransport transport(
        {sse_response("data: {\"type\":\"response.function_call.added\",\"item_id\":\"" + long_call_id +
                      "\",\"name\":\"read_file\"}\n\n"
                      "data: [DONE]\n\n")});
    ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                            .mode = ava::agent::Mode::Build,
                                                            .provider_id = "openai",
                                                            .model_id = "gpt-5.5",
                                                            .system_prompt = "system prompt",
                                                            .access_token = "token"});
    auto result = loop.run_turn("long id", store, provider, transport);
    expect(!result && result.error().message().find("too long") != std::string::npos,
           "agent loop rejects overlong provider tool call ids");
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
  ava::tests::FakeTransport transport(
      {sse_response(
           "data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_1\",\"name\":\"read_file\"}\n\n"
           "data: "
           "{\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_1\",\"delta\":\"{\\\"path\\\":"
           "\\\"one.txt\\\"}\"}\n\n"
           "data: {\"type\":\"response.function_call.done\",\"item_id\":\"call_1\"}\n\n"
           "data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_2\",\"name\":\"read_file\"}\n\n"
           "data: "
           "{\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_2\",\"delta\":\"{\\\"path\\\":"
           "\\\"two.txt\\\"}\"}\n\n"
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
  ava::tests::FakeTransport denied_transport(
      {sse_response(
           "data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_write\",\"name\":\"write_file\"}\n\n"
           "data: "
           "{\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_write\",\"delta\":\"{\\\"path\\\":"
           "\\\"src/new.cpp\\\",\\\"content\\\":\\\"bad\\\"}\"}\n\n"
           "data: [DONE]\n\n"),
       sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"permission explained\"}\n\n"
                    "data: [DONE]\n\n")});
  ava::agent::AgentLoop denied_loop(ava::agent::AgentLoopOptions{.workspace_dir = denied_workspace,
                                                                 .mode = ava::agent::Mode::Plan,
                                                                 .provider_id = "openai",
                                                                 .model_id = "gpt-5.5",
                                                                 .system_prompt = "system prompt",
                                                                 .access_token = "token",
                                                                 .openai_oauth = true,
                                                                 .openai_account_id = "acct_123"});
  auto denied_result = denied_loop.run_turn("write source", denied_store, provider, denied_transport);
  expect(
      denied_result && denied_result->final_text == "permission explained" && denied_result->provider_iterations == 2,
      "agent loop continues after permission-denied tool results");
  expect(denied_result && denied_result->tool_timeline.size() == 1 &&
             denied_result->tool_timeline.front().status == ava::agent::ToolTimelineStatus::Error &&
             denied_result->tool_timeline.front().argument_summary.find("content=3 bytes") != std::string::npos &&
             denied_result->tool_timeline.front().argument_summary.find("bad") == std::string::npos &&
             denied_result->tool_timeline.front().result_summary.find("error:") == 0,
         "agent loop marks denied tool results as safe error timeline entries");
  expect(denied_transport.requests().size() == 2 &&
             denied_transport.requests()[1].body.find("permission_denied") != std::string::npos,
         "permission-denied tool result is framed into continuation context");
}

void test_agent_loop_tool_delta_dedupes_and_rejects_empty_tool_ids() {
  const ava::provider::OpenAIProvider provider("https://api.example.test");

  {
    const auto root = temp_root() / "agent-delta-before-start";
    std::error_code remove_error;
    std::filesystem::remove_all(root, remove_error);
    const auto workspace = root / "workspace";
    std::filesystem::create_directories(workspace);
    {
      std::ofstream file(workspace / "note.txt", std::ios::binary | std::ios::trunc);
      file << "dedupe content";
    }
    ava::session::SessionStore store(ava::session::SessionStoreOptions{
        .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "delta-before-start"});
    ava::tests::FakeTransport transport(
        {sse_response(
             "data: "
             "{\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_1\",\"delta\":\"{\\\"path\\\":"
             "\\\"note.txt\\\"}\"}\n\n"
             "data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_1\",\"name\":\"read_file\"}\n\n"
             "data: {\"type\":\"response.function_call.done\",\"item_id\":\"call_1\"}\n\n"
             "data: [DONE]\n\n"),
         sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"done\"}\n\n"
                      "data: [DONE]\n\n")});
    ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                            .mode = ava::agent::Mode::Build,
                                                            .provider_id = "openai",
                                                            .model_id = "gpt-5.5",
                                                            .system_prompt = "system prompt",
                                                            .access_token = "token"});
    auto result = loop.run_turn("read note", store, provider, transport);
    expect(result && result->tool_calls == 1 && result->tool_timeline.size() == 1 &&
               result->tool_timeline.front().status == ava::agent::ToolTimelineStatus::Success &&
               result->tool_timeline.front().name == "read_file",
           "agent loop deduplicates tool deltas that arrive before tool start events");

    auto entries = store.load();
    std::size_t tool_calls = 0;
    std::size_t tool_results = 0;
    if (entries) {
      for (const auto& entry : *entries) {
        if (entry.type == ava::session::EntryType::ToolCall) ++tool_calls;
        if (entry.type == ava::session::EntryType::ToolResult) ++tool_results;
      }
    }
    expect(entries && tool_calls == 1 && tool_results == 1, "deduped streamed tool call has one paired result");
  }

  {
    const auto root = temp_root() / "agent-empty-call-id";
    std::error_code remove_error;
    std::filesystem::remove_all(root, remove_error);
    const auto workspace = root / "workspace";
    std::filesystem::create_directories(workspace);
    ava::session::SessionStore store(ava::session::SessionStoreOptions{
        .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "empty-call-id"});
    ava::tests::FakeTransport transport(
        {sse_response("data: {\"type\":\"response.function_call.added\",\"item_id\":\"\",\"name\":\"read_file\"}\n\n"
                      "data: [DONE]\n\n")});
    ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                            .mode = ava::agent::Mode::Build,
                                                            .provider_id = "openai",
                                                            .model_id = "gpt-5.5",
                                                            .system_prompt = "system prompt",
                                                            .access_token = "token"});
    auto result = loop.run_turn("read missing-id", store, provider, transport);
    auto entries = store.load();
    bool saw_tool_entry = false;
    if (entries) {
      for (const auto& entry : *entries) {
        saw_tool_entry = saw_tool_entry || entry.type == ava::session::EntryType::ToolCall ||
                         entry.type == ava::session::EntryType::ToolResult;
      }
    }
    expect(!result && result.error().message().find("empty") != std::string::npos && entries && !saw_tool_entry,
           "agent loop rejects empty provider tool call ids before session or timeline use");
  }
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
  ava::tests::FakeTransport transport(
      {sse_response(
           "data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_large\",\"name\":\"read_file\"}\n\n"
           "data: "
           "{\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_large\",\"delta\":\"{\\\"path\\\":"
           "\\\"large.txt\\\"}\"}\n\n"
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
  ava::tests::FakeTransport transport({sse_response(tool_sse), sse_response(tool_sse)});
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
  auto prompt_input = ava::tui::handle_permission_prompt_input(ava::tui::PermissionPromptChoice::Deny,
                                                               ava::tui::InputEvent{.key = ava::tui::Key::Tab});
  expect(prompt_input.action == ava::tui::PermissionPromptInputAction::Redraw &&
             prompt_input.selected_choice == ava::tui::PermissionPromptChoice::Allow,
         "permission prompt tab toggles focus to allow");
  prompt_input = ava::tui::handle_permission_prompt_input(ava::tui::PermissionPromptChoice::Allow,
                                                          ava::tui::InputEvent{.key = ava::tui::Key::Tab});
  expect(prompt_input.action == ava::tui::PermissionPromptInputAction::Redraw &&
             prompt_input.selected_choice == ava::tui::PermissionPromptChoice::Deny,
         "permission prompt tab toggles focus back to deny");
  prompt_input = ava::tui::handle_permission_prompt_input(ava::tui::PermissionPromptChoice::Allow,
                                                          ava::tui::InputEvent{.key = ava::tui::Key::ArrowLeft});
  expect(prompt_input.action == ava::tui::PermissionPromptInputAction::Redraw &&
             prompt_input.selected_choice == ava::tui::PermissionPromptChoice::Deny,
         "permission prompt left arrow selects deny");
  prompt_input = ava::tui::handle_permission_prompt_input(ava::tui::PermissionPromptChoice::Deny,
                                                          ava::tui::InputEvent{.key = ava::tui::Key::ArrowRight});
  expect(prompt_input.action == ava::tui::PermissionPromptInputAction::Redraw &&
             prompt_input.selected_choice == ava::tui::PermissionPromptChoice::Allow,
         "permission prompt right arrow selects allow");
  prompt_input = ava::tui::handle_permission_prompt_input(ava::tui::PermissionPromptChoice::Allow,
                                                          ava::tui::InputEvent{.key = ava::tui::Key::Enter});
  expect(prompt_input.action == ava::tui::PermissionPromptInputAction::ResolveAllow,
         "permission prompt enter confirms selected allow");
  prompt_input = ava::tui::handle_permission_prompt_input(
      ava::tui::PermissionPromptChoice::Deny, ava::tui::InputEvent{.key = ava::tui::Key::Character, .character = ' '});
  expect(prompt_input.action == ava::tui::PermissionPromptInputAction::ResolveDeny,
         "permission prompt space confirms selected deny");
  prompt_input = ava::tui::handle_permission_prompt_input(ava::tui::PermissionPromptChoice::Allow,
                                                          ava::tui::InputEvent{.key = ava::tui::Key::Escape});
  expect(prompt_input.action == ava::tui::PermissionPromptInputAction::ResolveDeny,
         "permission prompt escape resolves deny");
  prompt_input = ava::tui::handle_permission_prompt_input(ava::tui::PermissionPromptChoice::Allow,
                                                          ava::tui::InputEvent{.key = ava::tui::Key::CtrlC});
  expect(prompt_input.action == ava::tui::PermissionPromptInputAction::ResolveDeny,
         "permission prompt ctrl-c resolves deny");
  prompt_input = ava::tui::handle_permission_prompt_input(ava::tui::PermissionPromptChoice::Allow,
                                                          ava::tui::InputEvent{.key = ava::tui::Key::CtrlD});
  expect(prompt_input.action == ava::tui::PermissionPromptInputAction::ResolveDeny,
         "permission prompt ctrl-d resolves deny");
  prompt_input = ava::tui::handle_permission_prompt_input(
      ava::tui::PermissionPromptChoice::Deny, ava::tui::InputEvent{.key = ava::tui::Key::Character, .character = 'A'});
  expect(prompt_input.action == ava::tui::PermissionPromptInputAction::ResolveAllow,
         "permission prompt A resolves allow");
  prompt_input = ava::tui::handle_permission_prompt_input(
      ava::tui::PermissionPromptChoice::Allow, ava::tui::InputEvent{.key = ava::tui::Key::Character, .character = 'D'});
  expect(prompt_input.action == ava::tui::PermissionPromptInputAction::ResolveDeny,
         "permission prompt D resolves deny");
  prompt_input = ava::tui::handle_permission_prompt_input(
      ava::tui::PermissionPromptChoice::Allow, ava::tui::InputEvent{.key = ava::tui::Key::Character, .character = 'x'});
  expect(prompt_input.action == ava::tui::PermissionPromptInputAction::None &&
             prompt_input.selected_choice == ava::tui::PermissionPromptChoice::Allow,
         "permission prompt ignores unmapped character keys without changing focus");
  std::string utf8_input = std::string("ab") + "\xC3\xA9";
  ava::tui::erase_last_utf8_codepoint(utf8_input);
  expect(utf8_input == "ab", "tui backspace erases a complete utf-8 codepoint");
  std::string orphan_continuation = std::string("a") + std::string("\x80", 1);
  ava::tui::erase_last_utf8_codepoint(orphan_continuation);
  expect(orphan_continuation == "a", "tui backspace erases only a trailing orphan utf-8 continuation byte");
  std::string orphan_after_utf8 = std::string("a") + "\xC3\xA9" + std::string("\x80", 1);
  ava::tui::erase_last_utf8_codepoint(orphan_after_utf8);
  expect(orphan_after_utf8 == std::string("a") + "\xC3\xA9",
         "tui backspace preserves the preceding utf-8 codepoint when erasing an orphan continuation byte");
  std::string incomplete_starter = std::string("a") + std::string("\xE2", 1);
  ava::tui::erase_last_utf8_codepoint(incomplete_starter);
  expect(incomplete_starter == "a", "tui backspace erases an incomplete trailing utf-8 starter byte");
  std::string incomplete_starter_with_continuation = std::string("a") + std::string("\xF0\x9F", 2);
  ava::tui::erase_last_utf8_codepoint(incomplete_starter_with_continuation);
  expect(incomplete_starter_with_continuation == std::string("a") + std::string("\xF0", 1),
         "tui backspace erases only one byte from an incomplete trailing utf-8 sequence");

  const auto split_empty = ava::tui::split_lines("");
  expect(split_empty.size() == 1 && split_empty.front().empty(), "tui split keeps empty input as one line");
  const auto split_trailing = ava::tui::split_lines("a\n");
  expect(split_trailing.size() == 2 && split_trailing[0] == "a" && split_trailing[1].empty(),
         "tui split preserves trailing empty line");
  const auto split_crlf = ava::tui::split_lines("a\r\nb\rc");
  expect(split_crlf.size() == 3 && split_crlf[0] == "a" && split_crlf[1] == "b" && split_crlf[2] == "c",
         "tui split treats crlf and carriage-return output as line breaks");

  const auto lines = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "/help",
                                 .status = "ready",
                                 .transcript = {ava::tui::TranscriptItem{.label = "you", .text = "hello"},
                                                ava::tui::TranscriptItem{.label = "ava", .text = "world"}},
                                 .width = 80,
                                 .height = 14});
  expect(lines.size() == 14, "tui pins compact content to the bottom of the viewport");
  expect(!lines.empty() && lines.back().find("\x1b[48;2;26;31;46m") != std::string::npos &&
             std::ranges::none_of(lines,
                                  [](const std::string& line) { return line.find("/ commands") != std::string::npos; }),
         "tui keeps only the composer block at the bottom");
  expect(std::ranges::any_of(
             lines, [](const std::string& line) { return strip_sgr(line).find("▎  ❯ /help") != std::string::npos; }),
         "tui renders old AVA-style composer input");
  expect(std::ranges::any_of(lines,
                             [](const std::string& line) {
                               return line.find("\x1b[48;2;26;31;46m") != std::string::npos &&
                                      line.find("\x1b[38;2;77;158;246m▎") != std::string::npos &&
                                      line.find("\x1b[1m\x1b[38;2;77;158;246m❯") != std::string::npos;
                             }),
         "tui uses old AVA elevated composer surface, primary rail, and prompt color");
  expect(std::ranges::any_of(
             lines, [](const std::string& line) { return strip_sgr(line).find("╭─ You") != std::string::npos; }) &&
             std::ranges::any_of(
                 lines, [](const std::string& line) { return strip_sgr(line).find("│ hello") != std::string::npos; }) &&
             std::ranges::any_of(
                 lines, [](const std::string& line) { return strip_sgr(line).find("╭─ AVA") != std::string::npos; }) &&
             std::ranges::any_of(
                 lines, [](const std::string& line) { return strip_sgr(line).find("│ world") != std::string::npos; }),
         "tui renders visually separated user and assistant message blocks");

  const auto markdown_transcript = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{
          .label = "ava",
          .text =
              "First paragraph wraps cleanly across words.\n\nSecond paragraph stays separate.\n- bullet item\n* star "
              "item\n1. numbered item\n> quoted text\n```cpp\nint main() {}\n```\nUse `ava build` and "
              "**bold text**."}},
      .width = 72,
      .height = 24});
  expect(
      std::ranges::any_of(markdown_transcript,
                          [](const std::string& line) {
                            return strip_sgr(line).find("│ First paragraph wraps cleanly") != std::string::npos;
                          }) &&
          std::ranges::any_of(markdown_transcript,
                              [](const std::string& line) {
                                return strip_sgr(line).find("│ Second paragraph stays separate") != std::string::npos;
                              }) &&
          std::ranges::any_of(
              markdown_transcript,
              [](const std::string& line) { return strip_sgr(line).find("│ - bullet item") != std::string::npos; }) &&
          std::ranges::any_of(
              markdown_transcript,
              [](const std::string& line) { return strip_sgr(line).find("│ * star item") != std::string::npos; }) &&
          std::ranges::any_of(markdown_transcript,
                              [](const std::string& line) {
                                return strip_sgr(line).find("│ 1. numbered item") != std::string::npos;
                              }) &&
          std::ranges::any_of(
              markdown_transcript,
              [](const std::string& line) { return strip_sgr(line).find("│ > quoted text") != std::string::npos; }) &&
          std::ranges::any_of(
              markdown_transcript,
              [](const std::string& line) { return strip_sgr(line).find("│ ``` cpp") != std::string::npos; }) &&
          std::ranges::any_of(
              markdown_transcript,
              [](const std::string& line) { return strip_sgr(line).find("│   int main() {}") != std::string::npos; }) &&
          std::ranges::any_of(markdown_transcript,
                              [](const std::string& line) {
                                return strip_sgr(line).find("Use ava build and bold text") != std::string::npos;
                              }) &&
          std::ranges::none_of(markdown_transcript,
                               [](const std::string& line) {
                                 const auto visible = strip_sgr(line);
                                 return visible.find("`ava build`") != std::string::npos ||
                                        visible.find("**bold text**") != std::string::npos;
                               }),
      "tui assistant renderer handles paragraphs, lists, quotes, fenced code, inline code, and bold");

  constexpr auto kBoldSgr = std::string_view{"\x1b[1m"};
  constexpr auto kMutedSgr = std::string_view{"\x1b[38;2;139;149;165m"};
  constexpr auto kWarningSgr = std::string_view{"\x1b[38;2;251;191;36m"};

  const auto role_markup_transcript = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "ready",
                                 .transcript = {ava::tui::TranscriptItem{.label = "you", .text = "Use `x` and **y**."},
                                                ava::tui::TranscriptItem{.label = "ava", .text = "Use `x` and **y**."}},
                                 .width = 28,
                                 .height = 16});
  const auto user_markup_line = std::ranges::find_if(role_markup_transcript, [](const std::string& line) {
    return strip_sgr(line).find("You: Use `x` and **y**.") != std::string::npos;
  });
  const auto assistant_markup_line = std::ranges::find_if(role_markup_transcript, [](const std::string& line) {
    return strip_sgr(line).find("AVA: Use x and y.") != std::string::npos;
  });
  expect(user_markup_line != role_markup_transcript.end() && assistant_markup_line != role_markup_transcript.end() &&
             !has_active_sgr_at_text(*user_markup_line, "x", kWarningSgr) &&
             !has_active_sgr_at_text(*user_markup_line, "y", kBoldSgr) &&
             has_active_sgr_at_text(*assistant_markup_line, "x", kWarningSgr) &&
             has_active_sgr_at_text(*assistant_markup_line, "y", kBoldSgr),
         "tui keeps user inline markdown literal while formatting assistant inline markdown");

  const auto wrapped_markdown_transcript = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{
          .label = "ava",
          .text = "- alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu\n> quoted alpha beta gamma "
                  "delta epsilon zeta eta theta iota kappa"}},
      .width = 48,
      .height = 16});
  const auto bullet_continuation = std::ranges::find_if(wrapped_markdown_transcript, [](const std::string& line) {
    return strip_sgr(line).find("│   theta iota") != std::string::npos;
  });
  const auto quote_continuation = std::ranges::find_if(wrapped_markdown_transcript, [](const std::string& line) {
    return strip_sgr(line).find("│   eta theta") != std::string::npos;
  });
  const auto bullet_continuation_is_plain = bullet_continuation != wrapped_markdown_transcript.end() &&
                                            !has_active_sgr_at_text(*bullet_continuation, "theta iota", kMutedSgr);
  const auto quote_continuation_is_plain = quote_continuation != wrapped_markdown_transcript.end() &&
                                           !has_active_sgr_at_text(*quote_continuation, "eta theta", kMutedSgr);
  expect(bullet_continuation_is_plain && quote_continuation_is_plain,
         "tui assistant renderer keeps wrapped list and quote continuations out of code styling");

  const auto wrapped_code_fence_transcript = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{
          .label = "ava",
          .text = std::string("```text\n") + std::string(42, 'a') + "```literal\nomega\n```\nAfter **bold**"}},
      .width = 48,
      .height = 16});
  const auto code_after_wrapped_ticks = std::ranges::find_if(
      wrapped_code_fence_transcript,
      [](const std::string& line) { return strip_sgr(line).find("│   omega") != std::string::npos; });
  const auto text_after_code = std::ranges::find_if(wrapped_code_fence_transcript, [](const std::string& line) {
    return strip_sgr(line).find("│ After bold") != std::string::npos;
  });
  expect(code_after_wrapped_ticks != wrapped_code_fence_transcript.end() &&
             has_active_sgr_at_text(*code_after_wrapped_ticks, "omega", kMutedSgr) &&
             text_after_code != wrapped_code_fence_transcript.end() &&
             !has_active_sgr_at_text(*text_after_code, "After", kMutedSgr) &&
             has_active_sgr_at_text(*text_after_code, "bold", kBoldSgr),
         "tui assistant renderer keeps wrapped code content beginning with backticks inside the code block");

  const auto indented_fence_content_transcript = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "ready",
                                 .transcript = {ava::tui::TranscriptItem{
                                     .label = "ava", .text = "```text\n  ```literal\nomega\n```\nAfter **bold**"}},
                                 .width = 56,
                                 .height = 16});
  const auto indented_ticks = std::ranges::find_if(indented_fence_content_transcript, [](const std::string& line) {
    return strip_sgr(line).find("```literal") != std::string::npos;
  });
  const auto code_after_indented_ticks = std::ranges::find_if(
      indented_fence_content_transcript,
      [](const std::string& line) { return strip_sgr(line).find("│   omega") != std::string::npos; });
  const auto text_after_indented_code = std::ranges::find_if(
      indented_fence_content_transcript,
      [](const std::string& line) { return strip_sgr(line).find("│ After bold") != std::string::npos; });
  expect(indented_ticks != indented_fence_content_transcript.end() &&
             strip_sgr(*indented_ticks).find("``` literal") == std::string::npos &&
             code_after_indented_ticks != indented_fence_content_transcript.end() &&
             has_active_sgr_at_text(*code_after_indented_ticks, "omega", kMutedSgr) &&
             text_after_indented_code != indented_fence_content_transcript.end() &&
             !has_active_sgr_at_text(*text_after_indented_code, "After", kMutedSgr) &&
             has_active_sgr_at_text(*text_after_indented_code, "bold", kBoldSgr),
         "tui assistant renderer keeps indented backtick content inside fenced code blocks");

  const auto narrow_code_transcript = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{
          .label = "ava", .text = "Intro `ok` and **bold**.\n```text\nvalue `x` and **y**\n```\nDone **bold**."}},
      .width = 30,
      .height = 18});
  const auto narrow_inline = std::ranges::find_if(narrow_code_transcript, [](const std::string& line) {
    return strip_sgr(line).find("AVA: Intro ok and bold.") != std::string::npos;
  });
  const auto narrow_code = std::ranges::find_if(narrow_code_transcript, [](const std::string& line) {
    return strip_sgr(line).find("value `x` and **y**") != std::string::npos;
  });
  const auto narrow_after_code = std::ranges::find_if(narrow_code_transcript, [](const std::string& line) {
    return strip_sgr(line).find("Done bold.") != std::string::npos;
  });
  expect(narrow_inline != narrow_code_transcript.end() && has_active_sgr_at_text(*narrow_inline, "ok", kWarningSgr) &&
             has_active_sgr_at_text(*narrow_inline, "bold", kBoldSgr) && narrow_code != narrow_code_transcript.end() &&
             !has_active_sgr_at_text(*narrow_code, "x", kWarningSgr) &&
             !has_active_sgr_at_text(*narrow_code, "y", kBoldSgr) &&
             narrow_after_code != narrow_code_transcript.end() &&
             has_active_sgr_at_text(*narrow_after_code, "bold", kBoldSgr),
         "tui narrow assistant renderer keeps code literal while formatting inline markdown outside code");

  const auto narrow_transcript = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = std::string(64, 'x') + " done"}},
      .width = 20,
      .height = 14});
  expect(
      std::ranges::any_of(narrow_transcript,
                          [](const std::string& line) { return strip_sgr(line).find("AVA: ") != std::string::npos; }) &&
          std::ranges::all_of(narrow_transcript, [](const std::string& line) { return visible_columns(line) <= 20; }),
      "tui assistant renderer keeps long words readable at narrow widths");

  const auto rows_transcript = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "ready",
                                 .transcript = {ava::tui::TranscriptItem{.label = "error", .text = "bad command"},
                                                ava::tui::TranscriptItem{.label = "command", .text = "/help"}},
                                 .width = 50,
                                 .height = 10});
  expect(std::ranges::any_of(
             rows_transcript,
             [](const std::string& line) { return strip_sgr(line).find("! bad command") != std::string::npos; }) &&
             std::ranges::any_of(
                 rows_transcript,
                 [](const std::string& line) { return strip_sgr(line).find("· /help") != std::string::npos; }),
         "tui keeps errors and generic command rows distinct from message blocks");
  const auto compact_status = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "Enter submits. Ctrl-J/Shift+Enter inserts newline. / opens commands. Page/wheel scroll.",
      .transcript = {},
      .width = 120,
      .height = 8});
  expect(std::ranges::none_of(compact_status,
                              [](const std::string& line) {
                                return strip_sgr(line).find("Ctrl-J/Shift+Enter inserts newline") != std::string::npos;
                              }),
         "tui keeps the composer status compact instead of rendering verbose help");

  const auto minimum_width = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                  .provider = "openai",
                                                                                  .model = "gpt-5.5",
                                                                                  .session_id = "session_test",
                                                                                  .input = "hello",
                                                                                  .status = "ready",
                                                                                  .transcript = {},
                                                                                  .width = 1,
                                                                                  .height = 1});
  expect(std::ranges::all_of(minimum_width,
                             [](const std::string& line) {
                               return line.find('\n') == std::string::npos && visible_columns(line) <= 20;
                             }) &&
             std::ranges::any_of(
                 minimum_width,
                 [](const std::string& line) { return strip_sgr(line).find("❯ hello") != std::string::npos; }),
         "tui clamps normal composer rendering to the minimum viewport");

  const std::vector<ava::tui::SlashCommandItem> slash_commands = {
      ava::tui::SlashCommandItem{.command = "/help", .description = "Show help", .category = "General"},
      ava::tui::SlashCommandItem{
          .command = "/grep", .description = "Search files", .hint = "<text> [glob]", .category = "Files"},
      ava::tui::SlashCommandItem{
          .command = "/glob", .description = "List matching files", .hint = "<pattern>", .category = "Files"},
      ava::tui::SlashCommandItem{.command = "/quit", .description = "Exit", .category = "General"}};
  const auto grep_commands = ava::tui::filter_slash_commands("/gr", slash_commands);
  expect(grep_commands.size() == 1 && grep_commands.front().command == "/grep",
         "tui slash palette filters commands by typed prefix");
  expect(ava::tui::filter_slash_commands("hello", slash_commands).empty(),
         "tui slash palette stays hidden for normal chat input");
  expect(ava::tui::slash_palette_visible("/g", slash_commands),
         "tui slash palette is visible while filtering commands");
  expect(!ava::tui::slash_palette_visible("/help", slash_commands),
         "tui slash palette hides after an exact no-argument command");
  expect(ava::tui::slash_command_selection_text("/g", slash_commands, 1) == "/glob ",
         "tui slash selection fills argument-taking command with a trailing space");
  expect(ava::tui::slash_command_selection_text("/h", slash_commands, 0) == "/help",
         "tui slash selection fills no-argument command without submitting it");
  expect(ava::tui::clamp_slash_palette_selection("/g", slash_commands, 99) == 1,
         "tui clamps out-of-range slash palette selection to the last match");
  expect(ava::tui::previous_slash_palette_selection("/g", slash_commands, 0) == 1 &&
             ava::tui::next_slash_palette_selection("/g", slash_commands, 1) == 0,
         "tui slash palette arrow selection wraps through filtered commands");

  const auto palette = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                            .provider = "openai",
                                                                            .model = "gpt-5.5",
                                                                            .session_id = "session_test",
                                                                            .input = "/g",
                                                                            .status = "ready",
                                                                            .transcript = {},
                                                                            .slash_commands = slash_commands,
                                                                            .selected_slash_command_index = 1,
                                                                            .width = 80,
                                                                            .height = 12});
  expect(std::ranges::any_of(
             palette, [](const std::string& line) { return line.find("commands matching /g") != std::string::npos; }) &&
             std::ranges::any_of(palette,
                                 [](const std::string& line) {
                                   return line.find("/grep") != std::string::npos &&
                                          line.find("Files") != std::string::npos &&
                                          line.find("Search files") != std::string::npos;
                                 }) &&
             std::ranges::any_of(palette,
                                 [](const std::string& line) {
                                   return line.find("/glob") != std::string::npos &&
                                          line.find("List matching files") != std::string::npos;
                                 }) &&
             std::ranges::any_of(palette,
                                 [](const std::string& line) {
                                   const auto visible = strip_sgr(line);
                                   return visible.find("> /glob (2/2)") != std::string::npos &&
                                          visible.find("selected") != std::string::npos;
                                 }) &&
             std::ranges::any_of(palette,
                                 [](const std::string& line) {
                                   return line.find("\x1b[7m> /glob (2/2)") != std::string::npos &&
                                          line.find("\x1b[0m") != std::string::npos;
                                 }) &&
             std::ranges::none_of(palette,
                                  [](const std::string& line) { return line.find("/help") != std::string::npos; }),
         "tui renders filtered slash-command palette with columns and selected item marker");
  const auto clicked_palette_index =
      ava::tui::slash_palette_selection_for_screen_row(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                  .provider = "openai",
                                                                                  .model = "gpt-5.5",
                                                                                  .session_id = "session_test",
                                                                                  .input = "/g",
                                                                                  .status = "ready",
                                                                                  .transcript = {},
                                                                                  .slash_commands = slash_commands,
                                                                                  .selected_slash_command_index = 1,
                                                                                  .width = 80,
                                                                                  .height = 12},
                                                       8);
  expect(clicked_palette_index && *clicked_palette_index == 1,
         "tui maps slash palette screen rows back to selectable commands for clicks");

  std::vector<ava::tui::SlashCommandItem> many_slash_commands;
  for (int index = 0; index < 8; ++index) {
    many_slash_commands.push_back(ava::tui::SlashCommandItem{.command = "/item" + std::to_string(index),
                                                             .description = "Command " + std::to_string(index)});
  }
  const auto tiny_palette = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                 .provider = "openai",
                                                                                 .model = "gpt-5.5",
                                                                                 .session_id = "session_test",
                                                                                 .input = "/",
                                                                                 .status = "ready",
                                                                                 .transcript = {},
                                                                                 .slash_commands = many_slash_commands,
                                                                                 .selected_slash_command_index = 6,
                                                                                 .width = 80,
                                                                                 .height = 8});
  expect(std::ranges::any_of(tiny_palette,
                             [](const std::string& line) { return line.find("> /item6") != std::string::npos; }) &&
             std::ranges::none_of(tiny_palette,
                                  [](const std::string& line) { return line.find("/item0") != std::string::npos; }),
         "tui keeps selected slash palette item visible when height is tight");
  const auto first_scrolled_palette_click =
      ava::tui::slash_palette_selection_for_screen_row(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                  .provider = "openai",
                                                                                  .model = "gpt-5.5",
                                                                                  .session_id = "session_test",
                                                                                  .input = "/",
                                                                                  .status = "ready",
                                                                                  .transcript = {},
                                                                                  .slash_commands = many_slash_commands,
                                                                                  .selected_slash_command_index = 6,
                                                                                  .width = 80,
                                                                                  .height = 8},
                                                       2);
  const auto selected_scrolled_palette_click =
      ava::tui::slash_palette_selection_for_screen_row(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                  .provider = "openai",
                                                                                  .model = "gpt-5.5",
                                                                                  .session_id = "session_test",
                                                                                  .input = "/",
                                                                                  .status = "ready",
                                                                                  .transcript = {},
                                                                                  .slash_commands = many_slash_commands,
                                                                                  .selected_slash_command_index = 6,
                                                                                  .width = 80,
                                                                                  .height = 8},
                                                       4);
  const auto outside_scrolled_palette_click =
      ava::tui::slash_palette_selection_for_screen_row(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                  .provider = "openai",
                                                                                  .model = "gpt-5.5",
                                                                                  .session_id = "session_test",
                                                                                  .input = "/",
                                                                                  .status = "ready",
                                                                                  .transcript = {},
                                                                                  .slash_commands = many_slash_commands,
                                                                                  .selected_slash_command_index = 6,
                                                                                  .width = 80,
                                                                                  .height = 8},
                                                       5);
  expect(first_scrolled_palette_click && *first_scrolled_palette_click == 4 && selected_scrolled_palette_click &&
             *selected_scrolled_palette_click == 6 && !outside_scrolled_palette_click,
         "tui maps slash palette click rows through a scrolled visible window and ignores outside rows");

  const auto starved_palette = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "/",
                                 .status = "ready",
                                 .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "must not leak"}},
                                 .slash_commands = many_slash_commands,
                                 .selected_slash_command_index = 4,
                                 .width = 40,
                                 .height = 8});
  expect(starved_palette.size() == 8 &&
             std::ranges::any_of(
                 starved_palette,
                 [](const std::string& line) { return strip_sgr(line).find("> /item4") != std::string::npos; }) &&
             std::ranges::none_of(
                 starved_palette,
                 [](const std::string& line) { return strip_sgr(line).find("must not leak") != std::string::npos; }),
         "tui keeps the bottom composer fixed when the slash palette exhausts transcript height");

  const auto no_match_palette = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                     .provider = "openai",
                                                                                     .model = "gpt-5.5",
                                                                                     .session_id = "session_test",
                                                                                     .input = "/zz",
                                                                                     .status = "ready",
                                                                                     .transcript = {},
                                                                                     .slash_commands = slash_commands,
                                                                                     .width = 80,
                                                                                     .height = 12});
  expect(std::ranges::any_of(no_match_palette,
                             [](const std::string& line) {
                               return strip_sgr(line).find("no commands match /zz") != std::string::npos;
                             }),
         "tui slash-command palette renders deterministic empty state");

  const auto permission_modal = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "permission required",
      .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "thinking"}},
      .permission_prompt = ava::tui::PermissionPromptView{.tool_name = "bash\x1b[31m",
                                                          .operation = "bash",
                                                          .target = "/tmp/outside",
                                                          .command = "git push origin main",
                                                          .reason = "command can change external state"},
      .width = 80,
      .height = 12});
  expect(std::ranges::any_of(
             permission_modal,
             [](const std::string& line) { return line.find("PERMISSION REQUIRED") != std::string::npos; }) &&
             std::ranges::any_of(permission_modal,
                                 [](const std::string& line) {
                                   auto visible = strip_sgr(line);
                                   return visible.find("git push origin main") != std::string::npos;
                                 }) &&
             std::ranges::any_of(permission_modal,
                                 [](const std::string& line) {
                                   return line.find("[Deny]") != std::string::npos &&
                                          line.find("[Allow once]") != std::string::npos;
                                 }) &&
             std::ranges::any_of(permission_modal,
                                 [](const std::string& line) {
                                   return line.find("\x1b[7m> [Deny]") != std::string::npos &&
                                          strip_sgr(line).find("[Deny] (selected)") != std::string::npos;
                                 }) &&
             std::ranges::any_of(permission_modal,
                                 [](const std::string& line) {
                                   auto visible = strip_sgr(line);
                                   return visible.find("A allow") != std::string::npos &&
                                          visible.find("D deny") != std::string::npos &&
                                          visible.find("Enter confirm") != std::string::npos &&
                                          visible.find("Esc deny") != std::string::npos;
                                 }) &&
             std::ranges::none_of(permission_modal,
                                  [](const std::string& line) {
                                    return line.find("bash") != std::string::npos &&
                                           line.find("\x1b[31m") != std::string::npos;
                                  }),
         "tui renders Rust AVA-style permission dock with default deny focus");
  expect(std::ranges::all_of(permission_modal,
                             [](const std::string& line) {
                               return line.find('\n') == std::string::npos && visible_columns(line) <= 80;
                             }) &&
             std::ranges::any_of(
                 permission_modal,
                 [](const std::string& line) { return strip_sgr(line).find("[Deny]") != std::string::npos; }) &&
             std::ranges::any_of(
                 permission_modal,
                 [](const std::string& line) { return strip_sgr(line).find("[Allow once]") != std::string::npos; }) &&
             std::ranges::any_of(
                 permission_modal,
                 [](const std::string& line) { return strip_sgr(line).find("Enter confirm") != std::string::npos; }) &&
             std::ranges::any_of(
                 permission_modal,
                 [](const std::string& line) { return strip_sgr(line).find("Esc deny") != std::string::npos; }),
         "tui permission dock controls stay within 80 visible columns without losing controls");

  const auto allow_focused_modal = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "permission required",
      .transcript = {},
      .permission_prompt = ava::tui::PermissionPromptView{.tool_name = "bash",
                                                          .operation = "bash",
                                                          .target = "",
                                                          .command = "true",
                                                          .reason = "unknown risk",
                                                          .selected_choice = ava::tui::PermissionPromptChoice::Allow},
      .width = 80,
      .height = 14});
  expect(std::ranges::any_of(allow_focused_modal,
                             [](const std::string& line) {
                               return line.find("\x1b[7m> [Allow once]") != std::string::npos &&
                                      strip_sgr(line).find("[Allow once] (selected)") != std::string::npos;
                             }),
         "tui permission dock highlights the selected allow choice");

  const auto long_permission_modal = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "permission required",
                                 .transcript = {},
                                 .permission_prompt = ava::tui::PermissionPromptView{.tool_name = "",
                                                                                     .operation = "write_file",
                                                                                     .target = std::string(120, 't'),
                                                                                     .command = std::string(120, 'c'),
                                                                                     .reason = std::string(120, 'r')},
                                 .width = 80,
                                 .height = 10});
  expect(std::ranges::all_of(long_permission_modal,
                             [](const std::string& line) {
                               return line.find('\n') == std::string::npos && visible_columns(line) <= 80;
                             }) &&
             std::ranges::any_of(long_permission_modal,
                                 [](const std::string& line) {
                                   const auto visible = strip_sgr(line);
                                   return visible.find("cccc") != std::string::npos &&
                                          visible.find("...") != std::string::npos;
                                 }),
         "tui permission dock truncates long detail text and handles an empty tool name");

  const auto tight_permission_modal = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "permission required",
      .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "thinking"}},
      .permission_prompt =
          ava::tui::PermissionPromptView{
              .tool_name = "bash", .operation = "bash", .target = "", .command = "true", .reason = ""},
      .width = 36,
      .height = 8});
  expect(std::ranges::any_of(
             tight_permission_modal,
             [](const std::string& line) { return line.find("PERMISSION REQUIRED") != std::string::npos; }) &&
             tight_permission_modal.size() <= 8 &&
             std::ranges::all_of(tight_permission_modal,
                                 [](const std::string& line) {
                                   return line.find('\n') == std::string::npos && visible_columns(line) <= 36;
                                 }) &&
             std::ranges::any_of(tight_permission_modal,
                                 [](const std::string& line) {
                                   return line.find("[Deny]") != std::string::npos &&
                                          line.find("[Allow once]") != std::string::npos;
                                 }),
         "tui permission dock keeps header and controls visible in tight height");

  const auto ultra_tight_permission_modal = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "permission required",
      .transcript = {},
      .permission_prompt =
          ava::tui::PermissionPromptView{
              .tool_name = "bash", .operation = "bash", .target = "", .command = "true", .reason = ""},
      .width = 20,
      .height = 8});
  expect(std::ranges::all_of(ultra_tight_permission_modal,
                             [](const std::string& line) {
                               return line.find('\n') == std::string::npos && visible_columns(line) <= 20;
                             }) &&
             ultra_tight_permission_modal.size() <= 8 &&
             std::ranges::any_of(ultra_tight_permission_modal,
                                 [](const std::string& line) {
                                   const auto visible = strip_sgr(line);
                                   return visible.find("PERMISSION") != std::string::npos;
                                 }) &&
             std::ranges::any_of(ultra_tight_permission_modal,
                                 [](const std::string& line) {
                                   const auto visible = strip_sgr(line);
                                   return visible.find("> [D] sel") != std::string::npos &&
                                          visible.find("[A]") != std::string::npos;
                                 }) &&
             std::ranges::any_of(ultra_tight_permission_modal,
                                 [](const std::string& line) {
                                   const auto visible = strip_sgr(line);
                                   return visible.find("A=allow") != std::string::npos &&
                                          visible.find("D=deny") != std::string::npos;
                                 }),
         "tui permission dock preserves deny and allow choices at minimum width");

  std::vector<ava::tui::TranscriptItem> permission_overflow_items;
  for (int index = 0; index < 8; ++index) {
    permission_overflow_items.push_back(
        ava::tui::TranscriptItem{.label = "ava", .text = "permission item " + std::to_string(index)});
  }
  const auto permission_starved = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "hidden input",
      .status = "permission required",
      .transcript = permission_overflow_items,
      .permission_prompt =
          ava::tui::PermissionPromptView{
              .tool_name = "bash", .operation = "bash", .target = "", .command = "true", .reason = ""},
      .width = 40,
      .height = 8});
  expect(permission_starved.size() <= 8 &&
             std::ranges::all_of(permission_starved,
                                 [](const std::string& line) { return visible_columns(line) <= 40; }) &&
             std::ranges::any_of(permission_starved,
                                 [](const std::string& line) {
                                   return strip_sgr(line).find("older lines hidden") != std::string::npos;
                                 }) &&
             std::ranges::none_of(
                 permission_starved,
                 [](const std::string& line) { return strip_sgr(line).find("❯ hidden input") != std::string::npos; }),
         "tui permission prompt handles height-starved transcript overflow without showing the composer");

  const auto sanitized = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "bad\x1b[31mstatus",
                                 .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "bad\x1b[31mred"}},
                                 .width = 80,
                                 .height = 8});
  expect(std::ranges::any_of(sanitized,
                             [](const std::string& line) {
                               auto visible = strip_sgr(line);
                               return visible.find("?[31mred") != std::string::npos;
                             }),
         "tui render sanitizes transcript escape bytes in user content");
  const auto sanitized_input = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                    .provider = "openai",
                                                                                    .model = "gpt-5.5",
                                                                                    .session_id = "session_test",
                                                                                    .input = "bad\x1b[31mred",
                                                                                    .status = "ready",
                                                                                    .transcript = {},
                                                                                    .width = 80,
                                                                                    .height = 8});
  expect(std::ranges::any_of(
             sanitized_input,
             [](const std::string& line) { return strip_sgr(line).find("❯ bad?[31mred") != std::string::npos; }),
         "tui render sanitizes composer input escape bytes");
  expect(ava::tui::sanitize_terminal_text(std::string("osc") + static_cast<char>(0x9D) + "payload") == "osc?payload",
         "tui sanitizes raw c1 terminal control bytes");
  expect(ava::tui::sanitize_terminal_text("a\tb") == "a  b", "tui expands tabs before width accounting");
  expect(ava::tui::sanitize_terminal_text(std::string("ok ") + "\xC3\xA9") == std::string("ok ") + "\xC3\xA9",
         "tui sanitizer preserves valid utf-8 text");
  expect(ava::tui::sanitize_terminal_text(std::string("x") + std::string("\xC0\x80", 2) + "y") == "x??y",
         "tui sanitizer rejects overlong two-byte utf-8 controls");
  expect(ava::tui::sanitize_terminal_text(std::string("x") + std::string("\xE0\x80\x80", 3) + "y") == "x???y",
         "tui sanitizer rejects overlong three-byte utf-8 forms");
  expect(ava::tui::sanitize_terminal_text(std::string("x") + std::string("\xF0\x80\x80\x80", 4) + "y") == "x????y",
         "tui sanitizer rejects overlong four-byte utf-8 forms");
  expect(ava::tui::sanitize_terminal_text(std::string("x") + std::string("\xE2\x82", 2)) == "x??",
         "tui sanitizer replaces truncated utf-8 at the string boundary");
  expect(ava::tui::sanitize_terminal_text(std::string("x") + std::string("\xED\xA0\x80", 3) + "y") == "x???y",
         "tui sanitizer rejects utf-8 surrogate codepoints");
  expect(ava::tui::sanitize_terminal_text(std::string("x") + std::string("\xF4\x90\x80\x80", 4) + "y") == "x????y",
         "tui sanitizer rejects utf-8 codepoints above the unicode maximum");

  const auto composer_frame = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "plan",
                                                                                   .provider = "openai",
                                                                                   .model = "gpt-5.5",
                                                                                   .session_id = "session_test",
                                                                                   .input = "hello",
                                                                                   .status = "ready",
                                                                                   .transcript = {},
                                                                                   .width = 40,
                                                                                   .height = 8});
  expect(composer_frame.size() == 8, "tui composer frame fills the requested terminal height");
  expect(std::ranges::any_of(
             composer_frame,
             [](const std::string& line) { return strip_sgr(line).find("▎  ❯ hello") != std::string::npos; }),
         "tui composer frame renders the input prompt content");
  const auto wide_frame = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = std::string("wide ") + "\xE6\xBC\xA2\xE6\xBC\xA2\xF0\x9F\x98\x80",
                                 .status = "ready",
                                 .transcript = {ava::tui::TranscriptItem{
                                     .label = "ava", .text = "\xE6\xBC\xA2\xE6\xBC\xA2\xE6\xBC\xA2\xE6\xBC\xA2"}},
                                 .width = 24,
                                 .height = 10});
  expect(std::ranges::all_of(wide_frame, [](const std::string& line) { return visible_columns(line) <= 24; }),
         "tui treats CJK and emoji as wide cells when fitting rendered lines");
  ava::tui::clear_terminal_signal();
  expect(!ava::tui::terminal_signal_received(), "tui terminal signal state can be cleared before curses entry");

  const auto permission_frame = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "do not focus composer",
      .status = "permission required",
      .transcript = {},
      .permission_prompt =
          ava::tui::PermissionPromptView{
              .tool_name = "bash", .operation = "bash", .target = "", .command = "true", .reason = ""},
      .width = 60,
      .height = 12});
  expect(permission_frame.size() == 12 &&
             std::ranges::none_of(permission_frame,
                                  [](const std::string& line) {
                                    return strip_sgr(line).find("❯ do not focus composer") != std::string::npos;
                                  }) &&
             std::ranges::any_of(permission_frame,
                                 [](const std::string& line) {
                                   return strip_sgr(line).find("PERMISSION REQUIRED") != std::string::npos;
                                 }),
         "tui composer frame replaces composer input with permission dock while active");

  const auto multiline_input = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                    .provider = "openai",
                                                                                    .model = "gpt-5.5",
                                                                                    .session_id = "session_test",
                                                                                    .input = "first\nsecond",
                                                                                    .status = "ready",
                                                                                    .transcript = {},
                                                                                    .width = 50,
                                                                                    .height = 8});
  expect(std::ranges::any_of(
             multiline_input,
             [](const std::string& line) { return strip_sgr(line).find("▎  ❯ first") != std::string::npos; }) &&
             std::ranges::any_of(
                 multiline_input,
                 [](const std::string& line) { return strip_sgr(line).find("▎    second") != std::string::npos; }),
         "tui renders shift-enter newlines as multiline composer input");
  const auto empty_composer_height = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                          .provider = "openai",
                                                                                          .model = "gpt-5.5",
                                                                                          .session_id = "session_test",
                                                                                          .input = "",
                                                                                          .status = "ready",
                                                                                          .transcript = {},
                                                                                          .width = 50,
                                                                                          .height = 12});
  const auto grown_composer_height =
      ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                           .provider = "openai",
                                                           .model = "gpt-5.5",
                                                           .session_id = "session_test",
                                                           .input = "one\ntwo\nthree\nfour\nfive",
                                                           .status = "ready",
                                                           .transcript = {},
                                                           .width = 50,
                                                           .height = 12});
  const auto composer_bg_rows = [](const std::vector<std::string>& rendered) {
    return static_cast<std::size_t>(std::ranges::count_if(
        rendered, [](const std::string& line) { return line.find("\x1b[48;2;26;31;46m") != std::string::npos; }));
  };
  expect(composer_bg_rows(grown_composer_height) > composer_bg_rows(empty_composer_height) &&
             std::ranges::any_of(
                 grown_composer_height,
                 [](const std::string& line) { return strip_sgr(line).find("▎    five") != std::string::npos; }),
         "tui composer grows with multiline input and keeps the latest line visible");

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
                                                                             .height = 12});
  expect(
      std::ranges::any_of(scrolled,
                          [](const std::string& line) { return line.find("item 19") != std::string::npos; }) &&
          std::ranges::any_of(
              scrolled, [](const std::string& line) { return line.find("older lines hidden") != std::string::npos; }) &&
          std::ranges::none_of(scrolled,
                               [](const std::string& line) { return line.find("item 0") != std::string::npos; }),
      "tui transcript viewport keeps newest lines and indicates hidden history");

  const auto scrolled_up = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                .provider = "openai",
                                                                                .model = "gpt-5.5",
                                                                                .session_id = "session_test",
                                                                                .input = "",
                                                                                .status = "ready",
                                                                                .transcript = many_items,
                                                                                .selected_slash_command_index = 0,
                                                                                .transcript_scroll_offset = 4,
                                                                                .width = 80,
                                                                                .height = 12});
  expect(
      std::ranges::any_of(
          scrolled_up, [](const std::string& line) { return line.find("newer lines hidden") != std::string::npos; }) &&
          std::ranges::any_of(scrolled_up,
                              [](const std::string& line) { return line.find("item 15") != std::string::npos; }) &&
          std::ranges::none_of(scrolled_up,
                               [](const std::string& line) { return line.find("item 19") != std::string::npos; }),
      "tui transcript viewport supports an explicit scroll offset");

  const auto wrapped_transcript = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{.label = "ava",
                                              .text = "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
                                                      "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
                                                      "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
                                                      "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
                                                      "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"}},
      .selected_slash_command_index = 0,
      .transcript_scroll_offset = 1,
      .width = 60,
      .height = 8});
  expect(std::ranges::any_of(
             wrapped_transcript,
             [](const std::string& line) { return strip_sgr(line).find("newer lines hidden") != std::string::npos; }),
         "tui transcript viewport wraps long transcript text before applying scroll offset");

  std::vector<ava::tui::TranscriptItem> mixed_items;
  for (int index = 0; index < 8; ++index) {
    mixed_items.push_back(ava::tui::TranscriptItem{.label = "line", .text = "old " + std::to_string(index)});
  }
  mixed_items.push_back(
      ava::tui::TranscriptItem{.tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                  .name = "grep",
                                                                  .argument_summary = "needle",
                                                                  .result_summary = "2 matches"}});
  mixed_items.push_back(ava::tui::TranscriptItem{.label = "ava", .text = "done"});
  const auto mixed_scrolled = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                   .provider = "openai",
                                                                                   .model = "gpt-5.5",
                                                                                   .session_id = "session_test",
                                                                                   .input = "",
                                                                                   .status = "ready",
                                                                                   .transcript = mixed_items,
                                                                                   .width = 60,
                                                                                   .height = 12});
  std::string mixed_visible;
  for (const auto& line : mixed_scrolled) {
    mixed_visible += strip_sgr(line);
    mixed_visible += '\n';
  }
  expect(mixed_visible.find("older lines hidden") != std::string::npos &&
             mixed_visible.find("[+]") != std::string::npos && mixed_visible.find("2 matches") != std::string::npos &&
             mixed_visible.find("AVA") != std::string::npos && mixed_visible.find("│ done") != std::string::npos &&
             mixed_visible.find("old 0") == std::string::npos,
         "tui transcript viewport scrolls mixed text and tool-card lines together");

  const auto multiline = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "ready",
                                 .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "one\ntwo"}},
                                 .width = 80,
                                 .height = 14});
  expect(std::ranges::any_of(multiline,
                             [](const std::string& line) {
                               auto visible = strip_sgr(line);
                               return visible.find("│ one") != std::string::npos;
                             }) &&
             std::ranges::any_of(multiline,
                                 [](const std::string& line) {
                                   auto visible = strip_sgr(line);
                                   return visible.find("│ two") != std::string::npos;
                                 }),
         "tui renders multiline assistant transcript content inside the message block");

  const auto tool_card = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "ready",
                                 .transcript = {ava::tui::TranscriptItem{
                                     .tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                        .name = "read_file",
                                                                        .argument_summary = "path=note.txt\x1b[31m",
                                                                        .result_summary = "read 12/12 bytes"}}},
                                 .width = 80,
                                 .height = 10});
  expect(std::ranges::any_of(tool_card,
                             [](const std::string& line) {
                               auto visible = strip_sgr(line);
                               return visible.find("[+]") != std::string::npos &&
                                      visible.find("read_file") != std::string::npos &&
                                      visible.find("path=note.txt") != std::string::npos;
                             }) &&
             std::ranges::any_of(tool_card,
                                 [](const std::string& line) {
                                   auto visible = strip_sgr(line);
                                   return visible.find("read 12/12 bytes") != std::string::npos;
                                 }) &&
             std::ranges::any_of(tool_card,
                                 [](const std::string& line) {
                                   return line.find("\x1b[38;2;52;211;153m[+]") != std::string::npos &&
                                          line.find("\x1b[1m\x1b[38;2;77;158;246mread_file") != std::string::npos &&
                                          line.find("\x1b[38;2;139;149;165mpath=note.txt") != std::string::npos;
                                 }),
         "tui renders compact styled tool timeline cards");
  expect(std::ranges::none_of(tool_card,
                              [](const std::string& line) { return line.find("\x1b[31m") != std::string::npos; }),
         "tui tool card rendering removes untrusted raw sgr escape sequences");

  const auto empty_tool_card = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "ready",
                                 .transcript = {ava::tui::TranscriptItem{
                                     .tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                        .name = "",
                                                                        .argument_summary = "",
                                                                        .result_summary = ""}}},
                                 .width = 40,
                                 .height = 8});
  expect(std::ranges::any_of(empty_tool_card,
                             [](const std::string& line) {
                               const auto visible = strip_sgr(line);
                               return visible.find("[+]") != std::string::npos &&
                                      visible.find("unknown") != std::string::npos;
                             }) &&
             std::ranges::all_of(empty_tool_card, [](const std::string& line) { return visible_columns(line) <= 40; }),
         "tui renders empty tool-card fields with a safe fallback name");

  const auto running_error_cards = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{
                         .tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Running,
                                                            .name = "bash",
                                                            .argument_summary = "command=build\x1b[31m now",
                                                            .result_summary = ""}},
                     ava::tui::TranscriptItem{
                         .tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Error,
                                                            .name = "write_file",
                                                            .argument_summary = std::string(120, 'a') + "\x1b[31m",
                                                            .result_summary = "error: denied\x1b[31m"}}},
      .width = 60,
      .height = 14});
  expect(
      std::ranges::any_of(running_error_cards,
                          [](const std::string& line) {
                            auto visible = strip_sgr(line);
                            return visible.find("[~]") != std::string::npos &&
                                   visible.find("bash") != std::string::npos &&
                                   visible.find("command=build?") != std::string::npos;
                          }) &&
          std::ranges::any_of(running_error_cards,
                              [](const std::string& line) {
                                auto visible = strip_sgr(line);
                                return visible.find("[x]") != std::string::npos &&
                                       visible.find("write_file") != std::string::npos;
                              }) &&
          std::ranges::all_of(running_error_cards, [](const std::string& line) { return visible_columns(line) <= 60; }),
      "tui renders running/error tool cards with sanitized truncated summaries");
  expect(std::ranges::none_of(running_error_cards,
                              [](const std::string& line) { return line.find("\x1b[31m") != std::string::npos; }),
         "tui running/error tool cards remove untrusted raw sgr escape sequences");
  expect(std::ranges::any_of(
             running_error_cards,
             [](const std::string& line) { return line.find("\x1b[38;2;251;191;36m[~]") != std::string::npos; }) &&
             std::ranges::any_of(
                 running_error_cards,
                 [](const std::string& line) { return line.find("\x1b[38;2;248;113;113m[x]") != std::string::npos; }),
         "tui emits trusted sgr status colors for running and error tool cards");

  const auto tabbed = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "tab\tstatus",
                                 .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "tab\ttext"}},
                                 .width = 30,
                                 .height = 8});
  expect(std::ranges::none_of(tabbed, [](const std::string& line) { return line.find('\t') != std::string::npos; }) &&
             std::ranges::all_of(tabbed, [](const std::string& line) { return visible_columns(line) <= 30; }),
         "tui expands tabs before rendering width-bounded lines");

  std::string exact_width_utf8_status;
  for (int index = 0; index < 12; ++index) {
    exact_width_utf8_status += "\xC3\xA9";
  }
  const auto exact_width_utf8 = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                     .provider = "openai",
                                                                                     .model = "gpt-5.5",
                                                                                     .session_id = "session_test",
                                                                                     .input = "",
                                                                                     .status = exact_width_utf8_status,
                                                                                     .transcript = {},
                                                                                     .width = 20,
                                                                                     .height = 8});
  expect(std::ranges::all_of(exact_width_utf8, [](const std::string& line) { return visible_columns(line) <= 20; }) &&
             std::ranges::any_of(
                 exact_width_utf8,
                 [](const std::string& line) { return strip_sgr(line).find("▎  [build]") != std::string::npos; }),
         "tui width fitting preserves the old AVA composer surface at minimum width");

  const auto utf8 = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = std::string(13, 'x') + "\xC3\xA9" + "zzz"}},
      .width = 20,
      .height = 8});
  expect(std::ranges::none_of(utf8,
                              [](const std::string& line) {
                                return !line.empty() && (static_cast<unsigned char>(line.back()) & 0xC0U) == 0xC0U;
                              }),
         "tui truncation does not leave a trailing utf-8 starter byte");
}

}  // namespace

int main() {
  test_mode_parsing();
  test_session_store_round_trip();
  test_session_resume_and_listing();
  test_session_compaction_entry_round_trip();
  test_session_markdown_export();
  test_compaction_config_and_thresholds();
  test_compaction_context_reconstruction();
  test_json_escape_control_characters();
  test_core_json_top_level_lookup();
  test_permission_defaults();
  test_command_classification();
  test_file_tools();
  test_permission_audit_persistence();
  test_search_tools();
  test_bash_tool();
  test_xdg_paths();
  test_context_loader();
  test_auth_load_and_store();
  test_openai_oauth_helpers();
  test_openai_oauth_refresh();
  test_model_and_prompt_config();
  test_app_event_serialization();
  test_app_runtime_open_session_and_context_prompt();
  test_app_run_prompt_emits_events();
  test_app_run_prompt_event_sink_failure_cancels_before_next_provider_call();
  test_app_print_prompt_merging();
  test_headless_permission_policy();
  test_app_print_text_mode_outputs_final_text_only();
  test_app_print_text_mode_reports_stdout_write_failure();
  test_app_print_mode_uses_headless_permission_policy();
  test_app_print_mode_refreshes_expired_oauth_before_provider_request();
  test_app_print_json_mode_outputs_runtime_events();
  test_app_command_dispatcher();
  test_app_rpc_parsing_and_response_serialization();
  test_app_rpc_prompt_with_fake_transport_streams_events();
  test_app_rpc_prompt_refreshes_expired_oauth_before_provider_request();
  test_app_rpc_malformed_line_recovery_and_unknown_command();
  test_app_rpc_state_list_sessions_and_open_session();
  test_app_rpc_command_responses_for_context_compact_export();
  test_app_rpc_cancel_affects_subsequent_prompt();
  test_openai_provider_contract();
  test_tool_dispatcher();
  test_tool_dispatcher_plan_mode_denies_mutation();
  test_agent_loop_text_only_turn();
  test_agent_loop_tool_turn_and_continuation();
  test_agent_loop_permission_resolver_threads_to_tools();
  test_agent_loop_question_resolver_threads_to_tools();
  test_agent_loop_non_stream_response();
  test_agent_loop_compaction_status_metadata();
  test_agent_loop_cancellation_boundaries();
  test_agent_loop_error_paths_and_bounds();
  test_agent_loop_multiple_tools_and_denied_continuation();
  test_agent_loop_tool_delta_dedupes_and_rejects_empty_tool_ids();
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
