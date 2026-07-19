#include "sys.h"
#include "tests/support/fake_transport.h"
#include "tests/support/test_harness.h"
#include "ava/app/commands.h"
#include "ava/app/events.h"
#include "ava/app/headless_policy.h"
#include "ava/app/print_mode.h"
#include "ava/app/rpc_mode.h"
#include "ava/app/runtime.h"
#include "ava/agent/agent_loop.h"
#include "ava/agent/message_builder.h"
#include "ava/agent/mode.h"
#include "ava/agent/tool_dispatcher.h"
#include "ava/tools/bash_tool.h"
#include "ava/tools/file_tools.h"
#include "ava/tools/search_tools.h"
#include "ava/tui/composer.h"
#include "ava/tui/terminal.h"
#include "ava/config/auth.h"
#include "ava/config/model_config.h"
#include "ava/config/openai_oauth.h"
#include "ava/config/prompt_config.h"
#include "ava/config/xdg_paths.h"
#include "ava/session/assistant_output.h"
#include "ava/session/attachments.h"
#include "ava/session/compaction.h"
#include "ava/session/export.h"
#include "ava/session/logical_projection.h"
#include "ava/session/record.h"
#include "ava/session/session_branch.h"
#include "ava/session/session_store.h"
#include "ava/session/session_tree.h"
#include "ava/session/stats.h"
#include "ava/session/transcript.h"
#include "ava/session/validation.h"
#include "ava/session/validation_fields.h"
#include "ava/permissions/permission.h"
#include "ava/provider/anthropic_provider.h"
#include "ava/provider/openai_provider.h"
#include "ava/provider/provider_utils.h"
#include "ava/context/context_loader.h"
#include "ava/core/ids.h"
#include "ava/core/json.h"

#include <algorithm>
#include <barrier>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>

namespace {

bool has_replay_issue(ava::session::SessionReplayValidation const& validation, ava::session::SessionReplayIssueKind kind)
{
  return std::ranges::any_of(validation.issues, [kind](ava::session::SessionReplayIssue const& issue) { return issue.kind == kind; });
}

std::string tiny_png_bytes()
{
  std::string bytes;
  bytes.push_back(static_cast<char>(0x89));
  bytes += "PNG\r\n";
  bytes.push_back(static_cast<char>(0x1A));
  bytes += "\nava-image";
  return bytes;
}

void write_binary_file(std::filesystem::path const& path, std::string_view bytes)
{
  std::filesystem::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::string read_binary_file(std::filesystem::path const& path)
{
  std::ifstream file(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

void test_session_store_round_trip()
{
  std::error_code remove_error;
  std::filesystem::remove_all(temp_root(), remove_error);

  auto store = ava::session::SessionStore::create(std::filesystem::current_path(), temp_root());
  expect(store.has_value(), "session store creates");
  if (!store)
    return;

  auto append = append_session_entry_for_test(*store, ava::session::SessionEntry{
                                                          .id = "entry_1",
                                                          .parent_id = "",
                                                          .type = ava::session::EntryType::SessionStart,
                                                          .timestamp = "2026-04-27T00:00:00Z",
                                                          .data_json = "{\"mode\":\"build\"}",
                                                      });
  expect(append.has_value(), "session entry appends");

  struct stat session_stat{};
  if (stat(store->session_path().c_str(), &session_stat) == 0)
  {
    expect((session_stat.st_mode & 0777) == 0600, "session file is owner read/write only");
  }
  struct stat session_dir_stat{};
  auto const session_dir = store->session_path().parent_path();
  if (stat(session_dir.c_str(), &session_dir_stat) == 0)
  {
    expect((session_dir_stat.st_mode & 0777) == 0700, "session directory is owner-only");
  }

  auto loaded = store->load();
  expect(loaded.has_value(), "session entries load");
  if (!loaded)
    return;

  expect(loaded->size() == 1, "one entry loaded");
  expect((*loaded)[0].id == "entry_1", "entry id round trips");
  expect((*loaded)[0].type == ava::session::EntryType::SessionStart, "entry type round trips");
  expect((*loaded)[0].version == ava::session::kCurrentSessionEntryVersion, "session entry version round trips from current JSONL records");

  auto text_store = ava::session::SessionStore::create(std::filesystem::current_path(), temp_root());
  expect(text_store.has_value(), "text session store creates");
  if (!text_store)
    return;
  auto const text_append = append_session_entry_for_test(*text_store, ava::session::SessionEntry{
                                                                          .id = "entry_2\n",
                                                                          .parent_id = "",
                                                                          .type = ava::session::EntryType::UserMessage,
                                                                          .timestamp = "2026-04-27T00:00:00Z",
                                                                          .data_json = "{\"text\":\"hello\"}",
                                                                      });
  expect(text_append.has_value(), "session entry with escaped newline appends");
  auto text_loaded = text_store->load();
  expect(text_loaded && !text_loaded->empty() && (*text_loaded)[0].id == "entry_2\n", "session escaped strings round trip");

  auto const raw_newline_append = append_session_entry_for_test(*text_store, ava::session::SessionEntry{
                                                                                 .id = "entry_raw_newline",
                                                                                 .parent_id = "",
                                                                                 .type = ava::session::EntryType::UserMessage,
                                                                                 .timestamp = "2026-04-27T00:00:00Z",
                                                                                 .data_json = "{\"text\":\"bad\nsplit\"}",
                                                                             });
  expect(!raw_newline_append, "session data_json rejects raw newlines to preserve JSONL entries");

  auto const raw_carriage_return_append = append_session_entry_for_test(*text_store, ava::session::SessionEntry{
                                                                                         .id = "entry_raw_carriage_return",
                                                                                         .parent_id = "",
                                                                                         .type = ava::session::EntryType::UserMessage,
                                                                                         .timestamp = "2026-04-27T00:00:00Z",
                                                                                         .data_json = "{\"text\":\"bad\rsplit\"}",
                                                                                     });
  expect(!raw_carriage_return_append, "session data_json rejects raw carriage returns to preserve JSONL entries");

  auto large_store = ava::session::SessionStore::create(std::filesystem::current_path(), temp_root());
  expect(large_store.has_value(), "large session store creates");
  if (large_store)
  {
    auto const large_append = append_session_entry_for_test(*large_store, ava::session::SessionEntry{
                                                                              .id = std::string(600000, '"'),
                                                                              .parent_id = "",
                                                                              .type = ava::session::EntryType::UserMessage,
                                                                              .timestamp = "2026-04-27T00:00:00Z",
                                                                              .data_json = "{\"text\":\"hello\"}",
                                                                          });
    expect(!large_append, "session append rejects entries whose escaped JSONL line is too large");
    auto large_loaded = large_store->load();
    expect(large_loaded && large_loaded->empty(), "oversized rejected session entry does not corrupt later loads");
  }

  ava::session::SessionStore oversized_load_store(ava::session::SessionStoreOptions{
      .root_dir = temp_root(),
      .workspace_dir = std::filesystem::current_path(),
      .session_id = "oversized-load",
  });
  std::filesystem::create_directories(oversized_load_store.session_path().parent_path());
  {
    std::ofstream file(oversized_load_store.session_path(), std::ios::binary | std::ios::trunc);
    file << std::string(1024 * 1024, '{') << '\n';
  }
  expect(!oversized_load_store.load(), "session load rejects oversized JSONL lines without parsing them");

  std::vector<std::string> const bad_session_ids = {
      "", ".", "..", "/", "\\", "../escape", "with/slash", "with\\slash", std::string("bad\0id", 6), std::string("bad\x1Fid", 6)};
  for (auto const& bad_session_id : bad_session_ids)
  {
    ava::session::SessionStore bad_store(ava::session::SessionStoreOptions{
        .root_dir = temp_root(),
        .workspace_dir = std::filesystem::current_path(),
        .session_id = bad_session_id,
    });
    auto const bad_append = append_session_entry_for_test(bad_store, ava::session::SessionEntry{
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
  auto const attempted_traversal_path = traversal_store.session_path().lexically_normal();
  std::error_code cleanup_error;
  std::filesystem::remove(attempted_traversal_path, cleanup_error);
  auto const traversal_append = append_session_entry_for_test(traversal_store, ava::session::SessionEntry{
                                                                                   .id = "entry_traversal",
                                                                                   .parent_id = "",
                                                                                   .type = ava::session::EntryType::UserMessage,
                                                                                   .timestamp = "2026-04-27T00:00:00Z",
                                                                                   .data_json = "{\"text\":\"hello\"}",
                                                                               });
  expect(!traversal_append && !std::filesystem::exists(attempted_traversal_path), "session traversal id append is rejected before creating attempted path");

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
  expect(unicode_loaded && !unicode_loaded->empty() && (*unicode_loaded)[0].id == std::string("entry_\xC3\xA9"), "session unicode escapes decode as utf-8");

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
  expect(surrogate_loaded && !surrogate_loaded->empty() && (*surrogate_loaded)[0].id == std::string("entry_\xF0\x9D\x84\x9E"),
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
  expect(bad_surrogate_loaded && !bad_surrogate_loaded->empty() && (*bad_surrogate_loaded)[0].id == std::string("entry_\xEF\xBF\xBD"),
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
  expect(nested_key_loaded && !nested_key_loaded->empty() && (*nested_key_loaded)[0].id == "entry_safe" && (*nested_key_loaded)[0].parent_id == "parent_safe" &&
             (*nested_key_loaded)[0].type == ava::session::EntryType::UserMessage && (*nested_key_loaded)[0].timestamp == "2026-04-27T00:00:00Z",
         "session load ignores key-looking strings and nested data keys");

  ava::session::SessionStore missing_version_store(ava::session::SessionStoreOptions{
      .root_dir = temp_root(),
      .workspace_dir = std::filesystem::current_path(),
      .session_id = "missing-version",
  });
  std::filesystem::create_directories(missing_version_store.session_path().parent_path());
  {
    std::ofstream file(missing_version_store.session_path(), std::ios::binary | std::ios::trunc);
    file << "{\"id\":\"entry_legacy\",\"parent_id\":\"\",\"type\":\"user_message\","
            "\"timestamp\":\"2026-04-27T00:00:00Z\",\"data\":{\"text\":\"hello\"}}\n";
  }
  auto missing_version_loaded = missing_version_store.load();
  expect(missing_version_loaded && missing_version_loaded->size() == 1 && (*missing_version_loaded)[0].id == "entry_legacy" &&
             (*missing_version_loaded)[0].version == 0,
         "session loader treats missing entry version as legacy-compatible");

  ava::session::SessionStore future_version_store(ava::session::SessionStoreOptions{
      .root_dir = temp_root(),
      .workspace_dir = std::filesystem::current_path(),
      .session_id = "future-version",
  });
  std::filesystem::create_directories(future_version_store.session_path().parent_path());
  {
    std::ofstream file(future_version_store.session_path(), std::ios::binary | std::ios::trunc);
    file << "{\"version\":" << (ava::session::kCurrentSessionEntryVersion + 1)
         << ",\"id\":\"entry_future\",\"parent_id\":\"\",\"type\":\"user_message\","
            "\"timestamp\":\"2026-04-27T00:00:00Z\",\"data\":{\"text\":\"hello\"}}\n";
  }
  auto future_version_loaded = future_version_store.load();
  expect(!future_version_loaded && future_version_loaded.error().category() == ava::core::ErrorCategory::Session,
         "session loader rejects unsupported future entry versions");

  ava::session::SessionStore bad_parent_store(ava::session::SessionStoreOptions{
      .root_dir = temp_root(),
      .workspace_dir = std::filesystem::current_path(),
      .session_id = "bad-parent",
  });
  std::filesystem::create_directories(bad_parent_store.session_path().parent_path());
  {
    std::ofstream file(bad_parent_store.session_path(), std::ios::binary | std::ios::trunc);
    file << "{\"version\":1,\"id\":\"entry_bad_parent\",\"parent_id\":\"../escape\","
            "\"type\":\"user_message\",\"timestamp\":\"2026-04-27T00:00:00Z\","
            "\"data\":{\"text\":\"hello\"}}\n";
  }
  auto bad_parent_loaded = bad_parent_store.load();
  expect(!bad_parent_loaded && bad_parent_loaded.error().category() == ava::core::ErrorCategory::InvalidArgument,
         "session loader rejects unsafe parent_id values without requiring parent existence");

  auto invalid_parent_append = append_session_entry_for_test(*text_store, ava::session::SessionEntry{.id = "entry_invalid_parent",
                                                                                                     .parent_id = "bad/parent",
                                                                                                     .type = ava::session::EntryType::UserMessage,
                                                                                                     .timestamp = "2026-04-27T00:00:00Z",
                                                                                                     .data_json = "{\"text\":\"hello\"}"});
  expect(!invalid_parent_append, "session append rejects unsafe parent_id values");
}

void test_bounded_session_reads_strictly_classify_framed_records()
{
  auto const root = temp_root() / "bounded-strict-session-records";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const sessions = root / "sessions";
  std::filesystem::create_directories(workspace);

  std::string deep_record =
      "{\"version\":3,\"id\":\"deep\",\"parent_id\":\"\",\"type\":\"user_message\",\"timestamp\":\"2026-07-14T00:00:00Z\",\"data\":{\"x\":";
  deep_record += std::string(70, '[') + "0" + std::string(70, ']') + "}}";
  std::vector<std::pair<std::string, std::string>> const records = {
      {"duplicate",
       "{\"version\":3,\"id\":\"first\",\"id\":\"second\",\"parent_id\":\"\",\"type\":\"user_message\","
       "\"timestamp\":\"2026-07-14T00:00:00Z\",\"data\":{}}"},
      {"nesting", std::move(deep_record)},
  };

  for (auto const& [kind, record] : records)
  {
    ava::session::SessionStore store(ava::session::SessionStoreOptions{.root_dir = sessions, .workspace_dir = workspace, .session_id = "strict_" + kind});
    write_binary_file(store.session_path(), record + "\n");
    auto loaded = store.load_bounded(ava::session::SessionReadLimits{});
    std::size_t visited_entries = 0;
    auto visited = store.visit_entries(ava::session::SessionReadLimits{}, [&](ava::session::SessionEntry const&) -> ava::core::Result<bool> {
      ++visited_entries;
      return true;
    });
    expect(!loaded && !visited && visited_entries == 0 && loaded.error().message().find(kind) != std::string::npos &&
               visited.error().message().find(kind) != std::string::npos,
           "bounded loads and visits reject newline-terminated " + kind + " JSON before permissive session parsing");
  }
}

void test_ephemeral_session_store_stays_in_memory()
{
  auto const workspace = temp_root() / "ephemeral-session-store" / "workspace";
  std::filesystem::create_directories(workspace);

  std::filesystem::path scratch_root;
  std::filesystem::path session_path;
  {
    auto store = ava::session::SessionStore::create_ephemeral(workspace);
    expect(store.has_value() && store->is_ephemeral(), "ephemeral session store creates in no-session mode");
    if (!store)
      return;

    session_path = store->session_path();
    scratch_root = session_path.parent_path().parent_path();
    auto append = append_session_entry_for_test(*store, ava::session::SessionEntry{
                                                            .id = "entry_ephemeral",
                                                            .parent_id = "",
                                                            .type = ava::session::EntryType::UserMessage,
                                                            .timestamp = "2026-04-27T00:00:00Z",
                                                            .data_json = "{\"text\":\"hello\"}",
                                                        });
    expect(append.has_value(), "ephemeral session store accepts valid entries");
    expect(!std::filesystem::exists(session_path), "ephemeral session store does not create a JSONL history file");

    auto loaded = store->load();
    expect(loaded && loaded->size() == 1 && (*loaded)[0].id == "entry_ephemeral", "ephemeral session store reloads entries from memory");

    auto imported = ava::session::import_image_attachment_bytes(*store, tiny_png_bytes(), std::string_view("image/png"));
    expect(imported.has_value(), "ephemeral session store supports temp-only attachment storage");
    if (imported)
    {
      auto loaded_attachment = ava::session::load_image_attachment(*store, *imported);
      expect(loaded_attachment && loaded_attachment->bytes == tiny_png_bytes(), "ephemeral session attachments reload while the store is alive");
    }
    expect(!std::filesystem::exists(session_path), "ephemeral attachments do not create a JSONL history file");
  }

  expect(!scratch_root.empty() && !std::filesystem::exists(scratch_root), "ephemeral session scratch directory is removed when the store is released");
}

void test_session_record_round_trip()
{
  ava::session::SessionEntry const original{.id = "entry_\n",
                                            .parent_id = "parent_id",
                                            .type = ava::session::EntryType::ToolResult,
                                            .timestamp = "2026-04-27T00:00:00Z",
                                            .data_json = "{\"text\":\"hello\",\"nested\":{\"ok\":true}}"};

  auto line = ava::session::serialize_session_entry_line(original);
  auto const current_version_text = std::string("\"version\":") + std::to_string(ava::session::kCurrentSessionEntryVersion);
  expect(line && line->find('\n') == std::string::npos && line->find(current_version_text) != std::string::npos,
         line ? "session record serializer emits one current-version JSONL record"
              : "session record serializer emits one current-version JSONL record: " + line.error().format());
  if (!line)
    return;

  auto parsed = ava::session::parse_session_entry_line(*line, "/tmp/session.jsonl");
  expect(parsed && parsed->id == original.id && parsed->parent_id == original.parent_id && parsed->type == original.type &&
             parsed->timestamp == original.timestamp && parsed->data_json == original.data_json && parsed->version == ava::session::kCurrentSessionEntryVersion,
         parsed ? "session record parser round-trips serialized entries" : "session record parser round-trips serialized entries: " + parsed.error().format());

  auto legacy = ava::session::parse_session_entry_line(
      "{\"version\":1,\"id\":\"entry_\\uD834\\uDD1E\",\"parent_id\":\"\",\"type\":\"user_message\","
      "\"timestamp\":\"2026-04-27T00:00:00Z\",\"data\":{\"text\":\"hello\"}}",
      "/tmp/session.jsonl");
  expect(legacy && legacy->id == std::string("entry_\xF0\x9D\x84\x9E") && legacy->version == 1,
         legacy ? "session record parser decodes legacy unicode escapes" : "session record parser decodes legacy unicode escapes: " + legacy.error().format());

  auto unsupported = ava::session::parse_session_entry_line(
      "{\"version\":999,\"id\":\"entry\",\"parent_id\":\"\",\"type\":\"user_message\","
      "\"timestamp\":\"2026-04-27T00:00:00Z\",\"data\":{\"text\":\"hello\"}}",
      "/tmp/session.jsonl");
  expect(!unsupported && ava::session::is_unsupported_session_version_error(unsupported.error()),
         "session record parser flags unsupported versions explicitly");

  auto malformed = ava::session::parse_session_entry_line(
      "{\"version\":2,\"id\":\"entry\",\"parent_id\":\"bad/slash\",\"type\":\"user_message\","
      "\"timestamp\":\"2026-04-27T00:00:00Z\",\"data\":{\"text\":\"hello\"}}",
      "/tmp/session.jsonl");
  expect(!malformed && malformed.error().format().find("parent_id") != std::string::npos, "session record parser rejects unsafe parent ids");

  std::string invalid_utf8_record =
      "{\"version\":2,\"id\":\"entry\",\"parent_id\":\"\",\"type\":\"user_message\",\"timestamp\":\"2026-04-27T00:00:00Z\",\"data\":{\"text\":\"";
  invalid_utf8_record.push_back(static_cast<char>(0xFF));
  invalid_utf8_record += "\"}}";
  auto invalid_utf8 = ava::session::parse_session_entry_line(invalid_utf8_record, "/tmp/session.jsonl");
  expect(!invalid_utf8 && invalid_utf8.error().message() == "session entry is not valid UTF-8",
         "session persistence parsing rejects invalid UTF-8 before semantic field extraction");

  auto too_large = ava::session::serialize_session_entry_line(ava::session::SessionEntry{
      .id = std::string(600000, '"'),
      .parent_id = "",
      .type = ava::session::EntryType::UserMessage,
      .timestamp = "2026-04-27T00:00:00Z",
      .data_json = "{\"text\":\"hello\"}",
  });
  expect(!too_large, "session record serializer rejects oversized records before append");
}

void test_session_tree_metadata_entries_validate_and_export()
{
  std::vector<ava::session::SessionEntry> const entries = {
      ava::session::SessionEntry{.id = "entry_metadata",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::SessionMetadata,
                                 .timestamp = "2026-04-27T00:00:00Z",
                                 .data_json = "{\"schema_version\":1,\"name\":\"Investigate auth flow\","
                                              "\"labels\":[\"bug\",\"auth\"],\"archived\":true,\"branch_origin\":\"root\","
                                              "\"actor\":\"auditor\"}"},
      ava::session::SessionEntry{.id = "entry_branch_summary",
                                 .parent_id = "entry_metadata",
                                 .type = ava::session::EntryType::BranchSummary,
                                 .timestamp = "2026-04-27T00:00:01Z",
                                 .data_json = "{\"schema_version\":1,\"source_session_id\":\"session_parent\","
                                              "\"branch_root_entry_id\":\"entry_metadata\","
                                              "\"branch_tip_entry_id\":\"entry_metadata\","
                                              "\"summary\":\"Branch tested the auth hypothesis.\","
                                              "\"provider\":\"openai\",\"model\":\"gpt-test\","
                                              "\"reason\":\"test\"}"},
  };

  auto const validation = ava::session::validate_session_replay(entries);
  expect(validation.ok(), "session tree metadata and branch summary entries are replay-valid");
  auto const metadata = ava::session::session_metadata_from_entries(entries);
  expect(metadata && metadata->actor == "auditor" && metadata->archived && metadata->labels_updated == "2026-04-27T00:00:00Z",
         "session metadata read-back exposes persisted actor, archive state, and label update time");

  auto const metadata_type = ava::session::parse_entry_type("session_metadata");
  auto const summary_type = ava::session::parse_entry_type("branch_summary");
  expect(metadata_type && *metadata_type == ava::session::EntryType::SessionMetadata && ava::session::to_string(*metadata_type) == "session_metadata" &&
             summary_type && *summary_type == ava::session::EntryType::BranchSummary && ava::session::to_string(*summary_type) == "branch_summary",
         "session tree entry types parse and serialize by stable names");

  auto const exported = ava::session::format_session_markdown(entries, ava::session::ExportOptions{});
  expect(exported.find("Session Metadata") != std::string::npos && exported.find("Branch Summary") != std::string::npos &&
             exported.find("Branch tested the auth hypothesis.") != std::string::npos,
         "session export includes tree metadata and branch summaries");

  auto invalid_metadata = entries;
  invalid_metadata[0].data_json = "{\"schema_version\":1,\"labels\":[\"dup\",\"dup\"]}";
  auto const invalid_metadata_validation = ava::session::validate_session_replay(invalid_metadata);
  expect(!invalid_metadata_validation.ok() && has_replay_issue(invalid_metadata_validation, ava::session::SessionReplayIssueKind::InvalidSessionMetadataEntry),
         "session replay validator rejects malformed tree metadata entries");

  auto invalid_summary = entries;
  invalid_summary[1].data_json = "{\"schema_version\":1,\"summary\":\"\"}";
  auto const invalid_summary_validation = ava::session::validate_session_replay(invalid_summary);
  expect(!invalid_summary_validation.ok() && has_replay_issue(invalid_summary_validation, ava::session::SessionReplayIssueKind::InvalidBranchSummaryEntry),
         "session replay validator rejects malformed branch summary entries");

  auto expect_invalid_branch_summary = [&](std::string data_json, std::string const& message) {
    auto invalid = entries;
    invalid[1].data_json = std::move(data_json);
    auto const validation = ava::session::validate_session_replay(invalid);
    expect(!validation.ok() && has_replay_issue(validation, ava::session::SessionReplayIssueKind::InvalidBranchSummaryEntry), message);
  };
  expect_invalid_branch_summary("{\"schema_version\":1,\"summary\":\"Missing provider\",\"model\":\"gpt-test\",\"reason\":\"test\"}",
                                "session replay validator rejects branch summaries missing provider");
  expect_invalid_branch_summary("{\"schema_version\":1,\"summary\":\"Missing model\",\"provider\":\"openai\",\"reason\":\"test\"}",
                                "session replay validator rejects branch summaries missing model");
  expect_invalid_branch_summary("{\"schema_version\":1,\"summary\":\"Missing reason\",\"provider\":\"openai\",\"model\":\"gpt-test\"}",
                                "session replay validator rejects branch summaries missing reason");
  expect_invalid_branch_summary(
      "{\"schema_version\":1,\"summary\":\"Missing source\",\"branch_root_entry_id\":\"entry_metadata\","
      "\"branch_tip_entry_id\":\"entry_metadata\",\"provider\":\"openai\",\"model\":\"gpt-test\",\"reason\":\"test\"}",
      "session replay validator rejects branch summaries missing source_session_id");
  expect_invalid_branch_summary(
      "{\"schema_version\":1,\"summary\":\"Missing root\",\"source_session_id\":\"session_parent\","
      "\"branch_tip_entry_id\":\"entry_metadata\",\"provider\":\"openai\",\"model\":\"gpt-test\",\"reason\":\"test\"}",
      "session replay validator rejects branch summaries missing branch_root_entry_id");
  expect_invalid_branch_summary(
      "{\"schema_version\":1,\"summary\":\"Missing tip\",\"source_session_id\":\"session_parent\","
      "\"branch_root_entry_id\":\"entry_metadata\",\"provider\":\"openai\",\"model\":\"gpt-test\",\"reason\":\"test\"}",
      "session replay validator rejects branch summaries missing branch_tip_entry_id");
  expect_invalid_branch_summary(
      "{\"schema_version\":1,\"summary\":\"Missing referenced tip\",\"source_session_id\":\"session_parent\","
      "\"branch_root_entry_id\":\"entry_metadata\",\"branch_tip_entry_id\":\"missing\","
      "\"provider\":\"openai\",\"model\":\"gpt-test\",\"reason\":\"test\"}",
      "session replay validator rejects branch summaries with dangling tip references");
  expect_invalid_branch_summary(
      "{\"schema_version\":1,\"summary\":\"Self referenced tip\",\"source_session_id\":\"session_parent\","
      "\"branch_root_entry_id\":\"entry_metadata\",\"branch_tip_entry_id\":\"entry_branch_summary\","
      "\"provider\":\"openai\",\"model\":\"gpt-test\",\"reason\":\"test\"}",
      "session replay validator rejects branch summaries that point at the summary entry itself");
  auto inverted_summary_entries = std::vector<ava::session::SessionEntry>{
      ava::session::SessionEntry{.id = "entry_first",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::UserMessage,
                                 .timestamp = "2026-04-27T00:00:00Z",
                                 .data_json = "{\"text\":\"first\"}"},
      ava::session::SessionEntry{.id = "entry_second",
                                 .parent_id = "entry_first",
                                 .type = ava::session::EntryType::UserMessage,
                                 .timestamp = "2026-04-27T00:00:01Z",
                                 .data_json = "{\"text\":\"second\"}"},
      ava::session::SessionEntry{.id = "entry_inverted_summary",
                                 .parent_id = "entry_second",
                                 .type = ava::session::EntryType::BranchSummary,
                                 .timestamp = "2026-04-27T00:00:02Z",
                                 .data_json = "{\"schema_version\":1,\"summary\":\"Inverted range\",\"source_session_id\":\"session_parent\","
                                              "\"branch_root_entry_id\":\"entry_second\",\"branch_tip_entry_id\":\"entry_first\","
                                              "\"provider\":\"openai\",\"model\":\"gpt-test\",\"reason\":\"test\"}"}};
  auto const inverted_summary_validation = ava::session::validate_session_replay(inverted_summary_entries);
  expect(!inverted_summary_validation.ok() && has_replay_issue(inverted_summary_validation, ava::session::SessionReplayIssueKind::InvalidBranchSummaryEntry),
         "session replay validator rejects branch summaries with inverted root/tip ranges");
  expect_invalid_branch_summary(
      "{\"schema_version\":1,\"summary\":\"Bad actor\",\"source_session_id\":\"session_parent\","
      "\"branch_root_entry_id\":\"entry_metadata\",\"branch_tip_entry_id\":\"entry_metadata\","
      "\"provider\":\"openai\",\"model\":\"gpt-test\",\"reason\":\"test\",\"actor\":123}",
      "session replay validator rejects malformed branch summary actor");
  expect_invalid_branch_summary(
      "{\"schema_version\":1,\"summary\":\"Bad provider\",\"source_session_id\":\"session_parent\","
      "\"branch_root_entry_id\":\"entry_metadata\",\"branch_tip_entry_id\":\"entry_metadata\","
      "\"provider\":\"bad\\u001b\",\"model\":\"gpt-test\",\"reason\":\"test\"}",
      "session replay validator rejects malformed branch summary provider text");
  expect_invalid_branch_summary(
      "{\"schema_version\":1,\"summary\":\"Bad\\u001bsummary\",\"source_session_id\":\"session_parent\","
      "\"branch_root_entry_id\":\"entry_metadata\",\"branch_tip_entry_id\":\"entry_metadata\","
      "\"provider\":\"openai\",\"model\":\"gpt-test\",\"reason\":\"test\"}",
      "session replay validator rejects malformed branch summary text");
  expect_invalid_branch_summary(
      "{\"schema_version\":1,\"summary\":\"Bad reason\",\"source_session_id\":\"session_parent\","
      "\"branch_root_entry_id\":\"entry_metadata\",\"branch_tip_entry_id\":\"entry_metadata\","
      "\"provider\":\"openai\",\"model\":\"gpt-test\",\"reason\":\"" +
          std::string(1025, 'x') + "\"}",
      "session replay validator rejects oversized branch summary reason");

  auto empty_origin_with_name = entries;
  empty_origin_with_name[0].data_json = "{\"schema_version\":1,\"name\":\"Named\",\"branch_origin\":\"\"}";
  auto const empty_origin_validation = ava::session::validate_session_replay(empty_origin_with_name);
  expect(empty_origin_validation.ok(), "session replay validator treats empty branch_origin as absent");

  auto non_string_origin = entries;
  non_string_origin[0].data_json = "{\"schema_version\":1,\"name\":\"Named\",\"branch_origin\":123}";
  auto const non_string_origin_validation = ava::session::validate_session_replay(non_string_origin);
  expect(
      !non_string_origin_validation.ok() && has_replay_issue(non_string_origin_validation, ava::session::SessionReplayIssueKind::InvalidSessionMetadataEntry),
      "session replay validator rejects non-string branch_origin values");

  auto non_bool_archived = entries;
  non_bool_archived[0].data_json = "{\"schema_version\":1,\"name\":\"Named\",\"archived\":\"yes\"}";
  auto const non_bool_archived_validation = ava::session::validate_session_replay(non_bool_archived);
  expect(
      !non_bool_archived_validation.ok() && has_replay_issue(non_bool_archived_validation, ava::session::SessionReplayIssueKind::InvalidSessionMetadataEntry),
      "session replay validator rejects non-boolean archived metadata values");

  auto only_empty_origin = entries;
  only_empty_origin[0].data_json = "{\"schema_version\":1,\"branch_origin\":\"\"}";
  auto const only_empty_origin_validation = ava::session::validate_session_replay(only_empty_origin);
  expect(
      !only_empty_origin_validation.ok() && has_replay_issue(only_empty_origin_validation, ava::session::SessionReplayIssueKind::InvalidSessionMetadataEntry),
      "session replay validator rejects metadata entries with no meaningful fields");

  auto unsupported_metadata = entries;
  unsupported_metadata[0].data_json = "{\"schema_version\":2,\"name\":\"future\"}";
  auto const unsupported_metadata_validation = ava::session::validate_session_replay(unsupported_metadata);
  expect(!unsupported_metadata_validation.ok() &&
             has_replay_issue(unsupported_metadata_validation, ava::session::SessionReplayIssueKind::InvalidSessionMetadataEntry),
         "session replay validator rejects unsupported session_metadata schema_version values");

  auto unsupported_summary = entries;
  unsupported_summary[1].data_json =
      "{\"schema_version\":2,\"summary\":\"future\","
      "\"source_session_id\":\"session_parent\",\"branch_root_entry_id\":\"entry_metadata\","
      "\"branch_tip_entry_id\":\"entry_metadata\",\"provider\":\"openai\",\"model\":\"gpt-test\","
      "\"reason\":\"test\"}";
  auto const unsupported_summary_validation = ava::session::validate_session_replay(unsupported_summary);
  expect(
      !unsupported_summary_validation.ok() && has_replay_issue(unsupported_summary_validation, ava::session::SessionReplayIssueKind::InvalidBranchSummaryEntry),
      "session replay validator rejects unsupported branch_summary schema_version values");
}

void test_session_tree_index_derives_branches()
{
  auto const root = temp_root() / "session-tree-index";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const sessions_dir = root / "sessions";
  std::filesystem::create_directories(workspace);

  auto make_store = [&](std::string session_id) {
    return ava::session::SessionStore(
        ava::session::SessionStoreOptions{.root_dir = sessions_dir, .workspace_dir = workspace, .session_id = std::move(session_id)});
  };
  auto append_start = [](ava::session::SessionStore& store, std::string_view entry_id) {
    return append_session_entry_for_test(store, ava::session::SessionEntry{.id = std::string(entry_id),
                                                                           .parent_id = "",
                                                                           .type = ava::session::EntryType::SessionStart,
                                                                           .timestamp = "2026-04-27T00:00:00Z",
                                                                           .data_json = "{\"mode\":\"build\"}"});
  };

  auto root_store = make_store("session_root");
  auto child_store = make_store("session_child");
  auto grandchild_store = make_store("session_grandchild");
  auto orphan_store = make_store("session_orphan");
  auto corrupt_metadata_store = make_store("session_corrupt_metadata");
  expect(append_start(root_store, "entry_root_start").has_value() && append_start(child_store, "entry_child_start").has_value() &&
             append_start(grandchild_store, "entry_grandchild_start").has_value() && append_start(orphan_store, "entry_orphan_start").has_value() &&
             append_start(corrupt_metadata_store, "entry_corrupt_start").has_value(),
         "session tree index test creates session files");

  ava::session::SessionMetadataUpdate root_metadata;
  root_metadata.name = "Root";
  root_metadata.labels = std::vector<std::string>{"root"};
  root_metadata.branch_origin = "root";
  root_metadata.actor = "test";
  auto root_meta = append_session_metadata_for_test(root_store, std::move(root_metadata));

  ava::session::SessionMetadataUpdate child_metadata;
  child_metadata.name = "Child";
  child_metadata.labels = std::vector<std::string>{"branch"};
  child_metadata.parent_session_id = "session_root";
  child_metadata.source_session_id = "session_root";
  child_metadata.branch_from_entry_id = "entry_root_start";
  child_metadata.branch_origin = "fork";
  child_metadata.actor = "test";
  auto child_meta = append_session_metadata_for_test(child_store, std::move(child_metadata));

  ava::session::SessionMetadataUpdate grandchild_metadata;
  grandchild_metadata.name = "Grandchild";
  grandchild_metadata.parent_session_id = "session_child";
  grandchild_metadata.source_session_id = "session_child";
  grandchild_metadata.branch_from_entry_id = "entry_child_start";
  grandchild_metadata.branch_origin = "clone";
  grandchild_metadata.actor = "test";
  auto grandchild_meta = append_session_metadata_for_test(grandchild_store, std::move(grandchild_metadata));

  ava::session::SessionMetadataUpdate orphan_metadata;
  orphan_metadata.name = "Orphan";
  orphan_metadata.parent_session_id = "session_missing";
  orphan_metadata.branch_origin = "manual";
  orphan_metadata.actor = "test";
  auto orphan_meta = append_session_metadata_for_test(orphan_store, std::move(orphan_metadata));
  auto corrupt_meta = append_session_entry_for_test(corrupt_metadata_store, ava::session::SessionEntry{.id = "entry_corrupt_metadata",
                                                                                                       .parent_id = "entry_corrupt_start",
                                                                                                       .type = ava::session::EntryType::SessionMetadata,
                                                                                                       .timestamp = "2026-04-27T00:00:01Z",
                                                                                                       .data_json = "{\"schema_version\":1,\"name\":123}"});
  expect(root_meta && child_meta && grandchild_meta && orphan_meta && corrupt_meta, "session tree index test persists branch metadata");

  auto tree = ava::session::build_session_tree(workspace, sessions_dir, "session_grandchild");
  expect(tree.has_value(), tree ? "session tree index builds" : "session tree index builds: " + tree.error().format());
  if (!tree)
    return;

  auto contains = [](std::vector<std::string> const& values, std::string_view value) {
    return std::ranges::any_of(values, [value](std::string const& item) { return item == value; });
  };
  auto find_node = [&](std::string_view session_id) -> ava::session::SessionTreeNode const* {
    auto const found =
        std::ranges::find_if(tree->sessions, [session_id](ava::session::SessionTreeNode const& node) { return node.summary.session_id == session_id; });
    return found == tree->sessions.end() ? nullptr : &*found;
  };

  auto const* root_node = find_node("session_root");
  auto const* child_node = find_node("session_child");
  auto const* grandchild_node = find_node("session_grandchild");
  auto const* orphan_node = find_node("session_orphan");
  auto const* corrupt_node = find_node("session_corrupt_metadata");
  expect(root_node != nullptr && child_node != nullptr && grandchild_node != nullptr && orphan_node != nullptr && tree->sessions.size() == 4,
         "session tree index includes valid session summaries and skips malformed metadata sessions");
  expect(corrupt_node == nullptr, "session tree index skips sessions with malformed metadata");
  expect(root_node && root_node->metadata.name == "Root" && contains(root_node->metadata.labels, "root") && root_node->metadata.actor == "test" &&
             contains(root_node->children, "session_child"),
         "session tree index folds root metadata, actor, and direct children");
  expect(child_node && child_node->metadata.parent_session_id == "session_root" && child_node->metadata.source_session_id == "session_root" &&
             child_node->metadata.branch_from_entry_id == "entry_root_start" && child_node->metadata.branch_origin == "fork" &&
             contains(child_node->children, "session_grandchild"),
         "session tree index preserves child provenance metadata");
  expect(grandchild_node && grandchild_node->current && grandchild_node->children.empty() && orphan_node && orphan_node->children.empty(),
         "session tree index marks current and leaf sessions");
  expect(contains(tree->roots, "session_root") && contains(tree->roots, "session_orphan") && !contains(tree->roots, "session_child") &&
             contains(tree->leaves, "session_grandchild") && contains(tree->leaves, "session_orphan") && !contains(tree->leaves, "session_root"),
         "session tree index derives roots and leaves from parent metadata");
  expect(tree->current_path == std::vector<std::string>({"session_root", "session_child", "session_grandchild"}),
         "session tree index derives current branch path without scanning child lists");
}

void test_session_tree_index_handles_parent_cycles()
{
  auto const root = temp_root() / "session-tree-cycle";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const sessions_dir = root / "sessions";
  std::filesystem::create_directories(workspace);

  auto make_store = [&](std::string session_id) {
    return ava::session::SessionStore(
        ava::session::SessionStoreOptions{.root_dir = sessions_dir, .workspace_dir = workspace, .session_id = std::move(session_id)});
  };
  auto append_start = [](ava::session::SessionStore& store, std::string_view entry_id) {
    return append_session_entry_for_test(store, ava::session::SessionEntry{.id = std::string(entry_id),
                                                                           .parent_id = "",
                                                                           .type = ava::session::EntryType::SessionStart,
                                                                           .timestamp = "2026-04-27T00:00:00Z",
                                                                           .data_json = "{\"mode\":\"build\"}"});
  };

  auto first_store = make_store("session_cycle_a");
  auto second_store = make_store("session_cycle_b");
  expect(append_start(first_store, "entry_cycle_a_start").has_value() && append_start(second_store, "entry_cycle_b_start").has_value(),
         "session tree cycle test creates session files");

  ava::session::SessionMetadataUpdate first_metadata;
  first_metadata.parent_session_id = "session_cycle_b";
  first_metadata.branch_origin = "manual";
  first_metadata.actor = "test";
  auto first_meta = append_session_metadata_for_test(first_store, std::move(first_metadata));

  ava::session::SessionMetadataUpdate second_metadata;
  second_metadata.parent_session_id = "session_cycle_a";
  second_metadata.branch_origin = "manual";
  second_metadata.actor = "test";
  auto second_meta = append_session_metadata_for_test(second_store, std::move(second_metadata));
  expect(first_meta && second_meta, "session tree cycle test persists cyclic parent metadata");

  auto tree = ava::session::build_session_tree(workspace, sessions_dir, "session_cycle_a");
  expect(tree.has_value(), tree ? "session tree index builds with cycles" : "session tree index builds with cycles: " + tree.error().format());
  if (!tree)
    return;

  auto contains = [](std::vector<std::string> const& values, std::string_view value) {
    return std::ranges::any_of(values, [value](std::string const& item) { return item == value; });
  };
  auto find_node = [&](std::string_view session_id) -> ava::session::SessionTreeNode const* {
    auto const found =
        std::ranges::find_if(tree->sessions, [session_id](ava::session::SessionTreeNode const& node) { return node.summary.session_id == session_id; });
    return found == tree->sessions.end() ? nullptr : &*found;
  };

  auto const* first_node = find_node("session_cycle_a");
  auto const* second_node = find_node("session_cycle_b");
  expect(first_node && second_node && first_node->children.empty() && second_node->children.empty() && contains(tree->roots, "session_cycle_a") &&
             contains(tree->roots, "session_cycle_b") && contains(tree->leaves, "session_cycle_a") && contains(tree->leaves, "session_cycle_b") &&
             tree->current_path == std::vector<std::string>({"session_cycle_a"}),
         "session tree index treats parent cycles as usable root/leaf nodes");
}

void test_session_branch_fork_and_clone_copy_source_safely()
{
  auto const root = temp_root() / "session-branch-copy";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const sessions_dir = root / "sessions";
  std::filesystem::create_directories(workspace);

  auto source_store =
      ava::session::SessionStore(ava::session::SessionStoreOptions{.root_dir = sessions_dir, .workspace_dir = workspace, .session_id = "session_source"});
  expect(append_session_entry_for_test(
             source_store, ava::session::SessionEntry{.id = "entry_start",
                                                      .parent_id = "",
                                                      .type = ava::session::EntryType::SessionStart,
                                                      .timestamp = "2026-04-27T00:00:00Z",
                                                      .data_json = "{\"mode\":\"build\",\"provider\":\"openai\",\"model\":\"gpt-test\","
                                                                   "\"context_sources\":0,\"context_window_tokens\":128000,\"max_output_tokens\":4096,"
                                                                   "\"prompt_override\":false,\"supports_tools\":true,\"supports_streaming\":true,"
                                                                   "\"supports_reasoning\":true,\"reports_usage\":true}",
                                                      .version = 0}) &&
             append_session_entry_for_test(
                 source_store, ava::session::SessionEntry{.id = "entry_user",
                                                          .parent_id = "entry_start",
                                                          .type = ava::session::EntryType::UserMessage,
                                                          .timestamp = "2026-04-27T00:00:01Z",
                                                          .data_json = "{\"text\":\"question\",\"attachments\":[{\"id\":\"branch_img\","
                                                                       "\"type\":\"image\",\"mime_type\":\"image/png\",\"byte_size\":5,"
                                                                       "\"sha256\":\"2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824\","
                                                                       "\"storage_path\":\"attachments/branch_img.txt\"}]}",
                                                          .version = 2}) &&
             append_session_entry_for_test(source_store, ava::session::SessionEntry{.id = "entry_assistant",
                                                                                    .parent_id = "entry_user",
                                                                                    .type = ava::session::EntryType::AssistantMessage,
                                                                                    .timestamp = "2026-04-27T00:00:02Z",
                                                                                    .data_json = "{\"text\":\"answer\"}"}),
         "session branch test creates source entries");

  ava::session::SessionMetadataUpdate source_metadata;
  source_metadata.name = "Source";
  source_metadata.labels = std::vector<std::string>{"source"};
  source_metadata.branch_origin = "root";
  source_metadata.actor = "test";
  auto source_meta = append_session_metadata_for_test(source_store, std::move(source_metadata));
  auto source_entries_before = source_store.load();
  expect(source_meta && source_entries_before && source_entries_before->size() == 4, "session branch test source metadata is append-only");
  auto const source_attachment_path = ava::session::attachment_storage_root(source_store) / "attachments" / "branch_img.txt";
  std::filesystem::create_directories(source_attachment_path.parent_path());
  {
    std::ofstream file(source_attachment_path, std::ios::binary);
    file << "hello";
  }

  auto limited_branch = ava::session::create_session_branch(ava::session::SessionBranchOptions{
      .workspace_dir = workspace,
      .root_dir = sessions_dir,
      .source_session_id = "session_source",
      .branch_from_entry_id = "entry_user",
      .name = std::nullopt,
      .labels = std::nullopt,
      .read_limits = ava::session::SessionReadLimits{.max_file_bytes = 1024U * 1024U, .max_line_bytes = 1024U * 1024U, .max_entries = 1},
      .mode = ava::session::SessionBranchMode::Fork,
      .actor = "test"});
  expect(!limited_branch && limited_branch.error().message().find("entry count") != std::string::npos,
         "an explicit tiny branch read limit rejects the source before creating a destination");

  auto forked = ava::session::create_session_branch(
      ava::session::SessionBranchOptions{.workspace_dir = workspace,
                                         .root_dir = sessions_dir,
                                         .source_session_id = "session_source",
                                         .branch_from_entry_id = "entry_user",
                                         .name = std::optional<std::string>{"Forked"},
                                         .labels = std::optional<std::vector<std::string>>{std::vector<std::string>{"forked"}},
                                         .mode = ava::session::SessionBranchMode::Fork,
                                         .actor = "test"});
  expect(forked.has_value(), forked ? "default branch reads the same source with legacy-unbounded limits"
                                    : "default branch reads the same source with legacy-unbounded limits: " + forked.error().format());
  if (!forked)
    return;
  auto fork_contender = ava::session::SessionLease::acquire(forked->store.session_path());
  expect(!fork_contender && fork_contender.error().message().find("already owned") != std::string::npos,
         "session branch retains destination ownership after all copied records and metadata are published");
  auto fork_entries = forked->store.load();
  auto const fork_bytes = read_binary_file(forked->store.session_path());
  expect(fork_entries && forked->copied_entry_count == 2 && fork_entries->size() == 3 && (*fork_entries)[0].version == 0 && (*fork_entries)[1].version == 2 &&
             fork_bytes.starts_with("{\"id\":\"entry_start\"") && fork_entries->back().type == ava::session::EntryType::SessionMetadata &&
             forked->metadata.name == "Forked" && forked->metadata.labels.size() == 1 && forked->metadata.labels[0] == "forked" &&
             forked->metadata.parent_session_id == "session_source" && forked->metadata.source_session_id == "session_source" &&
             forked->metadata.branch_from_entry_id == "entry_user" && forked->metadata.branch_origin == "fork",
         "session fork preserves copied entry versions and appends provenance metadata");
  auto fork_attachment = ava::session::load_image_attachment(
      forked->store, ava::session::ImageAttachmentRef{.id = "branch_img",
                                                      .mime_type = "image/png",
                                                      .storage_path = "attachments/branch_img.txt",
                                                      .sha256 = "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824",
                                                      .byte_size = 5});
  expect(fork_attachment && fork_attachment->bytes == "hello", "session fork copies verified image attachment storage for copied entries");

  auto clone_with_branch_from = ava::session::create_session_branch(ava::session::SessionBranchOptions{.workspace_dir = workspace,
                                                                                                       .root_dir = sessions_dir,
                                                                                                       .source_session_id = "session_source",
                                                                                                       .branch_from_entry_id = "entry_user",
                                                                                                       .name = std::nullopt,
                                                                                                       .labels = std::nullopt,
                                                                                                       .mode = ava::session::SessionBranchMode::Clone,
                                                                                                       .actor = "test"});
  expect(!clone_with_branch_from && clone_with_branch_from.error().category() == ava::core::ErrorCategory::InvalidArgument,
         "backend session clone rejects explicit branch_from_entry_id");

  auto cloned = ava::session::create_session_branch(ava::session::SessionBranchOptions{.workspace_dir = workspace,
                                                                                       .root_dir = sessions_dir,
                                                                                       .source_session_id = "session_source",
                                                                                       .branch_from_entry_id = "",
                                                                                       .name = std::optional<std::string>{"Cloned"},
                                                                                       .labels = std::nullopt,
                                                                                       .mode = ava::session::SessionBranchMode::Clone,
                                                                                       .actor = "test"});
  expect(cloned.has_value(), cloned ? "session clone creates a new branch" : "session clone creates a new branch: " + cloned.error().format());
  if (!cloned)
    return;
  auto clone_entries = cloned->store.load();
  auto source_entries_after = source_store.load();
  expect(clone_entries && source_entries_after && source_entries_after->size() == source_entries_before->size() &&
             cloned->copied_entry_count == source_entries_before->size() && clone_entries->size() == source_entries_before->size() + 1 &&
             (*clone_entries)[0].version == 0 && cloned->metadata.name == "Cloned" && cloned->metadata.labels.size() == 1 &&
             cloned->metadata.labels[0] == "source" && cloned->metadata.parent_session_id == "session_source" &&
             cloned->metadata.branch_from_entry_id == source_entries_before->back().id && cloned->metadata.branch_origin == "clone" &&
             cloned->metadata.actor == "test",
         "session clone copies the full source session without modifying the source file and exposes actor");
  auto clone_attachment = ava::session::load_image_attachment(
      cloned->store, ava::session::ImageAttachmentRef{.id = "branch_img",
                                                      .mime_type = "image/png",
                                                      .storage_path = "attachments/branch_img.txt",
                                                      .sha256 = "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824",
                                                      .byte_size = 5});
  expect(clone_attachment && clone_attachment->bytes == "hello", "session clone copies verified image attachment storage for copied entries");

  auto missing = ava::session::create_session_branch(ava::session::SessionBranchOptions{.workspace_dir = workspace,
                                                                                        .root_dir = sessions_dir,
                                                                                        .source_session_id = "session_source",
                                                                                        .branch_from_entry_id = "missing",
                                                                                        .name = std::nullopt,
                                                                                        .labels = std::nullopt,
                                                                                        .mode = ava::session::SessionBranchMode::Fork,
                                                                                        .actor = "test"});
  expect(!missing && missing.error().category() == ava::core::ErrorCategory::NotFound, "session fork rejects missing branch source entries");

  auto v4_source =
      ava::session::SessionStore(ava::session::SessionStoreOptions{.root_dir = sessions_dir, .workspace_dir = workspace, .session_id = "session_v4_prefix"});
  expect(append_session_entry_for_test(v4_source, ava::session::SessionEntry{.id = "v4_safe_prefix",
                                                                             .parent_id = "",
                                                                             .type = ava::session::EntryType::UserMessage,
                                                                             .timestamp = "2026-07-18T00:00:00Z",
                                                                             .data_json = "{\"text\":\"safe\"}",
                                                                             .version = 3}) &&
             append_session_entry_for_test(
                 v4_source, ava::session::SessionEntry{.id = "v4_committed_item",
                                                       .parent_id = "v4_safe_prefix",
                                                       .type = ava::session::EntryType::AssistantOutputItem,
                                                       .timestamp = "2026-07-18T00:00:01Z",
                                                       .data_json = "{\"schema_version\":1,\"assistant_turn_id\":\"turn_committed\",\"sequence\":0,\"kind\":"
                                                                    "\"text\",\"text\":\"committed\",\"assistant_phase\":\"commentary\"}"}) &&
             append_session_entry_for_test(
                 v4_source, ava::session::SessionEntry{.id = "v4_committed_commit",
                                                       .parent_id = "v4_committed_item",
                                                       .type = ava::session::EntryType::AssistantTurnCommit,
                                                       .timestamp = "2026-07-18T00:00:02Z",
                                                       .data_json = "{\"schema_version\":1,\"assistant_turn_id\":\"turn_committed\",\"item_count\":1,"
                                                                    "\"provider\":\"openai\",\"model\":\"gpt-5.5\",\"finish_reason\":\"completed\"}"}) &&
             append_session_entry_for_test(
                 v4_source, ava::session::SessionEntry{.id = "v4_staged_item",
                                                       .parent_id = "v4_committed_commit",
                                                       .type = ava::session::EntryType::AssistantOutputItem,
                                                       .timestamp = "2026-07-18T00:00:03Z",
                                                       .data_json = "{\"schema_version\":1,\"assistant_turn_id\":\"turn_staged\",\"sequence\":0,\"kind\":"
                                                                    "\"text\",\"text\":\"staged\",\"assistant_phase\":\"final_answer\"}"}),
         "session branch test writes committed and staged v4 prefix fixtures");
  auto branch_options = [&](std::string branch_from_entry_id, ava::session::SessionBranchMode mode = ava::session::SessionBranchMode::Fork) {
    return ava::session::SessionBranchOptions{.workspace_dir = workspace,
                                              .root_dir = sessions_dir,
                                              .source_session_id = "session_v4_prefix",
                                              .branch_from_entry_id = std::move(branch_from_entry_id),
                                              .name = std::nullopt,
                                              .labels = std::nullopt,
                                              .mode = mode,
                                              .actor = "test"};
  };
  auto before_staged = ava::session::create_session_branch(branch_options("v4_safe_prefix"));
  auto inside_committed = ava::session::create_session_branch(branch_options("v4_committed_item"));
  auto at_committed_boundary = ava::session::create_session_branch(branch_options("v4_committed_commit"));
  auto inside_staged = ava::session::create_session_branch(branch_options("v4_staged_item"));
  auto clone_with_staged_suffix = ava::session::create_session_branch(branch_options({}, ava::session::SessionBranchMode::Clone));
  expect(before_staged && before_staged->copied_entry_count == 1 && at_committed_boundary && at_committed_boundary->copied_entry_count == 3 &&
             !inside_committed && !inside_staged && !clone_with_staged_suffix &&
             inside_committed.error().message() ==
                 "branch prefix contains an assistant-output diagnostic; choose a committed boundary or recover the source session" &&
             inside_staged.error().message() ==
                 "branch prefix contains an assistant-output diagnostic; choose a committed boundary or recover the source session" &&
             clone_with_staged_suffix.error().message() ==
                 "branch prefix contains an assistant-output diagnostic; choose a committed boundary or recover the source session",
         "fork and clone accept only v4 logical boundaries and allow an explicit target before a later staged suffix");
}

void test_session_branch_summary_appends_to_source_session()
{
  auto const root = temp_root() / "session-branch-summary";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const sessions_dir = root / "sessions";
  std::filesystem::create_directories(workspace);

  auto source_store =
      ava::session::SessionStore(ava::session::SessionStoreOptions{.root_dir = sessions_dir, .workspace_dir = workspace, .session_id = "session_source"});
  expect(append_session_entry_for_test(
             source_store, ava::session::SessionEntry{.id = "entry_start",
                                                      .parent_id = "",
                                                      .type = ava::session::EntryType::SessionStart,
                                                      .timestamp = "2026-04-27T00:00:00Z",
                                                      .data_json = "{\"mode\":\"build\",\"provider\":\"openai\",\"model\":\"gpt-test\","
                                                                   "\"context_sources\":0,\"context_window_tokens\":128000,\"max_output_tokens\":4096,"
                                                                   "\"prompt_override\":false,\"supports_tools\":true,\"supports_streaming\":true,"
                                                                   "\"supports_reasoning\":true,\"reports_usage\":true}"}) &&
             append_session_entry_for_test(source_store, ava::session::SessionEntry{.id = "entry_user",
                                                                                    .parent_id = "entry_start",
                                                                                    .type = ava::session::EntryType::UserMessage,
                                                                                    .timestamp = "2026-04-27T00:00:01Z",
                                                                                    .data_json = "{\"text\":\"question\"}"}) &&
             append_session_entry_for_test(source_store, ava::session::SessionEntry{.id = "entry_assistant",
                                                                                    .parent_id = "entry_user",
                                                                                    .type = ava::session::EntryType::AssistantMessage,
                                                                                    .timestamp = "2026-04-27T00:00:02Z",
                                                                                    .data_json = "{\"text\":\"answer\"}"}),
         "branch summary test creates source entries");
  auto const source_entries_before = source_store.load();
  expect(source_entries_before && source_entries_before->size() == 3, "branch summary test loads source before append");
  if (!source_entries_before)
    return;

  auto summary = ava::session::append_branch_summary(ava::session::BranchSummaryOptions{.workspace_dir = workspace,
                                                                                        .root_dir = sessions_dir,
                                                                                        .source_session_id = "session_source",
                                                                                        .branch_root_entry_id = "entry_user",
                                                                                        .branch_tip_entry_id = "entry_assistant",
                                                                                        .summary = "Abandoned branch explored the alternate answer.",
                                                                                        .provider = "openai",
                                                                                        .model = "gpt-test",
                                                                                        .reason = "test",
                                                                                        .actor = "test"});
  expect(summary.has_value(), summary ? "branch summary appends to source session" : "branch summary appends to source session: " + summary.error().format());
  if (!summary)
    return;
  auto source_entries_after = source_store.load();
  expect(source_entries_after && source_entries_after->size() == source_entries_before->size() + 1 &&
             source_entries_after->back().type == ava::session::EntryType::BranchSummary &&
             source_entries_after->back().parent_id == source_entries_before->back().id && summary->entry.id == source_entries_after->back().id,
         "branch summary is append-only at the source session tip");
  if (!source_entries_after)
    return;

  auto const validation = ava::session::validate_session_replay(*source_entries_after);
  auto const stats = ava::session::compute_session_stats(*source_entries_after);
  auto const exported = ava::session::format_session_markdown(*source_entries_after, ava::session::ExportOptions{});
  auto validation_message = std::string("branch summary entries validate from the source session");
  if (!validation.ok() && !validation.issues.empty())
  {
    validation_message += ": ";
    validation_message += validation.issues.front().message;
  }
  expect(validation.ok(), validation_message);
  expect(stats->counts.branch_summary == 1, "branch summary entries count in source session stats");
  expect(exported.find("Abandoned branch explored the alternate answer.") != std::string::npos, "branch summary entries export from the source session");

  auto root_after_tip = ava::session::append_branch_summary(ava::session::BranchSummaryOptions{.workspace_dir = workspace,
                                                                                               .root_dir = sessions_dir,
                                                                                               .source_session_id = "session_source",
                                                                                               .branch_root_entry_id = "entry_assistant",
                                                                                               .branch_tip_entry_id = "entry_user",
                                                                                               .summary = "bad range",
                                                                                               .provider = "openai",
                                                                                               .model = "gpt-test",
                                                                                               .reason = "test",
                                                                                               .actor = "test"});
  expect(!root_after_tip && root_after_tip.error().category() == ava::core::ErrorCategory::InvalidArgument,
         "branch summary rejects root entries after tip entries");

  auto missing_tip = ava::session::append_branch_summary(ava::session::BranchSummaryOptions{.workspace_dir = workspace,
                                                                                            .root_dir = sessions_dir,
                                                                                            .source_session_id = "session_source",
                                                                                            .branch_root_entry_id = "entry_user",
                                                                                            .branch_tip_entry_id = "missing",
                                                                                            .summary = "missing tip",
                                                                                            .provider = "openai",
                                                                                            .model = "gpt-test",
                                                                                            .reason = "test",
                                                                                            .actor = "test"});
  expect(!missing_tip && missing_tip.error().category() == ava::core::ErrorCategory::NotFound, "branch summary rejects missing tip entries");

  auto bad_provider = ava::session::append_branch_summary(ava::session::BranchSummaryOptions{.workspace_dir = workspace,
                                                                                             .root_dir = sessions_dir,
                                                                                             .source_session_id = "session_source",
                                                                                             .branch_root_entry_id = "entry_user",
                                                                                             .branch_tip_entry_id = "entry_assistant",
                                                                                             .summary = "summary with\nallowed whitespace",
                                                                                             .provider = "open\nai",
                                                                                             .model = "gpt-test",
                                                                                             .reason = "test",
                                                                                             .actor = "test"});
  expect(!bad_provider && bad_provider.error().category() == ava::core::ErrorCategory::InvalidArgument,
         "branch summary rejects control bytes in provider metadata while allowing summary newlines");

  auto staged = append_session_entry_for_test(
      source_store, ava::session::SessionEntry{
                        .id = "entry_staged",
                        .parent_id = source_entries_after->back().id,
                        .type = ava::session::EntryType::AssistantOutputItem,
                        .timestamp = "2026-04-27T00:00:03Z",
                        .data_json = "{\"schema_version\":1,\"assistant_turn_id\":\"summary_staged\",\"sequence\":0,\"kind\":\"text\",\"text\":\"staged\","
                                     "\"assistant_phase\":\"commentary\"}"});
  auto incomplete_source = ava::session::append_branch_summary(ava::session::BranchSummaryOptions{.workspace_dir = workspace,
                                                                                                  .root_dir = sessions_dir,
                                                                                                  .source_session_id = "session_source",
                                                                                                  .branch_root_entry_id = "entry_user",
                                                                                                  .branch_tip_entry_id = "entry_assistant",
                                                                                                  .summary = "must not append after a staged output",
                                                                                                  .provider = "openai",
                                                                                                  .model = "gpt-test",
                                                                                                  .reason = "test",
                                                                                                  .actor = "test"});
  auto after_incomplete = source_store.load();
  auto incomplete_summary_message =
      std::string("direct branch summary appends refuse even an incomplete assistant-output suffix without adding an interior record");
  if (!staged)
    incomplete_summary_message += ": staged fixture append failed: " + staged.error().format();
  else if (incomplete_source)
    incomplete_summary_message += ": branch summary unexpectedly appended";
  else if (incomplete_source.error().category() != ava::core::ErrorCategory::Session)
    incomplete_summary_message += ": branch summary error: " + incomplete_source.error().format();
  else if (!after_incomplete)
    incomplete_summary_message += ": source reload failed: " + after_incomplete.error().format();
  expect(staged && !incomplete_source && incomplete_source.error().category() == ava::core::ErrorCategory::Session && after_incomplete &&
             after_incomplete->size() == source_entries_after->size() + 1 && after_incomplete->back().id == "entry_staged",
         incomplete_summary_message);
}

void test_session_stats_helper()
{
  std::vector<ava::session::SessionEntry> const entries = {
      ava::session::SessionEntry{
          .id = "start_1", .parent_id = "", .type = ava::session::EntryType::SessionStart, .timestamp = "2026-04-29T00:00:00Z", .data_json = "{}"},
      ava::session::SessionEntry{.id = "user_1",
                                 .parent_id = "start_1",
                                 .type = ava::session::EntryType::UserMessage,
                                 .timestamp = "2026-04-29T00:00:01Z",
                                 .data_json = "{\"usage\":{\"input_tokens\":3,\"output_tokens\":2,"
                                              "\"reasoning_tokens\":1,\"cache_read_tokens\":2,"
                                              "\"total_tokens\":5,\"cost_usd\":0.001,"
                                              "\"source\":\"provider\"}}"},
      ava::session::SessionEntry{.id = "user_1_replay",
                                 .parent_id = "start_1",
                                 .type = ava::session::EntryType::UserMessage,
                                 .timestamp = "2026-04-29T00:00:01Z",
                                 .data_json = "{\"text\":\"hidden replay\",\"internal_replay\":true,"
                                              "\"replay_of\":\"user_1\",\"reason\":\"test\"}"},
      ava::session::SessionEntry{.id = "assistant_1",
                                 .parent_id = "user_1",
                                 .type = ava::session::EntryType::AssistantMessage,
                                 .timestamp = "2026-04-29T00:00:02Z",
                                 .data_json = "{\"usage\":{\"input_tokens\":1,\"output_tokens\":4,"
                                              "\"cache_write_tokens\":3,\"total_tokens\":5,"
                                              "\"estimated_input_bytes\":10,\"estimated_output_bytes\":20,"
                                              "\"estimated_total_bytes\":30,"
                                              "\"cost_usd\":0.0025,\"estimated\":true}}"},
      ava::session::SessionEntry{.id = "reasoning_1",
                                 .parent_id = "assistant_1",
                                 .type = ava::session::EntryType::ReasoningBlock,
                                 .timestamp = "2026-04-29T00:00:02Z",
                                 .data_json = "{\"provider\":\"anthropic\",\"model\":\"claude\","
                                              "\"format\":\"anthropic_thinking\",\"text\":\"visible\","
                                              "\"signature\":\"secret-signature\"}"},
      ava::session::SessionEntry{.id = "reasoning_change_1",
                                 .parent_id = "reasoning_1",
                                 .type = ava::session::EntryType::ReasoningChange,
                                 .timestamp = "2026-04-29T00:00:02Z",
                                 .data_json = "{\"level\":\"high\"}"},
      ava::session::SessionEntry{.id = "mode_1",
                                 .parent_id = "reasoning_change_1",
                                 .type = ava::session::EntryType::ModeChange,
                                 .timestamp = "2026-04-29T00:00:03Z",
                                 .data_json = "{\"mode\":\"plan\"}"},
      ava::session::SessionEntry{.id = "compact_1",
                                 .parent_id = "mode_1",
                                 .type = ava::session::EntryType::Compaction,
                                 .timestamp = "2026-04-29T00:00:04Z",
                                 .data_json = "{\"summary\":\"prior\"}"},
      ava::session::SessionEntry{
          .id = "cancel_1", .parent_id = "compact_1", .type = ava::session::EntryType::Cancel, .timestamp = "2026-04-29T00:00:05Z", .data_json = "{}"},
      ava::session::SessionEntry{
          .id = "error_1", .parent_id = "cancel_1", .type = ava::session::EntryType::Error, .timestamp = "2026-04-29T00:00:06Z", .data_json = "{}"},
  };

  auto const stats = ava::session::compute_session_stats(entries);
  expect(stats->entry_count == entries.size() && stats->first_timestamp == "2026-04-29T00:00:00Z" && stats->last_timestamp == "2026-04-29T00:00:06Z",
         "session stats helper reports entry count and timestamps");
  expect(stats->counts.session_start == 1 && stats->counts.user_message == 1 && stats->counts.assistant_message == 1 && stats->counts.reasoning_block == 1 &&
             stats->counts.reasoning_change == 1 && stats->counts.mode_change == 1 && stats->counts.compaction == 1 && stats->counts.cancel == 1 &&
             stats->counts.error == 1,
         "session stats helper reports current counts without durable internal replay user messages");
  expect(stats->input_tokens && *stats->input_tokens == 3 && stats->output_tokens && *stats->output_tokens == 2 && stats->total_tokens &&
             *stats->total_tokens == 5,
         "session stats helper aggregates exact token fields only from provider usage");
  expect(stats->reasoning_tokens && *stats->reasoning_tokens == 1 && stats->cache_read_tokens && *stats->cache_read_tokens == 2 && !stats->cache_write_tokens,
         "session stats helper keeps estimated cache token fields out of exact totals");
  expect(stats->estimated_input_bytes && *stats->estimated_input_bytes == 10 && stats->estimated_output_bytes && *stats->estimated_output_bytes == 20 &&
             stats->estimated_total_bytes && *stats->estimated_total_bytes == 30,
         "session stats helper aggregates separate estimated byte totals");
  expect(stats->total_cost_usd && *stats->total_cost_usd > 0.0009L && *stats->total_cost_usd < 0.0011L,
         "session stats helper aggregates exact cost fields without estimated fallback cost");
  expect(stats->exact_usage_entries == 1 && stats->estimated_usage_entries == 1, "session stats helper counts exact and estimated usage entries");

  auto const empty_stats = ava::session::compute_session_stats({});
  expect(!empty_stats->input_tokens && !empty_stats->total_cost_usd,
         "session stats helper leaves token and cost totals absent when no entry JSON supplies them");
}

void test_session_stats_saturates_large_usage_and_costs()
{
  std::ostringstream maximum_cost;
  maximum_cost << std::scientific << std::setprecision(std::numeric_limits<long double>::max_digits10) << std::numeric_limits<long double>::max();
  auto make_usage_entry = [&](std::string id, std::string parent, long long input_tokens, long long total_tokens, std::string cost) {
    return ava::session::SessionEntry{.id = std::move(id),
                                      .parent_id = std::move(parent),
                                      .type = ava::session::EntryType::AssistantMessage,
                                      .timestamp = "2026-04-29T00:00:00Z",
                                      .data_json = "{\"usage\":{\"input_tokens\":" + std::to_string(input_tokens) + ",\"total_tokens\":" +
                                                   std::to_string(total_tokens) + ",\"cost_usd\":" + cost + ",\"source\":\"provider\"}}"};
  };
  std::vector<ava::session::SessionEntry> const entries = {
      make_usage_entry("large_usage_1", "", LLONG_MAX - 2, LLONG_MAX - 1, maximum_cost.str()),
      make_usage_entry("large_usage_2", "large_usage_1", 9, 7, "1.0"),
  };
  auto const stats = ava::session::compute_session_stats(entries);
  expect(stats && stats->input_tokens && *stats->input_tokens == LLONG_MAX && stats->total_tokens && *stats->total_tokens == LLONG_MAX &&
             stats->known_cost_usd && std::isfinite(*stats->known_cost_usd) && *stats->known_cost_usd == std::numeric_limits<long double>::max(),
         "session stats saturates near-LLONG_MAX token totals and overflowing finite cost aggregates");
}

void test_session_stats_omits_incomplete_cost_total()
{
  std::vector<ava::session::SessionEntry> const entries = {
      ava::session::SessionEntry{.id = "assistant_priced",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::AssistantMessage,
                                 .timestamp = "2026-04-29T00:00:00Z",
                                 .data_json = "{\"usage\":{\"input_tokens\":3,\"output_tokens\":2,"
                                              "\"total_tokens\":5,\"cost_usd\":0.001,"
                                              "\"source\":\"provider\"}}"},
      ava::session::SessionEntry{.id = "assistant_unpriced",
                                 .parent_id = "assistant_priced",
                                 .type = ava::session::EntryType::AssistantMessage,
                                 .timestamp = "2026-04-29T00:00:01Z",
                                 .data_json = "{\"usage\":{\"input_tokens\":4,\"cache_read_tokens\":4,"
                                              "\"total_tokens\":4,\"source\":\"provider\"}}"},
  };

  auto const stats = ava::session::compute_session_stats(entries);
  expect(stats->exact_usage_entries == 2 && stats->estimated_usage_entries == 0, "session stats counts mixed exact usage entries");
  expect(stats->known_cost_usd && *stats->known_cost_usd > 0.0009L && *stats->known_cost_usd < 0.0011L,
         "session stats preserves the known portion of incomplete cost totals");
  expect(!stats->total_cost_usd && !stats->cost_complete && stats->unknown_cost_entries == 1,
         "session stats omits total cost when exact billable usage has unknown cost");
}

void test_session_stats_flags_legacy_assistant_tokens_without_cost()
{
  std::vector<ava::session::SessionEntry> const entries = {
      ava::session::SessionEntry{.id = "assistant_legacy_tokens",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::AssistantMessage,
                                 .timestamp = "2026-04-29T00:00:00Z",
                                 .data_json = "{\"text\":\"legacy\",\"input_tokens\":7,"
                                              "\"output_tokens\":3,\"total_tokens\":10}"},
  };

  auto const stats = ava::session::compute_session_stats(entries);
  expect(stats->input_tokens && *stats->input_tokens == 7 && stats->output_tokens && *stats->output_tokens == 3 && stats->total_tokens &&
             *stats->total_tokens == 10,
         "session stats still aggregates legacy top-level assistant token totals");
  expect(stats->exact_usage_entries == 0 && stats->estimated_usage_entries == 0, "legacy top-level assistant token stats do not masquerade as usage objects");
  expect(!stats->total_cost_usd && !stats->cost_complete && stats->unknown_cost_entries == 1,
         "legacy top-level assistant tokens without cost make cost stats incomplete");
}

void test_assistant_output_v4_session_schema_and_replay()
{
  using ava::session::AssistantOutputFunctionCall;
  using ava::session::AssistantOutputItem;
  using ava::session::AssistantOutputItemKind;
  using ava::session::AssistantOutputReasoning;
  using ava::session::AssistantOutputText;
  using ava::session::AssistantOutputTextPhase;
  using ava::session::AssistantTurnCommit;
  using ava::session::EntryType;
  using ava::session::SessionEntry;

  auto make_entry = [](std::string id, EntryType type, std::string data_json, long long version = 4) {
    return SessionEntry{
        .id = std::move(id), .parent_id = "", .type = type, .timestamp = "2026-07-18T00:00:00Z", .data_json = std::move(data_json), .version = version};
  };
  auto make_item = [&](std::string id, AssistantOutputItem item) {
    auto data = ava::session::serialize_assistant_output_item_data_json(item);
    expect(data.has_value(), "v4 assistant output item codec serializes a strict item variant");
    return make_entry(std::move(id), EntryType::AssistantOutputItem, data ? *data : "{}");
  };
  auto make_text = [&](std::string id, std::string turn_id, std::size_t sequence, std::string provider_item_id, std::size_t output_index,
                       std::string text = "text") {
    return make_item(std::move(id),
                     AssistantOutputItem{.assistant_turn_id = std::move(turn_id),
                                         .sequence = sequence,
                                         .kind = AssistantOutputItemKind::Text,
                                         .provider_item_id = std::move(provider_item_id),
                                         .provider_output_index = output_index,
                                         .payload = AssistantOutputText{.text = std::move(text), .assistant_phase = AssistantOutputTextPhase::Commentary}});
  };
  auto make_reasoning = [&](std::string id, std::string turn_id, std::size_t sequence, std::string provider_item_id, std::size_t output_index) {
    return make_item(std::move(id), AssistantOutputItem{.assistant_turn_id = std::move(turn_id),
                                                        .sequence = sequence,
                                                        .kind = AssistantOutputItemKind::Reasoning,
                                                        .provider_item_id = std::move(provider_item_id),
                                                        .provider_output_index = output_index,
                                                        .payload = AssistantOutputReasoning{
                                                            .text = "reasoning",
                                                            .format = "openai_responses",
                                                            .redacted = false,
                                                            .signature = "PRIVATE_SIGNATURE_CANARY",
                                                            .redacted_data = "PRIVATE_REDACTED_CANARY",
                                                            .native_item_json = "{\"id\":\"rs_schema\",\"type\":\"reasoning\",\"summary\":[]}"}});
  };
  auto make_function = [&](std::string id, std::string turn_id, std::size_t sequence, std::string provider_item_id, std::size_t output_index,
                           std::string call_id = "call_schema", std::string name = "read_file") {
    return make_item(std::move(id),
                     AssistantOutputItem{.assistant_turn_id = std::move(turn_id),
                                         .sequence = sequence,
                                         .kind = AssistantOutputItemKind::FunctionCall,
                                         .provider_item_id = std::move(provider_item_id),
                                         .provider_output_index = output_index,
                                         .payload = AssistantOutputFunctionCall{
                                             .call_id = std::move(call_id), .name = std::move(name), .arguments_json = "{\"path\":\"note.txt\"}"}});
  };
  auto make_commit = [&](std::string id, std::string turn_id, std::size_t item_count, std::string finish_reason = "completed") {
    auto data = ava::session::serialize_assistant_turn_commit_data_json(AssistantTurnCommit{
        .assistant_turn_id = std::move(turn_id),
        .item_count = item_count,
        .provider = "openai",
        .model = "gpt-5.5",
        .finish_reason = std::move(finish_reason),
        .usage_json = "{\"input_tokens\":1,\"output_tokens\":2,\"total_tokens\":3,\"estimated\":false,\"source\":\"provider\",\"cost_usd\":0.01}"});
    expect(data.has_value(), "v4 assistant turn commit codec serializes valid bounded usage metadata");
    return make_entry(std::move(id), EntryType::AssistantTurnCommit, data ? *data : "{}");
  };

  auto const text = make_text("out_text", "turn_codec", 0, "msg_codec", 0);
  auto const reasoning = make_reasoning("out_reasoning", "turn_codec", 1, "rs_codec", 1);
  auto const function = make_function("out_function", "turn_codec", 2, "fc_codec", 2);
  auto const parsed_text = ava::session::parse_assistant_output_item(text);
  auto const parsed_reasoning = ava::session::parse_assistant_output_item(reasoning);
  auto const parsed_function = ava::session::parse_assistant_output_item(function);
  expect(parsed_text && parsed_reasoning && parsed_function && std::holds_alternative<AssistantOutputText>(parsed_text->payload) &&
             std::holds_alternative<AssistantOutputReasoning>(parsed_reasoning->payload) &&
             std::holds_alternative<AssistantOutputFunctionCall>(parsed_function->payload),
         "v4 codecs round-trip text, reasoning, and function_call item variants");
  if (parsed_reasoning)
  {
    auto const* private_reasoning = std::get_if<AssistantOutputReasoning>(&parsed_reasoning->payload);
    expect(private_reasoning && private_reasoning->signature == "PRIVATE_SIGNATURE_CANARY" && private_reasoning->redacted_data == "PRIVATE_REDACTED_CANARY",
           "v4 reasoning codec preserves private-field canaries for later dedicated sanitization work");
  }

  auto portable_reasoning = *parsed_reasoning;
  auto* portable_reasoning_payload = std::get_if<AssistantOutputReasoning>(&portable_reasoning.payload);
  if (portable_reasoning_payload)
  {
    portable_reasoning_payload->signature.reset();
    portable_reasoning_payload->redacted_data.reset();
    portable_reasoning_payload->native_item_json.reset();
    portable_reasoning_payload->private_replay_metadata_omitted = true;
  }
  auto portable_reasoning_data = ava::session::serialize_assistant_output_item_data_json(portable_reasoning);
  auto portable_reasoning_entry = make_entry("portable_reasoning", EntryType::AssistantOutputItem, portable_reasoning_data.value_or("{}"));
  auto contradictory_portable_reasoning = portable_reasoning_entry;
  contradictory_portable_reasoning.data_json.pop_back();
  contradictory_portable_reasoning.data_json += ",\"signature\":\"PRIVATE_CONTRADICTION\"}";
  auto false_portable_marker = portable_reasoning_entry;
  false_portable_marker.data_json.replace(false_portable_marker.data_json.find("\"private_replay_metadata_omitted\":true"),
                                          std::string("\"private_replay_metadata_omitted\":true").size(), "\"private_replay_metadata_omitted\":false");
  expect(portable_reasoning_data && ava::session::parse_assistant_output_item(portable_reasoning_entry) &&
             !ava::session::parse_assistant_output_item(contradictory_portable_reasoning) && !ava::session::parse_assistant_output_item(false_portable_marker),
         "v4 portable reasoning marker is canonical, strict, and forbids retained private replay fields");

  auto line = ava::session::serialize_session_entry_line(text);
  auto round_trip = line ? ava::session::parse_session_entry_line(*line, "v4-round-trip.jsonl") : ava::core::Result<SessionEntry>{};
  auto legacy_v3_item = text;
  legacy_v3_item.version = 3;
  auto v3_item_line = line;
  if (v3_item_line)
    v3_item_line->replace(v3_item_line->find("\"version\":4"), std::string("\"version\":4").size(), "\"version\":3");
  expect(line && round_trip && round_trip->type == EntryType::AssistantOutputItem && round_trip->version == 4 &&
             !ava::session::serialize_session_entry_line(legacy_v3_item) &&
             (!v3_item_line || !ava::session::parse_session_entry_line(*v3_item_line, "v3-output-item.jsonl")),
         "v4 assistant output entry type round-trips only at version 4 or newer");

  auto incompatible_text = text;
  incompatible_text.data_json.pop_back();
  incompatible_text.data_json += ",\"format\":\"not-text\"}";
  auto duplicate_key = text;
  duplicate_key.data_json.pop_back();
  duplicate_key.data_json += ",\"sequence\":0}";
  auto invalid_arguments = function;
  invalid_arguments.data_json =
      "{\"schema_version\":1,\"assistant_turn_id\":\"turn_codec\",\"sequence\":2,\"kind\":\"function_call\","
      "\"call_id\":\"call_schema\",\"name\":\"read_file\",\"arguments\":\"[]\"}";
  auto invalid_native = reasoning;
  invalid_native.data_json =
      "{\"schema_version\":1,\"assistant_turn_id\":\"turn_codec\",\"sequence\":1,\"kind\":\"reasoning\","
      "\"text\":\"reasoning\",\"format\":\"openai_responses\",\"redacted\":false,\"native_item_json\":\"{}\"}";
  auto invalid_commit = make_commit("commit_invalid", "turn_codec", 3, "completed");
  invalid_commit.data_json =
      "{\"schema_version\":1,\"assistant_turn_id\":\"turn_codec\",\"item_count\":3,\"provider\":\"openai\","
      "\"model\":\"gpt-5.5\",\"finish_reason\":\"not_normalized\"}";
  auto maximum_usage_commit = make_commit("commit_max_usage", "turn_max_usage", 0);
  maximum_usage_commit.data_json =
      "{\"schema_version\":1,\"assistant_turn_id\":\"turn_max_usage\",\"item_count\":0,\"provider\":\"openai\",\"model\":\"gpt-5.5\","
      "\"finish_reason\":\"completed\",\"usage\":{\"input_tokens\":9223372036854775807,\"total_tokens\":9223372036854775807}}";
  auto overflowing_usage_commit = maximum_usage_commit;
  overflowing_usage_commit.id = "commit_overflowing_usage";
  overflowing_usage_commit.data_json.replace(overflowing_usage_commit.data_json.find("9223372036854775807"), std::string("9223372036854775807").size(),
                                             "9223372036854775808");
  auto leading_zero_sequence = text;
  leading_zero_sequence.data_json.replace(leading_zero_sequence.data_json.find("\"sequence\":0"), std::string("\"sequence\":0").size(), "\"sequence\":00");
  auto over_limit_sequence = text;
  over_limit_sequence.data_json.replace(over_limit_sequence.data_json.find("\"sequence\":0"), std::string("\"sequence\":0").size(), "\"sequence\":4096");
  auto over_limit_provider_index = text;
  over_limit_provider_index.data_json.replace(over_limit_provider_index.data_json.find("\"provider_output_index\":0"),
                                              std::string("\"provider_output_index\":0").size(), "\"provider_output_index\":4096");
  auto empty_reasoning = reasoning;
  empty_reasoning.data_json =
      "{\"schema_version\":1,\"assistant_turn_id\":\"turn_codec\",\"sequence\":1,\"kind\":\"reasoning\","
      "\"text\":\"\",\"format\":\"openai_responses\",\"redacted\":false}";
  auto const invalid_item_validation = ava::session::validate_session_replay({incompatible_text});
  auto const invalid_commit_validation = ava::session::validate_session_replay({invalid_commit});
  expect(ava::session::kCurrentAssistantOutputSchemaVersion == 1 && ava::session::to_string(static_cast<AssistantOutputItemKind>(999)) == "unknown" &&
             !ava::session::parse_assistant_output_item(incompatible_text) && !ava::session::parse_assistant_output_item(duplicate_key) &&
             !ava::session::parse_assistant_output_item(invalid_arguments) && !ava::session::parse_assistant_output_item(invalid_native) &&
             !ava::session::parse_assistant_output_item(leading_zero_sequence) && !ava::session::parse_assistant_output_item(over_limit_sequence) &&
             !ava::session::parse_assistant_output_item(over_limit_provider_index) && !ava::session::parse_assistant_output_item(empty_reasoning) &&
             !ava::session::parse_assistant_turn_commit(invalid_commit) && ava::session::parse_assistant_turn_commit(maximum_usage_commit) &&
             !ava::session::parse_assistant_turn_commit(overflowing_usage_commit) &&
             has_replay_issue(invalid_item_validation, ava::session::SessionReplayIssueKind::InvalidAssistantOutputItem) &&
             has_replay_issue(invalid_commit_validation, ava::session::SessionReplayIssueKind::InvalidAssistantTurnCommit),
         "v4 codecs and replay diagnostics reject duplicate keys, incompatible variants, invalid arguments/native reasoning, and commits");

  auto nested_object = [](std::size_t depth) {
    std::string json;
    for (std::size_t index = 0; index < depth; ++index) json += "{\"x\":";
    json += '0';
    json.append(depth, '}');
    return json;
  };
  auto function_at_depth = [&](std::size_t depth) {
    auto candidate = function;
    candidate.data_json =
        "{\"schema_version\":1,\"assistant_turn_id\":\"turn_codec\",\"sequence\":2,\"kind\":\"function_call\","
        "\"call_id\":\"call_schema\",\"name\":\"read_file\",\"arguments\":\"" +
        ava::core::json::escape(nested_object(depth)) + "\"}";
    return ava::session::parse_assistant_output_item(candidate).has_value();
  };
  auto reasoning_at_depth = [&](std::size_t depth) {
    auto candidate = reasoning;
    auto const native_item = std::string("{\"id\":\"rs_depth\",\"type\":\"reasoning\",\"summary\":[],\"unknown\":") + nested_object(depth - 1) + "}";
    candidate.data_json =
        "{\"schema_version\":1,\"assistant_turn_id\":\"turn_codec\",\"sequence\":1,\"kind\":\"reasoning\","
        "\"text\":\"reasoning\",\"format\":\"openai_responses\",\"redacted\":false,\"native_item_json\":\"" +
        ava::core::json::escape(native_item) + "\"}";
    return ava::session::parse_assistant_output_item(candidate).has_value();
  };
  expect(function_at_depth(ava::core::json::kMaxNestingDepth - 1) && function_at_depth(ava::core::json::kMaxNestingDepth) &&
             !function_at_depth(ava::core::json::kMaxNestingDepth + 1) && reasoning_at_depth(ava::core::json::kMaxNestingDepth - 1) &&
             reasoning_at_depth(ava::core::json::kMaxNestingDepth) && !reasoning_at_depth(ava::core::json::kMaxNestingDepth + 1),
         "v4 function arguments and native reasoning enforce the shared JSON nesting boundary");

  auto zero_commit = make_commit("commit_zero", "turn_zero", 0);
  auto commit = make_commit("commit_codec", "turn_codec", 3);
  auto const complete = ava::session::classify_assistant_output({text, reasoning, function, commit, zero_commit});
  auto const projected_stats = ava::session::compute_session_stats({text, reasoning, function, commit, zero_commit});
  expect(complete.diagnostics.empty() && complete.turns.size() == 2 && complete.turns[0].start_index == 0 && complete.turns[0].commit_index == 3 &&
             complete.turns[0].items.size() == 3 && complete.turns[1].items.empty() && complete.find_turn_by_commit_index(3) == &complete.turns[0] &&
             complete.find_item_by_output_entry_id("out_function") != nullptr && projected_stats->entry_count == 4 &&
             projected_stats->counts.assistant_message == 2 && projected_stats->counts.reasoning_block == 1 && projected_stats->counts.tool_call == 1 &&
             projected_stats->input_tokens && *projected_stats->input_tokens == 2 && projected_stats->known_cost_usd &&
             *projected_stats->known_cost_usd > 0.019L,
         "v4 classifier projects committed turns into legacy-equivalent stats without counting physical staging records");

  for (std::size_t prefix_size = 1; prefix_size <= 4; ++prefix_size)
  {
    std::vector<SessionEntry> prefix{text, reasoning, function, commit};
    prefix.resize(prefix_size);
    auto const projection = ava::session::classify_assistant_output(prefix);
    bool const complete_prefix = prefix_size == 4;
    expect((complete_prefix && projection.turns.size() == 1 && projection.diagnostics.empty()) ||
               (!complete_prefix && projection.turns.empty() && projection.diagnostics.size() == 1 &&
                projection.diagnostics.front().kind == ava::session::AssistantOutputDiagnosticKind::IncompleteAssistantTurn &&
                projection.diagnostics.front().severity == ava::session::AssistantOutputDiagnosticSeverity::Warning),
           "every complete-line prefix keeps staged assistant output invisible until its trailing commit");
  }

  auto sparse = reasoning;
  auto sparse_item = ava::session::parse_assistant_output_item(sparse);
  if (sparse_item)
  {
    sparse_item->sequence = 2;
    sparse.data_json = ava::session::serialize_assistant_output_item_data_json(*sparse_item).value_or("{}");
  }
  auto bad_count = make_commit("commit_bad_count", "turn_codec", 1);
  auto mismatched_turn = make_commit("commit_mismatch", "other_turn", 2);
  auto unrelated = make_entry("user_interleave", EntryType::UserMessage, "{\"text\":\"interleaved\"}");
  auto has_malformed = [](ava::session::AssistantOutputProjection const& projection) {
    return std::ranges::any_of(projection.diagnostics,
                               [](auto const& diagnostic) { return diagnostic.kind == ava::session::AssistantOutputDiagnosticKind::MalformedAssistantTurn; });
  };
  auto const final_sparse_projection = ava::session::classify_assistant_output({text, sparse});
  auto const final_invalid_projection = ava::session::classify_assistant_output({incompatible_text});
  expect(has_malformed(ava::session::classify_assistant_output({text, sparse, make_commit("commit_sparse", "turn_codec", 2)})) &&
             has_malformed(final_sparse_projection) && has_malformed(final_invalid_projection) &&
             std::ranges::none_of(
                 final_invalid_projection.diagnostics,
                 [](auto const& diagnostic) { return diagnostic.kind == ava::session::AssistantOutputDiagnosticKind::IncompleteAssistantTurn; }) &&
             std::ranges::none_of(
                 final_sparse_projection.diagnostics,
                 [](auto const& diagnostic) { return diagnostic.kind == ava::session::AssistantOutputDiagnosticKind::IncompleteAssistantTurn; }) &&
             has_malformed(ava::session::classify_assistant_output({text, reasoning, bad_count})) &&
             has_malformed(ava::session::classify_assistant_output({text, reasoning, mismatched_turn})) &&
             has_malformed(ava::session::classify_assistant_output({text, unrelated})),
         "v4 classifier rejects sparse sequences, count/turn mismatches, and unrelated interleaving");

  std::vector<SessionEntry> over_limit_staging;
  over_limit_staging.reserve(ava::session::kMaxAssistantOutputItemsPerTurn + 1);
  for (std::size_t index = 0; index <= ava::session::kMaxAssistantOutputItemsPerTurn; ++index)
  {
    over_limit_staging.push_back(
        make_item("out_over_limit_" + std::to_string(index),
                  AssistantOutputItem{.assistant_turn_id = "turn_over_limit",
                                      .sequence = index % ava::session::kMaxAssistantOutputItemsPerTurn,
                                      .kind = AssistantOutputItemKind::Text,
                                      .provider_item_id = std::nullopt,
                                      .provider_output_index = std::nullopt,
                                      .payload = AssistantOutputText{.text = "x", .assistant_phase = AssistantOutputTextPhase::Commentary}}));
  }
  auto const over_limit_projection = ava::session::classify_assistant_output(over_limit_staging);
  expect(
      std::ranges::any_of(over_limit_projection.diagnostics,
                          [](auto const& diagnostic) {
                            return diagnostic.severity == ava::session::AssistantOutputDiagnosticSeverity::Error &&
                                   diagnostic.message == "staged assistant turn exceeds 4096 output items";
                          }) &&
          std::ranges::none_of(over_limit_projection.diagnostics,
                               [](auto const& diagnostic) { return diagnostic.kind == ava::session::AssistantOutputDiagnosticKind::IncompleteAssistantTurn; }),
      "v4 classifier treats staging beyond 4096 items as an error rather than an incomplete suffix");

  auto duplicate_turn = make_commit("commit_duplicate_turn", "turn_codec", 0);
  auto duplicate_item_id = reasoning;
  auto duplicate_item_id_payload = ava::session::parse_assistant_output_item(duplicate_item_id);
  if (duplicate_item_id_payload)
  {
    duplicate_item_id_payload->provider_item_id = "msg_codec";
    duplicate_item_id.data_json = ava::session::serialize_assistant_output_item_data_json(*duplicate_item_id_payload).value_or("{}");
  }
  auto duplicate_item_index = reasoning;
  auto duplicate_item_index_payload = ava::session::parse_assistant_output_item(duplicate_item_index);
  if (duplicate_item_index_payload)
  {
    duplicate_item_index_payload->provider_output_index = 0;
    duplicate_item_index.data_json = ava::session::serialize_assistant_output_item_data_json(*duplicate_item_index_payload).value_or("{}");
  }
  auto const final_duplicate_id_projection = ava::session::classify_assistant_output({text, duplicate_item_id});
  auto const final_duplicate_index_projection = ava::session::classify_assistant_output({text, duplicate_item_index});
  expect(
      has_malformed(ava::session::classify_assistant_output({text, reasoning, function, commit, duplicate_turn})) &&
          has_malformed(ava::session::classify_assistant_output({text, duplicate_item_id, make_commit("commit_duplicate_id", "turn_codec", 2)})) &&
          has_malformed(ava::session::classify_assistant_output({text, duplicate_item_index, make_commit("commit_duplicate_index", "turn_codec", 2)})) &&
          has_malformed(final_duplicate_id_projection) && has_malformed(final_duplicate_index_projection) &&
          std::ranges::none_of(
              final_duplicate_id_projection.diagnostics,
              [](auto const& diagnostic) { return diagnostic.kind == ava::session::AssistantOutputDiagnosticKind::IncompleteAssistantTurn; }) &&
          std::ranges::none_of(final_duplicate_index_projection.diagnostics,
                               [](auto const& diagnostic) { return diagnostic.kind == ava::session::AssistantOutputDiagnosticKind::IncompleteAssistantTurn; }),
      "v4 classifier rejects duplicate committed and final-staged provider item IDs or indexes");

  auto const reused_committed_turn_item = make_text("out_reused_committed_turn", "turn_codec", 0, "msg_reused_committed_turn", 0);
  auto const reused_committed_turn_projection = ava::session::classify_assistant_output({text, reasoning, function, commit, reused_committed_turn_item});
  auto const reused_committed_turn_replay = ava::session::validate_session_replay({text, reasoning, function, commit, reused_committed_turn_item});
  expect(has_malformed(reused_committed_turn_projection) &&
             std::ranges::none_of(
                 reused_committed_turn_projection.diagnostics,
                 [](auto const& diagnostic) { return diagnostic.kind == ava::session::AssistantOutputDiagnosticKind::IncompleteAssistantTurn; }) &&
             has_replay_issue(reused_committed_turn_replay, ava::session::SessionReplayIssueKind::MalformedAssistantTurn),
         "a final staged v4 item cannot reuse a committed assistant turn id as an incomplete warning");

  auto legacy_user = make_entry("legacy_user", EntryType::UserMessage, "{\"text\":\"legacy\"}", 3);
  auto const mixed = ava::session::validate_session_replay({legacy_user, zero_commit});
  expect(mixed.ok() && mixed.issues.empty(), "mixed v3 and v4 histories remain replay-valid");

  auto bound_function = make_function("out_bound_function", "turn_bound", 0, "fc_bound", 0, "call_bound", "read_file");
  auto bound_commit = make_commit("commit_bound", "turn_bound", 1, "tool_calls");
  auto bound_result = make_entry("result_bound", EntryType::ToolResult,
                                 "{\"assistant_output_entry_id\":\"out_bound_function\",\"call_id\":\"call_bound\","
                                 "\"name\":\"read_file\",\"success\":true,\"result\":\"ok\"}");
  auto const exact_binding = ava::session::validate_session_replay({bound_function, bound_commit, bound_result});
  auto missing_binding = bound_result;
  missing_binding.data_json = "{\"call_id\":\"call_bound\",\"name\":\"read_file\",\"success\":true,\"result\":\"ok\"}";
  auto wrong_binding = bound_result;
  wrong_binding.data_json = "{\"assistant_output_entry_id\":\"out_text\",\"call_id\":\"call_bound\",\"name\":\"read_file\",\"success\":true,\"result\":\"ok\"}";
  auto wrong_name_binding = bound_result;
  wrong_name_binding.data_json =
      "{\"assistant_output_entry_id\":\"out_bound_function\",\"call_id\":\"call_bound\",\"name\":\"bash\",\"success\":true,\"result\":\"ok\"}";
  auto const missing_binding_validation = ava::session::validate_session_replay({bound_function, bound_commit, missing_binding});
  auto const wrong_binding_validation = ava::session::validate_session_replay({bound_function, bound_commit, wrong_binding});
  auto const wrong_name_binding_validation = ava::session::validate_session_replay({bound_function, bound_commit, wrong_name_binding});
  auto const duplicate_result_validation = ava::session::validate_session_replay({bound_function, bound_commit, bound_result, bound_result});
  auto const uncommitted_function = ava::session::validate_session_replay({bound_function});
  auto const unresolved_function = ava::session::validate_session_replay({bound_function, bound_commit});
  auto const lenient_missing_binding = ava::session::validate_session_replay(
      {bound_function, bound_commit, missing_binding}, ava::session::SessionReplayValidationOptions{.require_tool_result_pairing = false});
  auto const result_before_commit = ava::session::validate_session_replay({bound_function, bound_result, bound_commit});
  auto const lenient_legacy_result = ava::session::validate_session_replay(
      {make_entry("legacy_result", EntryType::ToolResult, "{\"call_id\":\"legacy\",\"name\":\"read_file\",\"success\":true,\"result\":\"ok\"}", 3)},
      ava::session::SessionReplayValidationOptions{.require_tool_result_pairing = false});
  auto v3_private_binding = bound_result;
  v3_private_binding.id = "v3_private_binding";
  v3_private_binding.version = 3;
  auto const v3_private_binding_validation = ava::session::validate_session_replay({v3_private_binding});
  auto const mixed_private_projection = ava::session::project_logical_session_history({bound_function, bound_commit, v3_private_binding});
  expect(exact_binding.ok() && has_replay_issue(missing_binding_validation, ava::session::SessionReplayIssueKind::ToolResultOutputItemMismatch) &&
             has_replay_issue(wrong_binding_validation, ava::session::SessionReplayIssueKind::ToolResultOutputItemMismatch) &&
             has_replay_issue(wrong_name_binding_validation, ava::session::SessionReplayIssueKind::ToolResultOutputItemMismatch) &&
             has_replay_issue(duplicate_result_validation, ava::session::SessionReplayIssueKind::DuplicateToolResult) && uncommitted_function.ok() &&
             has_replay_issue(uncommitted_function, ava::session::SessionReplayIssueKind::IncompleteAssistantTurn) &&
             has_replay_issue(unresolved_function, ava::session::SessionReplayIssueKind::UnresolvedToolCall) &&
             has_replay_issue(lenient_missing_binding, ava::session::SessionReplayIssueKind::ToolResultOutputItemMismatch) &&
             has_replay_issue(result_before_commit, ava::session::SessionReplayIssueKind::ToolResultOutputItemMismatch) && lenient_legacy_result.ok() &&
             has_replay_issue(v3_private_binding_validation, ava::session::SessionReplayIssueKind::ToolResultOutputItemMismatch) && mixed_private_projection &&
             mixed_private_projection->back().data_json.find("assistant_output_entry_id") == std::string::npos,
         "v4 tool results require exact committed bindings while every public projection strips v3/v4 private bindings");

  auto permission_audit = make_entry("permission_audit", EntryType::PermissionDecision,
                                     "{\"operation\":\"read\",\"mode\":\"build\",\"tool_name\":\"read_file\",\"action\":\"allow\",\"reason\":\"test\","
                                     "\"permission_request_id\":\"permission-window\",\"resolution\":\"allow\",\"resolution_source\":\"policy\"}");
  auto const allowed_window = ava::session::validate_session_replay(
      {bound_function, bound_commit, make_entry("window_error", EntryType::Error, "{\"message\":\"audit\"}"), permission_audit, bound_result});
  auto const after_user = ava::session::validate_session_replay(
      {bound_function, bound_commit, make_entry("window_user", EntryType::UserMessage, "{\"text\":\"next\"}"), bound_result});
  auto const after_assistant = ava::session::validate_session_replay(
      {bound_function, bound_commit, make_entry("window_assistant", EntryType::AssistantMessage, "{\"text\":\"next\"}"), bound_result});
  auto const after_output_item = ava::session::validate_session_replay(
      {bound_function, bound_commit, make_text("window_output_item", "window-next-turn", 0, "window-msg", 0), bound_result});
  auto const after_turn_commit =
      ava::session::validate_session_replay({bound_function, bound_commit, make_commit("window_turn_commit", "window-next-turn", 0), bound_result});
  auto const after_compaction = ava::session::validate_session_replay(
      {bound_function, bound_commit, make_entry("window_compaction", EntryType::Compaction, "{\"summary\":\"next\"}"), bound_result});
  expect(allowed_window.ok() && has_replay_issue(after_user, ava::session::SessionReplayIssueKind::ToolResultOutputItemMismatch) &&
             has_replay_issue(after_assistant, ava::session::SessionReplayIssueKind::ToolResultOutputItemMismatch) &&
             has_replay_issue(after_output_item, ava::session::SessionReplayIssueKind::ToolResultOutputItemMismatch) &&
             has_replay_issue(after_turn_commit, ava::session::SessionReplayIssueKind::ToolResultOutputItemMismatch) &&
             has_replay_issue(after_compaction, ava::session::SessionReplayIssueKind::ToolResultOutputItemMismatch),
         "v4 tool results remain valid across bookkeeping but must stay in their committed turn's immediate post-commit window");

  auto reconciliation_rejects = [](std::vector<SessionEntry> const& entries) { return !ava::session::find_unresolved_committed_function_calls(entries); };
  auto const next_unresolved_function =
      make_function("out_current_unresolved", "turn_current_unresolved", 0, "fc_current_unresolved", 0, "call_current_unresolved", "read_file");
  auto const next_unresolved_commit = make_commit("commit_current_unresolved", "turn_current_unresolved", 1, "tool_calls");
  auto const closed_then_current_unresolved = ava::session::find_unresolved_committed_function_calls(
      {bound_function, bound_commit, make_entry("closed_window_user", EntryType::UserMessage, "{\"text\":\"later\"}"), next_unresolved_function,
       next_unresolved_commit});
  auto missing_output_binding = bound_result;
  missing_output_binding.id = "missing-output-binding";
  missing_output_binding.data_json =
      "{\"assistant_output_entry_id\":\"unknown-output\",\"call_id\":\"call_bound\",\"name\":\"read_file\",\"success\":true,\"result\":\"ok\"}";
  auto empty_output_binding = missing_output_binding;
  empty_output_binding.id = "empty-output-binding";
  empty_output_binding.data_json = "{\"assistant_output_entry_id\":\"\",\"call_id\":\"call_bound\",\"name\":\"read_file\",\"success\":true,\"result\":\"ok\"}";
  auto wrong_type_output_binding = missing_output_binding;
  wrong_type_output_binding.id = "wrong-type-output-binding";
  wrong_type_output_binding.data_json =
      "{\"assistant_output_entry_id\":false,\"call_id\":\"call_bound\",\"name\":\"read_file\",\"success\":true,\"result\":\"ok\"}";
  auto nonfunction_output = make_text("nonfunction-output", "nonfunction-turn", 0, "nonfunction-msg", 0);
  auto nonfunction_commit = make_commit("nonfunction-commit", "nonfunction-turn", 1);
  auto nonfunction_binding = missing_output_binding;
  nonfunction_binding.id = "nonfunction-binding";
  nonfunction_binding.data_json =
      "{\"assistant_output_entry_id\":\"nonfunction-output\",\"call_id\":\"call_bound\",\"name\":\"read_file\",\"success\":true,\"result\":\"ok\"}";
  auto out_of_window_binding = bound_result;
  out_of_window_binding.id = "out-of-window-binding";
  auto missing_binding_for_function = bound_result;
  missing_binding_for_function.id = "missing-binding-for-function";
  missing_binding_for_function.data_json = "{\"call_id\":\"call_bound\",\"name\":\"read_file\",\"success\":true,\"result\":\"ok\"}";
  auto wrong_call_binding = bound_result;
  wrong_call_binding.id = "wrong-call-binding";
  wrong_call_binding.data_json =
      "{\"assistant_output_entry_id\":\"out_bound_function\",\"call_id\":\"other\",\"name\":\"read_file\",\"success\":true,\"result\":\"ok\"}";
  auto future_bound_function = make_function("future-bound-function", "future-bound-turn", 0, "future-fc", 0, "future-call", "read_file");
  auto future_bound_commit = make_commit("future-bound-commit", "future-bound-turn", 1, "tool_calls");
  auto future_binding = make_entry("future-binding", EntryType::ToolResult,
                                   "{\"assistant_output_entry_id\":\"future-bound-function\",\"call_id\":\"future-call\",\"name\":\"read_file\","
                                   "\"success\":true,\"result\":\"ok\"}");
  auto duplicate_bound_result = bound_result;
  duplicate_bound_result.id = "duplicate-bound-result";
  auto exact_reconciled = ava::session::find_unresolved_committed_function_calls({bound_function, bound_commit, bound_result});
  expect(exact_reconciled && exact_reconciled->empty() && !closed_then_current_unresolved &&
             closed_then_current_unresolved.error().message().find("active EOF tool-result window") != std::string::npos &&
             reconciliation_rejects({bound_function, bound_commit, missing_output_binding}) &&
             reconciliation_rejects({bound_function, bound_commit, empty_output_binding}) &&
             reconciliation_rejects({bound_function, bound_commit, wrong_type_output_binding}) &&
             reconciliation_rejects({bound_function, bound_commit, nonfunction_output, nonfunction_commit, nonfunction_binding}) &&
             reconciliation_rejects({bound_function, bound_commit, make_entry("window-user-for-reconcile", EntryType::UserMessage, "{\"text\":\"next\"}"),
                                     out_of_window_binding}) &&
             reconciliation_rejects({bound_function, bound_commit, missing_binding_for_function}) &&
             reconciliation_rejects({bound_function, bound_commit, wrong_call_binding}) &&
             reconciliation_rejects({future_binding, future_bound_function, future_bound_commit}) &&
             reconciliation_rejects({bound_function, bound_commit, bound_result, duplicate_bound_result}),
         "reconciliation rejects closed unresolved windows before synthetic append, including histories with a later current call");

  auto const compaction_after_unresolved = ava::session::validate_session_replay(
      {bound_function, bound_commit, make_entry("compact_pending", EntryType::Compaction, "{\"summary\":\"pending tool\"}")});
  expect(has_replay_issue(compaction_after_unresolved, ava::session::SessionReplayIssueKind::CompactionWithUnresolvedToolCall),
         "committed v4 function calls participate in compaction unresolved-call boundaries");

  auto v3_native_reasoning =
      make_entry("v3_native_reasoning", EntryType::ReasoningBlock,
                 "{\"provider\":\"openai\",\"model\":\"gpt-5.5\",\"format\":\"openai_responses\",\"text\":\"reasoned\",\"redacted\":false,"
                 "\"native_item_json\":\"{\\\"type\\\":\\\"reasoning\\\",\\\"summary\\\":[]}\"}",
                 3);
  auto const v3_native_validation = ava::session::validate_session_replay({v3_native_reasoning});
  auto const public_markdown = ava::session::format_session_markdown({reasoning, commit});
  expect(has_replay_issue(v3_native_validation, ava::session::SessionReplayIssueKind::InvalidReasoningEntry) &&
             public_markdown.find("PRIVATE_SIGNATURE_CANARY") == std::string::npos && public_markdown.find("PRIVATE_REDACTED_CANARY") == std::string::npos,
         "native OpenAI reasoning remains strict from v3 and private v4 physical records stay out of public export");

  auto future_item = text;
  future_item.version = 5;
  auto future_line = line;
  if (future_line)
  {
    auto future = *future_line;
    future.replace(future.find("\"version\":4"), std::string("\"version\":4").size(), "\"version\":5");
    auto const future_projection = ava::session::classify_assistant_output({future_item});
    expect(!ava::session::parse_session_entry_line(future, "future-v4.jsonl") && future_projection.turns.empty() && !future_projection.diagnostics.empty() &&
               future_projection.diagnostics.front().kind == ava::session::AssistantOutputDiagnosticKind::InvalidAssistantOutputItem,
           "record and v4 classifier reject future entry versions after the v4 bump");
  }
}

void test_session_stats_projects_mixed_v3_v4_history()
{
  using ava::session::EntryType;
  using ava::session::SessionEntry;
  std::vector<SessionEntry> const entries = {
      SessionEntry{
          .id = "legacy_assistant",
          .parent_id = "",
          .type = EntryType::AssistantMessage,
          .timestamp = "2026-07-18T00:00:00Z",
          .data_json = "{\"text\":\"legacy\",\"usage\":{\"input_tokens\":1,\"output_tokens\":2,\"total_tokens\":3,\"source\":\"provider\",\"cost_usd\":0.1}}",
          .version = 3},
      SessionEntry{.id = "v4_text",
                   .parent_id = "legacy_assistant",
                   .type = EntryType::AssistantOutputItem,
                   .timestamp = "2026-07-18T00:00:01Z",
                   .data_json = "{\"schema_version\":1,\"assistant_turn_id\":\"turn_stats\",\"sequence\":0,\"kind\":\"text\",\"text\":\"one\",\"assistant_"
                                "phase\":\"commentary\"}"},
      SessionEntry{.id = "v4_reasoning",
                   .parent_id = "v4_text",
                   .type = EntryType::AssistantOutputItem,
                   .timestamp = "2026-07-18T00:00:02Z",
                   .data_json = "{\"schema_version\":1,\"assistant_turn_id\":\"turn_stats\",\"sequence\":1,\"kind\":\"reasoning\",\"text\":\"think\","
                                "\"format\":\"openai_responses\",\"redacted\":false}"},
      SessionEntry{.id = "v4_function",
                   .parent_id = "v4_reasoning",
                   .type = EntryType::AssistantOutputItem,
                   .timestamp = "2026-07-18T00:00:03Z",
                   .data_json = "{\"schema_version\":1,\"assistant_turn_id\":\"turn_stats\",\"sequence\":2,\"kind\":\"function_call\",\"call_id\":\"call_"
                                "stats\",\"name\":\"read_file\",\"arguments\":\"{}\"}"},
      SessionEntry{.id = "v4_commit",
                   .parent_id = "v4_function",
                   .type = EntryType::AssistantTurnCommit,
                   .timestamp = "2026-07-18T00:00:04Z",
                   .data_json = "{\"schema_version\":1,\"assistant_turn_id\":\"turn_stats\",\"item_count\":3,\"provider\":\"openai\",\"model\":\"gpt-5.5\","
                                "\"finish_reason\":\"tool_calls\",\"usage\":{\"input_tokens\":5,\"output_tokens\":6,\"reasoning_tokens\":4,\"total_tokens\":11,"
                                "\"source\":\"provider\",\"cost_usd\":0.2}}"},
      SessionEntry{.id = "v4_incomplete",
                   .parent_id = "v4_commit",
                   .type = EntryType::AssistantOutputItem,
                   .timestamp = "2026-07-18T00:00:05Z",
                   .data_json = "{\"schema_version\":1,\"assistant_turn_id\":\"turn_incomplete\",\"sequence\":0,\"kind\":\"text\",\"text\":\"must stay "
                                "invisible\",\"assistant_phase\":\"final_answer\"}"},
  };

  auto const stats = ava::session::compute_session_stats(entries);
  expect(stats && stats->entry_count == 4 && stats->first_timestamp == "2026-07-18T00:00:00Z" && stats->last_timestamp == "2026-07-18T00:00:04Z" &&
             stats->counts.assistant_message == 2 && stats->counts.reasoning_block == 1 && stats->counts.tool_call == 1 && stats->input_tokens &&
             *stats->input_tokens == 6 && stats->output_tokens && *stats->output_tokens == 8 && stats->reasoning_tokens && *stats->reasoning_tokens == 4 &&
             stats->total_tokens && *stats->total_tokens == 14 && stats->exact_usage_entries == 2 && stats->estimated_usage_entries == 0 &&
             stats->cost_complete && stats->total_cost_usd && *stats->total_cost_usd > 0.299L && *stats->total_cost_usd < 0.301L,
         "session stats project mixed v3/v4 committed turns once and exclude incomplete v4 staging");

  auto malformed_v4 = entries[1];
  malformed_v4.data_json = "{\"schema_version\":1,\"assistant_turn_id\":\"turn_stats\",\"sequence\":0,\"kind\":\"text\"}";
  auto malformed_stats = ava::session::compute_session_stats({malformed_v4});
  expect(!malformed_stats && malformed_stats.error().message().find("malformed assistant-output") != std::string::npos,
         "session stats fail closed on malformed v4 classifier diagnostics instead of silently omitting them");
}

void test_session_replay_validation()
{
  std::vector<std::string_view> const current_resolution_sources{"client_cancel", "hard_scope", "session_grant", "session_config", "client"};
  expect(std::ranges::all_of(current_resolution_sources, ava::session::valid_resolution_source),
         "session validation accepts current protocol-neutral permission resolution sources");
  std::vector<std::string_view> const legacy_acp_resolution_sources{"acp_client_cancel", "acp_hard_policy", "acp_session_grant",
                                                                    "acp_session_mcp",   "acp_client",      "acp_client_error"};
  expect(
      std::ranges::all_of(legacy_acp_resolution_sources, ava::session::valid_resolution_source) && !ava::session::valid_resolution_source("acp_unknown_source"),
      "session validation accepts only known legacy ACP permission source aliases for read compatibility");

  std::vector<ava::session::SessionEntry> const valid_entries = {
      ava::session::SessionEntry{.id = "start",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::SessionStart,
                                 .timestamp = "2026-04-29T00:00:00Z",
                                 .data_json = "{\"mode\":\"build\",\"provider\":\"openai\","
                                              "\"model\":\"gpt-5.5\",\"prompt_override\":false,"
                                              "\"context_sources\":0}"},
      ava::session::SessionEntry{.id = "user",
                                 .parent_id = "start",
                                 .type = ava::session::EntryType::UserMessage,
                                 .timestamp = "2026-04-29T00:00:01Z",
                                 .data_json = "{\"text\":\"read note\"}"},
      ava::session::SessionEntry{.id = "assistant",
                                 .parent_id = "user",
                                 .type = ava::session::EntryType::AssistantMessage,
                                 .timestamp = "2026-04-29T00:00:02Z",
                                 .data_json = "{\"text\":\"\",\"tool_calls\":1}"},
      ava::session::SessionEntry{.id = "tool_call",
                                 .parent_id = "assistant",
                                 .type = ava::session::EntryType::ToolCall,
                                 .timestamp = "2026-04-29T00:00:03Z",
                                 .data_json = "{\"call_id\":\"call_read\",\"name\":\"read_file\","
                                              "\"arguments\":\"{\\\"path\\\":\\\"note.txt\\\"}\"}"},
      ava::session::SessionEntry{.id = "tool_result",
                                 .parent_id = "tool_call",
                                 .type = ava::session::EntryType::ToolResult,
                                 .timestamp = "2026-04-29T00:00:04Z",
                                 .data_json = "{\"call_id\":\"call_read\",\"name\":\"read_file\",\"success\":true,"
                                              "\"status\":\"success\",\"result\":\"note contents\","
                                              "\"structured_result\":{\"schema_version\":1,\"call_id\":\"call_read\","
                                              "\"tool\":\"read_file\",\"status\":\"success\",\"ok\":true,"
                                              "\"content_type\":\"text/plain\",\"content\":\"note contents\"}}"},
  };

  auto const valid =
      ava::session::validate_session_replay(valid_entries, ava::session::SessionReplayValidationOptions{.require_structured_tool_results = true});
  expect(valid.ok() && valid.issues.empty(), "session replay validator accepts paired structured tool history");

  auto nested_structured_value = [](std::size_t depth) {
    std::string value;
    value.reserve(depth * 6 + 1);
    for (std::size_t index = 0; index < depth; ++index) value += "{\"x\":";
    value += '0';
    value.append(depth, '}');
    return value;
  };
  auto over_depth_structured_entries = valid_entries;
  over_depth_structured_entries.back().data_json =
      "{\"call_id\":\"call_read\",\"name\":\"read_file\",\"success\":true,\"status\":\"success\",\"result\":\"note contents\","
      "\"structured_result\":{\"schema_version\":1,\"call_id\":\"call_read\",\"tool\":\"read_file\",\"status\":\"success\",\"ok\":true,"
      "\"content_type\":\"application/json\",\"content\":" +
      nested_structured_value(ava::core::json::kMaxNestingDepth + 1) + "}}";
  auto const over_depth_structured = ava::session::validate_session_replay(
      over_depth_structured_entries, ava::session::SessionReplayValidationOptions{.require_structured_tool_results = true});
  expect(!over_depth_structured.ok() && has_replay_issue(over_depth_structured, ava::session::SessionReplayIssueKind::InvalidStructuredToolResult) &&
             std::ranges::any_of(over_depth_structured.issues, [](auto const& issue) { return issue.message.find("not valid JSON") != std::string::npos; }),
         "session replay rejects over-depth structured tool results through the shared JSON nesting boundary");

  auto unsupported_version_entries = valid_entries;
  unsupported_version_entries[1].version = ava::session::kCurrentSessionEntryVersion + 1;
  auto const unsupported_version = ava::session::validate_session_replay(unsupported_version_entries);
  expect(!unsupported_version.ok() && has_replay_issue(unsupported_version, ava::session::SessionReplayIssueKind::UnsupportedEntryVersion),
         "session replay validator flags unsupported in-memory entry versions");

  std::vector<ava::session::SessionEntry> const duplicate_entry_entries = {
      valid_entries[0],
      valid_entries[0],
  };
  auto const duplicate_entry = ava::session::validate_session_replay(duplicate_entry_entries);
  expect(!duplicate_entry.ok() && has_replay_issue(duplicate_entry, ava::session::SessionReplayIssueKind::DuplicateEntryId),
         "session replay validator flags duplicate entry ids");

  std::vector<ava::session::SessionEntry> const unknown_parent_entries = {
      ava::session::SessionEntry{.id = "child",
                                 .parent_id = "missing_parent",
                                 .type = ava::session::EntryType::UserMessage,
                                 .timestamp = "2026-04-29T00:00:00Z",
                                 .data_json = "{\"text\":\"orphan\"}"},
  };
  auto const unknown_parent = ava::session::validate_session_replay(unknown_parent_entries);
  expect(!unknown_parent.ok() && has_replay_issue(unknown_parent, ava::session::SessionReplayIssueKind::UnknownParentId),
         "session replay validator flags parent ids that do not reference earlier entries");

  std::vector<ava::session::SessionEntry> const result_without_call_entries = {
      ava::session::SessionEntry{.id = "tool_result",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::ToolResult,
                                 .timestamp = "2026-04-29T00:00:00Z",
                                 .data_json = "{\"call_id\":\"call_missing\",\"name\":\"read_file\","
                                              "\"success\":true,\"result\":\"orphan\"}"},
  };
  auto const result_without_call = ava::session::validate_session_replay(result_without_call_entries);
  expect(!result_without_call.ok() && has_replay_issue(result_without_call, ava::session::SessionReplayIssueKind::ToolResultWithoutCall),
         "session replay validator flags tool results without earlier tool calls");

  auto mismatch_entries = valid_entries;
  mismatch_entries.back().data_json = "{\"call_id\":\"call_read\",\"name\":\"bash\",\"success\":true,\"result\":\"wrong tool\"}";
  auto const mismatch = ava::session::validate_session_replay(mismatch_entries);
  expect(!mismatch.ok() && has_replay_issue(mismatch, ava::session::SessionReplayIssueKind::ToolResultToolMismatch),
         "session replay validator flags tool result name mismatches");

  auto unresolved_entries = valid_entries;
  unresolved_entries.pop_back();
  auto const unresolved = ava::session::validate_session_replay(unresolved_entries);
  expect(!unresolved.ok() && has_replay_issue(unresolved, ava::session::SessionReplayIssueKind::UnresolvedToolCall),
         "session replay validator flags unresolved tool calls");

  auto missing_structured_entries = valid_entries;
  missing_structured_entries.back().data_json =
      "{\"call_id\":\"call_read\",\"name\":\"read_file\",\"success\":true,\"status\":\"success\","
      "\"result\":\"legacy result\"}";
  auto const missing_structured =
      ava::session::validate_session_replay(missing_structured_entries, ava::session::SessionReplayValidationOptions{.require_structured_tool_results = true});
  expect(!missing_structured.ok() && has_replay_issue(missing_structured, ava::session::SessionReplayIssueKind::MissingStructuredToolResult),
         "session replay validator can require structured tool result payloads");

  auto structured_mismatch_entries = valid_entries;
  structured_mismatch_entries.back().data_json =
      "{\"call_id\":\"call_read\",\"name\":\"read_file\",\"success\":true,\"status\":\"success\","
      "\"result\":\"note contents\",\"structured_result\":{\"schema_version\":1,"
      "\"call_id\":\"call_other\",\"tool\":\"read_file\",\"status\":\"success\",\"ok\":true,"
      "\"content_type\":\"text/plain\",\"content\":\"note contents\"}}";
  auto const structured_mismatch =
      ava::session::validate_session_replay(structured_mismatch_entries, ava::session::SessionReplayValidationOptions{.require_structured_tool_results = true});
  expect(!structured_mismatch.ok() && has_replay_issue(structured_mismatch, ava::session::SessionReplayIssueKind::StructuredToolResultMismatch),
         "session replay validator flags structured result call/tool/status mismatches");

  auto missing_schema_entries = valid_entries;
  missing_schema_entries.back().data_json =
      "{\"call_id\":\"call_read\",\"name\":\"read_file\",\"success\":true,\"status\":\"success\","
      "\"result\":\"note contents\",\"structured_result\":{\"call_id\":\"call_read\","
      "\"tool\":\"read_file\",\"status\":\"success\",\"ok\":true,"
      "\"content_type\":\"text/plain\",\"content\":\"note contents\"}}";
  auto const missing_schema =
      ava::session::validate_session_replay(missing_schema_entries, ava::session::SessionReplayValidationOptions{.require_structured_tool_results = true});
  expect(!missing_schema.ok() && has_replay_issue(missing_schema, ava::session::SessionReplayIssueKind::InvalidStructuredToolResult),
         "session replay validator requires structured result schema versions");

  auto missing_ok_entries = valid_entries;
  missing_ok_entries.back().data_json =
      "{\"call_id\":\"call_read\",\"name\":\"read_file\",\"success\":true,\"status\":\"success\","
      "\"result\":\"note contents\",\"structured_result\":{\"schema_version\":1,\"call_id\":\"call_read\","
      "\"tool\":\"read_file\",\"status\":\"success\","
      "\"content_type\":\"text/plain\",\"content\":\"note contents\"}}";
  auto const missing_ok =
      ava::session::validate_session_replay(missing_ok_entries, ava::session::SessionReplayValidationOptions{.require_structured_tool_results = true});
  expect(!missing_ok.ok() && has_replay_issue(missing_ok, ava::session::SessionReplayIssueKind::InvalidStructuredToolResult),
         "session replay validator requires structured result ok flags");

  auto ok_mismatch_entries = valid_entries;
  ok_mismatch_entries.back().data_json =
      "{\"call_id\":\"call_read\",\"name\":\"read_file\",\"success\":true,\"status\":\"success\","
      "\"result\":\"note contents\",\"structured_result\":{\"schema_version\":1,\"call_id\":\"call_read\","
      "\"tool\":\"read_file\",\"status\":\"success\",\"ok\":false,"
      "\"content_type\":\"text/plain\",\"content\":\"note contents\"}}";
  auto const ok_mismatch =
      ava::session::validate_session_replay(ok_mismatch_entries, ava::session::SessionReplayValidationOptions{.require_structured_tool_results = true});
  expect(!ok_mismatch.ok() && has_replay_issue(ok_mismatch, ava::session::SessionReplayIssueKind::StructuredToolResultMismatch),
         "session replay validator requires structured result ok to match success status");

  auto missing_content_entries = valid_entries;
  missing_content_entries.back().data_json =
      "{\"call_id\":\"call_read\",\"name\":\"read_file\",\"success\":true,\"status\":\"success\","
      "\"result\":\"note contents\",\"structured_result\":{\"schema_version\":1,\"call_id\":\"call_read\","
      "\"tool\":\"read_file\",\"status\":\"success\",\"ok\":true,"
      "\"content_type\":\"text/plain\"}}";
  auto const missing_content =
      ava::session::validate_session_replay(missing_content_entries, ava::session::SessionReplayValidationOptions{.require_structured_tool_results = true});
  expect(!missing_content.ok() && has_replay_issue(missing_content, ava::session::SessionReplayIssueKind::InvalidStructuredToolResult),
         "session replay validator requires structured result content data");

  auto invalid_metadata_entries = valid_entries;
  invalid_metadata_entries.back().data_json =
      "{\"call_id\":\"call_read\",\"name\":\"read_file\",\"success\":true,\"status\":\"success\","
      "\"result\":\"note contents\",\"structured_result\":{\"schema_version\":1,\"call_id\":\"call_read\","
      "\"tool\":\"read_file\",\"status\":\"success\",\"ok\":true,"
      "\"content_type\":\"text/plain\",\"content\":\"note contents\","
      "\"changed_paths\":[\"note.txt\",\"note.txt\"]}}";
  auto const invalid_metadata =
      ava::session::validate_session_replay(invalid_metadata_entries, ava::session::SessionReplayValidationOptions{.require_structured_tool_results = true});
  expect(!invalid_metadata.ok() && has_replay_issue(invalid_metadata, ava::session::SessionReplayIssueKind::InvalidStructuredToolResult),
         "session replay validator rejects malformed structured result metadata arrays");

  auto failed_missing_error_entries = valid_entries;
  failed_missing_error_entries.back().data_json =
      "{\"call_id\":\"call_read\",\"name\":\"read_file\",\"success\":false,\"status\":\"error\","
      "\"result\":\"failed\",\"structured_result\":{\"schema_version\":1,\"call_id\":\"call_read\","
      "\"tool\":\"read_file\",\"status\":\"error\",\"ok\":false,"
      "\"content_type\":\"text/plain\",\"content\":\"failed\"}}";
  auto const failed_missing_error = ava::session::validate_session_replay(
      failed_missing_error_entries, ava::session::SessionReplayValidationOptions{.require_structured_tool_results = true});
  expect(!failed_missing_error.ok() && has_replay_issue(failed_missing_error, ava::session::SessionReplayIssueKind::InvalidStructuredToolResult),
         "session replay validator requires failed structured results to carry error details");

  std::vector<ava::session::SessionEntry> const valid_permission_entries = {
      ava::session::SessionEntry{.id = "permission_policy_allow",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::PermissionDecision,
                                 .timestamp = "2026-04-29T00:00:00Z",
                                 .data_json = "{\"permission_request_id\":\"permreq_read\","
                                              "\"operation\":\"read\",\"mode\":\"build\","
                                              "\"tool_name\":\"read_file\",\"action\":\"allow\","
                                              "\"reason\":\"allowed by default workspace policy\","
                                              "\"risk\":\"low\",\"target_path\":\"note.txt\",\"resolution\":\"allow\","
                                              "\"resolution_source\":\"policy\"}"},
      ava::session::SessionEntry{.id = "permission_ask",
                                 .parent_id = "permission_policy_allow",
                                 .type = ava::session::EntryType::PermissionDecision,
                                 .timestamp = "2026-04-29T00:00:01Z",
                                 .data_json = "{\"permission_request_id\":\"permreq_edit\","
                                              "\"operation\":\"edit\",\"mode\":\"build\","
                                              "\"tool_name\":\"write_file\",\"action\":\"ask\","
                                              "\"reason\":\"target is outside the workspace\","
                                              "\"risk\":\"high\",\"target_path\":\"/tmp/outside.txt\","
                                              "\"resolution_source\":\"policy\"}"},
      ava::session::SessionEntry{.id = "permission_resolution",
                                 .parent_id = "permission_ask",
                                 .type = ava::session::EntryType::PermissionDecision,
                                 .timestamp = "2026-04-29T00:00:02Z",
                                 .data_json = "{\"permission_request_id\":\"permreq_edit\","
                                              "\"operation\":\"edit\",\"mode\":\"build\","
                                              "\"tool_name\":\"write_file\",\"action\":\"ask\","
                                              "\"reason\":\"target is outside the workspace\","
                                              "\"risk\":\"high\",\"target_path\":\"/tmp/outside.txt\","
                                              "\"resolution\":\"deny\",\"resolution_source\":\"resolver\"}"},
      ava::session::SessionEntry{.id = "permission_lsp_launch",
                                 .parent_id = "permission_resolution",
                                 .type = ava::session::EntryType::PermissionDecision,
                                 .timestamp = "2026-04-29T00:00:03Z",
                                 .data_json = "{\"permission_request_id\":\"permreq_lsp\","
                                              "\"operation\":\"lsp.server.launch\",\"mode\":\"build\","
                                              "\"tool_name\":\"lsp_server_launch\",\"action\":\"allow\","
                                              "\"reason\":\"LSP server launch requires explicit approval\","
                                              "\"risk\":\"high\",\"command\":\"[\\\"clangd\\\"]\","
                                              "\"resolution\":\"allow\",\"resolution_source\":\"policy\"}"},
      ava::session::SessionEntry{.id = "permission_mcp_resource",
                                 .parent_id = "permission_lsp_launch",
                                 .type = ava::session::EntryType::PermissionDecision,
                                 .timestamp = "2026-04-29T00:00:04Z",
                                 .data_json = "{\"permission_request_id\":\"permreq_mcp_resource\","
                                              "\"operation\":\"mcp.resource.read\",\"mode\":\"build\","
                                              "\"tool_name\":\"mcp_demo_resource\",\"action\":\"allow\","
                                              "\"reason\":\"MCP resource read requires permission\","
                                              "\"risk\":\"medium\",\"command\":\"demo:file:///workspace/notes.md\","
                                              "\"resolution\":\"allow\",\"resolution_source\":\"policy\"}"},
  };
  auto const valid_permission = ava::session::validate_session_replay(valid_permission_entries);
  expect(valid_permission.ok() && valid_permission.issues.empty(), "session replay validator accepts complete permission audit decisions");

  std::vector<ava::session::SessionEntry> const valid_session_grant_entries = {
      ava::session::SessionEntry{.id = "permission_ask",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::PermissionDecision,
                                 .timestamp = "2026-04-29T00:00:01Z",
                                 .data_json = "{\"permission_request_id\":\"permreq_granted\","
                                              "\"operation\":\"read\",\"mode\":\"build\","
                                              "\"tool_name\":\"read_file\",\"action\":\"ask\","
                                              "\"reason\":\"target is outside the workspace\","
                                              "\"risk\":\"high\",\"target_path\":\"/tmp/outside.txt\","
                                              "\"resolution_source\":\"policy\"}"},
      ava::session::SessionEntry{.id = "permission_granted",
                                 .parent_id = "permission_ask",
                                 .type = ava::session::EntryType::PermissionDecision,
                                 .timestamp = "2026-04-29T00:00:02Z",
                                 .data_json = "{\"permission_request_id\":\"permreq_granted\","
                                              "\"operation\":\"read\",\"mode\":\"build\","
                                              "\"tool_name\":\"read_file\",\"action\":\"ask\","
                                              "\"reason\":\"target is outside the workspace\","
                                              "\"risk\":\"high\",\"target_path\":\"/tmp/outside.txt\","
                                              "\"resolution\":\"allow\","
                                              "\"resolution_source\":\"session_grant\"}"},
  };
  auto const valid_session_grant = ava::session::validate_session_replay(valid_session_grant_entries);
  expect(valid_session_grant.ok() && valid_session_grant.issues.empty(), "session replay validator accepts permission outcomes resolved by session grants");

  std::vector<ava::session::SessionEntry> const invalid_permission_entries = {
      ava::session::SessionEntry{.id = "permission_invalid",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::PermissionDecision,
                                 .timestamp = "2026-04-29T00:00:00Z",
                                 .data_json = "{\"operation\":\"not-real\",\"mode\":\"build\","
                                              "\"tool_name\":\"read_file\",\"action\":\"allow\","
                                              "\"reason\":\"bad\",\"resolution\":\"allow\","
                                              "\"resolution_source\":\"policy\"}"},
  };
  auto const invalid_permission = ava::session::validate_session_replay(invalid_permission_entries);
  expect(!invalid_permission.ok() && has_replay_issue(invalid_permission, ava::session::SessionReplayIssueKind::InvalidPermissionDecision),
         "session replay validator flags malformed permission audit decisions");

  std::vector<ava::session::SessionEntry> const invalid_risk_entries = {
      ava::session::SessionEntry{.id = "permission_invalid_risk",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::PermissionDecision,
                                 .timestamp = "2026-04-29T00:00:00Z",
                                 .data_json = "{\"permission_request_id\":\"permreq_bad\","
                                              "\"operation\":\"read\",\"mode\":\"build\","
                                              "\"tool_name\":\"read_file\",\"action\":\"allow\","
                                              "\"reason\":\"bad\",\"risk\":\"extreme\","
                                              "\"resolution\":\"allow\",\"resolution_source\":\"policy\"}"},
  };
  auto const invalid_risk = ava::session::validate_session_replay(invalid_risk_entries);
  expect(!invalid_risk.ok() && has_replay_issue(invalid_risk, ava::session::SessionReplayIssueKind::InvalidPermissionDecision),
         "session replay validator flags malformed permission risk values");

  std::vector<ava::session::SessionEntry> const resolution_without_ask_entries = {
      ava::session::SessionEntry{.id = "permission_resolution_without_ask",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::PermissionDecision,
                                 .timestamp = "2026-04-29T00:00:00Z",
                                 .data_json = "{\"operation\":\"edit\",\"mode\":\"build\","
                                              "\"tool_name\":\"write_file\",\"action\":\"ask\","
                                              "\"reason\":\"target is outside the workspace\","
                                              "\"target_path\":\"/tmp/outside.txt\","
                                              "\"resolution\":\"allow\",\"resolution_source\":\"resolver\"}"},
  };
  auto const resolution_without_ask = ava::session::validate_session_replay(resolution_without_ask_entries);
  expect(!resolution_without_ask.ok() && has_replay_issue(resolution_without_ask, ava::session::SessionReplayIssueKind::PermissionResolutionWithoutAsk),
         "session replay validator flags resolver outcomes without earlier ask prompts");

  auto mismatched_permission_id_entries = valid_permission_entries;
  mismatched_permission_id_entries.back().data_json =
      "{\"permission_request_id\":\"permreq_other\",\"operation\":\"edit\",\"mode\":\"build\","
      "\"tool_name\":\"write_file\",\"action\":\"ask\","
      "\"reason\":\"target is outside the workspace\","
      "\"risk\":\"high\",\"target_path\":\"/tmp/outside.txt\","
      "\"resolution\":\"deny\",\"resolution_source\":\"resolver\"}";
  auto const mismatched_permission_id = ava::session::validate_session_replay(mismatched_permission_id_entries);
  expect(!mismatched_permission_id.ok() && has_replay_issue(mismatched_permission_id, ava::session::SessionReplayIssueKind::PermissionResolutionWithoutAsk),
         "session replay validator pairs permission resolver outcomes by stable request id when present");

  std::vector<ava::session::SessionEntry> const unresolved_permission_entries = {
      ava::session::SessionEntry{.id = "permission_unresolved",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::PermissionDecision,
                                 .timestamp = "2026-04-29T00:00:00Z",
                                 .data_json = "{\"operation\":\"network.fetch\",\"mode\":\"build\","
                                              "\"tool_name\":\"webfetch\",\"action\":\"ask\","
                                              "\"reason\":\"network fetch requires explicit approval\","
                                              "\"command\":\"https://example.com\","
                                              "\"resolution_source\":\"policy\"}"},
  };
  auto const unresolved_permission = ava::session::validate_session_replay(unresolved_permission_entries);
  expect(!unresolved_permission.ok() && has_replay_issue(unresolved_permission, ava::session::SessionReplayIssueKind::UnresolvedPermissionPrompt),
         "session replay validator flags ask permission prompts without outcomes");

  auto valid_compaction_entries = valid_entries;
  valid_compaction_entries.push_back(ava::session::SessionEntry{.id = "compaction",
                                                                .parent_id = "tool_result",
                                                                .type = ava::session::EntryType::Compaction,
                                                                .timestamp = "2026-04-29T00:00:05Z",
                                                                .data_json =
                                                                    "{\"trigger\":\"manual\",\"status\":\"recorded\",\"summary_unavailable\":false,"
                                                                    "\"summary\":\"read_file returned note contents\","
                                                                    "\"instructions\":\"keep the file result\",\"model\":\"gpt-5.5\","
                                                                    "\"threshold_tokens\":100,\"estimated_tokens\":125,"
                                                                    "\"keep_recent_tokens\":64,\"keep_recent_messages\":4,\"max_summary_bytes\":65536}"});
  auto const valid_compaction =
      ava::session::validate_session_replay(valid_compaction_entries, ava::session::SessionReplayValidationOptions{.require_structured_tool_results = true});
  expect(valid_compaction.ok() && valid_compaction.issues.empty(), "session replay validator accepts compaction after resolved tool state");

  auto invalid_compaction_entries = valid_entries;
  invalid_compaction_entries.push_back(ava::session::SessionEntry{.id = "compaction_invalid",
                                                                  .parent_id = "tool_result",
                                                                  .type = ava::session::EntryType::Compaction,
                                                                  .timestamp = "2026-04-29T00:00:05Z",
                                                                  .data_json = "{\"status\":\"recorded\","
                                                                               "\"summary_unavailable\":false,"
                                                                               "\"summary\":\"\"}"});
  auto const invalid_compaction = ava::session::validate_session_replay(invalid_compaction_entries);
  expect(!invalid_compaction.ok() && has_replay_issue(invalid_compaction, ava::session::SessionReplayIssueKind::InvalidCompactionEntry),
         "session replay validator flags compaction entries without durable summaries");

  auto malformed_compaction_metadata_entries = valid_entries;
  malformed_compaction_metadata_entries.push_back(ava::session::SessionEntry{.id = "compaction_malformed_metadata",
                                                                             .parent_id = "tool_result",
                                                                             .type = ava::session::EntryType::Compaction,
                                                                             .timestamp = "2026-04-29T00:00:05Z",
                                                                             .data_json = "{\"status\":\"recorded\",\"summary\":\"durable summary\","
                                                                                          "\"summary_unavailable\":false,\"threshold_tokens\":1.5}"});
  auto const malformed_compaction_metadata = ava::session::validate_session_replay(malformed_compaction_metadata_entries);
  expect(!malformed_compaction_metadata.ok() && has_replay_issue(malformed_compaction_metadata, ava::session::SessionReplayIssueKind::InvalidCompactionEntry),
         "session replay validator flags non-integer compaction token metadata");

  auto unresolved_tool_compaction_entries = valid_entries;
  unresolved_tool_compaction_entries.pop_back();
  unresolved_tool_compaction_entries.push_back(ava::session::SessionEntry{.id = "compaction_before_tool_result",
                                                                          .parent_id = "tool_call",
                                                                          .type = ava::session::EntryType::Compaction,
                                                                          .timestamp = "2026-04-29T00:00:04Z",
                                                                          .data_json = "{\"summary\":\"tool call still pending\"}"});
  auto const unresolved_tool_compaction = ava::session::validate_session_replay(unresolved_tool_compaction_entries);
  expect(
      !unresolved_tool_compaction.ok() && has_replay_issue(unresolved_tool_compaction, ava::session::SessionReplayIssueKind::CompactionWithUnresolvedToolCall),
      "session replay validator flags compaction before unresolved tool results");

  std::vector<ava::session::SessionEntry> const unresolved_permission_compaction_entries = {
      unresolved_permission_entries[0],
      ava::session::SessionEntry{.id = "compaction_before_permission_resolution",
                                 .parent_id = "permission_unresolved",
                                 .type = ava::session::EntryType::Compaction,
                                 .timestamp = "2026-04-29T00:00:01Z",
                                 .data_json = "{\"summary\":\"permission prompt still pending\"}"},
  };
  auto const unresolved_permission_compaction = ava::session::validate_session_replay(unresolved_permission_compaction_entries);
  expect(!unresolved_permission_compaction.ok() &&
             has_replay_issue(unresolved_permission_compaction, ava::session::SessionReplayIssueKind::CompactionWithUnresolvedPermissionPrompt),
         "session replay validator flags compaction before unresolved permission decisions");

  std::vector<ava::session::SessionEntry> const valid_model_reasoning_entries = {
      ava::session::SessionEntry{.id = "model_start",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::SessionStart,
                                 .timestamp = "2026-04-29T00:00:00Z",
                                 .data_json = "{\"mode\":\"build\",\"provider\":\"openai\","
                                              "\"model\":\"gpt-5.5\",\"prompt_override\":false,"
                                              "\"context_sources\":0,\"supports_reasoning\":true}"},
      ava::session::SessionEntry{.id = "model_change",
                                 .parent_id = "model_start",
                                 .type = ava::session::EntryType::ModelChange,
                                 .timestamp = "2026-04-29T00:00:01Z",
                                 .data_json = "{\"previous_provider\":\"openai\","
                                              "\"previous_model\":\"gpt-5.5\","
                                              "\"provider\":\"kimi\",\"model\":\"kimi-k2-thinking\","
                                              "\"supports_reasoning\":true,\"max_output_tokens\":8192}"},
      ava::session::SessionEntry{.id = "reasoning_change",
                                 .parent_id = "model_change",
                                 .type = ava::session::EntryType::ReasoningChange,
                                 .timestamp = "2026-04-29T00:00:02Z",
                                 .data_json = "{\"provider\":\"kimi\",\"model\":\"kimi-k2-thinking\","
                                              "\"format\":\"reasoning_content\",\"enabled\":true,"
                                              "\"level\":\"enabled\"}"},
      ava::session::SessionEntry{.id = "reasoning_block",
                                 .parent_id = "reasoning_change",
                                 .type = ava::session::EntryType::ReasoningBlock,
                                 .timestamp = "2026-04-29T00:00:03Z",
                                 .data_json = "{\"provider\":\"kimi\",\"model\":\"kimi-k2-thinking\","
                                              "\"format\":\"reasoning_content\",\"text\":\"reasoned\","
                                              "\"redacted\":false}"},
  };
  auto const valid_model_reasoning = ava::session::validate_session_replay(valid_model_reasoning_entries);
  expect(valid_model_reasoning.ok() && valid_model_reasoning.issues.empty(), "session replay validator accepts durable model and reasoning metadata");

  auto valid_native_reasoning_item_entries = valid_model_reasoning_entries;
  valid_native_reasoning_item_entries[3].data_json =
      "{\"provider\":\"openai\",\"model\":\"gpt-5.5\",\"format\":\"openai_responses\",\"text\":\"reasoned\",\"redacted\":false,"
      "\"native_item_json\":\"{\\\"id\\\":\\\"rs_session\\\",\\\"type\\\":\\\"reasoning\\\",\\\"summary\\\":[],\\\"encrypted_content\\\":\\\"cipher-"
      "session\\\"}\"}";
  auto const valid_native_reasoning_item = ava::session::validate_session_replay(valid_native_reasoning_item_entries);
  expect(valid_native_reasoning_item.ok(), "session replay validator accepts optional private native reasoning item objects");

  auto invalid_native_reasoning_item_entries = valid_native_reasoning_item_entries;
  invalid_native_reasoning_item_entries[3].data_json =
      "{\"provider\":\"openai\",\"model\":\"gpt-5.5\",\"format\":\"openai_responses\",\"text\":\"reasoned\",\"redacted\":false,"
      "\"native_item_json\":\"{\\\"type\\\":\\\"message\\\"}\"}";
  auto const invalid_native_reasoning_item = ava::session::validate_session_replay(invalid_native_reasoning_item_entries);
  expect(!invalid_native_reasoning_item.ok() && has_replay_issue(invalid_native_reasoning_item, ava::session::SessionReplayIssueKind::InvalidReasoningEntry),
         "session replay validator rejects private native items that are not reasoning objects");

  auto missing_native_reasoning_id_entries = valid_native_reasoning_item_entries;
  missing_native_reasoning_id_entries[3].data_json =
      "{\"provider\":\"openai\",\"model\":\"gpt-5.5\",\"format\":\"openai_responses\",\"text\":\"reasoned\",\"redacted\":false,"
      "\"native_item_json\":\"{\\\"type\\\":\\\"reasoning\\\",\\\"summary\\\":[]}\"}";
  auto const missing_native_reasoning_id = ava::session::validate_session_replay(missing_native_reasoning_id_entries);
  expect(!missing_native_reasoning_id.ok() && has_replay_issue(missing_native_reasoning_id, ava::session::SessionReplayIssueKind::InvalidReasoningEntry),
         "current-version sessions reject native reasoning metadata without an id");

  auto empty_native_reasoning_id_entries = valid_native_reasoning_item_entries;
  empty_native_reasoning_id_entries[3].data_json =
      "{\"provider\":\"openai\",\"model\":\"gpt-5.5\",\"format\":\"openai_responses\",\"text\":\"reasoned\",\"redacted\":false,"
      "\"native_item_json\":\"{\\\"id\\\":\\\"\\\",\\\"type\\\":\\\"reasoning\\\",\\\"summary\\\":[]}\"}";
  auto const empty_native_reasoning_id = ava::session::validate_session_replay(empty_native_reasoning_id_entries);
  expect(!empty_native_reasoning_id.ok() && has_replay_issue(empty_native_reasoning_id, ava::session::SessionReplayIssueKind::InvalidReasoningEntry),
         "current-version sessions reject native reasoning metadata with an empty id");

  auto missing_native_reasoning_summary_entries = valid_native_reasoning_item_entries;
  missing_native_reasoning_summary_entries[3].data_json =
      "{\"provider\":\"openai\",\"model\":\"gpt-5.5\",\"format\":\"openai_responses\",\"text\":\"reasoned\",\"redacted\":false,"
      "\"native_item_json\":\"{\\\"id\\\":\\\"rs_missing_summary\\\",\\\"type\\\":\\\"reasoning\\\"}\"}";
  auto const missing_native_reasoning_summary = ava::session::validate_session_replay(missing_native_reasoning_summary_entries);
  expect(
      !missing_native_reasoning_summary.ok() && has_replay_issue(missing_native_reasoning_summary, ava::session::SessionReplayIssueKind::InvalidReasoningEntry),
      "current-version sessions reject native reasoning metadata without a summary array");

  auto scalar_native_reasoning_summary_entries = valid_native_reasoning_item_entries;
  scalar_native_reasoning_summary_entries[3].data_json =
      "{\"provider\":\"openai\",\"model\":\"gpt-5.5\",\"format\":\"openai_responses\",\"text\":\"reasoned\",\"redacted\":false,"
      "\"native_item_json\":\"{\\\"id\\\":\\\"rs_scalar_summary\\\",\\\"type\\\":\\\"reasoning\\\",\\\"summary\\\":[\\\"not-an-object\\\"]}\"}";
  auto const scalar_native_reasoning_summary = ava::session::validate_session_replay(scalar_native_reasoning_summary_entries);
  expect(
      !scalar_native_reasoning_summary.ok() && has_replay_issue(scalar_native_reasoning_summary, ava::session::SessionReplayIssueKind::InvalidReasoningEntry),
      "current-version sessions reject native reasoning summary arrays with scalar items");

  auto malformed_native_reasoning_summary_entries = valid_native_reasoning_item_entries;
  malformed_native_reasoning_summary_entries[3].data_json =
      "{\"provider\":\"openai\",\"model\":\"gpt-5.5\",\"format\":\"openai_responses\",\"text\":\"reasoned\",\"redacted\":false,"
      "\"native_item_json\":\"{\\\"id\\\":\\\"rs_malformed_summary\\\",\\\"type\\\":\\\"reasoning\\\",\\\"summary\\\":[{\\\"type\\\":\\\"summary_text\\\"}]}"
      "\"}";
  auto const malformed_native_reasoning_summary = ava::session::validate_session_replay(malformed_native_reasoning_summary_entries);
  expect(!malformed_native_reasoning_summary.ok() &&
             has_replay_issue(malformed_native_reasoning_summary, ava::session::SessionReplayIssueKind::InvalidReasoningEntry),
         "current-version sessions reject native reasoning summary items without summary_text text");

  auto oversized_native_reasoning_item_entries = valid_native_reasoning_item_entries;
  auto const oversized_native_item =
      std::string("{\"id\":\"rs_oversized\",\"type\":\"reasoning\",\"summary\":[],\"opaque\":\"") + std::string(64U * 1024U, 'x') + "\"}";
  oversized_native_reasoning_item_entries[3].data_json =
      "{\"provider\":\"openai\",\"model\":\"gpt-5.5\",\"format\":\"openai_responses\",\"text\":\"reasoned\",\"redacted\":false,"
      "\"native_item_json\":\"" +
      ava::core::json::escape(oversized_native_item) + "\"}";
  auto const oversized_native_reasoning_item = ava::session::validate_session_replay(oversized_native_reasoning_item_entries);
  expect(
      !oversized_native_reasoning_item.ok() && has_replay_issue(oversized_native_reasoning_item, ava::session::SessionReplayIssueKind::InvalidReasoningEntry),
      "current-version sessions reject native reasoning metadata beyond the provider-private byte bound");

  auto legacy_native_reasoning_item_entries = missing_native_reasoning_summary_entries;
  legacy_native_reasoning_item_entries[3].version = 0;
  auto const legacy_native_reasoning_item = ava::session::validate_session_replay(legacy_native_reasoning_item_entries);
  expect(legacy_native_reasoning_item.ok(),
         "legacy sessions retain readable fallback compatibility when explicitly present native reasoning metadata is malformed");

  std::vector<ava::session::SessionEntry> const invalid_model_start_entries = {
      ava::session::SessionEntry{.id = "bad_start",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::SessionStart,
                                 .timestamp = "2026-04-29T00:00:00Z",
                                 .data_json = "{\"mode\":\"build\",\"model\":\"gpt-5.5\"}"},
  };
  auto const invalid_model_start = ava::session::validate_session_replay(invalid_model_start_entries);
  expect(!invalid_model_start.ok() && has_replay_issue(invalid_model_start, ava::session::SessionReplayIssueKind::InvalidModelEntry),
         "session replay validator flags session_start entries without provider/model metadata");

  auto invalid_model_change_entries = valid_model_reasoning_entries;
  invalid_model_change_entries[1].data_json =
      "{\"previous_provider\":\"anthropic\",\"previous_model\":\"claude\","
      "\"provider\":\"kimi\",\"model\":\"kimi-k2-thinking\"}";
  auto const invalid_model_change = ava::session::validate_session_replay(invalid_model_change_entries);
  expect(!invalid_model_change.ok() && has_replay_issue(invalid_model_change, ava::session::SessionReplayIssueKind::InvalidModelEntry),
         "session replay validator flags model_change entries whose previous model does not match active state");

  auto invalid_reasoning_change_entries = valid_model_reasoning_entries;
  invalid_reasoning_change_entries[2].data_json = "{\"provider\":\"kimi\",\"model\":\"kimi-k2-thinking\",\"enabled\":true}";
  auto const invalid_reasoning_change = ava::session::validate_session_replay(invalid_reasoning_change_entries);
  expect(!invalid_reasoning_change.ok() && has_replay_issue(invalid_reasoning_change, ava::session::SessionReplayIssueKind::InvalidReasoningEntry),
         "session replay validator flags enabled reasoning_change entries without a level");

  auto mismatched_reasoning_change_entries = valid_model_reasoning_entries;
  mismatched_reasoning_change_entries[2].data_json = "{\"provider\":\"openai\",\"model\":\"gpt-5.5\",\"enabled\":true,\"level\":\"low\"}";
  auto const mismatched_reasoning_change = ava::session::validate_session_replay(mismatched_reasoning_change_entries);
  expect(!mismatched_reasoning_change.ok() && has_replay_issue(mismatched_reasoning_change, ava::session::SessionReplayIssueKind::InvalidReasoningEntry),
         "session replay validator flags reasoning_change entries for the wrong active model");

  auto invalid_reasoning_block_entries = valid_model_reasoning_entries;
  invalid_reasoning_block_entries[3].data_json =
      "{\"provider\":\"kimi\",\"model\":\"kimi-k2-thinking\",\"format\":\"reasoning_content\","
      "\"redacted\":false}";
  auto const invalid_reasoning_block = ava::session::validate_session_replay(invalid_reasoning_block_entries);
  expect(!invalid_reasoning_block.ok() && has_replay_issue(invalid_reasoning_block, ava::session::SessionReplayIssueKind::InvalidReasoningEntry),
         "session replay validator flags reasoning_block entries without replayable content");

  expect(ava::session::to_string(ava::session::SessionReplayIssueKind::InvalidCompactionEntry) == "invalid_compaction_entry",
         "session replay issue kind names include compaction validation failures");
  expect(ava::session::to_string(ava::session::SessionReplayIssueKind::UnsupportedEntryVersion) == "unsupported_entry_version",
         "session replay issue kind names include entry version validation failures");
  expect(ava::session::to_string(ava::session::SessionReplayIssueKind::InvalidReasoningEntry) == "invalid_reasoning_entry",
         "session replay issue kind names include reasoning validation failures");
  expect(ava::session::to_string(ava::session::SessionReplayIssueKind::InvalidSessionMetadataEntry) == "invalid_session_metadata_entry" &&
             ava::session::to_string(ava::session::SessionReplayIssueKind::InvalidBranchSummaryEntry) == "invalid_branch_summary_entry",
         "session replay issue kind names include tree metadata validation failures");
}

void test_session_lease_creation_and_link_safety()
{
  auto const root = temp_root() / "session-lease-creation";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const sessions = root / "sessions";
  std::filesystem::create_directories(workspace);

  ava::session::SessionStore fresh_store(ava::session::SessionStoreOptions{.root_dir = sessions, .workspace_dir = workspace, .session_id = "fresh_owned"});
  expect(!std::filesystem::exists(fresh_store.session_path()), "fresh lease fixture starts without a session file");
  auto fresh_lease = ava::session::SessionLease::create_and_acquire(fresh_store.session_path());
  struct stat fresh_status{};
  bool const fresh_status_valid = stat(fresh_store.session_path().c_str(), &fresh_status) == 0;
  auto fresh_contender = ava::session::SessionLease::acquire(fresh_store.session_path());
  expect(fresh_lease && fresh_status_valid && S_ISREG(fresh_status.st_mode) && (fresh_status.st_mode & 0777) == 0600 && fresh_status.st_nlink == 1 &&
             fresh_status.st_size == 0 && !fresh_contender && fresh_contender.error().message().find("already owned") != std::string::npos,
         "create_and_acquire atomically publishes a mode-0600 regular file already owned before its first append");

  auto created_again = ava::session::SessionLease::create_and_acquire(fresh_store.session_path());
  expect(!created_again && fresh_status_valid && read_binary_file(fresh_store.session_path()).empty(),
         "create_and_acquire uses exclusive creation and leaves an existing fresh session unchanged");

  auto first_append = fresh_lease ? fresh_store.append(*fresh_lease, ava::session::SessionEntry{.id = "fresh_first",
                                                                                                .parent_id = "",
                                                                                                .type = ava::session::EntryType::SessionStart,
                                                                                                .timestamp = "2026-07-14T00:00:00Z",
                                                                                                .data_json = "{\"mode\":\"build\"}"})
                                  : ava::core::VoidResult(std::unexpected(std::move(fresh_lease.error())));
  expect(first_append && read_binary_file(fresh_store.session_path()).ends_with('\n'),
         "the fresh owner can append the first framed record while retaining its lease");
  fresh_lease = ava::session::SessionLease{};

  auto const linked_path = std::filesystem::path(fresh_store.session_path().string() + ".linked");
  std::error_code link_error;
  std::filesystem::create_hard_link(fresh_store.session_path(), linked_path, link_error);
  auto linked_original_lease = ava::session::SessionLease::acquire(fresh_store.session_path());
  auto linked_alias_lease = ava::session::SessionLease::acquire(linked_path);
  expect(!link_error && !linked_original_lease && !linked_alias_lease &&
             linked_original_lease.error().message().find("exactly one link") != std::string::npos &&
             linked_alias_lease.error().message().find("exactly one link") != std::string::npos,
         "existing session leases reject multiply-linked files through either name");
}

void test_session_torn_tail_recovery()
{
  auto const root = temp_root() / "session-torn-tail-recovery";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const sessions = root / "sessions";
  std::filesystem::create_directories(workspace);

  auto first_line = ava::session::serialize_session_entry_line(ava::session::SessionEntry{.id = "entry_first",
                                                                                          .parent_id = "",
                                                                                          .type = ava::session::EntryType::SessionStart,
                                                                                          .timestamp = "2026-07-14T00:00:00Z",
                                                                                          .data_json = "{\"mode\":\"build\"}"});
  auto second_line = ava::session::serialize_session_entry_line(ava::session::SessionEntry{.id = "entry_second",
                                                                                           .parent_id = "entry_first",
                                                                                           .type = ava::session::EntryType::UserMessage,
                                                                                           .timestamp = "2026-07-14T00:00:01Z",
                                                                                           .data_json = "{\"text\":\"escaped \\\\ slash \\\" quote\"}"});
  expect(first_line && second_line, "torn tail recovery test serializes representative entries");
  if (!first_line || !second_line)
    return;
  auto const valid_prefix = *first_line + "\n";
  auto recovery_files_for = [](std::filesystem::path const& session_path) {
    std::vector<std::filesystem::path> files;
    auto const final_prefix = session_path.filename().string() + ".torn-tail.";
    auto const temporary_prefix = "." + session_path.filename().string() + ".torn-tail.tmp.";
    std::error_code iter_error;
    for (std::filesystem::directory_iterator iterator(session_path.parent_path(), iter_error), end; !iter_error && iterator != end;
         iterator.increment(iter_error))
    {
      auto const name = iterator->path().filename().string();
      if (name.starts_with(final_prefix) || name.starts_with(temporary_prefix))
        files.push_back(iterator->path());
    }
    return files;
  };

  bool every_cut_recovered = true;
  for (std::size_t cut = 1; cut < second_line->size(); ++cut)
  {
    ava::session::SessionStore store(
        ava::session::SessionStoreOptions{.root_dir = sessions, .workspace_dir = workspace, .session_id = "cut_" + std::to_string(cut)});
    auto const suffix = second_line->substr(0, cut);
    write_binary_file(store.session_path(), valid_prefix + suffix);
    auto lease = ava::session::SessionLease::acquire(store.session_path());
    auto recovered = lease ? store.recover_torn_tail(*lease, ava::session::legacy_unbounded_session_read_limits())
                           : ava::core::Result<std::optional<std::filesystem::path>>(std::unexpected(std::move(lease.error())));
    if (!recovered || !recovered->has_value() || read_binary_file(store.session_path()) != valid_prefix || read_binary_file(**recovered) != suffix)
    {
      every_cut_recovered = false;
      break;
    }
    struct stat quarantine_status{};
    if (stat(recovered->value().c_str(), &quarantine_status) != 0 || (quarantine_status.st_mode & 0777) != 0600)
    {
      every_cut_recovered = false;
      break;
    }
  }
  expect(every_cut_recovered, "every byte cut of a serialized second entry is quarantined exactly and repaired to the validated prefix");

  ava::session::SessionStore idempotent_store(ava::session::SessionStoreOptions{.root_dir = sessions, .workspace_dir = workspace, .session_id = "idempotent"});
  std::string const idempotent_suffix = "{\"version\":3,\"id\":\"partial";
  write_binary_file(idempotent_store.session_path(), valid_prefix + idempotent_suffix);
  auto idempotent_lease = ava::session::SessionLease::acquire(idempotent_store.session_path());
  auto first_recovery = idempotent_lease ? idempotent_store.recover_torn_tail(*idempotent_lease, ava::session::legacy_unbounded_session_read_limits())
                                         : ava::core::Result<std::optional<std::filesystem::path>>(std::unexpected(std::move(idempotent_lease.error())));
  auto second_recovery =
      idempotent_lease
          ? idempotent_store.recover_torn_tail(*idempotent_lease, ava::session::legacy_unbounded_session_read_limits())
          : ava::core::Result<std::optional<std::filesystem::path>>(std::unexpected(ava::core::Error(ava::core::ErrorCategory::Session, "missing lease")));
  expect(first_recovery && first_recovery->has_value() && second_recovery && !second_recovery->has_value() &&
             read_binary_file(idempotent_store.session_path()) == valid_prefix,
         "torn tail recovery is idempotent after quarantining one invalid suffix");

  ava::session::SessionStore no_lf_store(ava::session::SessionStoreOptions{.root_dir = sessions, .workspace_dir = workspace, .session_id = "complete_no_lf"});
  write_binary_file(no_lf_store.session_path(), *first_line);
  auto no_lf_lease = ava::session::SessionLease::acquire(no_lf_store.session_path());
  auto no_lf_recovered = no_lf_lease ? no_lf_store.recover_torn_tail(*no_lf_lease, ava::session::legacy_unbounded_session_read_limits())
                                     : ava::core::Result<std::optional<std::filesystem::path>>(std::unexpected(std::move(no_lf_lease.error())));
  expect(no_lf_recovered && !no_lf_recovered->has_value() && read_binary_file(no_lf_store.session_path()) == *first_line + "\n",
         "a complete supported final record gains exactly one LF without changing its bytes");

  ava::session::SessionStore framed_store(ava::session::SessionStoreOptions{.root_dir = sessions, .workspace_dir = workspace, .session_id = "framed"});
  write_binary_file(framed_store.session_path(), valid_prefix);
  auto framed_lease = ava::session::SessionLease::acquire(framed_store.session_path());
  auto framed_recovered = framed_lease ? framed_store.recover_torn_tail(*framed_lease, ava::session::legacy_unbounded_session_read_limits())
                                       : ava::core::Result<std::optional<std::filesystem::path>>(std::unexpected(std::move(framed_lease.error())));
  expect(framed_recovered && !framed_recovered->has_value() && read_binary_file(framed_store.session_path()) == valid_prefix,
         "a fully framed session is unchanged by recovery");

  std::vector<std::string> invalid_suffixes = {
      "{\"version\":3,\"id\":\"escape\\",
      std::string("{\"version\":3,\"id\":\"") + std::string("\xF0\x9F", 2),
      std::string("{\"version\":3,\"id\":\"nul") + std::string(1, '\0'),
      "{\r",
  };
  bool special_tails_recovered = true;
  for (std::size_t index = 0; index < invalid_suffixes.size(); ++index)
  {
    ava::session::SessionStore store(
        ava::session::SessionStoreOptions{.root_dir = sessions, .workspace_dir = workspace, .session_id = "special_" + std::to_string(index)});
    write_binary_file(store.session_path(), valid_prefix + invalid_suffixes[index]);
    auto lease = ava::session::SessionLease::acquire(store.session_path());
    auto recovered = lease ? store.recover_torn_tail(*lease, ava::session::legacy_unbounded_session_read_limits())
                           : ava::core::Result<std::optional<std::filesystem::path>>(std::unexpected(std::move(lease.error())));
    if (!recovered || !recovered->has_value() || read_binary_file(**recovered) != invalid_suffixes[index] ||
        read_binary_file(store.session_path()) != valid_prefix)
    {
      special_tails_recovered = false;
      break;
    }
  }
  expect(special_tails_recovered, "escape, partial UTF-8, NUL, and CR torn suffixes are quarantined byte-for-byte");

  ava::session::SessionStore quarantine_failure_store(
      ava::session::SessionStoreOptions{.root_dir = sessions, .workspace_dir = workspace, .session_id = std::string(240, 'q')});
  auto const quarantine_failure_bytes = valid_prefix + "{";
  write_binary_file(quarantine_failure_store.session_path(), quarantine_failure_bytes);
  auto quarantine_failure_lease = ava::session::SessionLease::acquire(quarantine_failure_store.session_path());
  auto quarantine_failure = quarantine_failure_lease
                                ? quarantine_failure_store.recover_torn_tail(*quarantine_failure_lease, ava::session::legacy_unbounded_session_read_limits())
                                : ava::core::Result<std::optional<std::filesystem::path>>(std::unexpected(std::move(quarantine_failure_lease.error())));
  expect(!quarantine_failure && quarantine_failure.error().message().find("quarantine") != std::string::npos &&
             read_binary_file(quarantine_failure_store.session_path()) == quarantine_failure_bytes,
         "a quarantine creation failure leaves the source session byte-for-byte unchanged");

  ava::session::SessionStore oversized_store(ava::session::SessionStoreOptions{.root_dir = sessions, .workspace_dir = workspace, .session_id = "oversized"});
  auto const oversized_bytes = valid_prefix + std::string(ava::session::kMaxSessionLineBytes + 1, '{');
  write_binary_file(oversized_store.session_path(), oversized_bytes);
  auto oversized_lease = ava::session::SessionLease::acquire(oversized_store.session_path());
  auto oversized_recovery = oversized_lease ? oversized_store.recover_torn_tail(*oversized_lease, ava::session::legacy_unbounded_session_read_limits())
                                            : ava::core::Result<std::optional<std::filesystem::path>>(std::unexpected(std::move(oversized_lease.error())));
  expect(!oversized_recovery && oversized_recovery.error().message().find("scan limit") != std::string::npos &&
             read_binary_file(oversized_store.session_path()) == oversized_bytes,
         "an oversized unterminated suffix fails the bounded scan without mutation");

  ava::session::SessionStore byte_limited_store(
      ava::session::SessionStoreOptions{.root_dir = sessions, .workspace_dir = workspace, .session_id = "byte_limited"});
  auto const byte_limited_bytes = valid_prefix + idempotent_suffix;
  write_binary_file(byte_limited_store.session_path(), byte_limited_bytes);
  auto byte_limited_lease = ava::session::SessionLease::acquire(byte_limited_store.session_path());
  auto byte_limited_recovery =
      byte_limited_lease ? byte_limited_store.recover_torn_tail(
                               *byte_limited_lease,
                               ava::session::SessionReadLimits{.max_file_bytes = valid_prefix.size(), .max_line_bytes = valid_prefix.size(), .max_entries = 8})
                         : ava::core::Result<std::optional<std::filesystem::path>>(std::unexpected(std::move(byte_limited_lease.error())));
  expect(!byte_limited_recovery && byte_limited_recovery.error().message().find("byte limit") != std::string::npos &&
             read_binary_file(byte_limited_store.session_path()) == byte_limited_bytes && recovery_files_for(byte_limited_store.session_path()).empty(),
         "recovery enforces the initial file byte limit before publishing quarantine or mutating the source");

  ava::session::SessionStore entry_limited_store(
      ava::session::SessionStoreOptions{.root_dir = sessions, .workspace_dir = workspace, .session_id = "entry_limited"});
  auto const entry_limited_bytes = valid_prefix + *second_line + "\n" + idempotent_suffix;
  write_binary_file(entry_limited_store.session_path(), entry_limited_bytes);
  auto entry_limited_lease = ava::session::SessionLease::acquire(entry_limited_store.session_path());
  auto entry_limited_recovery =
      entry_limited_lease ? entry_limited_store.recover_torn_tail(
                                *entry_limited_lease, ava::session::SessionReadLimits{.max_file_bytes = 4096, .max_line_bytes = 2048, .max_entries = 1})
                          : ava::core::Result<std::optional<std::filesystem::path>>(std::unexpected(std::move(entry_limited_lease.error())));
  expect(!entry_limited_recovery && entry_limited_recovery.error().message().find("entry count") != std::string::npos &&
             read_binary_file(entry_limited_store.session_path()) == entry_limited_bytes && recovery_files_for(entry_limited_store.session_path()).empty(),
         "recovery enforces the entry limit before publishing quarantine or mutating the source");

  ava::session::SessionStore canceled_store(ava::session::SessionStoreOptions{.root_dir = sessions, .workspace_dir = workspace, .session_id = "canceled_scan"});
  auto const canceled_bytes = valid_prefix + idempotent_suffix;
  write_binary_file(canceled_store.session_path(), canceled_bytes);
  auto canceled_lease = ava::session::SessionLease::acquire(canceled_store.session_path());
  int cancellation_checks = 0;
  auto canceled_recovery = canceled_lease ? canceled_store.recover_torn_tail(*canceled_lease, ava::session::legacy_unbounded_session_read_limits(),
                                                                             [&cancellation_checks] { return ++cancellation_checks >= 2; })
                                          : ava::core::Result<std::optional<std::filesystem::path>>(std::unexpected(std::move(canceled_lease.error())));
  expect(!canceled_recovery && canceled_recovery.error().message().find("canceled") != std::string::npos &&
             read_binary_file(canceled_store.session_path()) == canceled_bytes && recovery_files_for(canceled_store.session_path()).empty(),
         "recovery observes cancellation while scanning before any source or quarantine mutation");

  std::vector<std::pair<std::string, std::string>> strict_failures = {
      {"semantic", "{}"},
      {"future", "{\"version\":99,\"id\":\"future\",\"parent_id\":\"\",\"type\":\"user_message\",\"timestamp\":\"2026-07-14T00:00:02Z\",\"data\":{}}"},
      {"unknown", "{\"version\":3,\"id\":\"unknown\",\"parent_id\":\"\",\"type\":\"new_type\",\"timestamp\":\"2026-07-14T00:00:02Z\",\"data\":{}}"},
      {"duplicate",
       "{\"version\":3,\"id\":\"one\",\"id\":\"two\",\"parent_id\":\"\",\"type\":\"user_message\",\"timestamp\":\"2026-07-14T00:00:02Z\",\"data\":{}}"},
  };
  std::string nested = "{\"version\":3,\"id\":\"deep\",\"parent_id\":\"\",\"type\":\"user_message\",\"timestamp\":\"2026-07-14T00:00:02Z\",\"data\":{\"x\":";
  nested += std::string(70, '[') + "0" + std::string(70, ']') + "}}";
  strict_failures.emplace_back("depth", std::move(nested));
  bool strict_failures_unchanged = true;
  for (auto const& [name, suffix] : strict_failures)
  {
    ava::session::SessionStore store(ava::session::SessionStoreOptions{.root_dir = sessions, .workspace_dir = workspace, .session_id = "strict_" + name});
    auto const original = valid_prefix + suffix;
    write_binary_file(store.session_path(), original);
    auto lease = ava::session::SessionLease::acquire(store.session_path());
    auto recovered = lease ? store.recover_torn_tail(*lease, ava::session::legacy_unbounded_session_read_limits())
                           : ava::core::Result<std::optional<std::filesystem::path>>(std::unexpected(std::move(lease.error())));
    if (recovered || read_binary_file(store.session_path()) != original)
    {
      strict_failures_unchanged = false;
      break;
    }
  }
  expect(strict_failures_unchanged, "strict-valid semantic/future/unknown records and duplicate/deep JSON tails fail unchanged");

  bool framed_legacy_records_unchanged = true;
  for (auto const& [name, record] : strict_failures)
  {
    ava::session::SessionStore store(
        ava::session::SessionStoreOptions{.root_dir = sessions, .workspace_dir = workspace, .session_id = "framed_strict_" + name});
    auto const original = valid_prefix + record + "\n";
    write_binary_file(store.session_path(), original);
    auto lease = ava::session::SessionLease::acquire(store.session_path());
    auto recovered = lease ? store.recover_torn_tail(*lease, ava::session::legacy_unbounded_session_read_limits())
                           : ava::core::Result<std::optional<std::filesystem::path>>(std::unexpected(std::move(lease.error())));
    if (!recovered || recovered->has_value() || read_binary_file(store.session_path()) != original)
    {
      framed_legacy_records_unchanged = false;
      break;
    }
  }
  expect(framed_legacy_records_unchanged,
         "newline-terminated legacy semantic/future/unknown and duplicate/deep JSON records bypass strict recovery classification without mutation");

  auto duplicate_id_line = ava::session::serialize_session_entry_line(ava::session::SessionEntry{.id = "entry_first",
                                                                                                 .parent_id = "",
                                                                                                 .type = ava::session::EntryType::UserMessage,
                                                                                                 .timestamp = "2026-07-14T00:00:02Z",
                                                                                                 .data_json = "{\"text\":\"duplicate\"}"});
  auto unknown_parent_line = ava::session::serialize_session_entry_line(ava::session::SessionEntry{.id = "entry_orphan",
                                                                                                   .parent_id = "entry_missing",
                                                                                                   .type = ava::session::EntryType::UserMessage,
                                                                                                   .timestamp = "2026-07-14T00:00:02Z",
                                                                                                   .data_json = "{\"text\":\"orphan\"}"});
  expect(duplicate_id_line && unknown_parent_line, "recovery integrity tests serialize duplicate and orphan records");
  if (duplicate_id_line && unknown_parent_line)
  {
    std::vector<std::pair<std::string, std::string>> const integrity_records = {{"duplicate_id", *duplicate_id_line}, {"unknown_parent", *unknown_parent_line}};
    for (auto const& [name, record] : integrity_records)
    {
      ava::session::SessionStore integrity_store(ava::session::SessionStoreOptions{.root_dir = sessions, .workspace_dir = workspace, .session_id = name});
      auto const integrity_bytes = valid_prefix + record + "\n" + idempotent_suffix;
      write_binary_file(integrity_store.session_path(), integrity_bytes);
      auto integrity_lease = ava::session::SessionLease::acquire(integrity_store.session_path());
      auto integrity_recovery = integrity_lease ? integrity_store.recover_torn_tail(*integrity_lease, ava::session::legacy_unbounded_session_read_limits())
                                                : ava::core::Result<std::optional<std::filesystem::path>>(std::unexpected(std::move(integrity_lease.error())));
      auto const expected_message = name == "duplicate_id" ? "duplicate entry id" : "earlier record";
      expect(!integrity_recovery && integrity_recovery.error().message().find(expected_message) != std::string::npos &&
                 read_binary_file(integrity_store.session_path()) == integrity_bytes && recovery_files_for(integrity_store.session_path()).empty(),
             "recovery rejects " + name + " without source mutation or quarantine publication");
    }
  }

  ava::session::SessionStore middle_store(ava::session::SessionStoreOptions{.root_dir = sessions, .workspace_dir = workspace, .session_id = "middle"});
  auto const middle_bytes = valid_prefix + "{\n" + *second_line + "\n";
  write_binary_file(middle_store.session_path(), middle_bytes);
  auto middle_lease = ava::session::SessionLease::acquire(middle_store.session_path());
  auto middle_recovery = middle_lease ? middle_store.recover_torn_tail(*middle_lease, ava::session::legacy_unbounded_session_read_limits())
                                      : ava::core::Result<std::optional<std::filesystem::path>>(std::unexpected(std::move(middle_lease.error())));
  expect(middle_recovery && !middle_recovery->has_value() && read_binary_file(middle_store.session_path()) == middle_bytes,
         "newline-terminated middle corruption is outside torn-tail recovery and is never mutated");
  auto branch = ava::session::create_session_branch(ava::session::SessionBranchOptions{.workspace_dir = workspace,
                                                                                       .root_dir = sessions,
                                                                                       .source_session_id = "middle",
                                                                                       .branch_from_entry_id = {},
                                                                                       .name = std::nullopt,
                                                                                       .labels = std::nullopt,
                                                                                       .mode = ava::session::SessionBranchMode::Clone,
                                                                                       .actor = "test"});
  expect(!branch && read_binary_file(middle_store.session_path()) == middle_bytes, "low-level branch creation remains read-only and fail-closed on corruption");

  ava::session::SessionStore append_store(ava::session::SessionStoreOptions{.root_dir = sessions, .workspace_dir = workspace, .session_id = "append_guard"});
  auto const first_entry = ava::session::SessionEntry{.id = "append_first",
                                                      .parent_id = "",
                                                      .type = ava::session::EntryType::UserMessage,
                                                      .timestamp = "2026-07-14T00:00:03Z",
                                                      .data_json = "{\"text\":\"first\"}"};
  auto const second_entry = ava::session::SessionEntry{.id = "append_second",
                                                       .parent_id = "append_first",
                                                       .type = ava::session::EntryType::UserMessage,
                                                       .timestamp = "2026-07-14T00:00:04Z",
                                                       .data_json = "{\"text\":\"second\"}"};
  expect(append_session_entry_for_test(append_store, first_entry).has_value(), "append guard test seeds a framed session");
  auto append_unterminated = read_binary_file(append_store.session_path());
  append_unterminated.pop_back();
  write_binary_file(append_store.session_path(), append_unterminated);
  auto guarded_append = append_session_entry_for_test(append_store, second_entry);
  expect(!guarded_append && guarded_append.error().message().find("unterminated tail") != std::string::npos &&
             read_binary_file(append_store.session_path()) == append_unterminated,
         "append refuses to concatenate onto an existing nonempty unterminated session");
  auto append_lease = ava::session::SessionLease::acquire(append_store.session_path());
  auto append_recovered = append_lease ? append_store.recover_torn_tail(*append_lease, ava::session::legacy_unbounded_session_read_limits())
                                       : ava::core::Result<std::optional<std::filesystem::path>>(std::unexpected(std::move(append_lease.error())));
  auto append_after_recovery =
      append_recovered ? append_store.append(*append_lease, second_entry) : ava::core::VoidResult(std::unexpected(append_recovered.error()));
  expect(append_recovered && append_after_recovery && append_store.load() && append_store.load()->size() == 2,
         "append proceeds after the leased recovery restores JSONL framing");

  ava::session::SessionStore replaced_append_store(
      ava::session::SessionStoreOptions{.root_dir = sessions, .workspace_dir = workspace, .session_id = "append_replaced_target"});
  expect(append_session_entry_for_test(replaced_append_store, first_entry).has_value(), "append replacement test seeds the intended inode");
  auto const append_original_bytes = read_binary_file(replaced_append_store.session_path());
  auto const append_replacement_bytes = valid_prefix;
  auto const append_displaced_path = replaced_append_store.session_path().string() + ".displaced";
  replaced_append_store.set_before_append_identity_check_for_test([&] {
    std::filesystem::rename(replaced_append_store.session_path(), append_displaced_path);
    write_binary_file(replaced_append_store.session_path(), append_replacement_bytes);
  });
  auto replaced_append = append_session_entry_for_test(replaced_append_store, second_entry);
  expect(!replaced_append && replaced_append.error().message().find("replaced") != std::string::npos &&
             read_binary_file(append_displaced_path) == append_original_bytes &&
             read_binary_file(replaced_append_store.session_path()) == append_replacement_bytes,
         "append rejects target replacement after opening and mutates neither the displaced inode nor replacement path");

  ava::session::SessionStore post_write_replaced_store(
      ava::session::SessionStoreOptions{.root_dir = sessions, .workspace_dir = workspace, .session_id = "append_post_write_replaced"});
  expect(append_session_entry_for_test(post_write_replaced_store, first_entry).has_value(), "post-write append replacement test seeds the intended inode");
  auto const post_write_original_bytes = read_binary_file(post_write_replaced_store.session_path());
  auto const post_write_displaced_path = post_write_replaced_store.session_path().string() + ".displaced";
  post_write_replaced_store.set_after_append_write_for_test([&] {
    std::filesystem::rename(post_write_replaced_store.session_path(), post_write_displaced_path);
    write_binary_file(post_write_replaced_store.session_path(), append_replacement_bytes);
  });
  auto post_write_replaced = append_session_entry_for_test(post_write_replaced_store, second_entry);
  expect(!post_write_replaced && post_write_replaced.error().message().find("after the entry write") != std::string::npos &&
             read_binary_file(post_write_displaced_path).starts_with(post_write_original_bytes) &&
             read_binary_file(post_write_displaced_path).find("append_second") != std::string::npos &&
             read_binary_file(post_write_displaced_path).ends_with("\n") &&
             read_binary_file(post_write_replaced_store.session_path()) == append_replacement_bytes,
         "append reports namespace replacement after mutating only its anchored descriptor");

  ava::session::SessionStore strict_append_store(
      ava::session::SessionStoreOptions{.root_dir = sessions, .workspace_dir = workspace, .session_id = "strict_append"});
  std::string deeply_nested_data = "{\"nested\":" + std::string(70, '[') + "0" + std::string(70, ']') + "}";
  std::vector<ava::session::SessionEntry> strict_append_failures = {
      ava::session::SessionEntry{.id = "append_duplicate_json",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::UserMessage,
                                 .timestamp = "2026-07-14T00:00:05Z",
                                 .data_json = "{\"text\":\"one\",\"text\":\"two\"}"},
      ava::session::SessionEntry{.id = "append_invalid_json",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::UserMessage,
                                 .timestamp = "2026-07-14T00:00:05Z",
                                 .data_json = "{\"text\":}"},
      ava::session::SessionEntry{.id = "append_deep_json",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::UserMessage,
                                 .timestamp = "2026-07-14T00:00:05Z",
                                 .data_json = std::move(deeply_nested_data)},
      ava::session::SessionEntry{.id = "append_future_version",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::UserMessage,
                                 .timestamp = "2026-07-14T00:00:05Z",
                                 .data_json = "{}",
                                 .version = 99},
  };
  bool strict_disk_append_rejected = true;
  for (auto const& invalid_entry : strict_append_failures)
    strict_disk_append_rejected = strict_disk_append_rejected && !append_session_entry_for_test(strict_append_store, invalid_entry);
  auto strict_ephemeral_store = ava::session::SessionStore::create_ephemeral(workspace);
  auto strict_ephemeral_append = strict_ephemeral_store ? append_session_entry_for_test(*strict_ephemeral_store, strict_append_failures.front())
                                                        : ava::core::VoidResult(std::unexpected(strict_ephemeral_store.error()));
  expect(strict_disk_append_rejected && strict_append_store.load() && strict_append_store.load()->empty() && !strict_ephemeral_append,
         "disk and ephemeral append reject records that the strict recovery parser would reject");

  auto competing_lease = ava::session::SessionLease::acquire(append_store.session_path());
  expect(!competing_lease && competing_lease.error().message().find("already owned") != std::string::npos,
         "torn tail recovery lease excludes a competing owner");
  ava::session::SessionStore other_store(ava::session::SessionStoreOptions{.root_dir = sessions, .workspace_dir = workspace, .session_id = "other_lease"});
  write_binary_file(other_store.session_path(), valid_prefix);
  auto other_lease = ava::session::SessionLease::acquire(other_store.session_path());
  auto mismatch = other_lease ? append_store.recover_torn_tail(*other_lease, ava::session::legacy_unbounded_session_read_limits())
                              : ava::core::Result<std::optional<std::filesystem::path>>(std::unexpected(std::move(other_lease.error())));
  expect(!mismatch && mismatch.error().message().find("does not match") != std::string::npos, "torn tail recovery rejects a lease for another session");

  bool stale_rejected = false;
  if (other_lease)
  {
    [[maybe_unused]] ava::session::SessionLease moved_stale_owner = std::move(*other_lease);
    auto stale = other_store.recover_torn_tail(*other_lease, ava::session::legacy_unbounded_session_read_limits());
    stale_rejected = !stale && stale.error().message().find("active session lease") != std::string::npos;
  }
  expect(stale_rejected, "torn tail recovery rejects a moved-from stale lease");

  ava::session::SessionStore replaced_store(
      ava::session::SessionStoreOptions{.root_dir = sessions, .workspace_dir = workspace, .session_id = "replaced_target"});
  auto const replaced_bytes = valid_prefix + idempotent_suffix;
  write_binary_file(replaced_store.session_path(), replaced_bytes);
  auto replaced_lease = ava::session::SessionLease::acquire(replaced_store.session_path());
  auto const displaced_path = replaced_store.session_path().string() + ".displaced";
  std::filesystem::rename(replaced_store.session_path(), displaced_path);
  write_binary_file(replaced_store.session_path(), replaced_bytes);
  auto replaced = replaced_lease ? replaced_store.recover_torn_tail(*replaced_lease, ava::session::legacy_unbounded_session_read_limits())
                                 : ava::core::Result<std::optional<std::filesystem::path>>(std::unexpected(std::move(replaced_lease.error())));
  expect(!replaced && replaced.error().message().find("does not identify the recovery target") != std::string::npos &&
             read_binary_file(replaced_store.session_path()) == replaced_bytes && read_binary_file(displaced_path) == replaced_bytes,
         "torn tail recovery rejects a path replaced with a different inode and leaves both files unchanged");

  ava::session::SessionStore prepublication_store(
      ava::session::SessionStoreOptions{.root_dir = sessions, .workspace_dir = workspace, .session_id = "prepublication_cleanup"});
  auto const prepublication_bytes = valid_prefix + idempotent_suffix;
  write_binary_file(prepublication_store.session_path(), prepublication_bytes);
  auto prepublication_lease = ava::session::SessionLease::acquire(prepublication_store.session_path());
  bool saw_prepublication_temporary = false;
  auto prepublication_recovery =
      prepublication_lease
          ? prepublication_store.recover_torn_tail(*prepublication_lease, ava::session::legacy_unbounded_session_read_limits(),
                                                   [&] {
                                                     auto const files = recovery_files_for(prepublication_store.session_path());
                                                     saw_prepublication_temporary = std::ranges::any_of(files, [](std::filesystem::path const& candidate) {
                                                       return candidate.filename().string().find(".torn-tail.tmp.") != std::string::npos;
                                                     });
                                                     return saw_prepublication_temporary;
                                                   })
          : ava::core::Result<std::optional<std::filesystem::path>>(std::unexpected(std::move(prepublication_lease.error())));
  expect(!prepublication_recovery && saw_prepublication_temporary && read_binary_file(prepublication_store.session_path()) == prepublication_bytes &&
             recovery_files_for(prepublication_store.session_path()).empty(),
         "a prepublication cancellation removes the temporary quarantine and leaves no final-looking artifact");

  ava::session::SessionStore quarantine_swap_store(
      ava::session::SessionStoreOptions{.root_dir = sessions, .workspace_dir = workspace, .session_id = "quarantine_temp_swap"});
  auto const quarantine_swap_bytes = valid_prefix + idempotent_suffix;
  write_binary_file(quarantine_swap_store.session_path(), quarantine_swap_bytes);
  auto quarantine_swap_lease = ava::session::SessionLease::acquire(quarantine_swap_store.session_path());
  std::filesystem::path attacker_temporary_path;
  std::filesystem::path validated_stash_path;
  struct stat attacker_status_before{};
  bool quarantine_temporary_swapped = false;
  quarantine_swap_store.set_before_recovery_quarantine_publication_for_test([&](std::filesystem::path const& temporary_path) {
    attacker_temporary_path = temporary_path;
    validated_stash_path = temporary_path.string() + ".validated-stash";
    std::filesystem::rename(temporary_path, validated_stash_path);
    write_binary_file(temporary_path, "ATTACKER_QUARANTINE_INODE_CANARY");
    quarantine_temporary_swapped = ::stat(temporary_path.c_str(), &attacker_status_before) == 0;
  });
  auto quarantine_swap_recovery = quarantine_swap_lease
                                      ? quarantine_swap_store.recover_torn_tail(*quarantine_swap_lease, ava::session::legacy_unbounded_session_read_limits())
                                      : ava::core::Result<std::optional<std::filesystem::path>>(std::unexpected(std::move(quarantine_swap_lease.error())));
  std::vector<std::filesystem::path> exact_swap_quarantines;
  std::error_code swap_iter_error;
  auto const swap_final_prefix = quarantine_swap_store.session_path().filename().string() + ".torn-tail.";
  for (std::filesystem::directory_iterator iterator(quarantine_swap_store.session_path().parent_path(), swap_iter_error), end;
       !swap_iter_error && iterator != end; iterator.increment(swap_iter_error))
  {
    if (iterator->path().filename().string().starts_with(swap_final_prefix))
      exact_swap_quarantines.push_back(iterator->path());
  }
  struct stat attacker_status_after{};
  struct stat published_status{};
  bool const attacker_preserved = quarantine_temporary_swapped && ::stat(attacker_temporary_path.c_str(), &attacker_status_after) == 0 &&
                                  attacker_status_before.st_dev == attacker_status_after.st_dev &&
                                  attacker_status_before.st_ino == attacker_status_after.st_ino &&
                                  read_binary_file(attacker_temporary_path) == "ATTACKER_QUARANTINE_INODE_CANARY";
  bool const exact_descriptor_published = exact_swap_quarantines.size() == 1 && ::stat(exact_swap_quarantines.front().c_str(), &published_status) == 0 &&
                                          read_binary_file(exact_swap_quarantines.front()) == idempotent_suffix &&
                                          read_binary_file(validated_stash_path) == idempotent_suffix &&
                                          (published_status.st_dev != attacker_status_after.st_dev || published_status.st_ino != attacker_status_after.st_ino);
  expect(!quarantine_swap_recovery && quarantine_swap_recovery.error().message().find("temporary name was replaced") != std::string::npos &&
             read_binary_file(quarantine_swap_store.session_path()) == quarantine_swap_bytes && attacker_preserved && exact_descriptor_published,
         "quarantine publication links the validated descriptor, preserves a swapped attacker inode, and fails before source recovery");

  ava::session::SessionStore committed_cancellation_store(
      ava::session::SessionStoreOptions{.root_dir = sessions, .workspace_dir = workspace, .session_id = "committed_cancellation"});
  auto const committed_cancellation_bytes = valid_prefix + idempotent_suffix;
  write_binary_file(committed_cancellation_store.session_path(), committed_cancellation_bytes);
  auto committed_cancellation_lease = ava::session::SessionLease::acquire(committed_cancellation_store.session_path());
  bool cancel_after_publication = false;
  committed_cancellation_store.set_after_recovery_quarantine_publication_for_test([&] { cancel_after_publication = true; });
  auto committed_cancellation_recovery =
      committed_cancellation_lease ? committed_cancellation_store.recover_torn_tail(*committed_cancellation_lease, ava::session::SessionReadLimits{},
                                                                                    [&] { return cancel_after_publication; })
                                   : ava::core::Result<std::optional<std::filesystem::path>>(std::unexpected(std::move(committed_cancellation_lease.error())));
  auto repeated_committed_recovery = committed_cancellation_lease
                                         ? committed_cancellation_store.recover_torn_tail(*committed_cancellation_lease, ava::session::SessionReadLimits{})
                                         : ava::core::Result<std::optional<std::filesystem::path>>(
                                               std::unexpected(ava::core::Error(ava::core::ErrorCategory::Session, "missing committed recovery lease")));
  auto const committed_recovery_files = recovery_files_for(committed_cancellation_store.session_path());
  expect(committed_cancellation_recovery && committed_cancellation_recovery->has_value() && repeated_committed_recovery &&
             !repeated_committed_recovery->has_value() && read_binary_file(committed_cancellation_store.session_path()) == valid_prefix &&
             committed_recovery_files.size() == 1 && read_binary_file(committed_recovery_files.front()) == idempotent_suffix,
         "quarantine publication commits cancellation handling through truncation and repeated recovery does not leak another artifact");

  ava::session::SessionStore truncate_replacement_store(
      ava::session::SessionStoreOptions{.root_dir = sessions, .workspace_dir = workspace, .session_id = "replace_before_truncate"});
  auto const truncate_replacement_bytes = valid_prefix + idempotent_suffix;
  auto const truncate_replacement_new_bytes = valid_prefix;
  write_binary_file(truncate_replacement_store.session_path(), truncate_replacement_bytes);
  auto truncate_replacement_lease = ava::session::SessionLease::acquire(truncate_replacement_store.session_path());
  auto const truncate_displaced_path = truncate_replacement_store.session_path().string() + ".displaced";
  bool replaced_before_truncate = false;
  truncate_replacement_store.set_after_recovery_quarantine_publication_for_test([&] {
    std::filesystem::rename(truncate_replacement_store.session_path(), truncate_displaced_path);
    write_binary_file(truncate_replacement_store.session_path(), truncate_replacement_new_bytes);
    replaced_before_truncate = true;
  });
  auto truncate_replacement =
      truncate_replacement_lease
          ? truncate_replacement_store.recover_torn_tail(*truncate_replacement_lease, ava::session::legacy_unbounded_session_read_limits())
          : ava::core::Result<std::optional<std::filesystem::path>>(std::unexpected(std::move(truncate_replacement_lease.error())));
  auto truncate_replacement_quarantines = recovery_files_for(truncate_replacement_store.session_path());
  bool exact_published_quarantine =
      truncate_replacement_quarantines.size() == 1 && read_binary_file(truncate_replacement_quarantines.front()) == idempotent_suffix;
  if (exact_published_quarantine)
  {
    struct stat status{};
    exact_published_quarantine = stat(truncate_replacement_quarantines.front().c_str(), &status) == 0 && (status.st_mode & 0777) == 0600;
  }
  expect(!truncate_replacement && replaced_before_truncate && truncate_replacement.error().format().find("quarantine_path") != std::string::npos &&
             read_binary_file(truncate_displaced_path) == truncate_replacement_bytes &&
             read_binary_file(truncate_replacement_store.session_path()) == truncate_replacement_new_bytes && exact_published_quarantine,
         "session-name replacement after quarantine publication is detected before truncate and preserves the exact mode-0600 quarantine");

  ava::session::SessionStore parent_replacement_store(
      ava::session::SessionStoreOptions{.root_dir = sessions, .workspace_dir = workspace, .session_id = "parent_replace_before_truncate"});
  auto const parent_replacement_bytes = valid_prefix + idempotent_suffix;
  write_binary_file(parent_replacement_store.session_path(), parent_replacement_bytes);
  auto parent_replacement_lease = ava::session::SessionLease::acquire(parent_replacement_store.session_path());
  auto const original_parent = parent_replacement_store.session_path().parent_path();
  auto const displaced_parent = std::filesystem::path(original_parent.string() + ".displaced");
  bool parent_replaced_before_truncate = false;
  parent_replacement_store.set_after_recovery_quarantine_publication_for_test([&] {
    std::filesystem::rename(original_parent, displaced_parent);
    std::filesystem::create_directories(original_parent);
    write_binary_file(parent_replacement_store.session_path(), valid_prefix);
    parent_replaced_before_truncate = true;
  });
  auto parent_replacement = parent_replacement_lease
                                ? parent_replacement_store.recover_torn_tail(*parent_replacement_lease, ava::session::legacy_unbounded_session_read_limits())
                                : ava::core::Result<std::optional<std::filesystem::path>>(std::unexpected(std::move(parent_replacement_lease.error())));
  auto const displaced_session = displaced_parent / parent_replacement_store.session_path().filename();
  std::vector<std::filesystem::path> displaced_quarantines;
  std::error_code displaced_iter_error;
  auto const displaced_final_prefix = parent_replacement_store.session_path().filename().string() + ".torn-tail.";
  for (std::filesystem::directory_iterator iterator(displaced_parent, displaced_iter_error), end; !displaced_iter_error && iterator != end;
       iterator.increment(displaced_iter_error))
  {
    auto const name = iterator->path().filename().string();
    if (name.starts_with(displaced_final_prefix) && name.find(".tmp.") == std::string::npos)
      displaced_quarantines.push_back(iterator->path());
  }
  expect(!parent_replacement && parent_replaced_before_truncate &&
             parent_replacement.error().message().find("directory namespace changed") != std::string::npos &&
             read_binary_file(displaced_session) == parent_replacement_bytes && read_binary_file(parent_replacement_store.session_path()) == valid_prefix &&
             displaced_quarantines.size() == 1 && read_binary_file(displaced_quarantines.front()) == idempotent_suffix,
         "parent-directory replacement after quarantine publication is detected before truncate and preserves source and quarantine");

  auto ephemeral = ava::session::SessionStore::create_ephemeral(workspace);
  auto ephemeral_recovery = ephemeral && other_lease ? ephemeral->recover_torn_tail(*other_lease, ava::session::legacy_unbounded_session_read_limits())
                                                     : ava::core::Result<std::optional<std::filesystem::path>>(
                                                           std::unexpected(ava::core::Error(ava::core::ErrorCategory::Session, "missing test fixture")));
  expect(!ephemeral_recovery && ephemeral_recovery.error().message().find("ephemeral") != std::string::npos, "torn tail recovery rejects ephemeral stores");
}

void test_session_torn_tail_listing()
{
  auto const root = temp_root() / "session-torn-tail-listing";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const sessions = root / "sessions";
  std::filesystem::create_directories(workspace);

  auto line = ava::session::serialize_session_entry_line(ava::session::SessionEntry{.id = "listed",
                                                                                    .parent_id = "",
                                                                                    .type = ava::session::EntryType::UserMessage,
                                                                                    .timestamp = "2026-07-14T00:00:00Z",
                                                                                    .data_json = "{\"text\":\"listed\"}"});
  expect(line.has_value(), "torn listing test serializes a valid record");
  if (!line)
    return;
  ava::session::SessionStore torn_store(ava::session::SessionStoreOptions{.root_dir = sessions, .workspace_dir = workspace, .session_id = "listed_torn"});
  auto const torn_bytes = *line + "\n{\"version\":3,\"id\":\"partial";
  write_binary_file(torn_store.session_path(), torn_bytes);
  ava::session::SessionStore complete_store(
      ava::session::SessionStoreOptions{.root_dir = sessions, .workspace_dir = workspace, .session_id = "listed_complete_no_lf"});
  write_binary_file(complete_store.session_path(), *line);

  auto listed = ava::session::SessionStore::list_sessions(workspace, sessions);
  bool normal_summaries_valid = listed && listed->size() == 2;
  if (normal_summaries_valid)
  {
    normal_summaries_valid = std::ranges::all_of(
        *listed, [](ava::session::SessionSummary const& summary) { return summary.entry_count == 1 && summary.last_updated == "2026-07-14T00:00:00Z"; });
  }
  expect(normal_summaries_valid && read_binary_file(torn_store.session_path()) == torn_bytes,
         "normal listing selects a valid prefix plus Invalid final suffix and includes a complete no-LF record without mutation");

  auto bounded = ava::session::SessionStore::list_sessions_bounded(workspace, sessions, ava::session::SessionListLimits{});
  expect(bounded && bounded->size() == 2 &&
             std::ranges::all_of(*bounded, [](ava::session::SessionSummary const& summary) { return summary.entry_count == 1; }) &&
             read_binary_file(torn_store.session_path()) == torn_bytes,
         "bounded and ACP-style listing derives summaries from validated records without repairing the file");

  ava::session::SessionStore framed_corrupt(
      ava::session::SessionStoreOptions{.root_dir = sessions, .workspace_dir = workspace, .session_id = "framed_corrupt"});
  auto const framed_corrupt_bytes = *line + "\n{\n";
  write_binary_file(framed_corrupt.session_path(), framed_corrupt_bytes);
  listed = ava::session::SessionStore::list_sessions(workspace, sessions);
  bounded = ava::session::SessionStore::list_sessions_bounded(workspace, sessions, ava::session::SessionListLimits{});
  expect(listed && listed->size() == 2 && !bounded && read_binary_file(framed_corrupt.session_path()) == framed_corrupt_bytes,
         "newline-terminated corruption keeps normal skip and bounded fail policies and is never treated as torn");
  std::filesystem::remove(framed_corrupt.session_path());

  std::string deep_record =
      "{\"version\":3,\"id\":\"deep\",\"parent_id\":\"\",\"type\":\"user_message\",\"timestamp\":\"2026-07-14T00:00:01Z\",\"data\":{\"x\":";
  deep_record += std::string(70, '[') + "0" + std::string(70, ']') + "}}";
  std::vector<std::pair<std::string, std::string>> non_torn_records = {
      {"semantic", "{}"},
      {"duplicate",
       "{\"version\":3,\"id\":\"one\",\"id\":\"two\",\"parent_id\":\"\",\"type\":\"user_message\",\"timestamp\":\"2026-07-14T00:00:01Z\",\"data\":{}}"},
      {"depth", std::move(deep_record)},
  };
  bool non_torn_listing_policy_preserved = true;
  for (auto const& [name, record] : non_torn_records)
  {
    ava::session::SessionStore store(ava::session::SessionStoreOptions{.root_dir = sessions, .workspace_dir = workspace, .session_id = "listed_" + name});
    auto const original = *line + "\n" + record;
    write_binary_file(store.session_path(), original);
    auto normal_result = ava::session::SessionStore::list_sessions(workspace, sessions);
    auto bounded_result = ava::session::SessionStore::list_sessions_bounded(workspace, sessions, ava::session::SessionListLimits{});
    if (!normal_result || normal_result->size() != 2 || bounded_result || read_binary_file(store.session_path()) != original)
    {
      non_torn_listing_policy_preserved = false;
      break;
    }
    std::filesystem::remove(store.session_path());
  }
  expect(non_torn_listing_policy_preserved, "strict-valid semantic errors and duplicate/deep final records keep normal-skip and bounded-fail listing policy");

  ava::session::SessionStore future_store(ava::session::SessionStoreOptions{.root_dir = sessions, .workspace_dir = workspace, .session_id = "listed_future"});
  auto const future_bytes =
      *line + "\n{\"version\":99,\"id\":\"future\",\"parent_id\":\"\",\"type\":\"user_message\",\"timestamp\":\"2026-07-14T00:00:01Z\",\"data\":{}}";
  write_binary_file(future_store.session_path(), future_bytes);
  listed = ava::session::SessionStore::list_sessions(workspace, sessions);
  bounded = ava::session::SessionStore::list_sessions_bounded(workspace, sessions, ava::session::SessionListLimits{});
  expect(!listed && listed.error().message().find("unsupported session entry version") != std::string::npos && !bounded &&
             bounded.error().message().find("unsupported session entry version") != std::string::npos &&
             read_binary_file(future_store.session_path()) == future_bytes,
         "a strict-valid future-version final record remains an actionable listing error and is not treated as torn");
}

void test_session_resume_and_listing()
{
  auto const root = temp_root() / "session-resume";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  auto const session_root = root / "sessions";

  auto first = ava::session::SessionStore::create(workspace, session_root);
  auto second = ava::session::SessionStore::create(workspace, session_root);
  expect(first && second, "resume test sessions create");
  if (!first || !second)
    return;

  expect(append_session_entry_for_test(*first, ava::session::SessionEntry{.id = "entry_first",
                                                                          .parent_id = "",
                                                                          .type = ava::session::EntryType::UserMessage,
                                                                          .timestamp = "2026-04-27T00:00:00Z",
                                                                          .data_json = "{\"text\":\"first\"}"})
             .has_value(),
         "first resume test session appends");
  expect(append_session_entry_for_test(*second, ava::session::SessionEntry{.id = "entry_second",
                                                                           .parent_id = "",
                                                                           .type = ava::session::EntryType::UserMessage,
                                                                           .timestamp = "2026-04-27T00:01:00Z",
                                                                           .data_json = "{\"text\":\"second\"}"})
             .has_value(),
         "second resume test session appends");

  auto const old_time = std::filesystem::file_time_type::clock::now() - std::chrono::minutes(2);
  auto const new_time = std::filesystem::file_time_type::clock::now();
  std::filesystem::last_write_time(first->session_path(), old_time);
  std::filesystem::last_write_time(second->session_path(), new_time);

  auto reopened = ava::session::SessionStore::open(workspace, first->session_id(), session_root);
  expect(reopened && reopened->session_id() == first->session_id(), "session opens by exact id");
  if (reopened)
  {
    auto reopened_entries = reopened->load();
    expect(reopened_entries && reopened_entries->size() == 1 && (*reopened_entries)[0].id == "entry_first", "opened session loads original history");
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

  ava::session::SessionStore future_store(ava::session::SessionStoreOptions{
      .root_dir = session_root,
      .workspace_dir = workspace,
      .session_id = "future",
  });
  std::filesystem::create_directories(future_store.session_path().parent_path());
  {
    std::ofstream file(future_store.session_path(), std::ios::binary | std::ios::trunc);
    file << "{\"version\":99,\"id\":\"entry_future\",\"parent_id\":\"\",\"type\":\"user_message\","
            "\"timestamp\":\"2026-04-27T00:02:00Z\",\"data\":{\"text\":\"future\"}}\n";
  }
  listed = ava::session::SessionStore::list_sessions(workspace, session_root);
  expect(!listed && listed.error().message().find("unsupported session entry version") != std::string::npos,
         "session listing reports unsupported future-version session files instead of hiding them");
  std::filesystem::remove(future_store.session_path());

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
  expect(crlf_loaded && crlf_loaded->size() == 1 && (*crlf_loaded)[0].id == "entry_crlf", "session loader accepts CRLF line endings");

  ava::session::SessionStore symlink_store(ava::session::SessionStoreOptions{
      .root_dir = session_root,
      .workspace_dir = workspace,
      .session_id = "link",
  });
  std::filesystem::create_directories(symlink_store.session_path().parent_path());
  std::error_code symlink_error;
  std::filesystem::create_symlink(first->session_path(), symlink_store.session_path(), symlink_error);
  if (!symlink_error)
  {
    auto const symlink_target_bytes = read_binary_file(first->session_path());
    auto symlink_open = ava::session::SessionStore::open(workspace, "link", session_root);
    expect(!symlink_open && symlink_open.error().category() == ava::core::ErrorCategory::PermissionDenied, "session open rejects symlink session files");
    auto symlink_load = symlink_store.load();
    expect(!symlink_load && symlink_load.error().category() == ava::core::ErrorCategory::PermissionDenied, "session load rejects symlink session files");
    auto symlink_append = append_session_entry_for_test(symlink_store, ava::session::SessionEntry{.id = "entry_symlink",
                                                                                                  .parent_id = "",
                                                                                                  .type = ava::session::EntryType::UserMessage,
                                                                                                  .timestamp = "2026-04-27T00:04:00Z",
                                                                                                  .data_json = "{\"text\":\"bad\"}"});
    expect(!symlink_append && symlink_append.error().category() == ava::core::ErrorCategory::PermissionDenied &&
               read_binary_file(first->session_path()) == symlink_target_bytes,
           "session append rejects symlink session files without mutating the target");
    auto symlink_lease = ava::session::SessionLease::acquire(symlink_store.session_path());
    expect(!symlink_lease && symlink_lease.error().category() == ava::core::ErrorCategory::PermissionDenied &&
               read_binary_file(first->session_path()) == symlink_target_bytes,
           "session lease opens the original final component with O_NOFOLLOW and never follows its target");
  }

  auto missing = ava::session::SessionStore::open(workspace, "missing-session", session_root);
  expect(!missing && missing.error().category() == ava::core::ErrorCategory::NotFound, "missing session open fails");
  auto bad = ava::session::SessionStore::open(workspace, "../escape", session_root);
  expect(!bad && bad.error().category() == ava::core::ErrorCategory::InvalidArgument, "resume rejects invalid session id");
}

void test_session_compaction_entry_round_trip()
{
  auto const root = temp_root() / "compaction-round-trip";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);

  ava::session::SessionStore store(ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "compact"});
  auto config = ava::session::default_compaction_config();
  config.auto_threshold_tokens = 1234;
  config.keep_recent_tokens = 321;
  config.keep_recent_messages = 6;
  config.model_id = "gpt-5.5-mini";
  config.max_summary_bytes = 1024;

  auto appended = append_manual_compaction_for_test(store, ava::session::ManualCompactionRequest{.summary = "Prior work summary",
                                                                                                 .instructions = "Keep the recent plan.",
                                                                                                 .config = config,
                                                                                                 .estimated_tokens = 1300,
                                                                                                 .threshold_tokens = 0,
                                                                                                 .trigger = "manual",
                                                                                                 .recent_context = ""});
  expect(appended.has_value(), "manual compaction entry appends");

  auto loaded = store.load();
  expect(loaded && loaded->size() == 1 && (*loaded)[0].type == ava::session::EntryType::Compaction, "compaction entry type round trips through session store");
  if (loaded && !loaded->empty())
  {
    expect(ava::core::json::string_field((*loaded)[0].data_json, "summary") == "Prior work summary", "compaction summary round trips");
    expect(ava::core::json::string_field((*loaded)[0].data_json, "instructions") == "Keep the recent plan.", "compaction instructions round trips");
    expect(ava::core::json::string_field((*loaded)[0].data_json, "model") == "gpt-5.5-mini", "compaction model metadata round trips");
    expect(ava::core::json::integer_field((*loaded)[0].data_json, "threshold_tokens") == 1234, "compaction threshold metadata round trips");
  }

  ava::session::SessionStore unavailable_store(
      ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "compact-unavailable"});
  auto unavailable = append_manual_compaction_for_test(
      unavailable_store,
      ava::session::ManualCompactionRequest{
          .summary = "", .instructions = "", .config = config, .estimated_tokens = 0, .threshold_tokens = 0, .trigger = "manual", .recent_context = ""});
  auto unavailable_loaded = unavailable_store.load();
  std::optional<std::string> unavailable_summary;
  if (unavailable_loaded && !unavailable_loaded->empty())
  {
    unavailable_summary = ava::core::json::string_field((*unavailable_loaded)[0].data_json, "summary");
  }
  expect(unavailable && unavailable_summary && unavailable_summary->find("unavailable") != std::string::npos,
         "manual compaction records deterministic unavailable summary when no provider summary exists");

  auto tiny_config = config;
  tiny_config.max_summary_bytes = 1;
  ava::session::SessionStore tiny_unavailable_store(
      ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "compact-tiny-unavailable"});
  auto tiny_unavailable = append_manual_compaction_for_test(
      tiny_unavailable_store,
      ava::session::ManualCompactionRequest{
          .summary = "", .instructions = "", .config = tiny_config, .estimated_tokens = 0, .threshold_tokens = 0, .trigger = "manual", .recent_context = ""});
  auto tiny_loaded = tiny_unavailable_store.load();
  std::optional<std::string> tiny_summary;
  if (tiny_loaded && !tiny_loaded->empty())
  {
    tiny_summary = ava::core::json::string_field((*tiny_loaded)[0].data_json, "summary");
  }
  expect(tiny_unavailable && tiny_summary && tiny_summary->find("unavailable") != std::string::npos,
         "empty manual compaction succeeds with tiny summary limit");

  ava::session::SessionStore oversized_summary_store(
      ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "compact-oversized-summary"});
  auto oversized_summary = append_manual_compaction_for_test(
      oversized_summary_store,
      ava::session::ManualCompactionRequest{
          .summary = "xx", .instructions = "", .config = tiny_config, .estimated_tokens = 0, .threshold_tokens = 0, .trigger = "manual", .recent_context = ""});
  expect(!oversized_summary && oversized_summary.error().category() == ava::core::ErrorCategory::InvalidArgument,
         "manual compaction rejects oversized user summary with tiny summary limit");
}

void test_session_markdown_export()
{
  std::vector<ava::session::SessionEntry> const entries = {
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
      ava::session::SessionEntry{.id = "user_1_replay",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::UserMessage,
                                 .timestamp = "2026-04-29T00:00:01Z",
                                 .data_json = "{\"text\":\"Hello AVA\",\"internal_replay\":true,"
                                              "\"replay_of\":\"user_1\",\"reason\":\"test\"}"},
      ava::session::SessionEntry{.id = "assistant_1",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::AssistantMessage,
                                 .timestamp = "2026-04-29T00:00:02Z",
                                 .data_json = "{\"text\":\"Hello human\",\"tool_calls\":1,"
                                              "\"usage\":{\"input_tokens\":10,\"output_tokens\":5,"
                                              "\"total_tokens\":15,\"source\":\"provider\"}}"},
      ava::session::SessionEntry{.id = "unsafe_html_1",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::UserMessage,
                                 .timestamp = "2026-04-29T00:00:02Z",
                                 .data_json = R"json({"text":"<script>alert('x')</script> & raw"})json"},
      ava::session::SessionEntry{.id = "reasoning_1",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::ReasoningBlock,
                                 .timestamp = "2026-04-29T00:00:02Z",
                                 .data_json = "{\"provider\":\"anthropic\",\"model\":\"claude-sonnet-4-5\","
                                              "\"format\":\"anthropic_thinking\","
                                              "\"text\":\"visible reasoning summary\","
                                              "\"signature\":\"super-secret-signature\","
                                              "\"native_item_json\":\"{\\\"id\\\":\\\"rs_export\\\",\\\"type\\\":\\\"reasoning\\\",\\\"summary\\\":[],"
                                              "\\\"encrypted_content\\\":\\\"export-private-cipher\\\"}\"}"},
      ava::session::SessionEntry{.id = "reasoning_change_1",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::ReasoningChange,
                                 .timestamp = "2026-04-29T00:00:02Z",
                                 .data_json = "{\"provider\":\"anthropic\",\"model\":\"claude-sonnet-4-5\","
                                              "\"format\":\"anthropic_thinking\",\"enabled\":true,"
                                              "\"level\":\"enabled\",\"budget_tokens\":4096,"
                                              "\"display\":\"summarized\"}"},
      ava::session::SessionEntry{.id = "tool_call_1",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::ToolCall,
                                 .timestamp = "2026-04-29T00:00:03Z",
                                 .data_json = "{\"call_id\":\"call_1\",\"name\":\"read_file\",\"arguments\":\"{\\\"path\\\":\\\"README.md\\\"}\"}"},
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
          .data_json = "{\"category\":\"provider\",\"message\":\"SESSION_PROVIDER_MESSAGE_CANARY\",\"details\":\"SESSION_PROVIDER_DETAILS_CANARY\"}"},
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

  auto const basic = ava::session::format_session_markdown(entries);
  auto const physical_error = std::ranges::find_if(entries, [](ava::session::SessionEntry const& entry) { return entry.id == "error_1"; });
  expect(physical_error != entries.end() && physical_error->data_json.find("SESSION_PROVIDER_MESSAGE_CANARY") != std::string::npos &&
             physical_error->data_json.find("SESSION_PROVIDER_DETAILS_CANARY") != std::string::npos,
         "public session projections leave the local physical error record unchanged");
  expect(basic.find("# AVA Session Export") != std::string::npos, "markdown export has deterministic title");
  expect(basic.find("## User") != std::string::npos && basic.find("Hello AVA") != std::string::npos, "markdown export renders user messages");
  expect(basic.find("internal_replay") == std::string::npos && basic.find("replay_of") == std::string::npos,
         "markdown export hides internal replay user messages");
  expect(basic.find("## Assistant") != std::string::npos && basic.find("Hello human") != std::string::npos, "markdown export renders assistant messages");
  expect(basic.find("## Reasoning") != std::string::npos && basic.find("visible reasoning summary") != std::string::npos &&
             basic.find("Signature present") != std::string::npos && basic.find("super-secret-signature") == std::string::npos &&
             basic.find("export-private-cipher") == std::string::npos,
         "markdown export renders reasoning blocks without leaking provider-private replay metadata");
  expect(basic.find("## Reasoning Change") != std::string::npos && basic.find("Budget tokens") != std::string::npos && basic.find("4096") != std::string::npos,
         "markdown export renders reasoning-change budget tokens when present");
  expect(basic.find("Usage:") != std::string::npos && basic.find("input_tokens") != std::string::npos, "markdown export renders assistant usage when present");
  expect(basic.find("## Tool Call") == std::string::npos && basic.find("README.md") == std::string::npos, "markdown export omits tool details by default");
  expect(basic.find("## Compaction") != std::string::npos && basic.find("Prior summary") != std::string::npos &&
             basic.find("Keep this constraint") != std::string::npos,
         "markdown export renders compactions by default");
  expect(basic.find("## Mode Change") != std::string::npos && basic.find("## Error") != std::string::npos &&
             basic.find(ava::session::kPublicSessionErrorOmission) != std::string::npos && basic.find("SESSION_PROVIDER_MESSAGE_CANARY") == std::string::npos &&
             basic.find("SESSION_PROVIDER_DETAILS_CANARY") == std::string::npos,
         "markdown export renders only a fixed omission for historical provider error diagnostics");
  expect(basic.find("Metadata:") == std::string::npos && basic.find("\"id\":\"user_1\"") == std::string::npos, "markdown export omits metadata by default");
  expect(basic.find("`````text\nbefore ``` after ```` done\n`````") != std::string::npos, "markdown export expands fences around backtick content");
  std::string const escaped_control_markdown = std::string("first\nsecond\tindent") + "\\u0000\\u001B\\u007F\\u000D";
  expect(basic.find(escaped_control_markdown) != std::string::npos, "markdown export escapes decoded fenced control bytes while preserving newlines and tabs");
  expect(basic.find('\0') == std::string::npos && basic.find('\x1B') == std::string::npos && basic.find('\x7F') == std::string::npos &&
             basic.find('\r') == std::string::npos,
         "markdown export does not emit raw NUL, escape, DEL, or carriage return bytes");

  auto const with_tools = ava::session::format_session_markdown(entries, ava::session::ExportOptions{.include_tool_details = true, .include_metadata = false});
  expect(with_tools.find("## Tool Call") != std::string::npos && with_tools.find("README.md") != std::string::npos &&
             with_tools.find("## Tool Result") != std::string::npos && with_tools.find("tool output") != std::string::npos,
         "markdown export includes tool calls and results when requested");

  auto const without_compactions = ava::session::format_session_markdown(
      entries, ava::session::ExportOptions{.include_tool_details = false, .include_metadata = false, .include_compactions = false});
  expect(without_compactions.find("## Compaction") == std::string::npos && without_compactions.find("Prior summary") == std::string::npos,
         "markdown export can omit compaction entries");

  auto const with_metadata = ava::session::format_session_markdown(
      entries, ava::session::ExportOptions{.include_tool_details = false, .include_metadata = true, .include_compactions = true});
  expect(with_metadata.find("Metadata:") != std::string::npos && with_metadata.find("\"id\":\"user_1\"") != std::string::npos &&
             with_metadata.find("Estimated tokens") != std::string::npos && with_metadata.find("gpt-5.5-mini") != std::string::npos,
         "markdown export includes entry and compaction metadata when requested");

  auto const html = ava::session::format_session_html(entries);
  expect(html.find("<!doctype html>") != std::string::npos && html.find("<title>AVA Session Export</title>") != std::string::npos &&
             html.find("<pre>") != std::string::npos && html.find("# AVA Session Export") != std::string::npos,
         "html export wraps the session markdown in a self-contained document");
  expect(html.find("&lt;script&gt;alert(&#39;x&#39;)&lt;/script&gt; &amp; raw") != std::string::npos && html.find("<script>alert") == std::string::npos,
         "html export escapes user and model text instead of emitting executable markup");
}

void test_session_portable_jsonl_sanitizer()
{
  auto const redacted_with_private = ava::session::sanitize_session_entry_for_portable_jsonl_export(
      ava::session::SessionEntry{.id = "portable_redacted_private",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::ReasoningBlock,
                                 .timestamp = "2026-05-11T00:00:00Z",
                                 .data_json = "{\"provider\":\"openai\",\"model\":\"gpt-5.5\",\"format\":\"openai_responses\","
                                              "\"text\":\"redacted secret text\",\"redacted\":true,\"signature\":\"secret-signature\","
                                              "\"native_item_json\":\"{\\\"id\\\":\\\"rs_portable\\\",\\\"type\\\":\\\"reasoning\\\",\\\"summary\\\":[]}\","
                                              "\"redacted_data\":\"secret-redacted-data\",\"unknown_provider_canary\":\"must-not-export\"}"});
  expect(redacted_with_private.data_json.find("redacted secret text") == std::string::npos &&
             redacted_with_private.data_json.find("secret-signature") == std::string::npos &&
             redacted_with_private.data_json.find("secret-redacted-data") == std::string::npos &&
             redacted_with_private.data_json.find("must-not-export") == std::string::npos &&
             redacted_with_private.data_json.find("[Provider-private reasoning metadata omitted from portable export.]") != std::string::npos &&
             redacted_with_private.data_json.find("\"redacted\":true") != std::string::npos &&
             redacted_with_private.data_json.find("\"native_item_json\":true") != std::string::npos &&
             redacted_with_private.data_json.find("\"signature\":true") != std::string::npos &&
             redacted_with_private.data_json.find("\"redacted_data\":true") != std::string::npos,
         "portable JSONL rebuilds redacted reasoning with a neutral placeholder and private-field presence metadata");

  auto const redacted_without_private = ava::session::sanitize_session_entry_for_portable_jsonl_export(
      ava::session::SessionEntry{.id = "portable_redacted_plain",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::ReasoningBlock,
                                 .timestamp = "2026-05-11T00:00:01Z",
                                 .data_json = "{\"provider\":\"openai\",\"model\":\"gpt-5.5\",\"format\":\"openai_responses\","
                                              "\"text\":\"redacted text without known private fields\",\"redacted\":true,"
                                              "\"unknown_provider_canary\":\"must-not-export\"}"});
  expect(redacted_without_private.data_json.find("redacted text without known private fields") == std::string::npos &&
             redacted_without_private.data_json.find("must-not-export") == std::string::npos &&
             redacted_without_private.data_json.find("[Provider-private reasoning metadata omitted from portable export.]") != std::string::npos &&
             redacted_without_private.data_json.find("\"native_item_json\":false") != std::string::npos &&
             redacted_without_private.data_json.find("\"signature\":false") != std::string::npos &&
             redacted_without_private.data_json.find("\"redacted_data\":false") != std::string::npos,
         "portable JSONL redacts reasoning text and additive canaries even without known private fields");

  auto const portable_error = ava::session::sanitize_session_entry_for_portable_jsonl_export(ava::session::SessionEntry{
      .id = "portable_provider_error",
      .parent_id = "",
      .type = ava::session::EntryType::Error,
      .timestamp = "2026-05-11T00:00:02Z",
      .data_json = "{\"category\":\"provider\",\"message\":\"PORTABLE_PROVIDER_MESSAGE_CANARY\",\"details\":\"PORTABLE_PROVIDER_DETAILS_CANARY\"}"});
  expect(portable_error.data_json.find("PORTABLE_PROVIDER_MESSAGE_CANARY") == std::string::npos &&
             portable_error.data_json.find("PORTABLE_PROVIDER_DETAILS_CANARY") == std::string::npos &&
             portable_error.data_json.find(ava::session::kPublicSessionErrorOmission) != std::string::npos,
         "portable JSONL exports replace historical Error diagnostics with a fixed omission");

  auto const visible_without_private = ava::session::sanitize_session_entry_for_portable_jsonl_export(
      ava::session::SessionEntry{.id = "portable_visible_plain",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::ReasoningBlock,
                                 .timestamp = "2026-05-11T00:00:02Z",
                                 .data_json = "{\"provider\":\"openai\",\"model\":\"gpt-5.5\",\"format\":\"openai_responses\","
                                              "\"text\":\"visible safe summary\",\"redacted\":false,\"unknown_provider_canary\":\"must-not-export\"}"});
  expect(visible_without_private.data_json.find("visible safe summary") != std::string::npos &&
             visible_without_private.data_json.find("must-not-export") == std::string::npos,
         "portable JSONL retains only visible non-redacted reasoning text from its explicit allowlist");
}

void test_compaction_config_and_thresholds()
{
  auto const root = temp_root() / "compaction-config";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  setenv("HOME", (root / "home").c_str(), 1);
  setenv("XDG_CONFIG_HOME", (root / "config").c_str(), 1);
  setenv("XDG_STATE_HOME", (root / "state").c_str(), 1);
  setenv("XDG_DATA_HOME", (root / "data").c_str(), 1);
  auto const paths = ava::config::xdg_paths();

  expect(paths.compaction_file == root / "config" / "ava" / "compaction.json", "compaction config path follows XDG config home");
  auto missing = ava::session::load_compaction_config(paths);
  expect(missing && missing->model_id == "gpt-5.5" && missing->auto_threshold_tokens == 0, "missing compaction config uses safe defaults");
  auto const fallback_threshold = ava::session::effective_auto_threshold_tokens(*missing, std::nullopt);
  expect(fallback_threshold > 0, "missing compaction config uses a nonzero effective auto-compaction threshold without model metadata");

  std::filesystem::create_directories(paths.ava_config_dir);
  {
    std::ofstream file(paths.compaction_file, std::ios::binary | std::ios::trunc);
    file << "{\"auto_threshold_tokens\":4096,\"keep_recent_tokens\":512,"
            "\"keep_recent_messages\":7,\"compaction_model\":\"gpt-5.5-compact\","
            "\"max_summary_bytes\":2048}";
  }
  auto loaded = ava::session::load_compaction_config(paths);
  expect(loaded && loaded->auto_threshold_tokens == 4096 && loaded->keep_recent_tokens == 512 && loaded->keep_recent_messages == 7 &&
             loaded->model_id == "gpt-5.5-compact" && loaded->max_summary_bytes == 2048,
         "compaction config parses token budgets and model id from XDG file");

  expect(ava::session::estimate_tokens("") == 0 && ava::session::estimate_tokens("abcd") == 1 && ava::session::estimate_tokens("abcde") == 2,
         "compaction token estimate uses deterministic chars over four heuristic");
  std::vector<ava::session::SessionEntry> const entries = {ava::session::SessionEntry{.id = "u",
                                                                                      .parent_id = "",
                                                                                      .type = ava::session::EntryType::UserMessage,
                                                                                      .timestamp = "2026-04-27T00:00:00Z",
                                                                                      .data_json = "{\"text\":\"abcdefgh\"}"},
                                                           ava::session::SessionEntry{.id = "ignored",
                                                                                      .parent_id = "",
                                                                                      .type = ava::session::EntryType::ModeChange,
                                                                                      .timestamp = "2026-04-27T00:00:00Z",
                                                                                      .data_json = "{\"mode\":\"build\"}"}};
  auto config = ava::session::default_compaction_config();
  auto estimated = ava::session::estimate_session_tokens(entries);
  config.auto_threshold_tokens = estimated.value_or(0);
  auto decision = ava::session::should_auto_compact(entries, config);
  expect(estimated && decision && decision->should_compact && decision->estimated_tokens == config.auto_threshold_tokens,
         "auto compaction triggers when estimated tokens reach threshold");
  config.auto_threshold_tokens = decision ? decision->estimated_tokens + 1 : 1;
  auto below_threshold = ava::session::should_auto_compact(entries, config);
  expect(below_threshold && !below_threshold->should_compact, "auto compaction does not trigger below threshold");
  config.auto_threshold_tokens = 0;
  auto disabled = ava::session::should_auto_compact(entries, config);
  expect(disabled && !disabled->should_compact, "auto compaction threshold zero disables automatic compaction");
  config.auto_threshold_tokens_explicit = false;
  std::vector<ava::session::SessionEntry> const fallback_entries = {
      ava::session::SessionEntry{.id = "fallback_big",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::UserMessage,
                                 .timestamp = "2026-04-27T00:00:00Z",
                                 .data_json = "{\"text\":\"" + std::string(fallback_threshold * 4, 'x') + "\"}"}};
  auto fallback_decision = ava::session::should_auto_compact(fallback_entries, config, std::nullopt);
  expect(fallback_decision && fallback_decision->should_compact, "default auto compaction can trigger when model context-window metadata is absent");
  config.auto_threshold_tokens_explicit = true;
  auto explicitly_disabled = ava::session::should_auto_compact(fallback_entries, config, std::nullopt);
  expect(explicitly_disabled && !explicitly_disabled->should_compact, "explicit auto_threshold_tokens zero remains disabled without model metadata");
}

void test_compaction_context_reconstruction()
{
  std::vector<ava::session::SessionEntry> const entries = {
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
      ava::session::SessionEntry{.id = "old_tool",
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
      ava::session::SessionEntry{.id = "new_user_replay",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::UserMessage,
                                 .timestamp = "2026-04-27T00:00:06Z",
                                 .data_json = "{\"text\":\"new user\",\"internal_replay\":true,"
                                              "\"replay_of\":\"new_user\",\"reason\":\"test\"}"},
      ava::session::SessionEntry{.id = "new_assistant",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::AssistantMessage,
                                 .timestamp = "2026-04-27T00:00:07Z",
                                 .data_json = "{\"text\":\"new assistant\"}"},
      ava::session::SessionEntry{.id = "new_tool_call",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::ToolCall,
                                 .timestamp = "2026-04-27T00:00:08Z",
                                 .data_json = "{\"call_id\":\"call_read\",\"name\":\"read_file\","
                                              "\"arguments\":\"{\\\"path\\\":\\\"note.txt\\\"}\"}"},
      ava::session::SessionEntry{.id = "new_tool_result",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::ToolResult,
                                 .timestamp = "2026-04-27T00:00:09Z",
                                 .data_json = "{\"call_id\":\"call_read\",\"name\":\"read_file\","
                                              "\"result\":\"note contents\"}"}};

  auto messages = ava::agent::build_provider_messages_from_entries(entries);
  expect(messages && messages->size() == 6, "compacted context reconstructs summary plus post-compaction turns");
  if (!messages)
    return;
  expect((*messages)[0].role == "user" && (*messages)[0].content.find("latest summary") != std::string::npos &&
             (*messages)[0].content.find("carry this") != std::string::npos,
         "latest compaction summary becomes provider-visible context");
  std::string const joined =
      (*messages)[0].content + (*messages)[1].content + (*messages)[2].content + (*messages)[3].content + (*messages)[4].content + (*messages)[5].content;
  expect(joined.find("old raw user") == std::string::npos && joined.find("old raw assistant") == std::string::npos &&
             joined.find("old raw tool") == std::string::npos && joined.find("middle raw user") == std::string::npos &&
             joined.find("first summary") == std::string::npos,
         "context reconstruction omits raw messages and tool results before latest compaction");
  expect((*messages)[1].role == "user" && (*messages)[1].content == "new user" && (*messages)[2].role == "user" && (*messages)[2].content == "new user" &&
             (*messages)[3].role == "assistant" && (*messages)[3].content == "new assistant",
         "post-compaction entries include internal replays as normal provider messages");
  expect((*messages)[4].role == "assistant" && (*messages)[4].content.find("Tool call requested by assistant") != std::string::npos &&
             (*messages)[4].content.find("read_file") != std::string::npos && (*messages)[5].role == "user" &&
             (*messages)[5].content.find("note contents") != std::string::npos,
         "post-compaction context includes tool-call metadata before tool result data");
  expect((*messages)[4].content_parts.size() == 1 && (*messages)[4].content_parts[0].type == ava::provider::ContentPartType::ToolUse &&
             (*messages)[4].content_parts[0].tool_call_id == "call_read" && (*messages)[4].content_parts[0].tool_name == "read_file" &&
             (*messages)[4].content_parts[0].input_json.find("note.txt") != std::string::npos && (*messages)[5].content_parts.size() == 1 &&
             (*messages)[5].content_parts[0].type == ava::provider::ContentPartType::ToolResult &&
             (*messages)[5].content_parts[0].tool_call_id == "call_read" && (*messages)[5].content_parts[0].tool_name == "read_file" &&
             (*messages)[5].content_parts[0].text == "note contents" && !(*messages)[5].content_parts[0].is_error,
         "tool-call and tool-result entries carry native provider content parts");
}

void test_tool_content_parts_reconstruction()
{
  std::string const long_result(80, 'r');
  std::vector<ava::session::SessionEntry> const entries = {ava::session::SessionEntry{.id = "tool_call",
                                                                                      .parent_id = "",
                                                                                      .type = ava::session::EntryType::ToolCall,
                                                                                      .timestamp = "2026-04-27T00:00:00Z",
                                                                                      .data_json = "{\"call_id\":\"call_failed\",\"name\":\"bash\","
                                                                                                   "\"arguments\":\"{\\\"cmd\\\":\\\"false\\\"}\"}"},
                                                           ava::session::SessionEntry{.id = "tool_result",
                                                                                      .parent_id = "",
                                                                                      .type = ava::session::EntryType::ToolResult,
                                                                                      .timestamp = "2026-04-27T00:00:01Z",
                                                                                      .data_json = "{\"call_id\":\"call_failed\",\"name\":\"bash\","
                                                                                                   "\"success\":false,\"result\":\"" +
                                                                                                   long_result + "\"}"}};

  auto messages = ava::agent::build_provider_messages_from_entries(entries, ava::agent::MessageBuildOptions{.max_tool_result_context_bytes = 48});
  expect(messages && messages->size() == 2, "tool entries reconstruct as provider messages");
  if (!messages || messages->size() != 2)
    return;

  expect((*messages)[0].role == "assistant" && (*messages)[0].content.find("Tool call requested by assistant") != std::string::npos &&
             (*messages)[0].content_parts.size() == 1 && (*messages)[0].content_parts[0].type == ava::provider::ContentPartType::ToolUse &&
             (*messages)[0].content_parts[0].tool_call_id == "call_failed" && (*messages)[0].content_parts[0].tool_name == "bash" &&
             (*messages)[0].content_parts[0].input_json.find("false") != std::string::npos,
         "tool-call entry reconstructs an assistant tool-use content part with fallback text");
  expect((*messages)[1].role == "user" && !(*messages)[1].content.empty() && (*messages)[1].content_parts.size() == 1 &&
             (*messages)[1].content_parts[0].type == ava::provider::ContentPartType::ToolResult &&
             (*messages)[1].content_parts[0].tool_call_id == "call_failed" && (*messages)[1].content_parts[0].tool_name == "bash" &&
             (*messages)[1].content_parts[0].is_error && (*messages)[1].content_parts[0].text.size() == 48 &&
             (*messages)[1].content_parts[0].text.find("[AVA: tool result content truncated]") != std::string::npos,
         "failed tool-result entry reconstructs native error metadata and truncated result text");

  std::vector<ava::session::SessionEntry> const permission_entries = {
      ava::session::SessionEntry{.id = "permission_tool_call",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::ToolCall,
                                 .timestamp = "2026-04-27T00:00:00Z",
                                 .data_json = "{\"call_id\":\"call_permission\",\"name\":\"read_file\","
                                              "\"arguments\":\"{}\"}"},
      ava::session::SessionEntry{.id = "permission_decision",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::PermissionDecision,
                                 .timestamp = "2026-04-27T00:00:01Z",
                                 .data_json = "{\"resolution\":\"allow\"}"},
      ava::session::SessionEntry{.id = "permission_tool_result",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::ToolResult,
                                 .timestamp = "2026-04-27T00:00:02Z",
                                 .data_json = "{\"call_id\":\"call_permission\",\"name\":\"read_file\","
                                              "\"success\":true,\"result\":\"permission result\"}"}};
  auto permission_messages = ava::agent::build_provider_messages_from_entries(permission_entries);
  expect(permission_messages && permission_messages->size() == 2, "permission decisions are internal metadata during provider replay");
  if (!permission_messages || permission_messages->size() != 2)
    return;
  expect((*permission_messages)[0].content_parts.size() == 1 && (*permission_messages)[1].content_parts.size() == 1,
         "native tool replay allows internal permission metadata between tool call and result");

  std::vector<ava::session::SessionEntry> const paired_batch_entries = {
      ava::session::SessionEntry{.id = "batch_assistant",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::AssistantMessage,
                                 .timestamp = "2026-04-27T00:00:00Z",
                                 .data_json = "{\"text\":\"\",\"tool_calls\":2}"},
      ava::session::SessionEntry{.id = "batch_call_first",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::ToolCall,
                                 .timestamp = "2026-04-27T00:00:01Z",
                                 .data_json = "{\"call_id\":\"call_batch_first\",\"name\":\"read_file\","
                                              "\"arguments\":\"{\\\"path\\\":\\\"first.txt\\\"}\"}"},
      ava::session::SessionEntry{.id = "batch_result_first",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::ToolResult,
                                 .timestamp = "2026-04-27T00:00:02Z",
                                 .data_json = "{\"call_id\":\"call_batch_first\",\"name\":\"read_file\","
                                              "\"success\":true,\"result\":\"first result\"}"},
      ava::session::SessionEntry{.id = "batch_call_second",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::ToolCall,
                                 .timestamp = "2026-04-27T00:00:03Z",
                                 .data_json = "{\"call_id\":\"call_batch_second\",\"name\":\"read_file\","
                                              "\"arguments\":\"{\\\"path\\\":\\\"second.txt\\\"}\"}"},
      ava::session::SessionEntry{.id = "batch_result_second",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::ToolResult,
                                 .timestamp = "2026-04-27T00:00:04Z",
                                 .data_json = "{\"call_id\":\"call_batch_second\",\"name\":\"read_file\","
                                              "\"success\":true,\"result\":\"second result\"}"}};
  auto paired_batch_messages = ava::agent::build_provider_messages_from_entries(paired_batch_entries);
  expect(paired_batch_messages && paired_batch_messages->size() == 2, "provider-order multi-tool pairs replay as one native tool-use/tool-result batch");
  if (!paired_batch_messages || paired_batch_messages->size() != 2)
    return;
  expect((*paired_batch_messages)[0].role == "assistant" && (*paired_batch_messages)[0].content_parts.size() == 2 &&
             (*paired_batch_messages)[0].content_parts[0].type == ava::provider::ContentPartType::ToolUse &&
             (*paired_batch_messages)[0].content_parts[0].tool_call_id == "call_batch_first" &&
             (*paired_batch_messages)[0].content_parts[0].tool_name == "read_file" &&
             (*paired_batch_messages)[0].content_parts[1].type == ava::provider::ContentPartType::ToolUse &&
             (*paired_batch_messages)[0].content_parts[1].tool_call_id == "call_batch_second" &&
             (*paired_batch_messages)[0].content_parts[1].tool_name == "read_file" && (*paired_batch_messages)[1].role == "user" &&
             (*paired_batch_messages)[1].content_parts.size() == 2 &&
             (*paired_batch_messages)[1].content_parts[0].type == ava::provider::ContentPartType::ToolResult &&
             (*paired_batch_messages)[1].content_parts[0].tool_call_id == "call_batch_first" &&
             (*paired_batch_messages)[1].content_parts[1].type == ava::provider::ContentPartType::ToolResult &&
             (*paired_batch_messages)[1].content_parts[1].tool_call_id == "call_batch_second" &&
             (*paired_batch_messages)[1].content_parts[0].text == "first result" && (*paired_batch_messages)[1].content_parts[1].text == "second result",
         "native multi-tool replay preserves provider-order tool-use and tool-result content parts");

  auto const content_parts_empty = [](std::vector<ava::provider::ChatMessage> const& built_messages) {
    for (auto const& message : built_messages)
    {
      if (!message.content_parts.empty())
        return false;
    }
    return true;
  };

  std::vector<ava::session::SessionEntry> const detached_batch_entries = {paired_batch_entries[0], paired_batch_entries[1], paired_batch_entries[3],
                                                                          paired_batch_entries[2], paired_batch_entries[4]};
  auto detached_batch_messages = ava::agent::build_provider_messages_from_entries(detached_batch_entries);
  expect(detached_batch_messages && detached_batch_messages->size() == 5 && content_parts_empty(*detached_batch_messages),
         "native multi-tool replay requires contiguous call/result pairs and falls back when results are detached from their calls");

  std::vector<ava::session::SessionEntry> const reordered_batch_entries = {paired_batch_entries[0], paired_batch_entries[1], paired_batch_entries[4],
                                                                           paired_batch_entries[3], paired_batch_entries[2]};
  auto reordered_batch_messages = ava::agent::build_provider_messages_from_entries(reordered_batch_entries);
  expect(reordered_batch_messages && reordered_batch_messages->size() == 5 && content_parts_empty(*reordered_batch_messages),
         "native multi-tool replay rejects reordered tool results instead of attaching them to the wrong calls");

  constexpr std::string_view truncation_marker = "\n[AVA: tool result content truncated]";
  std::string const euro = std::string("\xE2") + "\x82" + "\xAC";
  std::string const utf8_result = "abc" + euro + std::string(80, 'x');
  std::vector<ava::session::SessionEntry> const utf8_entries = {ava::session::SessionEntry{.id = "utf8_tool_call",
                                                                                           .parent_id = "",
                                                                                           .type = ava::session::EntryType::ToolCall,
                                                                                           .timestamp = "2026-04-27T00:00:00Z",
                                                                                           .data_json = "{\"call_id\":\"call_utf8\",\"name\":\"bash\","
                                                                                                        "\"arguments\":\"{}\"}"},
                                                                ava::session::SessionEntry{.id = "utf8_tool_result",
                                                                                           .parent_id = "",
                                                                                           .type = ava::session::EntryType::ToolResult,
                                                                                           .timestamp = "2026-04-27T00:00:01Z",
                                                                                           .data_json = "{\"call_id\":\"call_utf8\",\"name\":\"bash\","
                                                                                                        "\"success\":true,\"result\":\"" +
                                                                                                        utf8_result + "\"}"}};
  auto utf8_messages = ava::agent::build_provider_messages_from_entries(
      utf8_entries, ava::agent::MessageBuildOptions{.max_tool_result_context_bytes = truncation_marker.size() + 4});
  expect(utf8_messages && utf8_messages->size() == 2, "utf8 tool entries reconstruct as provider messages");
  if (!utf8_messages || utf8_messages->size() != 2)
    return;
  expect((*utf8_messages)[1].content_parts[0].text.rfind("abc\n[AVA: tool result content truncated]", 0) == 0 &&
             (*utf8_messages)[1].content_parts[0].text.find(euro) == std::string::npos,
         "native tool-result truncation avoids splitting utf8 code points");

  std::vector<ava::session::SessionEntry> const malformed_success_entries = {
      ava::session::SessionEntry{.id = "malformed_success_call",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::ToolCall,
                                 .timestamp = "2026-04-27T00:00:00Z",
                                 .data_json = "{\"call_id\":\"call_success_prefix\",\"name\":\"bash\","
                                              "\"arguments\":\"{}\"}"},
      ava::session::SessionEntry{.id = "malformed_success_result",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::ToolResult,
                                 .timestamp = "2026-04-27T00:00:01Z",
                                 .data_json = "{\"call_id\":\"call_success_prefix\",\"name\":\"bash\","
                                              "\"success\":falsefoo,\"result\":\"bool result\"}"}};
  auto malformed_success_messages = ava::agent::build_provider_messages_from_entries(malformed_success_entries);
  expect(malformed_success_messages && malformed_success_messages->size() == 2, "malformed bool tool entries reconstruct as provider messages");
  if (!malformed_success_messages || malformed_success_messages->size() != 2)
    return;
  expect((*malformed_success_messages)[1].content_parts.size() == 1 && !(*malformed_success_messages)[1].content_parts[0].is_error,
         "malformed success bool prefixes do not parse as native error metadata");

  std::vector<ava::session::SessionEntry> const malformed_entries = {ava::session::SessionEntry{.id = "malformed_tool_call",
                                                                                                .parent_id = "",
                                                                                                .type = ava::session::EntryType::ToolCall,
                                                                                                .timestamp = "2026-04-27T00:00:00Z",
                                                                                                .data_json = "{\"call_id\":\"call_bad\",\"name\":\"bash\","
                                                                                                             "\"arguments\":\"{\\\"cmd\\\":}\"}"},
                                                                     ava::session::SessionEntry{.id = "malformed_tool_result",
                                                                                                .parent_id = "",
                                                                                                .type = ava::session::EntryType::ToolResult,
                                                                                                .timestamp = "2026-04-27T00:00:01Z",
                                                                                                .data_json = "{\"call_id\":\"call_bad\",\"name\":\"bash\","
                                                                                                             "\"success\":true,\"result\":\"still visible\"}"}};
  auto malformed_messages = ava::agent::build_provider_messages_from_entries(malformed_entries);
  expect(malformed_messages && malformed_messages->size() == 2, "malformed tool entries still reconstruct as fallback provider messages");
  if (!malformed_messages || malformed_messages->size() != 2)
    return;
  expect((*malformed_messages)[0].content.find("cmd") != std::string::npos && (*malformed_messages)[0].content_parts.empty() &&
             (*malformed_messages)[1].content.find("still visible") != std::string::npos && (*malformed_messages)[1].content_parts.empty(),
         "malformed native tool-use replay falls back to text-only without dangling native tool-result");

  std::vector<ava::session::SessionEntry> const malformed_id_entries = {
      ava::session::SessionEntry{.id = "malformed_id_tool_call",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::ToolCall,
                                 .timestamp = "2026-04-27T00:00:00Z",
                                 .data_json = "{\"call_id\":\"call\\nbad\",\"name\":\"bash\","
                                              "\"arguments\":\"{}\"}"},
      ava::session::SessionEntry{.id = "malformed_id_tool_result",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::ToolResult,
                                 .timestamp = "2026-04-27T00:00:01Z",
                                 .data_json = "{\"call_id\":\"call\\nbad\",\"name\":\"bash\","
                                              "\"success\":true,\"result\":\"still visible\"}"}};
  auto malformed_id_messages = ava::agent::build_provider_messages_from_entries(malformed_id_entries);
  expect(malformed_id_messages && malformed_id_messages->size() == 2, "malformed tool id entries still reconstruct as fallback provider messages");
  if (!malformed_id_messages || malformed_id_messages->size() != 2)
    return;
  expect((*malformed_id_messages)[0].content_parts.empty() && (*malformed_id_messages)[1].content_parts.empty(),
         "malformed native tool ids fall back to text-only replay");

  std::vector<ava::session::SessionEntry> const duplicate_batch_id_entries = {
      ava::session::SessionEntry{.id = "duplicate_batch_assistant",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::AssistantMessage,
                                 .timestamp = "2026-04-27T00:00:00Z",
                                 .data_json = "{\"text\":\"\",\"tool_calls\":2}"},
      ava::session::SessionEntry{.id = "duplicate_batch_tool_call_1",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::ToolCall,
                                 .timestamp = "2026-04-27T00:00:01Z",
                                 .data_json = "{\"call_id\":\"call_duplicate_batch\",\"name\":\"bash\","
                                              "\"arguments\":\"{}\"}"},
      ava::session::SessionEntry{.id = "duplicate_batch_tool_result_1",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::ToolResult,
                                 .timestamp = "2026-04-27T00:00:02Z",
                                 .data_json = "{\"call_id\":\"call_duplicate_batch\",\"name\":\"bash\","
                                              "\"success\":true,\"result\":\"first result\"}"},
      ava::session::SessionEntry{.id = "duplicate_batch_tool_call_2",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::ToolCall,
                                 .timestamp = "2026-04-27T00:00:03Z",
                                 .data_json = "{\"call_id\":\"call_duplicate_batch\",\"name\":\"bash\","
                                              "\"arguments\":\"{}\"}"},
      ava::session::SessionEntry{.id = "duplicate_batch_tool_result_2",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::ToolResult,
                                 .timestamp = "2026-04-27T00:00:04Z",
                                 .data_json = "{\"call_id\":\"call_duplicate_batch\",\"name\":\"bash\","
                                              "\"success\":true,\"result\":\"second result\"}"}};
  auto duplicate_batch_id_messages = ava::agent::build_provider_messages_from_entries(duplicate_batch_id_entries);
  expect(duplicate_batch_id_messages && duplicate_batch_id_messages->size() == 5, "duplicate same-turn tool ids reconstruct as fallback provider messages");
  if (!duplicate_batch_id_messages || duplicate_batch_id_messages->size() != 5)
    return;
  expect((*duplicate_batch_id_messages)[1].content_parts.empty() && (*duplicate_batch_id_messages)[2].content_parts.empty() &&
             (*duplicate_batch_id_messages)[3].content_parts.empty() && (*duplicate_batch_id_messages)[4].content_parts.empty(),
         "duplicate same-turn native tool ids fall back to text-only replay");

  std::vector<ava::session::SessionEntry> const reused_id_entries = {ava::session::SessionEntry{.id = "valid_tool_call",
                                                                                                .parent_id = "",
                                                                                                .type = ava::session::EntryType::ToolCall,
                                                                                                .timestamp = "2026-04-27T00:00:00Z",
                                                                                                .data_json = "{\"call_id\":\"call_reused\",\"name\":\"bash\","
                                                                                                             "\"arguments\":\"{}\"}"},
                                                                     ava::session::SessionEntry{.id = "valid_tool_result",
                                                                                                .parent_id = "",
                                                                                                .type = ava::session::EntryType::ToolResult,
                                                                                                .timestamp = "2026-04-27T00:00:01Z",
                                                                                                .data_json = "{\"call_id\":\"call_reused\",\"name\":\"bash\","
                                                                                                             "\"success\":true,\"result\":\"first result\"}"},
                                                                     ava::session::SessionEntry{.id = "valid_reused_tool_call",
                                                                                                .parent_id = "",
                                                                                                .type = ava::session::EntryType::ToolCall,
                                                                                                .timestamp = "2026-04-27T00:00:02Z",
                                                                                                .data_json = "{\"call_id\":\"call_reused\",\"name\":\"bash\","
                                                                                                             "\"arguments\":\"{}\"}"},
                                                                     ava::session::SessionEntry{.id = "valid_reused_tool_result",
                                                                                                .parent_id = "",
                                                                                                .type = ava::session::EntryType::ToolResult,
                                                                                                .timestamp = "2026-04-27T00:00:03Z",
                                                                                                .data_json = "{\"call_id\":\"call_reused\",\"name\":\"bash\","
                                                                                                             "\"success\":true,\"result\":\"second result\"}"}};
  auto reused_id_messages = ava::agent::build_provider_messages_from_entries(reused_id_entries);
  expect(reused_id_messages && reused_id_messages->size() == 4, "reused tool ids reconstruct as provider messages");
  if (!reused_id_messages || reused_id_messages->size() != 4)
    return;
  expect((*reused_id_messages)[1].content_parts.size() == 1 && (*reused_id_messages)[1].content_parts[0].type == ava::provider::ContentPartType::ToolResult &&
             (*reused_id_messages)[2].content_parts.empty() && (*reused_id_messages)[3].content_parts.empty(),
         "native tool-result matching is one-shot and reused native ids fall back to text-only");

  std::vector<ava::session::SessionEntry> const interrupted_entries = {ava::session::SessionEntry{.id = "interrupted_tool_call",
                                                                                                  .parent_id = "",
                                                                                                  .type = ava::session::EntryType::ToolCall,
                                                                                                  .timestamp = "2026-04-27T00:00:00Z",
                                                                                                  .data_json = "{\"call_id\":\"call_late\",\"name\":\"bash\","
                                                                                                               "\"arguments\":\"{}\"}"},
                                                                       ava::session::SessionEntry{.id = "intervening_user",
                                                                                                  .parent_id = "",
                                                                                                  .type = ava::session::EntryType::UserMessage,
                                                                                                  .timestamp = "2026-04-27T00:00:01Z",
                                                                                                  .data_json = "{\"text\":\"intervening user\"}"},
                                                                       ava::session::SessionEntry{.id = "late_tool_result",
                                                                                                  .parent_id = "",
                                                                                                  .type = ava::session::EntryType::ToolResult,
                                                                                                  .timestamp = "2026-04-27T00:00:02Z",
                                                                                                  .data_json = "{\"call_id\":\"call_late\",\"name\":\"bash\","
                                                                                                               "\"success\":true,\"result\":\"late result\"}"}};
  auto interrupted_messages = ava::agent::build_provider_messages_from_entries(interrupted_entries);
  expect(interrupted_messages && interrupted_messages->size() == 3, "interrupted tool entries still reconstruct as provider messages");
  if (!interrupted_messages || interrupted_messages->size() != 3)
    return;
  expect((*interrupted_messages)[0].content_parts.empty() && (*interrupted_messages)[2].content_parts.empty(),
         "non-contiguous tool-use/result history falls back to text-only native replay");
}

void test_portable_omitted_reasoning_reconstructs_as_safe_text()
{
  std::vector<ava::session::SessionEntry> const entries = {
      ava::session::SessionEntry{.id = "portable_reasoning",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::ReasoningBlock,
                                 .timestamp = "2026-05-11T00:00:00Z",
                                 .data_json = "{\"provider\":\"anthropic\",\"model\":\"claude-sonnet-4-5\",\"format\":\"anthropic_thinking\","
                                              "\"text\":\"[Provider-private reasoning metadata omitted from portable export.]\",\"redacted\":true,"
                                              "\"private_replay_metadata_omitted\":{\"native_item_json\":false,\"signature\":false,\"redacted_data\":true}}"},
      ava::session::SessionEntry{.id = "portable_answer",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::AssistantMessage,
                                 .timestamp = "2026-05-11T00:00:01Z",
                                 .data_json = "{\"text\":\"answer after portable import\",\"tool_calls\":0}"}};

  auto messages = ava::agent::build_provider_messages_from_entries(entries);
  expect(messages && messages->size() == 1 && (*messages)[0].content_parts.size() == 2 &&
             (*messages)[0].content_parts[0].type == ava::provider::ContentPartType::Text && !(*messages)[0].content_parts[0].redacted &&
             (*messages)[0].content_parts[0].text.find("metadata omitted") != std::string::npos,
         "portable private-replay omission markers reconstruct as ordinary safe assistant text");
  if (!messages)
    return;

  ava::provider::AnthropicProvider const provider("https://anthropic.example.test");
  auto request = provider.build_request(
      ava::provider::ProviderRequest{
          .provider_id = "anthropic", .model_id = "claude-sonnet-4-5", .system_prompt = "system", .messages = *messages, .tools_json = {}, .stream = false},
      "anthropic-key");
  expect(request && request->body.find("metadata omitted") != std::string::npos && request->body.find("redacted_thinking") == std::string::npos,
         "portable imported Anthropic reasoning builds a safe ordinary-text request without private redacted data");

  auto const v4_reasoning_data = ava::session::serialize_assistant_output_item_data_json(
      ava::session::AssistantOutputItem{.assistant_turn_id = "portable_v4_reasoning_turn",
                                        .sequence = 0,
                                        .kind = ava::session::AssistantOutputItemKind::Reasoning,
                                        .provider_item_id = "PRIVATE_PORTABLE_V4_ITEM",
                                        .provider_output_index = 0,
                                        .payload = ava::session::AssistantOutputReasoning{.text = "PRIVATE_REDACTED_V4_TEXT",
                                                                                          .format = "anthropic_thinking",
                                                                                          .redacted = true,
                                                                                          .signature = "PRIVATE_REDACTED_V4_SIGNATURE",
                                                                                          .redacted_data = "PRIVATE_REDACTED_V4_DATA",
                                                                                          .native_item_json = "{\"type\":\"reasoning\"}"}});
  auto const v4_commit_data =
      ava::session::serialize_assistant_turn_commit_data_json(ava::session::AssistantTurnCommit{.assistant_turn_id = "portable_v4_reasoning_turn",
                                                                                                .item_count = 1,
                                                                                                .provider = "anthropic",
                                                                                                .model = "claude-sonnet-4-5",
                                                                                                .finish_reason = "completed",
                                                                                                .usage_json = std::nullopt});
  std::vector<ava::session::SessionEntry> const private_v4_entries = {{.id = "portable_v4_reasoning",
                                                                       .parent_id = "",
                                                                       .type = ava::session::EntryType::AssistantOutputItem,
                                                                       .timestamp = "2026-07-18T00:00:00Z",
                                                                       .data_json = v4_reasoning_data.value_or("{}")},
                                                                      {.id = "portable_v4_commit",
                                                                       .parent_id = "",
                                                                       .type = ava::session::EntryType::AssistantTurnCommit,
                                                                       .timestamp = "2026-07-18T00:00:01Z",
                                                                       .data_json = v4_commit_data.value_or("{}")}};
  auto portable_v4_entries = ava::session::project_portable_session_history(private_v4_entries);
  std::optional<std::vector<ava::provider::ChatMessage>> portable_v4_messages;
  if (portable_v4_entries)
  {
    auto built_messages = ava::agent::build_provider_messages_from_entries(*portable_v4_entries);
    if (built_messages)
      portable_v4_messages = std::move(*built_messages);
  }
  std::optional<ava::provider::HttpRequest> portable_v4_anthropic_request;
  std::optional<ava::provider::HttpRequest> portable_v4_openai_request;
  if (portable_v4_messages)
  {
    auto anthropic = provider.build_request(ava::provider::ProviderRequest{.provider_id = "anthropic",
                                                                           .model_id = "claude-sonnet-4-5",
                                                                           .system_prompt = "system",
                                                                           .messages = *portable_v4_messages,
                                                                           .tools_json = {},
                                                                           .stream = false},
                                            "anthropic-key");
    ava::provider::OpenAIProvider const openai_provider("https://api.example.test");
    auto openai = openai_provider.build_request(
        ava::provider::ProviderRequest{
            .provider_id = "openai", .model_id = "gpt-5.5", .system_prompt = "system", .messages = *portable_v4_messages, .tools_json = {}, .stream = false},
        "openai-key");
    if (anthropic)
      portable_v4_anthropic_request = std::move(*anthropic);
    if (openai)
      portable_v4_openai_request = std::move(*openai);
  }
  expect(portable_v4_entries && portable_v4_entries->front().data_json.find("private_replay_metadata_omitted\":true") != std::string::npos &&
             portable_v4_entries->front().data_json.find("PRIVATE_REDACTED_V4_SIGNATURE") == std::string::npos && portable_v4_messages &&
             portable_v4_messages->size() == 1 && (*portable_v4_messages)[0].content_parts.size() == 1 &&
             (*portable_v4_messages)[0].content_parts[0].type == ava::provider::ContentPartType::Text &&
             !(*portable_v4_messages)[0].content_parts[0].redacted && portable_v4_anthropic_request && portable_v4_openai_request &&
             portable_v4_anthropic_request->body.find("redacted_thinking") == std::string::npos &&
             portable_v4_anthropic_request->body.find("PRIVATE_REDACTED_V4") == std::string::npos &&
             portable_v4_openai_request->body.find("PRIVATE_REDACTED_V4") == std::string::npos,
         "portable v4 signed/redacted reasoning replays only as safe ordered text for OpenAI and Anthropic");
}

void test_image_attachment_message_reconstruction_and_validation()
{
  std::string const attachment_json = R"({"id":"img_1","type":"image","mime_type":"image/png","byte_size":1234,)"
                                      R"("sha256":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",)"
                                      R"("storage_path":"attachments/img_1.png"})";
  std::vector<ava::session::SessionEntry> const entries = {
      ava::session::SessionEntry{.id = "image_user",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::UserMessage,
                                 .timestamp = "2026-05-08T00:00:00Z",
                                 .data_json = "{\"text\":\"describe this\",\"attachments\":[" + attachment_json + "]}"}};

  auto messages = ava::agent::build_provider_messages_from_entries(entries);
  expect(messages && messages->size() == 1, "image user message reconstructs as one provider message");
  if (!messages || messages->empty())
    return;
  expect((*messages)[0].content.find("describe this") != std::string::npos &&
             (*messages)[0].content.find("[image attachment: id=img_1 mime=image/png bytes=1234]") != std::string::npos,
         "image user message keeps text fallback metadata without raw bytes");
  expect((*messages)[0].content_parts.size() == 2 && (*messages)[0].content_parts[0].type == ava::provider::ContentPartType::Text &&
             (*messages)[0].content_parts[1].type == ava::provider::ContentPartType::Image && (*messages)[0].content_parts[1].attachment_id == "img_1" &&
             (*messages)[0].content_parts[1].mime_type == "image/png" && (*messages)[0].content_parts[1].storage_path == "attachments/img_1.png" &&
             (*messages)[0].content_parts[1].byte_size == 1234,
         "image user message carries provider-neutral image content metadata");

  auto const valid = ava::session::validate_session_replay(entries);
  expect(valid.ok(), "image attachment metadata validates when bounded and referenced");

  std::vector<ava::session::SessionEntry> const inline_data_entries = {
      ava::session::SessionEntry{.id = "image_inline",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::UserMessage,
                                 .timestamp = "2026-05-08T00:00:00Z",
                                 .data_json = R"({"text":"bad","attachments":[{"id":"img_2","type":"image","mime_type":"image/png",)"
                                              R"("byte_size":12,"sha256":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",)"
                                              R"("storage_path":"attachments/img_2.png","data_base64":"AAAA"}]})"}};
  auto const inline_validation = ava::session::validate_session_replay(inline_data_entries);
  expect(!inline_validation.ok() && inline_validation.issues.front().kind == ava::session::SessionReplayIssueKind::InvalidMessageEntry,
         "session replay rejects inline image bytes in message attachment metadata");

  std::vector<ava::session::SessionEntry> const unsupported_mime_entries = {
      ava::session::SessionEntry{.id = "image_svg",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::UserMessage,
                                 .timestamp = "2026-05-08T00:00:00Z",
                                 .data_json = R"({"text":"bad","attachments":[{"id":"img_3","type":"image","mime_type":"image/svg+xml",)"
                                              R"("byte_size":12,"sha256":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",)"
                                              R"("storage_path":"attachments/img_3.svg"}]})"}};
  auto const mime_validation = ava::session::validate_session_replay(unsupported_mime_entries);
  expect(!mime_validation.ok() && mime_validation.issues.front().kind == ava::session::SessionReplayIssueKind::InvalidMessageEntry,
         "session replay rejects unsupported image attachment MIME types");

  std::vector<ava::session::SessionEntry> const mixed_array_entries = {
      ava::session::SessionEntry{.id = "image_mixed_array",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::UserMessage,
                                 .timestamp = "2026-05-08T00:00:00Z",
                                 .data_json = "{\"text\":\"bad\",\"attachments\":[" + attachment_json + R"(,"raw-bytes"]})"}};
  auto const mixed_array_validation = ava::session::validate_session_replay(mixed_array_entries);
  expect(!mixed_array_validation.ok() && mixed_array_validation.issues.front().kind == ava::session::SessionReplayIssueKind::InvalidMessageEntry,
         "session replay rejects non-object attachment array elements");

  std::vector<ava::session::SessionEntry> const unknown_raw_entries = {
      ava::session::SessionEntry{.id = "image_unknown_raw",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::UserMessage,
                                 .timestamp = "2026-05-08T00:00:00Z",
                                 .data_json = R"({"text":"bad","attachments":[{"id":"img_4","type":"image","mime_type":"image/png",)"
                                              R"("byte_size":12,"sha256":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",)"
                                              R"("storage_path":"attachments/img_4.png","raw_bytes_base64":"AAAA"}]})"}};
  auto const unknown_raw_validation = ava::session::validate_session_replay(unknown_raw_entries);
  expect(!unknown_raw_validation.ok() && unknown_raw_validation.issues.front().kind == ava::session::SessionReplayIssueKind::InvalidMessageEntry,
         "session replay rejects unknown attachment fields that could carry inline bytes");

  std::vector<ava::session::SessionEntry> const traversal_path_entries = {
      ava::session::SessionEntry{.id = "image_traversal",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::UserMessage,
                                 .timestamp = "2026-05-08T00:00:00Z",
                                 .data_json = R"({"text":"bad","attachments":[{"id":"img_5","type":"image","mime_type":"image/png",)"
                                              R"("byte_size":12,"sha256":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",)"
                                              R"("storage_path":"../img_5.png"}]})"}};
  auto const traversal_validation = ava::session::validate_session_replay(traversal_path_entries);
  expect(!traversal_validation.ok() && traversal_validation.issues.front().kind == ava::session::SessionReplayIssueKind::InvalidMessageEntry,
         "session replay rejects escaping image storage paths");

  std::vector<ava::session::SessionEntry> const unanchored_path_entries = {
      ava::session::SessionEntry{.id = "image_unanchored",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::UserMessage,
                                 .timestamp = "2026-05-08T00:00:00Z",
                                 .data_json = R"({"text":"bad","attachments":[{"id":"img_6","type":"image","mime_type":"image/png",)"
                                              R"("byte_size":12,"sha256":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",)"
                                              R"("storage_path":"README.md"}]})"}};
  auto const unanchored_validation = ava::session::validate_session_replay(unanchored_path_entries);
  expect(!unanchored_validation.ok() && unanchored_validation.issues.front().kind == ava::session::SessionReplayIssueKind::InvalidMessageEntry,
         "session replay rejects image storage paths outside attachments namespace");

  std::vector<ava::session::SessionEntry> const fractional_size_entries = {
      ava::session::SessionEntry{.id = "image_fractional_size",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::UserMessage,
                                 .timestamp = "2026-05-08T00:00:00Z",
                                 .data_json = R"({"text":"bad","attachments":[{"id":"img_6a","type":"image","mime_type":"image/png",)"
                                              R"("byte_size":12.5,"sha256":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",)"
                                              R"("storage_path":"attachments/img_6a.png"}]})"}};
  auto const fractional_size_validation = ava::session::validate_session_replay(fractional_size_entries);
  expect(!fractional_size_validation.ok() && fractional_size_validation.issues.front().kind == ava::session::SessionReplayIssueKind::InvalidMessageEntry,
         "session replay rejects fractional image byte sizes");
  auto const fractional_size_sanitized = ava::session::sanitized_message_data_json(fractional_size_entries.front().data_json);
  expect(fractional_size_sanitized.find("attachments") == std::string::npos, "message data sanitizer omits fractional image byte sizes");
  auto const fractional_size_messages = ava::agent::build_provider_messages_from_entries(fractional_size_entries);
  expect(!fractional_size_messages && fractional_size_messages.error().message().find("provider replay") != std::string::npos,
         "provider replay rejects invalid fractional image byte sizes instead of dropping image metadata");

  std::vector<ava::session::SessionEntry> const exponent_size_entries = {
      ava::session::SessionEntry{.id = "image_exponent_size",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::UserMessage,
                                 .timestamp = "2026-05-08T00:00:00Z",
                                 .data_json = R"({"text":"bad","attachments":[{"id":"img_6b","type":"image","mime_type":"image/png",)"
                                              R"("byte_size":1e3,"sha256":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",)"
                                              R"("storage_path":"attachments/img_6b.png"}]})"}};
  auto const exponent_size_validation = ava::session::validate_session_replay(exponent_size_entries);
  expect(!exponent_size_validation.ok() && exponent_size_validation.issues.front().kind == ava::session::SessionReplayIssueKind::InvalidMessageEntry,
         "session replay rejects exponent image byte sizes");

  std::vector<ava::session::SessionEntry> const duplicate_key_entries = {
      ava::session::SessionEntry{.id = "image_duplicate_key",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::UserMessage,
                                 .timestamp = "2026-05-08T00:00:00Z",
                                 .data_json = R"({"text":"bad","attachments":[{"id":"img_7","id":"img_8","type":"image","mime_type":"image/png",)"
                                              R"("byte_size":12,"sha256":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",)"
                                              R"("storage_path":"attachments/img_7.png"}]})"}};
  auto const duplicate_key_validation = ava::session::validate_session_replay(duplicate_key_entries);
  expect(!duplicate_key_validation.ok() && duplicate_key_validation.issues.front().kind == ava::session::SessionReplayIssueKind::InvalidMessageEntry,
         "session replay rejects duplicate image attachment object keys");

  std::vector<ava::session::SessionEntry> const duplicate_id_entries = {
      ava::session::SessionEntry{.id = "image_duplicate_id",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::UserMessage,
                                 .timestamp = "2026-05-08T00:00:00Z",
                                 .data_json = "{\"text\":\"bad\",\"attachments\":[" + attachment_json + "," + attachment_json + "]}"}};
  auto const duplicate_id_validation = ava::session::validate_session_replay(duplicate_id_entries);
  expect(!duplicate_id_validation.ok() && duplicate_id_validation.issues.front().kind == ava::session::SessionReplayIssueKind::InvalidMessageEntry,
         "session replay rejects duplicate image attachment ids");
  auto const duplicate_id_sanitized = ava::session::sanitized_message_data_json(duplicate_id_entries.front().data_json);
  expect(duplicate_id_sanitized.find("attachments") == std::string::npos, "message data sanitizer omits duplicate image attachment ids");

  std::vector<ava::session::SessionEntry> const duplicate_top_level_entries = {
      ava::session::SessionEntry{.id = "image_duplicate_top_level",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::UserMessage,
                                 .timestamp = "2026-05-08T00:00:00Z",
                                 .data_json = "{\"text\":\"bad\",\"attachments\":[" + attachment_json + R"(],"attachments":["raw-bytes"]})"}};
  auto const duplicate_top_level_validation = ava::session::validate_session_replay(duplicate_top_level_entries);
  expect(
      !duplicate_top_level_validation.ok() && duplicate_top_level_validation.issues.front().kind == ava::session::SessionReplayIssueKind::InvalidMessageEntry,
      "session replay rejects duplicate top-level message attachment keys");
  auto const duplicate_top_level_sanitized = ava::session::sanitized_message_data_json(duplicate_top_level_entries.front().data_json);
  expect(duplicate_top_level_sanitized.find("raw-bytes") == std::string::npos, "message data sanitizer omits duplicate top-level attachment payloads");

  std::vector<ava::session::SessionEntry> const escaped_top_level_entries = {
      ava::session::SessionEntry{.id = "image_escaped_top_level",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::UserMessage,
                                 .timestamp = "2026-05-08T00:00:00Z",
                                 .data_json = "{\"text\":\"bad\",\"attach\\u006dents\":[" + attachment_json + "]}"}};
  auto const escaped_top_level_validation = ava::session::validate_session_replay(escaped_top_level_entries);
  expect(!escaped_top_level_validation.ok() && escaped_top_level_validation.issues.front().kind == ava::session::SessionReplayIssueKind::InvalidMessageEntry,
         "session replay rejects escaped top-level message keys in attachment-bearing data");

  std::vector<ava::session::SessionEntry> const assistant_attachment_entries = {
      ava::session::SessionEntry{.id = "assistant_image",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::AssistantMessage,
                                 .timestamp = "2026-05-08T00:00:00Z",
                                 .data_json = "{\"text\":\"assistant\",\"attachments\":[" + attachment_json + "]}"}};
  auto const assistant_validation = ava::session::validate_session_replay(assistant_attachment_entries);
  expect(!assistant_validation.ok() && assistant_validation.issues.front().kind == ava::session::SessionReplayIssueKind::InvalidMessageEntry,
         "session replay rejects assistant image attachments until assistant image semantics exist");
  auto const assistant_sanitized = ava::session::sanitized_message_data_json(assistant_attachment_entries.front().data_json, false);
  expect(assistant_sanitized.find("attachments") == std::string::npos,
         "message data sanitizer omits assistant image attachment metadata when attachments are disallowed");

  auto const sanitized = ava::session::sanitized_message_data_json(unknown_raw_entries.front().data_json);
  expect(sanitized.find("raw_bytes_base64") == std::string::npos && sanitized.find("AAAA") == std::string::npos,
         "message data sanitizer omits unknown attachment fields and inline bytes");

  ava::provider::ProviderRequest text_only_request{
      .provider_id = "test", .model_id = "text-only", .system_prompt = "", .messages = *messages, .tools_json = {}};
  auto const text_only = ava::provider::validate_image_content_parts(text_only_request, false);
  expect(!text_only && text_only.error().message().find("does not support image input") != std::string::npos,
         "provider image validation rejects image parts for text-only models");
  auto const image_model = ava::provider::validate_image_content_parts(text_only_request, true);
  expect(image_model.has_value(), "provider image validation accepts bounded user image metadata for image models");

  auto too_many_images = *messages;
  too_many_images[0].content_parts.clear();
  for (int index = 0; index < 17; ++index)
  {
    too_many_images[0].content_parts.push_back(ava::provider::ContentPart{.type = ava::provider::ContentPartType::Image,
                                                                          .attachment_id = "img_" + std::to_string(index),
                                                                          .mime_type = "image/png",
                                                                          .storage_path = "attachments/img_" + std::to_string(index) + ".png",
                                                                          .sha256 = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
                                                                          .byte_size = 1});
  }
  ava::provider::ProviderRequest too_many_request{
      .provider_id = "test", .model_id = "image-model", .system_prompt = "", .messages = too_many_images, .tools_json = {}};
  auto const too_many = ava::provider::validate_image_content_parts(too_many_request, true);
  expect(!too_many && too_many.error().message().find("count") != std::string::npos, "provider image validation caps image attachment count per request");

  auto too_large_total = *messages;
  too_large_total[0].content_parts.clear();
  for (int index = 0; index < 3; ++index)
  {
    too_large_total[0].content_parts.push_back(ava::provider::ContentPart{.type = ava::provider::ContentPartType::Image,
                                                                          .attachment_id = "big_" + std::to_string(index),
                                                                          .mime_type = "image/png",
                                                                          .storage_path = "attachments/big_" + std::to_string(index) + ".png",
                                                                          .sha256 = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
                                                                          .byte_size = 15 * 1024 * 1024});
  }
  too_many_request.messages = too_large_total;
  auto const too_large = ava::provider::validate_image_content_parts(too_many_request, true);
  expect(!too_large && too_large.error().message().find("total byte size") != std::string::npos,
         "provider image validation caps aggregate image bytes per request");

  auto invalid_path_messages = *messages;
  invalid_path_messages[0].content_parts[1].storage_path = "/tmp/img_1.png";
  ava::provider::ProviderRequest invalid_path_request{
      .provider_id = "test", .model_id = "image-model", .system_prompt = "", .messages = invalid_path_messages, .tools_json = {}};
  auto const invalid_path = ava::provider::validate_image_content_parts(invalid_path_request, true);
  expect(!invalid_path && invalid_path.error().message().find("storage path") != std::string::npos,
         "provider image validation rejects absolute image storage paths");
  invalid_path_messages[0].content_parts[1].storage_path = "README.md";
  invalid_path_request.messages = invalid_path_messages;
  auto const unanchored_path = ava::provider::validate_image_content_parts(invalid_path_request, true);
  expect(!unanchored_path && unanchored_path.error().message().find("storage path") != std::string::npos,
         "provider image validation rejects storage paths outside attachments namespace");
}

void test_image_attachment_storage_boundary()
{
  auto const root = temp_root() / "image-attachment-storage";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  auto store = ava::session::SessionStore::create(workspace, root / "sessions");
  expect(store.has_value(), "session store opens for image attachment storage test");
  if (!store)
    return;

  auto const attachment_root = ava::session::attachment_storage_root(*store);
  auto const attachment_path = attachment_root / "attachments" / "img_1.txt";
  std::filesystem::create_directories(attachment_path.parent_path());
  {
    std::ofstream file(attachment_path, std::ios::binary);
    file << "hello";
  }

  ava::session::ImageAttachmentRef attachment{.id = "img_1",
                                              .mime_type = "image/png",
                                              .storage_path = "attachments/img_1.txt",
                                              .sha256 = "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824",
                                              .byte_size = 5};
  auto loaded = ava::session::load_image_attachment(*store, attachment);
  expect(loaded && loaded->bytes == "hello" && loaded->path == attachment_path.lexically_normal(),
         "image attachment storage loads only verified session-owned bytes");

  auto escaped = ava::session::resolve_attachment_storage_path(*store, "attachments/../secrets.txt");
  expect(!escaped, "image attachment storage rejects escaping relative paths");

  auto wrong_size = attachment;
  wrong_size.byte_size = 4;
  auto const size_result = ava::session::load_image_attachment(*store, wrong_size);
  expect(!size_result && size_result.error().message().find("byte size") != std::string::npos,
         "image attachment storage verifies file size before returning bytes");

  auto wrong_hash = attachment;
  wrong_hash.sha256 = "0000000000000000000000000000000000000000000000000000000000000000";
  auto const hash_result = ava::session::load_image_attachment(*store, wrong_hash);
  expect(!hash_result && hash_result.error().message().find("sha256") != std::string::npos, "image attachment storage verifies sha256 before returning bytes");

  auto const symlink_path = attachment_root / "attachments" / "link.txt";
  std::error_code symlink_error;
  std::filesystem::create_symlink(attachment_path, symlink_path, symlink_error);
  if (!symlink_error)
  {
    auto symlink_attachment = attachment;
    symlink_attachment.storage_path = "attachments/link.txt";
    auto const symlink_result = ava::session::load_image_attachment(*store, symlink_attachment);
    expect(!symlink_result && symlink_result.error().message().find("symlink") != std::string::npos,
           "image attachment storage rejects symlink attachment paths");
  }

  auto const outside_dir = root / "outside";
  std::filesystem::create_directories(outside_dir);
  auto const outside_file = outside_dir / "secret.txt";
  {
    std::ofstream file(outside_file, std::ios::binary);
    file << "hello";
  }
  auto const symlink_directory = attachment_root / "attachments" / "linked-dir";
  symlink_error.clear();
  std::filesystem::create_directory_symlink(outside_dir, symlink_directory, symlink_error);
  if (!symlink_error)
  {
    auto intermediate_symlink = attachment;
    intermediate_symlink.storage_path = "attachments/linked-dir/secret.txt";
    auto const intermediate_result = ava::session::load_image_attachment(*store, intermediate_symlink);
    expect(!intermediate_result && intermediate_result.error().message().find("symlink") != std::string::npos,
           "image attachment storage rejects symlinked attachment directories");
  }

  auto symlink_root_store = ava::session::SessionStore::create(workspace, root / "symlink-root-sessions");
  expect(symlink_root_store.has_value(), "session store opens for symlinked attachment root test");
  if (symlink_root_store)
  {
    auto const symlink_root = ava::session::attachment_storage_root(*symlink_root_store);
    std::filesystem::create_directories(symlink_root.parent_path());
    symlink_error.clear();
    std::filesystem::create_directory_symlink(outside_dir, symlink_root, symlink_error);
    if (!symlink_error)
    {
      auto const symlink_root_result = ava::session::load_image_attachment(*symlink_root_store, attachment);
      expect(!symlink_root_result && symlink_root_result.error().message().find("symlink") != std::string::npos,
             "image attachment storage rejects symlinked attachment roots");
    }
  }
}

void test_image_attachment_import()
{
  auto const root = temp_root() / "image-attachment-import";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  auto store = ava::session::SessionStore::create(workspace, root / "sessions");
  expect(store.has_value(), "session store opens for image attachment import test");
  if (!store)
    return;

  auto const image_path = root / "input.png";
  auto const bytes = tiny_png_bytes();
  write_binary_file(image_path, bytes);

  auto imported = ava::session::import_image_attachment(*store, image_path);
  expect(imported && imported->id.starts_with("img_") && imported->mime_type == "image/png" && imported->storage_path.starts_with("attachments/") &&
             imported->byte_size == bytes.size(),
         "image attachment import stores byte-sniffed PNG metadata under the session attachment namespace");
  if (!imported)
    return;
  auto loaded = ava::session::load_image_attachment(*store, *imported);
  expect(loaded && loaded->bytes == bytes, "imported image attachment reloads only after size and sha verification");

  auto const unsupported_path = root / "not-image.txt";
  write_binary_file(unsupported_path, "hello");
  auto unsupported = ava::session::import_image_attachment(*store, unsupported_path);
  expect(!unsupported && unsupported.error().message().find("unsupported image format") != std::string::npos,
         "image attachment import rejects unsupported byte signatures");

  auto const outside = root / "outside.png";
  write_binary_file(outside, bytes);
  auto const link = root / "linked.png";
  std::error_code symlink_error;
  std::filesystem::create_symlink(outside, link, symlink_error);
  if (!symlink_error)
  {
    auto symlink_import = ava::session::import_image_attachment(*store, link);
    expect(!symlink_import && symlink_import.error().message().find("symlink") != std::string::npos, "image attachment import rejects symlink source paths");
  }
}

std::optional<std::filesystem::path> created_session_rollback_quarantine(std::filesystem::path const& session_path)
{
  auto const prefix = "." + session_path.filename().string() + ".rollback.";
  std::error_code iterator_error;
  for (std::filesystem::directory_iterator iterator(session_path.parent_path(), iterator_error), end; !iterator_error && iterator != end;
       iterator.increment(iterator_error))
  {
    if (iterator->path().filename().string().starts_with(prefix))
      return iterator->path();
  }
  return std::nullopt;
}

void test_created_session_rollback_is_identity_safe_and_preserves_attachments()
{
  auto const root = temp_root() / "created-session-rollback";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const sessions_dir = root / "sessions";
  std::filesystem::create_directories(workspace);

  auto create_owned = [&]() -> std::pair<ava::session::SessionStore, ava::session::SessionLease> {
    auto store = ava::session::SessionStore::create(workspace, sessions_dir);
    expect(store.has_value(), "created-session rollback test creates a store");
    if (!store)
      return {ava::session::SessionStore(ava::session::SessionStoreOptions{}), ava::session::SessionLease{}};
    auto lease = ava::session::SessionLease::create_and_acquire(store->session_path());
    expect(lease.has_value(), "created-session rollback test acquires the creating lease");
    if (!lease)
      return {std::move(*store), ava::session::SessionLease{}};
    return {std::move(*store), std::move(*lease)};
  };

  {
    auto [store, lease] = create_owned();
    if (!lease.canonical_path().empty())
    {
      auto const path = store.session_path();
      auto removed = store.remove_created_file(lease);
      expect(removed && !std::filesystem::exists(path) && !created_session_rollback_quarantine(path),
             "created-session rollback removes exactly the creating JSONL without leaving a quarantine sibling");
    }
  }

  {
    auto [store, lease] = create_owned();
    if (!lease.canonical_path().empty())
    {
      auto const path = store.session_path();
      auto const parked_original = root / "parked-original.jsonl";
      store.set_before_created_file_rollback_detach_for_test([&] {
        std::filesystem::rename(path, parked_original);
        write_binary_file(path, "replacement-session");
      });
      auto removed = store.remove_created_file(lease);
      expect(!removed && read_binary_file(path) == "replacement-session" && std::filesystem::exists(parked_original),
             "created-session rollback fails closed and preserves a replaced session basename");
    }
  }

  {
    auto [store, lease] = create_owned();
    if (!lease.canonical_path().empty())
    {
      auto const path = store.session_path();
      auto const parked_original = root / "fifo-parked-original.jsonl";
      store.set_before_created_file_rollback_detach_for_test([&] {
        std::filesystem::rename(path, parked_original);
        expect(::mkfifo(path.c_str(), 0600) == 0, "created-session rollback test installs a FIFO replacement");
      });
      auto removed = store.remove_created_file(lease);
      std::error_code fifo_status_error;
      auto const fifo_status = std::filesystem::symlink_status(path, fifo_status_error);
      expect(!removed && !fifo_status_error && std::filesystem::is_fifo(fifo_status) && std::filesystem::exists(parked_original) &&
                 !created_session_rollback_quarantine(path),
             "created-session rollback inspects a FIFO replacement without blocking and restores it without deletion");
    }
  }

  {
    auto [store, lease] = create_owned();
    if (!lease.canonical_path().empty())
    {
      auto const path = store.session_path();
      auto const original_parent = path.parent_path();
      auto const moved_parent = root / "moved-session-parent";
      store.set_after_created_file_rollback_detach_for_test([&] {
        std::filesystem::rename(original_parent, moved_parent);
        std::filesystem::create_directories(original_parent);
        write_binary_file(path, "parent-replacement-session");
      });
      auto removed = store.remove_created_file(lease);
      expect(removed && read_binary_file(path) == "parent-replacement-session" && !created_session_rollback_quarantine(moved_parent / path.filename()),
             "descriptor-anchored rollback cannot redirect deletion into a replacement parent directory");
    }
  }

  {
    auto [store, lease] = create_owned();
    if (!lease.canonical_path().empty())
    {
      auto const path = store.session_path();
      store.set_after_created_file_rollback_detach_for_test([&] { write_binary_file(path, "republished-session"); });
      auto removed = store.remove_created_file(lease);
      expect(!removed && read_binary_file(path) == "republished-session" && created_session_rollback_quarantine(path) &&
                 removed.error().format().find("created session name was republished") != std::string::npos &&
                 removed.error().format().find("quarantine_path:") != std::string::npos,
             "rollback preserves and reports its exact quarantine when the original name is republished after detach");
    }
  }

  {
    auto [store, lease] = create_owned();
    if (!lease.canonical_path().empty())
    {
      auto const path = store.session_path();
      auto const parked_original = root / "mismatch-parked-original.jsonl";
      store.set_after_created_file_rollback_detach_for_test([&] {
        auto quarantine = created_session_rollback_quarantine(path);
        if (!quarantine)
          return;
        std::filesystem::rename(*quarantine, parked_original);
        write_binary_file(*quarantine, "quarantine-replacement");
      });
      auto removed = store.remove_created_file(lease);
      expect(!removed && std::filesystem::exists(parked_original) && read_binary_file(path) == "quarantine-replacement" &&
                 !created_session_rollback_quarantine(path),
             "rollback restores a detached quarantine mismatch to the original name without deleting either inode");
    }
  }

  {
    auto [store, creating_lease] = create_owned();
    if (!creating_lease.canonical_path().empty())
    {
      auto const path = store.session_path();
      auto no_token = store.remove_created_file(ava::session::SessionLease{});
      creating_lease = ava::session::SessionLease{};
      auto noncreating_lease = ava::session::SessionLease::acquire(path);
      auto noncreating =
          noncreating_lease ? store.remove_created_file(*noncreating_lease) : ava::core::VoidResult(std::unexpected(std::move(noncreating_lease.error())));
      expect(!no_token && !noncreating && std::filesystem::exists(path),
             "created-session rollback rejects missing and non-creating leases without deleting the session");
    }
  }

  {
    auto [store, lease] = create_owned();
    if (!lease.canonical_path().empty())
    {
      auto const attachment_file = ava::session::attachment_storage_root(store) / "nested" / "attachment.bin";
      write_binary_file(attachment_file, "retained attachment bytes");
      ava::core::Error primary(ava::core::ErrorCategory::Unknown, "runtime construction failed");
      ava::session::rollback_created_session_with_context(store, lease, primary);
      auto const formatted = primary.format();
      expect(!std::filesystem::exists(store.session_path()) && read_binary_file(attachment_file) == "retained attachment bytes" &&
                 primary.message() == "runtime construction failed" && formatted.find("created_session_id: " + store.session_id()) != std::string::npos &&
                 formatted.find("rollback_attachment_path: " + ava::session::attachment_storage_root(store).string()) != std::string::npos &&
                 formatted.find("rollback_attachment_disposition: preserved") != std::string::npos,
             "session rollback preserves an attachment subtree byte-for-byte and reports its retained path without replacing the primary error");
    }
  }
}

std::size_t error_context_count(ava::core::Error const& error, std::string_view key)
{
  return static_cast<std::size_t>(std::ranges::count_if(error.context(), [&](ava::core::ErrorContext const& item) { return item.key == key; }));
}

std::optional<std::string> error_context_value(ava::core::Error const& error, std::string_view key)
{
  auto const item = std::ranges::find_if(error.context(), [&](ava::core::ErrorContext const& context) { return context.key == key; });
  return item == error.context().end() ? std::nullopt : std::optional<std::string>(item->value);
}

void test_assistant_output_append_target_state_and_batches()
{
  auto const root = temp_root() / "assistant-output-append-target";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const sessions = root / "sessions";
  std::filesystem::create_directories(workspace);

  auto item = [](std::string id, std::string turn_id, std::size_t sequence, std::string provider_item_id, std::size_t provider_output_index) {
    auto data = ava::session::serialize_assistant_output_item_data_json(ava::session::AssistantOutputItem{
        .assistant_turn_id = std::move(turn_id),
        .sequence = sequence,
        .kind = ava::session::AssistantOutputItemKind::Text,
        .provider_item_id = std::move(provider_item_id),
        .provider_output_index = provider_output_index,
        .payload = ava::session::AssistantOutputText{.text = "staged", .assistant_phase = ava::session::AssistantOutputTextPhase::Commentary}});
    return ava::session::SessionEntry{.id = std::move(id),
                                      .parent_id = "",
                                      .type = ava::session::EntryType::AssistantOutputItem,
                                      .timestamp = ava::session::now_timestamp(),
                                      .data_json = data.value_or("{}")};
  };
  auto commit = [](std::string id, std::string turn_id, std::size_t item_count) {
    auto data = ava::session::serialize_assistant_turn_commit_data_json(ava::session::AssistantTurnCommit{.assistant_turn_id = std::move(turn_id),
                                                                                                          .item_count = item_count,
                                                                                                          .provider = "openai",
                                                                                                          .model = "gpt-5.5",
                                                                                                          .finish_reason = "completed",
                                                                                                          .usage_json = std::nullopt});
    return ava::session::SessionEntry{.id = std::move(id),
                                      .parent_id = "",
                                      .type = ava::session::EntryType::AssistantTurnCommit,
                                      .timestamp = ava::session::now_timestamp(),
                                      .data_json = data.value_or("{}")};
  };
  auto ordinary = [](std::string id) {
    return ava::session::SessionEntry{.id = std::move(id),
                                      .parent_id = "",
                                      .type = ava::session::EntryType::UserMessage,
                                      .timestamp = ava::session::now_timestamp(),
                                      .data_json = "{\"text\":\"ordinary\"}"};
  };

  {
    auto persistent = ava::session::SessionStore::create(workspace, sessions);
    auto persistent_lease = persistent ? ava::session::SessionLease::create_and_acquire(persistent->session_path())
                                       : ava::core::Result<ava::session::SessionLease>(std::unexpected(persistent.error()));
    auto ephemeral = ava::session::SessionStore::create_ephemeral(workspace / "raw-v4");
    auto persistent_raw = persistent && persistent_lease ? persistent->append(*persistent_lease, item("raw-persistent", "raw-turn", 0, "raw-item", 0))
                                                         : ava::core::VoidResult(std::unexpected(persistent.error()));
    auto ephemeral_raw = ephemeral ? ephemeral->append_ephemeral(item("raw-ephemeral", "raw-ephemeral-turn", 0, "raw-ephemeral-item", 0))
                                   : ava::core::VoidResult(std::unexpected(ephemeral.error()));
    auto persistent_entries =
        persistent ? persistent->load(*persistent_lease) : ava::core::Result<std::vector<ava::session::SessionEntry>>(std::unexpected(persistent.error()));
    expect(!persistent_raw && !ephemeral_raw && persistent_entries && persistent_entries->empty(),
           "public raw SessionStore append APIs reject all v4 assistant-output mutations without writing records");
  }

  {
    auto destination = ava::session::SessionStore::create(workspace, sessions);
    auto lease = destination ? ava::session::SessionLease::create_and_acquire(destination->session_path())
                             : ava::core::Result<ava::session::SessionLease>(std::unexpected(destination.error()));
    if (destination && lease)
    {
      std::vector<ava::session::SessionEntry> copied{ordinary("copy-user"), item("copy-output", "copy-turn", 0, "copy-provider-item", 0),
                                                     commit("copy-commit", "copy-turn", 1)};
      auto copied_once = destination->append_validated_copy(*lease, copied);
      auto copied_twice = destination->append_validated_copy(*lease, copied);
      auto entries = destination->load(*lease);
      expect(copied_once && !copied_twice && entries && entries->size() == copied.size() && ava::session::classify_assistant_output(*entries).turns.size() == 1,
             "validated copy preflights a complete v4 history and permits it only once into an empty creating destination");
    }
  }

  {
    auto store = ava::session::SessionStore::create(workspace, sessions);
    auto lease = store ? ava::session::SessionLease::create_and_acquire(store->session_path())
                       : ava::core::Result<ava::session::SessionLease>(std::unexpected(store.error()));
    if (store && lease)
    {
      auto first_target = ava::session::SessionAppendTarget::create_persistent(*store, *lease);
      auto stale_target = ava::session::SessionAppendTarget::create_persistent(*store, *lease);
      if (first_target && stale_target)
      {
        auto first = (*first_target)->append_batch({commit("stale-first", "stale-turn", 0)});
        auto stale = (*stale_target)->append_batch({commit("stale-duplicate", "stale-turn", 0)});
        auto physical_first = first ? (*first_target)
                                          ->append_batch({item("duplicate-physical-output", "physical-turn", 0, "physical-provider-item", 0),
                                                          commit("physical-first-commit", "physical-turn", 1)})
                                    : ava::core::VoidResult(std::unexpected(first.error()));
        auto physical_duplicate = physical_first
                                      ? (*stale_target)
                                            ->append_batch({item("duplicate-physical-output", "physical-turn-two", 0, "physical-provider-item-two", 0),
                                                            commit("physical-duplicate-commit", "physical-turn-two", 1)})
                                      : ava::core::VoidResult(std::unexpected(physical_first.error()));
        auto entries = store->load(*lease);
        expect(first && !stale && physical_first && !physical_duplicate && entries && entries->size() == 3,
               "persistent targets reload v4 state under shared append serialization and reject stale turns or duplicate physical output ids");
      }
    }
  }

  {
    auto store = ava::session::SessionStore::create(workspace, sessions);
    auto lease = store ? ava::session::SessionLease::create_and_acquire(store->session_path())
                       : ava::core::Result<ava::session::SessionLease>(std::unexpected(store.error()));
    if (store && lease)
    {
      bool fail_first_write = true;
      std::size_t writes = 0;
      store->set_append_write_for_test([&fail_first_write, &writes](int fd, std::string_view bytes) -> ssize_t {
        if (fail_first_write && writes++ == 0)
          return ::write(fd, bytes.data(), std::max<std::size_t>(1, bytes.size() / 2));
        if (fail_first_write)
        {
          errno = EIO;
          return -1;
        }
        return ::write(fd, bytes.data(), bytes.size());
      });
      auto first_target = ava::session::SessionAppendTarget::create_persistent(*store, *lease);
      auto second_target = ava::session::SessionAppendTarget::create_persistent(*store, *lease);
      auto partial = first_target ? (*first_target)->append(ordinary("partial-first-target")) : ava::core::VoidResult(std::unexpected(first_target.error()));
      fail_first_write = false;
      auto bypass = second_target ? (*second_target)->append(ordinary("partial-second-target")) : ava::core::VoidResult(std::unexpected(second_target.error()));
      expect(first_target && second_target && !partial && !bypass,
             "a second persistent append target cannot bypass a malformed partial tail before explicit recovery");
    }
  }

  {
    auto store = ava::session::SessionStore::create(workspace, sessions);
    auto lease = store ? ava::session::SessionLease::create_and_acquire(store->session_path())
                       : ava::core::Result<ava::session::SessionLease>(std::unexpected(store.error()));
    if (store && lease)
    {
      auto target = ava::session::SessionAppendTarget::create_persistent(*store, *lease);
      auto staged = item("persistent-staged-0", "persistent-turn", 0, "persistent-item-0", 0);
      auto seeded = target ? (*target)->append(staged) : ava::core::VoidResult(std::unexpected(target.error()));
      if (target && seeded)
      {
        auto unrelated = (*target)->append(ordinary("persistent-unrelated"));
        auto raw_unrelated = store->append(*lease, ordinary("persistent-raw-unrelated"));
        auto zero_while_pending = (*target)->append(commit("persistent-wrong-zero", "persistent-turn", 0));
        auto batch_while_pending = (*target)->append_batch(
            {item("persistent-batch-staged-1", "persistent-turn", 1, "persistent-batch-item-1", 1), commit("persistent-batch-commit", "persistent-turn", 2)});
        auto after_reject = store->load();
        auto continued = (*target)->append(item("persistent-staged-1", "persistent-turn", 1, "persistent-item-1", 1));
        auto committed =
            continued ? (*target)->append(commit("persistent-commit", "persistent-turn", 2)) : ava::core::VoidResult(std::unexpected(continued.error()));
        auto after_commit = committed ? (*target)->append(ordinary("persistent-after-commit")) : ava::core::VoidResult(std::unexpected(committed.error()));
        auto final_entries = store->load();
        expect(!unrelated && !raw_unrelated && !zero_while_pending && !batch_while_pending && after_reject && after_reject->size() == 1 && continued &&
                   committed && after_commit && final_entries && final_entries->size() == 4 && final_entries->back().id == "persistent-after-commit",
               "persistent target and raw ordinary appends preserve a valid staged suffix, require its exact continuation and commit, then reopen ordinary "
               "appends");
      }
    }
  }

  {
    auto store = ava::session::SessionStore::create_ephemeral(workspace / "ephemeral-racing-targets");
    if (store)
    {
      auto first_target = ava::session::SessionAppendTarget::create_ephemeral(*store);
      auto second_target = ava::session::SessionAppendTarget::create_ephemeral(*store);
      if (first_target && second_target)
      {
        std::barrier start(2);
        auto first_append = std::async(std::launch::async, [target = *first_target, &start, &commit] {
          start.arrive_and_wait();
          return target->append_batch({commit("ephemeral-race-first", "ephemeral-race-turn", 0)});
        });
        auto second_append = std::async(std::launch::async, [target = *second_target, &start, &commit] {
          start.arrive_and_wait();
          return target->append_batch({commit("ephemeral-race-second", "ephemeral-race-turn", 0)});
        });
        bool const completed = first_append.wait_for(std::chrono::seconds(2)) == std::future_status::ready &&
                               second_append.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
        bool one_committed = false;
        if (completed)
        {
          auto first = first_append.get();
          auto second = second_append.get();
          one_committed = static_cast<bool>(first) != static_cast<bool>(second);
        }
        auto entries = completed ? store->load()
                                 : ava::core::Result<std::vector<ava::session::SessionEntry>>(
                                       std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "append targets did not complete")));
        expect(completed && one_committed && entries && entries->size() == 1,
               "independent ephemeral targets serialize stale v4 batches through one shared mutation lock without deadlocking");
      }
    }
  }

  {
    auto store = ava::session::SessionStore::create_ephemeral(workspace / "ephemeral");
    if (store)
    {
      auto target = ava::session::SessionAppendTarget::create_ephemeral(*store);
      auto seeded = target ? (*target)->append(item("ephemeral-staged-0", "ephemeral-turn", 0, "ephemeral-item-0", 0))
                           : ava::core::VoidResult(std::unexpected(target.error()));
      if (target && seeded)
      {
        auto unrelated = (*target)->append(ordinary("ephemeral-unrelated"));
        auto raw_unrelated = store->append_ephemeral(ordinary("ephemeral-raw-unrelated"));
        auto after_reject = store->load();
        auto continued = (*target)->append(item("ephemeral-staged-1", "ephemeral-turn", 1, "ephemeral-item-1", 1));
        auto committed =
            continued ? (*target)->append(commit("ephemeral-commit", "ephemeral-turn", 2)) : ava::core::VoidResult(std::unexpected(continued.error()));
        auto after_commit = committed ? (*target)->append(ordinary("ephemeral-after-commit")) : ava::core::VoidResult(std::unexpected(committed.error()));
        auto zero = after_commit ? (*target)->append(commit("ephemeral-zero", "zero-turn", 0)) : ava::core::VoidResult(std::unexpected(after_commit.error()));
        auto after_zero = zero ? (*target)->append(ordinary("ephemeral-after-zero")) : ava::core::VoidResult(std::unexpected(zero.error()));
        auto final_entries = store->load();
        expect(!unrelated && !raw_unrelated && after_reject && after_reject->size() == 1 && continued && committed && after_commit && zero && after_zero &&
                   final_entries && final_entries->size() == 6 && final_entries->back().id == "ephemeral-after-zero",
               "ephemeral target and raw ordinary appends preserve staged state and accept matching zero-item commits only from the closed state");

        auto before_invalid_batch = final_entries ? final_entries->size() : 0;
        auto invalid_batch = (*target)->append_batch({item("invalid-batch-item", "invalid-batch", 1, "invalid", 0)});
        auto ordinary_batch = (*target)->append_batch({ordinary("ordinary-batch")});
        auto multiple_transaction_batch =
            (*target)->append_batch({commit("first-zero-commit", "first-zero-turn", 0), commit("second-zero-commit", "second-zero-turn", 0)});
        std::vector<ava::session::SessionEntry> over_limit(ava::session::kMaxSessionAppendBatchEntries + 1, ordinary("over-limit"));
        auto over_limit_batch = (*target)->append_batch(std::move(over_limit));
        std::vector<ava::session::SessionEntry> oversize;
        for (std::size_t index = 0; index < 5; ++index)
        {
          auto large = ordinary("oversize-" + std::to_string(index));
          large.data_json = "{\"text\":\"" + std::string(900U * 1024U, 'x') + "\"}";
          oversize.push_back(std::move(large));
        }
        auto oversize_batch = (*target)->append_batch(std::move(oversize));
        auto after_invalid_batches = store->load();
        expect(!invalid_batch && !ordinary_batch && !multiple_transaction_batch && !over_limit_batch && !oversize_batch && after_invalid_batches &&
                   after_invalid_batches->size() == before_invalid_batch,
               "assistant-output batch preflight rejects non-transaction, multiple-transaction, invalid, over-limit, and oversize shapes without writing any "
               "record");
      }
    }
  }

  {
    auto store = ava::session::SessionStore::create_ephemeral(workspace / "max-batch");
    auto target = store ? ava::session::SessionAppendTarget::create_ephemeral(*store)
                        : ava::core::Result<std::shared_ptr<ava::session::SessionAppendTarget>>(std::unexpected(store.error()));
    if (store && target)
    {
      std::vector<ava::session::SessionEntry> maximum_transaction;
      maximum_transaction.reserve(ava::session::kMaxSessionAppendBatchEntries);
      for (std::size_t index = 0; index < ava::session::kMaxAssistantOutputItemsPerTurn; ++index)
      {
        maximum_transaction.push_back(
            item("maximum-item-" + std::to_string(index), "maximum-turn", index, "maximum-provider-item-" + std::to_string(index), index));
      }
      maximum_transaction.push_back(commit("maximum-commit", "maximum-turn", ava::session::kMaxAssistantOutputItemsPerTurn));
      auto appended = (*target)->append_batch(std::move(maximum_transaction));
      auto after_batch = store->load();
      auto ordinary_after_batch = appended ? (*target)->append(ordinary("maximum-after-commit")) : ava::core::VoidResult(std::unexpected(appended.error()));
      auto final_entries = store->load();
      expect(appended && after_batch && after_batch->size() == ava::session::kMaxSessionAppendBatchEntries && ordinary_after_batch && final_entries &&
                 final_entries->size() == ava::session::kMaxSessionAppendBatchEntries + 1,
             "one maximum-size v4 assistant transaction preflights and commits with linear state growth before reopening ordinary appends");
    }
  }

  {
    auto store = ava::session::SessionStore::create(workspace, sessions);
    auto lease = store ? ava::session::SessionLease::create_and_acquire(store->session_path())
                       : ava::core::Result<ava::session::SessionLease>(std::unexpected(store.error()));
    if (store && lease)
    {
      bool allow_writes = false;
      store->set_append_write_for_test([&allow_writes](int fd, std::string_view bytes) -> ssize_t {
        if (allow_writes)
          return ::write(fd, bytes.data(), bytes.size());
        errno = EIO;
        return -1;
      });
      auto target = ava::session::SessionAppendTarget::create_persistent(*store, *lease);
      if (target)
      {
        auto first_failure = (*target)->append_batch({commit("no-durable-commit", "no-durable-turn", 0)});
        auto after_failure = store->load();
        allow_writes = true;
        auto ordinary_after_failure = (*target)->append(ordinary("after-no-durable-failure"));
        auto final_entries = store->load();
        expect(!first_failure && after_failure && after_failure->empty() && ordinary_after_failure && final_entries && final_entries->size() == 1 &&
                   final_entries->front().id == "after-no-durable-failure",
               "a batch write failure before its first durable record leaves the ready append target usable");
      }
    }
  }

  {
    auto store = ava::session::SessionStore::create(workspace, sessions);
    auto lease = store ? ava::session::SessionLease::create_and_acquire(store->session_path())
                       : ava::core::Result<ava::session::SessionLease>(std::unexpected(store.error()));
    if (store && lease)
    {
      std::size_t writes = 0;
      bool allow_later_writes = false;
      store->set_append_write_for_test([&writes, &allow_later_writes](int fd, std::string_view bytes) -> ssize_t {
        if (writes++ == 0)
        {
          auto const short_count = std::max<std::size_t>(1, bytes.size() / 2);
          return ::write(fd, bytes.data(), short_count);
        }
        if (allow_later_writes)
          return ::write(fd, bytes.data(), bytes.size());
        errno = EIO;
        return -1;
      });
      auto target = ava::session::SessionAppendTarget::create_persistent(*store, *lease);
      if (target)
      {
        auto partial = (*target)->append(ordinary("single-short-write"));
        auto blocked = (*target)->append(ordinary("single-short-write-blocked"));
        allow_later_writes = true;
        auto recovered = (*target)->recover();
        auto reopened = recovered ? (*target)->append(ordinary("single-short-write-reopened")) : ava::core::VoidResult(std::unexpected(recovered.error()));
        auto final_entries = store->load();
        expect(!partial && error_context_value(partial.error(), "append_commit_state") == "partial_or_unknown",
               "a first-record single append short write reports partial_or_unknown");
        expect(!blocked, "a first-record single append short write blocks later mutation before recovery");
        expect(static_cast<bool>(recovered), "a first-record single append short write recovers its torn tail");
        expect(static_cast<bool>(reopened), reopened ? "a recovered first-record single append short write accepts a new mutation" : reopened.error().format());
        expect(final_entries && final_entries->size() == 1 && final_entries->front().id == "single-short-write-reopened",
               "a recovered first-record single append short write retains only the new mutation");
      }
    }
  }

  {
    auto store = ava::session::SessionStore::create(workspace, sessions);
    auto lease = store ? ava::session::SessionLease::create_and_acquire(store->session_path())
                       : ava::core::Result<ava::session::SessionLease>(std::unexpected(store.error()));
    if (store && lease)
    {
      std::size_t writes = 0;
      bool allow_later_writes = false;
      store->set_append_write_for_test([&writes, &allow_later_writes](int fd, std::string_view bytes) -> ssize_t {
        if (writes++ == 0)
        {
          auto const short_count = std::max<std::size_t>(1, bytes.size() / 2);
          return ::write(fd, bytes.data(), short_count);
        }
        if (allow_later_writes)
          return ::write(fd, bytes.data(), bytes.size());
        errno = EIO;
        return -1;
      });
      auto target = ava::session::SessionAppendTarget::create_persistent(*store, *lease);
      if (target)
      {
        auto partial = (*target)->append_batch({commit("batch-short-write", "batch-short-turn", 0)});
        auto blocked = (*target)->append(ordinary("batch-short-write-blocked"));
        allow_later_writes = true;
        auto recovered = (*target)->recover();
        auto reopened = recovered ? (*target)->append(ordinary("batch-short-write-reopened")) : ava::core::VoidResult(std::unexpected(recovered.error()));
        auto final_entries = store->load();
        expect(!partial && error_context_value(partial.error(), "append_commit_state") == "partial_or_unknown" &&
                   error_context_value(partial.error(), "batch_persisted_entries") == "0",
               "a first-record batch short write reports partial_or_unknown at zero completed records");
        expect(!blocked, "a first-record batch short write blocks later mutation before recovery");
        expect(static_cast<bool>(recovered), "a first-record batch short write recovers its torn tail");
        expect(static_cast<bool>(reopened), reopened ? "a recovered first-record batch short write accepts a new mutation" : reopened.error().format());
        expect(final_entries && final_entries->size() == 1 && final_entries->front().id == "batch-short-write-reopened",
               "a recovered first-record batch short write retains only the new mutation");
      }
    }
  }

  {
    auto store = ava::session::SessionStore::create(workspace, sessions);
    auto lease = store ? ava::session::SessionLease::create_and_acquire(store->session_path())
                       : ava::core::Result<ava::session::SessionLease>(std::unexpected(store.error()));
    if (store && lease)
    {
      int writes = 0;
      bool allow_later_writes = false;
      store->set_append_write_for_test([&writes, &allow_later_writes](int fd, std::string_view bytes) -> ssize_t {
        if (writes++ == 0 || allow_later_writes)
          return ::write(fd, bytes.data(), bytes.size());
        errno = EIO;
        return -1;
      });
      auto target = ava::session::SessionAppendTarget::create_persistent(*store, *lease);
      if (target)
      {
        auto partial = (*target)->append_batch({item("batch-staged-0", "batch-turn", 0, "batch-item-0", 0),
                                                item("batch-staged-1", "batch-turn", 1, "batch-item-1", 1), commit("batch-commit", "batch-turn", 2)});
        auto persisted = store->load();
        auto blocked_append = (*target)->append(ordinary("blocked-after-partial"));
        auto blocked_batch = (*target)->append_batch({commit("blocked-zero-commit", "blocked-zero-turn", 0)});
        allow_later_writes = true;
        auto recovered = (*target)->recover();
        auto recovered_entries = store->load();
        auto reopened = recovered ? (*target)->append(ordinary("after-partial-recovery")) : ava::core::VoidResult(std::unexpected(recovered.error()));
        auto final_entries = store->load();
        expect(!partial && error_context_value(partial.error(), "append_commit_state") == "partial_or_unknown" &&
                   error_context_value(partial.error(), "batch_persisted_entries") == "1" &&
                   error_context_value(partial.error(), "staged_prefix_recovery").has_value() && persisted && persisted->size() == 1 &&
                   persisted->front().id == "batch-staged-0" && !blocked_append && !blocked_batch && recovered && recovered_entries &&
                   recovered_entries->empty() && reopened && final_entries && final_entries->size() == 1 &&
                   final_entries->front().id == "after-partial-recovery",
               "a partial v4 batch latches mutation until explicit recovery truncates its staged prefix and reopens ordinary appends");
      }
    }
  }

  {
    auto store = ava::session::SessionStore::create(workspace, sessions);
    auto lease = store ? ava::session::SessionLease::create_and_acquire(store->session_path())
                       : ava::core::Result<ava::session::SessionLease>(std::unexpected(store.error()));
    if (store && lease)
    {
      store->set_after_append_write_for_test([] { throw 1; });
      auto target = ava::session::SessionAppendTarget::create_persistent(*store, *lease);
      if (target)
      {
        auto staged = (*target)->append(item("post-write-staged", "post-write-turn", 0, "post-write-item", 0));
        store->set_after_append_write_for_test({});
        auto ordinary_after_staged = (*target)->append(ordinary("post-write-ordinary"));
        auto persisted = store->load();
        expect(!staged && error_context_value(staged.error(), "append_commit_state") == "committed_to_leased_inode" && !ordinary_after_staged && persisted &&
                   persisted->size() == 1 && persisted->front().id == "post-write-staged",
               "a post-write failure publishes its durable staged state so ordinary appends remain rejected");
      }
    }
  }

  {
    auto store = ava::session::SessionStore::create(workspace, sessions);
    auto lease = store ? ava::session::SessionLease::create_and_acquire(store->session_path())
                       : ava::core::Result<ava::session::SessionLease>(std::unexpected(store.error()));
    if (store && lease)
    {
      std::size_t writes = 0;
      store->set_after_append_write_for_test([&writes] {
        if (++writes == 2)
          throw 1;
      });
      auto target = ava::session::SessionAppendTarget::create_persistent(*store, *lease);
      if (target)
      {
        auto partial = (*target)->append_batch(
            {item("post-write-batch-0", "post-write-batch", 0, "post-write-batch-item-0", 0), commit("post-write-batch-commit", "post-write-batch", 1)});
        store->set_after_append_write_for_test({});
        auto persisted = store->load();
        auto blocked = (*target)->append(ordinary("blocked-after-known-commit"));
        auto recovered = (*target)->recover();
        auto recovered_entries = store->load();
        auto reopened = recovered ? (*target)->append(ordinary("after-known-commit-recovery")) : ava::core::VoidResult(std::unexpected(recovered.error()));
        auto final_entries = store->load();
        auto projection = recovered_entries ? ava::session::classify_assistant_output(*recovered_entries) : ava::session::AssistantOutputProjection{};
        expect(!partial && error_context_value(partial.error(), "append_commit_state") == "partial_or_unknown" &&
                   error_context_value(partial.error(), "batch_persisted_entries") == "2" && error_context_value(partial.error(), "recovery").has_value() &&
                   persisted && persisted->size() == 2 && persisted->back().id == "post-write-batch-commit" && !blocked && recovered && recovered_entries &&
                   recovered_entries->size() == 2 && projection.turns.size() == 1 && reopened && final_entries && final_entries->size() == 3 &&
                   final_entries->back().id == "after-known-commit-recovery",
               "a final known-committed batch commit still requires explicit recovery but never truncates its completed turn");
      }
    }
  }

  {
    auto malformed = ava::session::SessionStore::create_ephemeral(workspace / "malformed");
    if (malformed)
    {
      auto sparse = item("malformed-sparse", "malformed-turn", 1, "malformed-item", 0);
      auto seeded = malformed->append_ephemeral(std::move(sparse));
      auto target = seeded ? ava::session::SessionAppendTarget::create_ephemeral(*malformed)
                           : ava::core::Result<std::shared_ptr<ava::session::SessionAppendTarget>>(std::unexpected(seeded.error()));
      expect(!target, "structurally malformed v4 history cannot create an append target");
    }
  }
}

void test_incomplete_assistant_output_suffix_recovery()
{
  auto const root = temp_root() / "incomplete-assistant-output-suffix-recovery";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const sessions = root / "sessions";
  std::filesystem::create_directories(workspace);

  auto ordinary = [](std::string id, std::string text = "ordinary") {
    return ava::session::SessionEntry{.id = std::move(id),
                                      .parent_id = "",
                                      .type = ava::session::EntryType::UserMessage,
                                      .timestamp = ava::session::now_timestamp(),
                                      .data_json = "{\"text\":\"" + text + "\"}"};
  };
  auto staged = [](std::string id, std::string turn_id, std::size_t sequence) {
    auto data = ava::session::serialize_assistant_output_item_data_json(ava::session::AssistantOutputItem{
        .assistant_turn_id = std::move(turn_id),
        .sequence = sequence,
        .kind = ava::session::AssistantOutputItemKind::Text,
        .provider_item_id = "provider-item-" + std::to_string(sequence),
        .provider_output_index = sequence,
        .payload = ava::session::AssistantOutputText{.text = "staged", .assistant_phase = ava::session::AssistantOutputTextPhase::Commentary}});
    return ava::session::SessionEntry{.id = std::move(id),
                                      .parent_id = "",
                                      .type = ava::session::EntryType::AssistantOutputItem,
                                      .timestamp = ava::session::now_timestamp(),
                                      .data_json = data.value_or("{}")};
  };
  auto commit = [](std::string id, std::string turn_id, std::size_t item_count) {
    auto data = ava::session::serialize_assistant_turn_commit_data_json(ava::session::AssistantTurnCommit{.assistant_turn_id = std::move(turn_id),
                                                                                                          .item_count = item_count,
                                                                                                          .provider = "openai",
                                                                                                          .model = "gpt-5.5",
                                                                                                          .finish_reason = "tool_calls",
                                                                                                          .usage_json = std::nullopt});
    return ava::session::SessionEntry{.id = std::move(id),
                                      .parent_id = "",
                                      .type = ava::session::EntryType::AssistantTurnCommit,
                                      .timestamp = ava::session::now_timestamp(),
                                      .data_json = data.value_or("{}")};
  };
  auto exact_line = [](ava::session::SessionEntry const& entry) {
    auto line = ava::session::serialize_session_entry_line(entry);
    return line ? *line + "\n" : std::string{};
  };
  // Crash-recovery fixtures intentionally model physical v4 bytes that exist
  // before an authority can finish the transaction. Do not route these through
  // the public raw append API, which correctly rejects v4 mutations.
  auto append_physical_v4_fixture = [&](ava::session::SessionStore const& store, ava::session::SessionEntry const& entry) {
    auto const line = exact_line(entry);
    if (line.empty())
      return ava::core::VoidResult(std::unexpected(ava::core::Error(ava::core::ErrorCategory::Session, "fixture entry did not serialize")));
    std::ofstream file(store.session_path(), std::ios::binary | std::ios::app);
    if (!file)
      return ava::core::VoidResult(std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to append physical v4 fixture")));
    file << line;
    file.flush();
    if (!file)
      return ava::core::VoidResult(std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to flush physical v4 fixture")));
    return ava::core::VoidResult{};
  };
  auto recovery_artifact_count = [](std::filesystem::path const& path) {
    std::size_t count = 0;
    std::error_code iter_error;
    auto const prefix = path.filename().string() + ".incomplete-assistant-output.";
    for (std::filesystem::directory_iterator it(path.parent_path(), iter_error), end; !iter_error && it != end; it.increment(iter_error))
      if (it->path().filename().string().starts_with(prefix))
        ++count;
    return count;
  };

  for (std::size_t count = 1; count <= 3; ++count)
  {
    auto store = ava::session::SessionStore::create(workspace, sessions);
    auto lease = store ? ava::session::SessionLease::create_and_acquire(store->session_path())
                       : ava::core::Result<ava::session::SessionLease>(std::unexpected(store.error()));
    if (!store || !lease)
      continue;
    auto const session_id = store->session_id();
    auto const path = store->session_path();
    auto seeded = store->append(*lease, ordinary("seed-" + std::to_string(count)));
    std::string expected_suffix;
    for (std::size_t index = 0; seeded && index < count; ++index)
    {
      auto entry = staged("stage-" + std::to_string(count) + "-" + std::to_string(index), "turn-" + std::to_string(count), index);
      expected_suffix += exact_line(entry);
      seeded = append_physical_v4_fixture(*store, entry);
    }
    lease = ava::session::SessionLease{};
    auto reopened = seeded ? ava::session::SessionStore::open(workspace, session_id, sessions)
                           : ava::core::Result<ava::session::SessionStore>(std::unexpected(seeded.error()));
    auto recovery_lease = reopened ? ava::session::SessionLease::acquire(reopened->session_path())
                                   : ava::core::Result<ava::session::SessionLease>(std::unexpected(reopened.error()));
    auto recovered = recovery_lease ? reopened->recover_incomplete_assistant_output_suffix(*recovery_lease, ava::session::SessionReadLimits{})
                                    : ava::core::Result<std::optional<ava::session::AssistantOutputSuffixRecovery>>(std::unexpected(recovery_lease.error()));
    auto entries = recovered && *recovered ? reopened->load(*recovery_lease)
                                           : ava::core::Result<std::vector<ava::session::SessionEntry>>(std::unexpected(
                                                 ava::core::Error(ava::core::ErrorCategory::Unknown, "staging recovery did not return metadata")));
    auto target = entries ? ava::session::SessionAppendTarget::create_persistent(*reopened, *recovery_lease)
                          : ava::core::Result<std::shared_ptr<ava::session::SessionAppendTarget>>(std::unexpected(entries.error()));
    auto next = target ? (*target)->append(ordinary("ordinary-after-" + std::to_string(count))) : ava::core::VoidResult(std::unexpected(target.error()));
    struct stat quarantine_status{};
    bool const quarantine_mode = recovered && *recovered && (*recovered)->quarantine_path &&
                                 ::stat((*recovered)->quarantine_path->c_str(), &quarantine_status) == 0 && (quarantine_status.st_mode & 0777) == 0600;
    expect(recovered && *recovered && (*recovered)->removed_entry_count == count && (*recovered)->removed_byte_count == expected_suffix.size() &&
               (*recovered)->quarantine_path && read_binary_file(*(*recovered)->quarantine_path) == expected_suffix && quarantine_mode && entries &&
               entries->size() == 1 && next && read_binary_file(path).ends_with("\n") && recovery_artifact_count(path) == 1,
           "restart recovery quarantines and removes exactly each complete uncommitted assistant-output crash prefix before a normal append");
  }

  auto make_persistent_fixture = [&](std::string id) {
    auto store = ava::session::SessionStore::create(workspace, sessions);
    auto lease = store ? ava::session::SessionLease::create_and_acquire(store->session_path())
                       : ava::core::Result<ava::session::SessionLease>(std::unexpected(store.error()));
    if (store && lease)
      static_cast<void>(store->append(*lease, ordinary("seed-" + id)));
    return std::pair(std::move(store), std::move(lease));
  };

  {
    auto [store, lease] = make_persistent_fixture("malformed");
    if (store && lease)
    {
      auto malformed = staged("malformed-stage", "malformed-turn", 1);
      static_cast<void>(append_physical_v4_fixture(*store, malformed));
      auto const before = read_binary_file(store->session_path());
      auto recovered = store->recover_incomplete_assistant_output_suffix(*lease, ava::session::SessionReadLimits{});
      expect(!recovered && read_binary_file(store->session_path()) == before && recovery_artifact_count(store->session_path()) == 0,
             "malformed complete staged suffix fails closed without a truncate or quarantine");
    }
  }
  {
    auto [store, lease] = make_persistent_fixture("interior");
    if (store && lease)
    {
      static_cast<void>(append_physical_v4_fixture(*store, staged("interior-stage", "interior-turn", 0)));
      static_cast<void>(append_physical_v4_fixture(*store, ordinary("interior-unrelated")));
      auto const before = read_binary_file(store->session_path());
      auto recovered = store->recover_incomplete_assistant_output_suffix(*lease, ava::session::SessionReadLimits{});
      expect(!recovered && read_binary_file(store->session_path()) == before && recovery_artifact_count(store->session_path()) == 0,
             "an interior staged group followed by an unrelated entry fails closed unchanged");
    }
  }
  {
    auto [store, lease] = make_persistent_fixture("committed");
    if (store && lease)
    {
      static_cast<void>(append_physical_v4_fixture(*store, staged("committed-stage", "committed-turn", 0)));
      static_cast<void>(append_physical_v4_fixture(*store, commit("committed-commit", "committed-turn", 1)));
      auto const before = read_binary_file(store->session_path());
      auto recovered = store->recover_incomplete_assistant_output_suffix(*lease, ava::session::SessionReadLimits{});
      expect(recovered && !*recovered && read_binary_file(store->session_path()) == before, "a committed assistant turn is never recovered or removed");
    }
  }
  {
    auto [store, lease] = make_persistent_fixture("limits-and-cancel");
    if (store && lease)
    {
      static_cast<void>(append_physical_v4_fixture(*store, staged("limited-stage", "limited-turn", 0)));
      auto const before = read_binary_file(store->session_path());
      auto canceled = store->recover_incomplete_assistant_output_suffix(*lease, ava::session::SessionReadLimits{}, [] { return true; });
      auto limited = store->recover_incomplete_assistant_output_suffix(
          *lease, ava::session::SessionReadLimits{.max_file_bytes = before.size(), .max_line_bytes = before.size(), .max_entries = 1});
      expect(!canceled && !limited && read_binary_file(store->session_path()) == before && recovery_artifact_count(store->session_path()) == 0,
             "assistant-output suffix recovery honors cancellation and entry limits before mutation");
    }
  }
  {
    auto [left_store, left_lease] = make_persistent_fixture("lease-mismatch-left");
    auto [right_store, right_lease] = make_persistent_fixture("lease-mismatch-right");
    if (left_store && left_lease && right_store && right_lease)
    {
      static_cast<void>(append_physical_v4_fixture(*left_store, staged("mismatch-stage", "mismatch-turn", 0)));
      auto const before = read_binary_file(left_store->session_path());
      auto mismatch = left_store->recover_incomplete_assistant_output_suffix(*right_lease, ava::session::SessionReadLimits{});
      expect(!mismatch && read_binary_file(left_store->session_path()) == before,
             "assistant-output suffix recovery rejects a lease for another session without touching the target");
    }
  }
  for (std::string const replacement_kind : {"replacement", "symlink", "fifo"})
  {
    auto [store, lease] = make_persistent_fixture("unsafe-" + replacement_kind);
    if (!store || !lease)
      continue;
    static_cast<void>(append_physical_v4_fixture(*store, staged("unsafe-stage-" + replacement_kind, "unsafe-turn-" + replacement_kind, 0)));
    auto const path = store->session_path();
    auto const parked = path.string() + ".parked";
    std::filesystem::rename(path, parked, remove_error);
    if (replacement_kind == "replacement")
      write_binary_file(path, "replacement\\n");
    else if (replacement_kind == "symlink")
      std::filesystem::create_symlink(parked, path, remove_error);
    else
      ::mkfifo(path.c_str(), 0600);
    auto recovered = store->recover_incomplete_assistant_output_suffix(*lease, ava::session::SessionReadLimits{});
    expect(!recovered && read_binary_file(parked).find("unsafe-stage-") != std::string::npos,
           "assistant-output suffix recovery rejects " + replacement_kind + " replacement without mutating the leased inode");
  }
  {
    auto ephemeral = ava::session::SessionStore::create_ephemeral(workspace / "ephemeral");
    if (ephemeral)
    {
      auto target = ava::session::SessionAppendTarget::create_ephemeral(*ephemeral);
      auto staged_append = target ? (*target)->append(staged("ephemeral-stage", "ephemeral-turn", 0)) : ava::core::VoidResult(std::unexpected(target.error()));
      auto recovered = staged_append && target ? (*target)->recover() : ava::core::VoidResult(std::unexpected(staged_append.error()));
      auto next = recovered && target ? (*target)->append(ordinary("ephemeral-ordinary")) : ava::core::VoidResult(std::unexpected(recovered.error()));
      auto entries = ephemeral->load();
      expect(staged_append && recovered && next && entries && entries->size() == 1 && entries->front().id == "ephemeral-ordinary",
             "ephemeral recovery erases only the proven trailing staging entries and rebuilds append state");
    }
  }
}

void test_lease_bound_session_reads_hold_exact_authority()
{
  auto const root = temp_root() / "lease-bound-session-reads";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);

  auto entry = [](std::string id, std::string text = "snapshot") {
    return ava::session::SessionEntry{.id = std::move(id),
                                      .parent_id = "",
                                      .type = ava::session::EntryType::UserMessage,
                                      .timestamp = ava::session::now_timestamp(),
                                      .data_json = "{\"text\":\"" + text + "\"}"};
  };
  auto make_owned = [&](std::string id) -> std::pair<ava::session::SessionStore, ava::session::SessionLease> {
    ava::session::SessionStore store(ava::session::SessionStoreOptions{.root_dir = root / id, .workspace_dir = workspace, .session_id = std::move(id)});
    auto lease = ava::session::SessionLease::create_and_acquire(store.session_path());
    if (!lease)
      return {std::move(store), ava::session::SessionLease{}};
    auto seeded = store.append(*lease, entry("seed"));
    expect(seeded.has_value(), "lease-bound read fixture seeds a framed session record");
    return {std::move(store), std::move(*lease)};
  };

  {
    auto [store, lease] = make_owned("static-replacement");
    if (lease.active())
    {
      auto const parked = store.session_path().string() + ".parked";
      std::filesystem::rename(store.session_path(), parked);
      write_binary_file(store.session_path(), "replacement\n");
      auto loaded = store.load(lease);
      expect(!loaded, "lease-bound load rejects a basename replaced before snapshot validation");
    }
  }

  {
    auto [store, lease] = make_owned("read-swap");
    if (lease.active())
    {
      auto const parked = store.session_path().string() + ".parked";
      store.set_after_lease_bound_read_for_test([&] {
        std::filesystem::rename(store.session_path(), parked);
        write_binary_file(store.session_path(), "replacement\n");
      });
      auto loaded = store.load_bounded(lease, ava::session::legacy_unbounded_session_read_limits());
      store.set_after_lease_bound_read_for_test({});
      expect(!loaded, "lease-bound load rejects a basename swap after its exact-offset read begins");
    }
  }

  {
    auto [store, lease] = make_owned("parent-swap");
    if (lease.active())
    {
      auto const parent = store.session_path().parent_path();
      auto const moved_parent = parent.string() + ".moved";
      store.set_after_lease_bound_read_for_test([&] {
        std::filesystem::rename(parent, moved_parent);
        std::filesystem::create_directories(parent);
        write_binary_file(store.session_path(), "replacement\n");
      });
      auto inspected = store.inspect_bounded(lease, ava::session::legacy_unbounded_session_read_limits());
      store.set_after_lease_bound_read_for_test({});
      expect(!inspected, "lease-bound inspection rejects replacement of its canonical parent publication during the read");
    }
  }

  {
    auto [store, lease] = make_owned("shrink");
    if (lease.active())
    {
      store.set_after_lease_bound_read_for_test([&] { std::filesystem::resize_file(store.session_path(), 1); });
      auto loaded = store.load(lease);
      store.set_after_lease_bound_read_for_test({});
      expect(!loaded, "lease-bound load rejects shrink of the leased inode during its initial-size snapshot");
    }
  }

  {
    auto [store, lease] = make_owned("growth");
    if (lease.active())
    {
      bool growth_appended = false;
      auto const initial_offset = lease.offset_for_test();
      store.set_after_lease_bound_read_for_test([&] { growth_appended = store.append(lease, entry("growth")).has_value(); });
      auto snapshot = store.load(lease);
      store.set_after_lease_bound_read_for_test({});
      auto complete = store.load(lease);
      expect(growth_appended && snapshot && snapshot->size() == 1 && snapshot->front().id == "seed" && complete && complete->size() == 2,
             "lease-bound load permits concurrent valid append growth while returning only its captured initial-size snapshot");
      expect(initial_offset >= 0 && lease.offset_for_test() == initial_offset,
             "lease-bound exact-offset pread leaves the shared lease open-file-description offset unchanged");
    }
  }
}

void test_session_read_authority_binding_and_descriptor_lifetime()
{
  auto const root = temp_root() / "session-read-authority";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const sessions = root / "sessions";
  std::filesystem::create_directories(workspace);

  auto entry = [](std::string id, std::string text) {
    return ava::session::SessionEntry{.id = std::move(id),
                                      .parent_id = "",
                                      .type = ava::session::EntryType::UserMessage,
                                      .timestamp = ava::session::now_timestamp(),
                                      .data_json = "{\"text\":\"" + text + "\"}"};
  };

  ava::session::SessionStore store(ava::session::SessionStoreOptions{.root_dir = sessions, .workspace_dir = workspace, .session_id = "persistent-authority"});
  auto lease = ava::session::SessionLease::create_and_acquire(store.session_path());
  expect(lease.has_value(), "session read authority fixture acquires its persistent lease");
  if (!lease)
    return;
  expect(store.append(*lease, entry("original", "ORIGINAL_HISTORY")).has_value(), "session read authority fixture seeds original history");

  auto bound = ava::session::SessionReadAuthority::create_persistent(store, *lease);
  expect(bound.has_value(), "persistent read authority binds the exact store and lease identity");
  if (!bound)
    return;
  std::optional<ava::session::SessionReadAuthority> authority(std::move(*bound));
  std::optional<ava::session::SessionReadAuthority> retained_copy(*authority);
  *lease = ava::session::SessionLease{};
  auto blocked_while_copied = ava::session::SessionLease::acquire(store.session_path());
  expect(!blocked_while_copied, "a copied read authority retains the duplicated lease after caller lease release");

  auto loaded = authority->load();
  expect(loaded && loaded->size() == 1 && loaded->front().id == "original", "bound read authority loads the leased original history");

  auto const parked = store.session_path().string() + ".parked";
  std::filesystem::rename(store.session_path(), parked);
  auto replacement_line = ava::session::serialize_session_entry_line(entry("replacement", "REPLACEMENT_CANARY"));
  expect(replacement_line.has_value(), "replacement read-authority fixture serializes a valid session record");
  if (!replacement_line)
    return;
  write_binary_file(store.session_path(), *replacement_line + "\n");
  auto rejected = authority->load();
  auto pathname_loaded = store.load();
  expect(!rejected && pathname_loaded && pathname_loaded->size() == 1 && pathname_loaded->front().id == "replacement",
         "bound read authority rejects a replaced live pathname while observational pathname loading sees only the replacement");

  authority.reset();
  auto still_blocked = ava::session::SessionLease::acquire(parked);
  expect(!still_blocked, "one retained authority copy continues holding the original inode lease");
  retained_copy.reset();
  auto released = ava::session::SessionLease::acquire(parked);
  expect(released.has_value(), "destroying the last read authority copy releases its duplicated lease descriptor");

  ava::session::SessionStore wrong_store(ava::session::SessionStoreOptions{.root_dir = sessions, .workspace_dir = workspace, .session_id = "wrong-authority"});
  auto wrong_lease = ava::session::SessionLease::create_and_acquire(wrong_store.session_path());
  if (wrong_lease)
  {
    auto wrong_binding = ava::session::SessionReadAuthority::create_persistent(store, *wrong_lease);
    expect(!wrong_binding, "persistent read authority rejects a lease for a different exact store path");
  }

  ava::session::SessionStore allocation_store(
      ava::session::SessionStoreOptions{.root_dir = sessions, .workspace_dir = workspace, .session_id = "read-authority-allocation"});
  auto allocation_lease = ava::session::SessionLease::create_and_acquire(allocation_store.session_path());
  if (allocation_lease)
  {
    allocation_store.fail_persistent_read_authority_allocation_for_test();
    auto failed = ava::session::SessionReadAuthority::create_persistent(allocation_store, *allocation_lease);
    allocation_store.fail_persistent_read_authority_allocation_for_test(false);
    *allocation_lease = ava::session::SessionLease{};
    auto reacquired = ava::session::SessionLease::acquire(allocation_store.session_path());
    expect(!failed && reacquired,
           "read authority allocation failure closes its immediately adopted duplicate descriptor and releases flock with the caller lease");
  }

  auto ephemeral = ava::session::SessionStore::create_ephemeral(workspace);
  if (ephemeral)
  {
    auto ephemeral_authority = ava::session::SessionReadAuthority::create_ephemeral(*ephemeral);
    expect(ephemeral_authority.has_value(), "ephemeral read authority binds copied shared in-memory state");
    auto appended = ephemeral->append_ephemeral(entry("ephemeral", "shared"));
    auto ephemeral_entries = ephemeral_authority ? ephemeral_authority->load() : ava::core::Result<std::vector<ava::session::SessionEntry>>{};
    expect(appended && ephemeral_entries && ephemeral_entries->size() == 1 && ephemeral_entries->front().id == "ephemeral",
           "ephemeral read authority observes later appends through shared in-memory state");
  }

#if defined(__linux__)
  ava::session::SessionStore throwing_store(ava::session::SessionStoreOptions{.root_dir = sessions, .workspace_dir = workspace, .session_id = "throwing-read"});
  auto throwing_lease = ava::session::SessionLease::create_and_acquire(throwing_store.session_path());
  if (throwing_lease && throwing_store.append(*throwing_lease, entry("throwing", "visitor")).has_value())
  {
    auto count_fds = [] {
      return static_cast<std::size_t>(std::distance(std::filesystem::directory_iterator("/proc/self/fd"), std::filesystem::directory_iterator{}));
    };
    throwing_store.set_after_lease_bound_read_for_test([] { throw std::bad_alloc(); });
    auto const before_visitor = count_fds();
    bool visitor_threw = false;
    try
    {
      auto loaded = throwing_store.load_bounded(*throwing_lease, ava::session::legacy_unbounded_session_read_limits());
      visitor_threw = !loaded;
    }
    catch (std::bad_alloc const&)
    {
      visitor_threw = true;
    }
    auto const after_visitor = count_fds();
    throwing_store.set_after_lease_bound_read_for_test({});

    auto const before_cancel = count_fds();
    bool cancel_threw = false;
    try
    {
      static_cast<void>(throwing_store.load_bounded(*throwing_lease, ava::session::legacy_unbounded_session_read_limits(),
                                                    []() -> bool { throw std::runtime_error("cancel callback failure"); }));
    }
    catch (std::runtime_error const&)
    {
      cancel_threw = true;
    }
    auto const after_cancel = count_fds();
    expect(visitor_threw, "lease-bound read test hook reports allocation exceptions");
    expect(before_visitor == after_visitor, "lease-bound read RAII closes owned descriptors after an allocation exception");
    expect(cancel_threw, "lease-bound read propagates cancellation callback exceptions");
    expect(before_cancel == after_cancel, "lease-bound read RAII closes owned descriptors after a cancellation callback exception");
  }
#endif
}

void test_session_read_authority_retains_runtime_policy()
{
  auto const root = temp_root() / "session-read-authority-policy";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const sessions = root / "sessions";
  std::filesystem::create_directories(workspace);

  auto entry = [](std::string id) {
    return ava::session::SessionEntry{.id = std::move(id),
                                      .parent_id = "",
                                      .type = ava::session::EntryType::UserMessage,
                                      .timestamp = ava::session::now_timestamp(),
                                      .data_json = "{\"text\":\"policy\"}"};
  };
  auto const limits = ava::session::SessionReadLimits{.max_file_bytes = 4096, .max_line_bytes = 2048, .max_entries = 1};

  auto store = ava::session::SessionStore::create(workspace, sessions);
  auto lease = store ? ava::session::SessionLease::create_and_acquire(store->session_path())
                     : ava::core::Result<ava::session::SessionLease>(std::unexpected(store.error()));
  if (!store || !lease)
    return;

  auto policy_authority = ava::session::SessionReadAuthority::create_persistent(*store, *lease, limits);
  auto target = ava::session::SessionAppendTarget::create_persistent(*store, *lease, limits);
  auto first = store->append(*lease, entry("first"));
  auto first_load =
      policy_authority ? policy_authority->load() : ava::core::Result<std::vector<ava::session::SessionEntry>>(std::unexpected(policy_authority.error()));
  auto second = first ? store->append(*lease, entry("second")) : ava::core::VoidResult(std::unexpected(first.error()));
  auto provider_iteration_load =
      second && policy_authority ? policy_authority->load() : ava::core::Result<std::vector<ava::session::SessionEntry>>(std::unexpected(second.error()));
  auto legacy_authority = ava::session::SessionReadAuthority::create_persistent(*store, *lease);
  auto legacy_load =
      legacy_authority ? legacy_authority->load() : ava::core::Result<std::vector<ava::session::SessionEntry>>(std::unexpected(legacy_authority.error()));
  auto target_authority = target ? (*target)->read_authority() : ava::core::Result<ava::session::SessionReadAuthority>(std::unexpected(target.error()));
  auto target_load =
      target_authority ? target_authority->load() : ava::core::Result<std::vector<ava::session::SessionEntry>>(std::unexpected(target_authority.error()));
  auto target_recovery = target ? (*target)->recover() : ava::core::VoidResult(std::unexpected(target.error()));

  expect(policy_authority && target && first && first_load && first_load->size() == 1 && second && !provider_iteration_load && legacy_authority &&
             legacy_load && legacy_load->size() == 2 && target_authority && !target_load && !target_recovery,
         "policy-bound runtime authorities reject history growth between provider iterations while legacy authorities remain compatible and targets retain the "
         "exact policy");
}

void test_session_append_authority_and_commit_state()
{
  auto const root = temp_root() / "session-append-authority";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const sessions = root / "sessions";
  std::filesystem::create_directories(workspace);

  auto entry = [](std::string id) {
    return ava::session::SessionEntry{.id = std::move(id),
                                      .parent_id = "",
                                      .type = ava::session::EntryType::UserMessage,
                                      .timestamp = ava::session::now_timestamp(),
                                      .data_json = "{\"text\":\"authority\"}"};
  };
  auto has_one_state = [](ava::core::VoidResult const& result, std::string_view expected) {
    return !result && error_context_count(result.error(), "append_commit_state") == 1 && error_context_value(result.error(), "append_commit_state") == expected;
  };

  ava::session::SessionStore store(ava::session::SessionStoreOptions{.root_dir = sessions, .workspace_dir = workspace, .session_id = "persistent"});
  auto wrong_mode = store.append_ephemeral(entry("wrong-mode"));
  auto no_lease = store.append(ava::session::SessionLease{}, entry("no-lease"));
  expect(!wrong_mode && error_context_count(wrong_mode.error(), "append_commit_state") == 0,
         "ephemeral append API failures remain free of persistent disk commit state");
  expect(has_one_state(no_lease, "not_started"), "persistent stores reject missing append authority with exactly one not-started state");

  auto lease = ava::session::SessionLease::create_and_acquire(store.session_path());
  expect(lease.has_value(), "append authority fixture creates its exact lease");
  if (!lease)
    return;
  auto invalid_data = entry("invalid-data");
  invalid_data.data_json.clear();
  auto invalid_data_result = store.append(*lease, invalid_data);
  auto invalid_parent = entry("invalid-parent");
  invalid_parent.parent_id = ".";
  auto invalid_parent_result = store.append(*lease, invalid_parent);
  auto oversized = entry("oversized");
  oversized.data_json = "{\"text\":\"" + std::string(ava::session::kMaxSessionLineBytes, 'x') + "\"}";
  auto oversized_result = store.append(*lease, oversized);
  ava::session::SessionStore invalid_session(ava::session::SessionStoreOptions{.root_dir = sessions, .workspace_dir = workspace, .session_id = "../invalid"});
  auto invalid_session_result = invalid_session.append(*lease, entry("invalid-session"));
  expect(has_one_state(invalid_data_result, "not_started") && has_one_state(invalid_parent_result, "not_started") &&
             has_one_state(oversized_result, "not_started") && has_one_state(invalid_session_result, "not_started"),
         "persistent invalid data, parent, serialization, and session failures each expose exactly one not-started commit state");

  auto first = store.append(*lease, entry("first"));
  auto second = store.append(*lease, entry("second"));
  auto loaded = store.load();
  expect(first && second && loaded && loaded->size() == 2, "one matching active lease authorizes multiple persistent appends");

  ava::session::SessionStore other(ava::session::SessionStoreOptions{.root_dir = sessions, .workspace_dir = workspace, .session_id = "other"});
  auto other_lease = ava::session::SessionLease::create_and_acquire(other.session_path());
  auto wrong = other_lease ? store.append(*other_lease, entry("wrong-lease")) : ava::core::VoidResult(std::unexpected(std::move(other_lease.error())));
  expect(has_one_state(wrong, "not_started") && store.load() && store.load()->size() == 2,
         "persistent append rejects an active lease for another exact path with exactly one not-started state");

  auto target = ava::session::SessionAppendTarget::create_persistent(store, *lease);
  expect(target.has_value(), "persistent append target duplicates a matching lease");
  if (target)
  {
    lease = ava::session::SessionLease{};
    auto through_target = (*target)->append(entry("target-owned"));
    expect(through_target && store.load() && store.load()->size() == 3,
           "persistent append target retains duplicated same-description lease authority after caller release");
  }

  ava::session::SessionStore moved_store(ava::session::SessionStoreOptions{.root_dir = sessions, .workspace_dir = workspace, .session_id = "moved"});
  auto movable = ava::session::SessionLease::create_and_acquire(moved_store.session_path());
  if (movable)
  {
    auto moved_to = std::move(*movable);
    auto moved_from_append = moved_store.append(*movable, entry("moved-from"));
    expect(has_one_state(moved_from_append, "not_started") && !movable->active() && moved_to.active(),
           "a moved-from lease cannot authorize a persistent append and reports one not-started state");
  }

  auto ephemeral = ava::session::SessionStore::create_ephemeral(workspace);
  if (ephemeral)
  {
    auto ephemeral_target = ava::session::SessionAppendTarget::create_ephemeral(*ephemeral);
    auto direct = ephemeral->append_ephemeral(entry("ephemeral"));
    auto persistent_target = ava::session::SessionAppendTarget::create_persistent(*ephemeral, ava::session::SessionLease{});
    auto ephemeral_with_lease = ephemeral->append(ava::session::SessionLease{}, entry("ephemeral-with-lease"));
    auto invalid_ephemeral = entry("invalid-ephemeral");
    invalid_ephemeral.data_json.clear();
    auto invalid_ephemeral_result = ephemeral->append_ephemeral(invalid_ephemeral);
    expect(direct && ephemeral_target && (*ephemeral_target)->append(entry("ephemeral-target")) && !persistent_target &&
               has_one_state(ephemeral_with_lease, "not_started") && !invalid_ephemeral_result &&
               error_context_count(invalid_ephemeral_result.error(), "append_commit_state") == 0,
           "ephemeral stores and targets accept only explicit in-memory authority without leaking disk commit state");
  }

  {
    ava::session::SessionStore allocation_store(
        ava::session::SessionStoreOptions{.root_dir = sessions, .workspace_dir = workspace, .session_id = "target-allocation"});
    auto allocation_lease = ava::session::SessionLease::create_and_acquire(allocation_store.session_path());
    if (allocation_lease)
    {
      allocation_store.fail_persistent_append_target_allocation_for_test();
      auto failed_target = ava::session::SessionAppendTarget::create_persistent(allocation_store, *allocation_lease);
      allocation_store.fail_persistent_append_target_allocation_for_test(false);
      *allocation_lease = ava::session::SessionLease{};
      auto reacquired = ava::session::SessionLease::acquire(allocation_store.session_path());
      expect(!failed_target && reacquired,
             "persistent append target allocation failure closes its immediately adopted duplicate descriptor and releases flock with the caller lease");
    }
  }

  ava::session::SessionStore partial_store(ava::session::SessionStoreOptions{.root_dir = sessions, .workspace_dir = workspace, .session_id = "partial"});
  auto partial_lease = ava::session::SessionLease::create_and_acquire(partial_store.session_path());
  if (partial_lease)
  {
    expect(partial_store.append(*partial_lease, entry("prefix")).has_value(), "partial-write fixture seeds framed record");
    int calls = 0;
    partial_store.set_append_write_for_test([&calls](int, std::string_view bytes) -> ssize_t {
      if (++calls == 1)
        return static_cast<ssize_t>(std::min<std::size_t>(1, bytes.size()));
      errno = EIO;
      return -1;
    });
    auto partial = partial_store.append(*partial_lease, entry("partial"));
    partial_store.set_append_write_for_test({});
    expect(has_one_state(partial, "partial_or_unknown") && partial.error().format().find("recover_torn_tail") != std::string::npos,
           "partial append failures carry stable recovery-required commit state");
    auto recovered = partial_store.recover_torn_tail(*partial_lease, ava::session::legacy_unbounded_session_read_limits());
    auto recovered_append =
        recovered ? partial_store.append(*partial_lease, entry("after-recovery")) : ava::core::VoidResult(std::unexpected(std::move(recovered.error())));
    expect(recovered && recovered_append, "the same retained lease repairs a partial append tail before a later append");

    partial_store.set_after_append_write_for_test([] { throw 1; });
    auto committed = partial_store.append(*partial_lease, entry("committed"));
    partial_store.set_after_append_write_for_test({});
    expect(has_one_state(committed, "committed_to_leased_inode") && read_binary_file(partial_store.session_path()).find("committed") != std::string::npos,
           "post-write failures report committed-to-leased-inode state without retrying");
  }

  ava::session::SessionStore parent_swap_store(
      ava::session::SessionStoreOptions{.root_dir = sessions, .workspace_dir = workspace, .session_id = "parent-swap"});
  auto parent_swap_lease = ava::session::SessionLease::create_and_acquire(parent_swap_store.session_path());
  if (parent_swap_lease)
  {
    expect(parent_swap_store.append(*parent_swap_lease, entry("parent-prefix")).has_value(), "parent-swap fixture seeds its leased inode");
    auto const original_bytes = read_binary_file(parent_swap_store.session_path());
    auto const parent = parent_swap_store.session_path().parent_path();
    auto const moved_parent = parent.string() + ".moved";
    parent_swap_store.set_before_append_identity_check_for_test([&] {
      std::filesystem::rename(parent, moved_parent);
      std::filesystem::create_directories(parent);
      write_binary_file(parent_swap_store.session_path(), "replacement\\n");
    });
    auto parent_swap = parent_swap_store.append(*parent_swap_lease, entry("parent-race"));
    parent_swap_store.set_before_append_identity_check_for_test({});
    expect(has_one_state(parent_swap, "not_started") && read_binary_file(moved_parent / parent_swap_store.session_path().filename()) == original_bytes &&
               read_binary_file(parent_swap_store.session_path()) == "replacement\\n",
           "persistent append rejects a parent-directory replacement before mutating either original or replacement name");
  }

  ava::session::SessionStore fifo_store(ava::session::SessionStoreOptions{.root_dir = sessions, .workspace_dir = workspace, .session_id = "fifo"});
  auto fifo_lease = ava::session::SessionLease::create_and_acquire(fifo_store.session_path());
  if (fifo_lease)
  {
    auto const parked = fifo_store.session_path().string() + ".parked";
    std::filesystem::rename(fifo_store.session_path(), parked);
    int const fifo_result = ::mkfifo(fifo_store.session_path().c_str(), 0600);
    auto fifo_append = fifo_result == 0 ? fifo_store.append(*fifo_lease, entry("fifo"))
                                        : ava::core::VoidResult(std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "mkfifo failed")));
    expect(!fifo_append, "persistent append rejects a FIFO replacement without blocking or recreating it");
  }
}

void test_logical_session_projection_v4_public_privacy_and_compatibility()
{
  using ava::session::AssistantOutputFunctionCall;
  using ava::session::AssistantOutputItem;
  using ava::session::AssistantOutputItemKind;
  using ava::session::AssistantOutputReasoning;
  using ava::session::AssistantOutputText;
  using ava::session::AssistantOutputTextPhase;
  using ava::session::AssistantTurnCommit;
  using ava::session::EntryType;
  using ava::session::SessionEntry;

  auto item = [](std::string id, std::size_t sequence, AssistantOutputItemKind kind, ava::session::AssistantOutputItemPayload payload, std::string timestamp) {
    auto data =
        ava::session::serialize_assistant_output_item_data_json(AssistantOutputItem{.assistant_turn_id = "turn_projection",
                                                                                    .sequence = sequence,
                                                                                    .kind = kind,
                                                                                    .provider_item_id = "PRIVATE_PROVIDER_ITEM_ID_" + std::to_string(sequence),
                                                                                    .provider_output_index = sequence,
                                                                                    .payload = std::move(payload)});
    return SessionEntry{
        .id = std::move(id), .parent_id = "", .type = EntryType::AssistantOutputItem, .timestamp = std::move(timestamp), .data_json = data.value_or("{}")};
  };
  auto commit_data = ava::session::serialize_assistant_turn_commit_data_json(
      AssistantTurnCommit{.assistant_turn_id = "turn_projection",
                          .item_count = 4,
                          .provider = "openai",
                          .model = "gpt-5.5",
                          .finish_reason = "tool_calls",
                          .usage_json = "{\"input_tokens\":7,\"output_tokens\":5,\"total_tokens\":12,\"source\":\"provider\"}"});
  std::string const sha256 = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
  std::vector<SessionEntry> const entries = {
      SessionEntry{
          .id = "legacy_user",
          .parent_id = "",
          .type = EntryType::UserMessage,
          .timestamp = "2026-07-18T00:00:00Z",
          .data_json = "{\"text\":\"legacy "
                       "user\",\"attachments\":[{\"id\":\"image_projection\",\"type\":\"image\",\"mime_type\":\"image/png\",\"byte_size\":12,\"sha256\":\"" +
                       sha256 + "\",\"storage_path\":\"attachments/private-source.png\"}]}",
          .version = 0},
      item("out_commentary", 0, AssistantOutputItemKind::Text,
           AssistantOutputText{.text = "commentary ", .assistant_phase = AssistantOutputTextPhase::Commentary}, "2026-07-18T00:00:01Z"),
      item("out_reasoning", 1, AssistantOutputItemKind::Reasoning,
           AssistantOutputReasoning{.text = "visible reasoning",
                                    .format = "openai_responses",
                                    .redacted = false,
                                    .signature = "PRIVATE_SIGNATURE_PROJECTION",
                                    .redacted_data = "PRIVATE_REDACTED_PROJECTION",
                                    .native_item_json = "{\"id\":\"PRIVATE_NATIVE_REASONING_ID\",\"type\":\"reasoning\",\"summary\":[]}"},
           "2026-07-18T00:00:02Z"),
      item("out_function", 2, AssistantOutputItemKind::FunctionCall,
           AssistantOutputFunctionCall{.call_id = "call_projection", .name = "read_file", .arguments_json = "{\"path\":\"README.md\"}"},
           "2026-07-18T00:00:03Z"),
      item("out_final", 3, AssistantOutputItemKind::Text, AssistantOutputText{.text = "final", .assistant_phase = AssistantOutputTextPhase::FinalAnswer},
           "2026-07-18T00:00:04Z"),
      SessionEntry{.id = "commit_projection",
                   .parent_id = "out_final",
                   .type = EntryType::AssistantTurnCommit,
                   .timestamp = "2026-07-18T00:00:05Z",
                   .data_json = commit_data.value_or("{}")},
      SessionEntry{.id = "result_projection",
                   .parent_id = "commit_projection",
                   .type = EntryType::ToolResult,
                   .timestamp = "2026-07-18T00:00:06Z",
                   .data_json =
                       "{\"assistant_output_entry_id\":\"out_function\",\"call_id\":\"call_projection\",\"name\":\"read_file\",\"success\":true,\"status\":"
                       "\"success\",\"result\":\"read "
                       "ok\",\"structured_result\":{\"schema_version\":1,\"call_id\":\"call_projection\",\"tool\":\"read_file\",\"status\":\"success\",\"ok\":"
                       "true,\"content_type\":\"text/plain\",\"content\":\"read ok\"},\"provider_private\":\"PRIVATE_RESULT_CANARY\"}"},
      SessionEntry{.id = "tail_projection",
                   .parent_id = "result_projection",
                   .type = EntryType::AssistantOutputItem,
                   .timestamp = "2026-07-18T00:00:07Z",
                   .data_json = "{\"schema_version\":1,\"assistant_turn_id\":\"tail_projection\",\"sequence\":0,\"kind\":\"text\",\"text\":\"INCOMPLETE_"
                                "STAGING_CANARY\",\"assistant_phase\":\"commentary\"}"},
  };

  auto projected = ava::session::project_logical_session_history(entries);
  auto ordered = ava::session::project_ordered_public_session_history(entries);
  auto jsonl = ava::session::format_session_portable_jsonl_checked(entries);
  auto markdown = ava::session::format_session_markdown_checked(entries, ava::session::ExportOptions{.include_tool_details = true});
  auto html = ava::session::format_session_html_checked(entries, ava::session::ExportOptions{.include_tool_details = true});
  auto transcript = ava::session::project_transcript(entries);
  auto estimated = ava::session::estimate_session_tokens(entries);
  ava::session::CompactionConfig threshold_config = ava::session::default_compaction_config();
  threshold_config.auto_threshold_tokens = estimated.value_or(0);
  auto threshold_decision = ava::session::should_auto_compact(entries, threshold_config);
  auto compaction_prompt = ava::app::build_compaction_summary_prompt(entries, threshold_config, "", 42);

  bool parsed_jsonl = jsonl.has_value();
  std::vector<SessionEntry> imported;
  if (jsonl)
  {
    std::istringstream input(*jsonl);
    std::string line;
    while (std::getline(input, line))
    {
      auto parsed = ava::session::parse_session_entry_line(line, "portable-v4-projection.jsonl");
      if (!parsed)
      {
        parsed_jsonl = false;
        break;
      }
      imported.push_back(std::move(*parsed));
    }
  }
  auto const replay = ava::session::validate_session_replay(imported);
  auto imported_messages = ava::agent::build_provider_messages_from_entries(imported);

  bool compatibility_shape =
      projected && projected->size() == 5 && (*projected)[0].type == EntryType::UserMessage && (*projected)[1].type == EntryType::ReasoningBlock &&
      (*projected)[1].id == "out_reasoning" && (*projected)[1].timestamp == "2026-07-18T00:00:02Z" && (*projected)[2].type == EntryType::AssistantMessage &&
      (*projected)[2].id == "commit_projection" && (*projected)[2].timestamp == "2026-07-18T00:00:05Z" &&
      (*projected)[2].data_json.find("commentary final") != std::string::npos && (*projected)[2].data_json.find("\"tool_calls\":1") != std::string::npos &&
      (*projected)[2].data_json.find("\"input_tokens\":7") != std::string::npos &&
      (*projected)[2].data_json.find("\"ordered_output\":[{") != std::string::npos &&
      (*projected)[2].data_json.find("\"assistant_phase\":\"commentary\"") != std::string::npos &&
      (*projected)[2].data_json.find("\"assistant_phase\":\"final_answer\"") != std::string::npos && (*projected)[3].type == EntryType::ToolCall &&
      (*projected)[3].id == "out_function" && (*projected)[3].timestamp == "2026-07-18T00:00:03Z" && (*projected)[4].type == EntryType::ToolResult &&
      (*projected)[4].data_json.find("assistant_output_entry_id") == std::string::npos &&
      (*projected)[4].data_json.find("PRIVATE_RESULT_CANARY") == std::string::npos;
  bool ordered_shape = ordered && ordered->size() == 6 && (*ordered)[0].type == EntryType::UserMessage && (*ordered)[1].type == EntryType::AssistantMessage &&
                       (*ordered)[1].id == "out_commentary" && (*ordered)[1].data_json.find("\"assistant_phase\":\"commentary\"") != std::string::npos &&
                       (*ordered)[2].type == EntryType::ReasoningBlock && (*ordered)[2].id == "out_reasoning" && (*ordered)[3].type == EntryType::ToolCall &&
                       (*ordered)[3].id == "out_function" && (*ordered)[4].type == EntryType::AssistantMessage && (*ordered)[4].id == "out_final" &&
                       (*ordered)[4].data_json.find("\"assistant_phase\":\"final_answer\"") != std::string::npos && (*ordered)[5].type == EntryType::ToolResult;
  bool portable_shape = imported.size() == 7 && imported[0].type == EntryType::UserMessage && imported[0].version == 0 &&
                        imported[1].type == EntryType::AssistantOutputItem && imported[1].data_json.find("provider_item_id") == std::string::npos &&
                        imported[2].type == EntryType::AssistantOutputItem && imported[3].type == EntryType::AssistantOutputItem &&
                        imported[4].type == EntryType::AssistantOutputItem && imported[5].type == EntryType::AssistantTurnCommit &&
                        imported[6].type == EntryType::ToolResult &&
                        imported[6].data_json.find("\"assistant_output_entry_id\":\"out_function\"") != std::string::npos;
  ava::provider::OpenAIProvider const portable_openai_provider("https://api.example.test");
  ava::provider::AnthropicProvider const portable_anthropic_provider("https://anthropic.example.test");
  std::optional<ava::provider::HttpRequest> portable_openai_request;
  std::optional<ava::provider::HttpRequest> portable_anthropic_request;
  if (imported_messages)
  {
    auto openai_request = portable_openai_provider.build_request(
        ava::provider::ProviderRequest{
            .provider_id = "openai", .model_id = "gpt-5.5", .system_prompt = "system", .messages = *imported_messages, .tools_json = {}, .stream = false},
        "test-key");
    auto anthropic_request = portable_anthropic_provider.build_request(ava::provider::ProviderRequest{.provider_id = "anthropic",
                                                                                                      .model_id = "claude-sonnet-4-5",
                                                                                                      .system_prompt = "system",
                                                                                                      .messages = *imported_messages,
                                                                                                      .tools_json = {},
                                                                                                      .stream = false},
                                                                       "test-key");
    if (openai_request)
      portable_openai_request = std::move(*openai_request);
    if (anthropic_request)
      portable_anthropic_request = std::move(*anthropic_request);
  }
  bool provider_replay_order =
      imported_messages && imported_messages->size() == 3 && (*imported_messages)[1].role == "assistant" && (*imported_messages)[1].content_parts.size() == 4 &&
      (*imported_messages)[1].content_parts[0].type == ava::provider::ContentPartType::Text && (*imported_messages)[1].content_parts[0].text == "commentary " &&
      (*imported_messages)[1].content_parts[1].type == ava::provider::ContentPartType::Text &&
      (*imported_messages)[1].content_parts[1].text == "visible reasoning" &&
      (*imported_messages)[1].content_parts[2].type == ava::provider::ContentPartType::ToolUse &&
      (*imported_messages)[1].content_parts[2].tool_call_id == "call_projection" &&
      (*imported_messages)[1].content_parts[3].type == ava::provider::ContentPartType::Text && (*imported_messages)[1].content_parts[3].text == "final" &&
      (*imported_messages)[2].role == "user";
  auto markdown_commentary = markdown ? markdown->find("commentary ") : std::string::npos;
  auto markdown_reasoning = markdown ? markdown->find("visible reasoning") : std::string::npos;
  auto markdown_function = markdown ? markdown->find("read_file") : std::string::npos;
  auto markdown_final = markdown ? markdown->find("final") : std::string::npos;
  auto html_commentary = html ? html->find("commentary ") : std::string::npos;
  auto html_reasoning = html ? html->find("visible reasoning") : std::string::npos;
  auto html_function = html ? html->find("read_file") : std::string::npos;
  auto html_final = html ? html->find("final") : std::string::npos;
  bool private_values_absent =
      jsonl && markdown && html && compaction_prompt && jsonl->starts_with("{\"id\":\"legacy_user\"") &&
      jsonl->find("PRIVATE_PROVIDER_ITEM_ID") == std::string::npos && jsonl->find("PRIVATE_SIGNATURE_PROJECTION") == std::string::npos &&
      jsonl->find("PRIVATE_REDACTED_PROJECTION") == std::string::npos && jsonl->find("PRIVATE_NATIVE_REASONING_ID") == std::string::npos &&
      jsonl->find("INCOMPLETE_STAGING_CANARY") == std::string::npos && jsonl->find("attachments/private-source.png") == std::string::npos &&
      jsonl->find("attachments/portable-redacted") != std::string::npos && markdown->find("PRIVATE_SIGNATURE_PROJECTION") == std::string::npos &&
      html->find("PRIVATE_REDACTED_PROJECTION") == std::string::npos && compaction_prompt->find("PRIVATE_SIGNATURE_PROJECTION") == std::string::npos &&
      compaction_prompt->find("PRIVATE_NATIVE_REASONING_ID") == std::string::npos;
  bool rendered_order = markdown_commentary < markdown_reasoning && markdown_reasoning < markdown_function && markdown_function < markdown_final &&
                        html_commentary < html_reasoning && html_reasoning < html_function && html_function < html_final &&
                        compaction_prompt->find("commentary ") < compaction_prompt->find("visible reasoning") &&
                        compaction_prompt->find("visible reasoning") < compaction_prompt->find("read_file") &&
                        compaction_prompt->find("read_file") < compaction_prompt->find("final");
  expect(compatibility_shape && ordered_shape && portable_shape && parsed_jsonl && replay.ok() && provider_replay_order && private_values_absent &&
             portable_openai_request && portable_anthropic_request && portable_openai_request->body.find("PRIVATE_SIGNATURE_PROJECTION") == std::string::npos &&
             portable_openai_request->body.find("PRIVATE_NATIVE_REASONING_ID") == std::string::npos &&
             portable_anthropic_request->body.find("PRIVATE_REDACTED_PROJECTION") == std::string::npos &&
             portable_anthropic_request->body.find("redacted_thinking") == std::string::npos && rendered_order && transcript && transcript->size() == 3 &&
             (*transcript)[1].role == ava::session::TranscriptRole::Assistant && (*transcript)[1].text == "commentary " &&
             (*transcript)[2].role == ava::session::TranscriptRole::Assistant && (*transcript)[2].text == "final" && estimated && *estimated > 0 &&
             threshold_decision && threshold_decision->should_compact,
         "v4 compatibility, ordered public, and portable projections preserve order while excluding provider-private data");

  auto malformed = entries;
  malformed[5].data_json =
      "{\"schema_version\":1,\"assistant_turn_id\":\"turn_projection\",\"item_count\":3,\"provider\":\"openai\",\"model\":\"gpt-5.5\",\"finish_reason\":\"tool_"
      "calls\"}";
  auto malformed_projection = ava::session::project_logical_session_history(malformed);
  std::vector<SessionEntry> const legacy_v3 = {
      SessionEntry{.id = "legacy_v3_user",
                   .parent_id = "",
                   .type = EntryType::UserMessage,
                   .timestamp = "2026-07-18T01:00:00Z",
                   .data_json = "{\"text\":\"v3\"}",
                   .version = 3},
      SessionEntry{.id = "legacy_v3_assistant",
                   .parent_id = "",
                   .type = EntryType::AssistantMessage,
                   .timestamp = "2026-07-18T01:00:01Z",
                   .data_json = "{\"text\":\"unchanged\"}",
                   .version = 3},
  };
  auto legacy_projection = ava::session::project_logical_session_history(legacy_v3);
  expect(!malformed_projection && legacy_projection && legacy_projection->size() == legacy_v3.size() && (*legacy_projection)[0].id == legacy_v3[0].id &&
             (*legacy_projection)[0].version == 3 && (*legacy_projection)[0].data_json == legacy_v3[0].data_json &&
             (*legacy_projection)[1].id == legacy_v3[1].id && (*legacy_projection)[1].version == 3 &&
             (*legacy_projection)[1].data_json == legacy_v3[1].data_json,
         "logical projection rejects malformed v4 transactions while preserving v3 records unchanged");
}

void test_provider_base64_encoding()
{
  expect(ava::provider::base64_encode("") == "", "base64 encoder handles empty input");
  expect(ava::provider::base64_encode(std::string_view("\0", 1)) == "AA==", "base64 encoder handles one-byte input padding");
  expect(ava::provider::base64_encode("a") == "YQ==", "base64 encoder handles text one-byte input");
  expect(ava::provider::base64_encode("ab") == "YWI=", "base64 encoder handles two-byte input padding");
  expect(ava::provider::base64_encode("abc") == "YWJj", "base64 encoder handles full triples");
  expect(ava::provider::base64_encode("hello") == "aGVsbG8=", "base64 encoder handles multi-block input");
}

}  // namespace

void run_session_tests()
{
  test_session_store_round_trip();
  test_bounded_session_reads_strictly_classify_framed_records();
  test_ephemeral_session_store_stays_in_memory();
  test_session_record_round_trip();
  test_session_tree_metadata_entries_validate_and_export();
  test_session_tree_index_derives_branches();
  test_session_tree_index_handles_parent_cycles();
  test_session_branch_fork_and_clone_copy_source_safely();
  test_session_branch_summary_appends_to_source_session();
  test_session_stats_helper();
  test_session_stats_saturates_large_usage_and_costs();
  test_session_stats_omits_incomplete_cost_total();
  test_session_stats_flags_legacy_assistant_tokens_without_cost();
  test_assistant_output_v4_session_schema_and_replay();
  test_session_stats_projects_mixed_v3_v4_history();
  test_session_replay_validation();
  test_session_lease_creation_and_link_safety();
  test_session_torn_tail_recovery();
  test_session_torn_tail_listing();
  test_session_resume_and_listing();
  test_session_compaction_entry_round_trip();
  test_session_markdown_export();
  test_session_portable_jsonl_sanitizer();
  test_compaction_config_and_thresholds();
  test_compaction_context_reconstruction();
  test_tool_content_parts_reconstruction();
  test_portable_omitted_reasoning_reconstructs_as_safe_text();
  test_image_attachment_message_reconstruction_and_validation();
  test_image_attachment_storage_boundary();
  test_image_attachment_import();
  test_created_session_rollback_is_identity_safe_and_preserves_attachments();
  test_lease_bound_session_reads_hold_exact_authority();
  test_session_read_authority_binding_and_descriptor_lifetime();
  test_session_read_authority_retains_runtime_policy();
  test_assistant_output_append_target_state_and_batches();
  test_incomplete_assistant_output_suffix_recovery();
  test_session_append_authority_and_commit_state();
  test_logical_session_projection_v4_public_privacy_and_compatibility();
  test_provider_base64_encoding();
}
