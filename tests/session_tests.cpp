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
#include "ava/session/session_entry_codec.h"
#include "ava/session/session_store.h"
#include "ava/session/stats.h"
#include "ava/session/validation.h"
#include "ava/tools/bash_tool.h"
#include "ava/tools/file_tools.h"
#include "ava/tools/search_tools.h"
#include "ava/tui/composer.h"
#include "ava/tui/terminal.h"
#include "tests/support/fake_transport.h"
#include "tests/support/test_harness.h"

namespace {

bool has_replay_issue(ava::session::SessionReplayValidation const& validation,
                      ava::session::SessionReplayIssueKind kind)
{
  return std::ranges::any_of(validation.issues,
                             [kind](ava::session::SessionReplayIssue const& issue) { return issue.kind == kind; });
}

void test_session_entry_codec_helpers()
{
  auto line = ava::session::encode_session_entry_line(ava::session::SessionEntry{
      .id = "entry_\n",
      .parent_id = "parent",
      .type = ava::session::EntryType::UserMessage,
      .timestamp = "2026-04-27T00:00:00Z",
      .data_json = "{\"text\":\"hello\"}",
  });
  auto const current_version_json = "\"version\":" + std::to_string(ava::session::kCurrentSessionEntryVersion);
  expect(line && line->find(current_version_json) != std::string::npos,
         "session entry codec encodes current entry version");
  if (line) {
    auto decoded = ava::session::decode_session_entry_line(*line, "/tmp/session.jsonl");
    expect(decoded && decoded->id == "entry_\n" && decoded->parent_id == "parent" &&
               decoded->type == ava::session::EntryType::UserMessage && decoded->timestamp == "2026-04-27T00:00:00Z" &&
               decoded->data_json == "{\"text\":\"hello\"}" &&
               decoded->version == ava::session::kCurrentSessionEntryVersion,
           "session entry codec decodes encoded JSONL line");
  }

  std::istringstream crlf_stream("first\r\nsecond");
  std::string limited;
  auto first = ava::session::read_limited_session_line(crlf_stream, limited);
  expect(first && *first && limited == "first", "session entry codec trims CRLF while reading bounded lines");
  auto second = ava::session::read_limited_session_line(crlf_stream, limited);
  expect(second && *second && limited == "second", "session entry codec reads final unterminated line");
  auto eof = ava::session::read_limited_session_line(crlf_stream, limited);
  expect(eof && !*eof, "session entry codec reports clean EOF");

  auto invalid_session_id = ava::session::validate_session_id("../escape");
  expect(!invalid_session_id && invalid_session_id.error().message().find("invalid session id") != std::string::npos,
         "session entry codec rejects unsafe session ids");
  auto invalid_parent = ava::session::validate_parent_id("bad/parent", "entry");
  expect(!invalid_parent && invalid_parent.error().message().find("invalid session parent_id") != std::string::npos,
         "session entry codec rejects unsafe parent ids");

  auto bad_data = ava::session::encode_session_entry_line(ava::session::SessionEntry{
      .id = "entry_bad_data",
      .parent_id = "",
      .type = ava::session::EntryType::UserMessage,
      .timestamp = "2026-04-27T00:00:00Z",
      .data_json = "{\"text\":\"bad\nsplit\"}",
  });
  expect(!bad_data && bad_data.error().message().find("raw newlines") != std::string::npos,
         "session entry codec rejects raw newlines inside JSONL data objects");

  auto future = ava::session::decode_session_entry_line(
      "{\"version\":999,\"id\":\"entry_future\",\"parent_id\":\"\","
      "\"type\":\"user_message\",\"timestamp\":\"2026-04-27T00:00:00Z\","
      "\"data\":{\"text\":\"hello\"}}",
      "/tmp/session.jsonl");
  expect(!future && ava::session::is_unsupported_session_version_error(future.error()),
         "session entry codec exposes unsupported-version errors for session listing");
}

void test_session_store_round_trip()
{
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
  auto const session_dir = store->session_path().parent_path();
  if (stat(session_dir.c_str(), &session_dir_stat) == 0) {
    expect((session_dir_stat.st_mode & 0777) == 0700, "session directory is owner-only");
  }

  auto loaded = store->load();
  expect(loaded.has_value(), "session entries load");
  if (!loaded) return;

  expect(loaded->size() == 1, "one entry loaded");
  expect((*loaded)[0].id == "entry_1", "entry id round trips");
  expect((*loaded)[0].type == ava::session::EntryType::SessionStart, "entry type round trips");
  expect((*loaded)[0].version == ava::session::kCurrentSessionEntryVersion,
         "session entry version round trips from current JSONL records");

  auto text_store = ava::session::SessionStore::create(std::filesystem::current_path(), temp_root());
  expect(text_store.has_value(), "text session store creates");
  if (!text_store) return;
  auto const text_append = text_store->append(ava::session::SessionEntry{
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

  auto const raw_newline_append = text_store->append(ava::session::SessionEntry{
      .id = "entry_raw_newline",
      .parent_id = "",
      .type = ava::session::EntryType::UserMessage,
      .timestamp = "2026-04-27T00:00:00Z",
      .data_json = "{\"text\":\"bad\nsplit\"}",
  });
  expect(!raw_newline_append, "session data_json rejects raw newlines to preserve JSONL entries");

  auto const raw_carriage_return_append = text_store->append(ava::session::SessionEntry{
      .id = "entry_raw_carriage_return",
      .parent_id = "",
      .type = ava::session::EntryType::UserMessage,
      .timestamp = "2026-04-27T00:00:00Z",
      .data_json = "{\"text\":\"bad\rsplit\"}",
  });
  expect(!raw_carriage_return_append, "session data_json rejects raw carriage returns to preserve JSONL entries");

  auto large_store = ava::session::SessionStore::create(std::filesystem::current_path(), temp_root());
  expect(large_store.has_value(), "large session store creates");
  if (large_store) {
    auto const large_append = large_store->append(ava::session::SessionEntry{
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

  std::vector<std::string> const bad_session_ids = {"",
                                                    ".",
                                                    "..",
                                                    "/",
                                                    "\\",
                                                    "../escape",
                                                    "with/slash",
                                                    "with\\slash",
                                                    std::string("bad\0id", 6),
                                                    std::string("bad\x1Fid", 6)};
  for (auto const& bad_session_id : bad_session_ids) {
    ava::session::SessionStore bad_store(ava::session::SessionStoreOptions{
        .root_dir = temp_root(),
        .workspace_dir = std::filesystem::current_path(),
        .session_id = bad_session_id,
    });
    auto const bad_append = bad_store.append(ava::session::SessionEntry{
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
  auto const traversal_append = traversal_store.append(ava::session::SessionEntry{
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
  expect(missing_version_loaded && missing_version_loaded->size() == 1 &&
             (*missing_version_loaded)[0].id == "entry_legacy" && (*missing_version_loaded)[0].version == 0,
         "session loader treats missing entry version as legacy-compatible");

  ava::session::SessionStore future_version_store(ava::session::SessionStoreOptions{
      .root_dir = temp_root(),
      .workspace_dir = std::filesystem::current_path(),
      .session_id = "future-version",
  });
  std::filesystem::create_directories(future_version_store.session_path().parent_path());
  {
    std::ofstream file(future_version_store.session_path(), std::ios::binary | std::ios::trunc);
    file << "{\"version\":3,\"id\":\"entry_future\",\"parent_id\":\"\",\"type\":\"user_message\","
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

  auto invalid_parent_append =
      text_store->append(ava::session::SessionEntry{.id = "entry_invalid_parent",
                                                    .parent_id = "bad/parent",
                                                    .type = ava::session::EntryType::UserMessage,
                                                    .timestamp = "2026-04-27T00:00:00Z",
                                                    .data_json = "{\"text\":\"hello\"}"});
  expect(!invalid_parent_append, "session append rejects unsafe parent_id values");
}

void test_session_stats_helper()
{
  std::vector<ava::session::SessionEntry> const entries = {
      ava::session::SessionEntry{.id = "start_1",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::SessionStart,
                                 .timestamp = "2026-04-29T00:00:00Z",
                                 .data_json = "{}"},
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
      ava::session::SessionEntry{.id = "cancel_1",
                                 .parent_id = "compact_1",
                                 .type = ava::session::EntryType::Cancel,
                                 .timestamp = "2026-04-29T00:00:05Z",
                                 .data_json = "{}"},
      ava::session::SessionEntry{.id = "error_1",
                                 .parent_id = "cancel_1",
                                 .type = ava::session::EntryType::Error,
                                 .timestamp = "2026-04-29T00:00:06Z",
                                 .data_json = "{}"},
  };

  auto const stats = ava::session::compute_session_stats(entries);
  expect(stats.entry_count == entries.size() && stats.first_timestamp == "2026-04-29T00:00:00Z" &&
             stats.last_timestamp == "2026-04-29T00:00:06Z",
         "session stats helper reports entry count and timestamps");
  expect(stats.counts.session_start == 1 && stats.counts.user_message == 1 && stats.counts.assistant_message == 1 &&
             stats.counts.reasoning_block == 1 && stats.counts.reasoning_change == 1 && stats.counts.mode_change == 1 &&
             stats.counts.compaction == 1 && stats.counts.cancel == 1 && stats.counts.error == 1,
         "session stats helper reports current counts without durable internal replay user messages");
  expect(stats.input_tokens && *stats.input_tokens == 3 && stats.output_tokens && *stats.output_tokens == 2 &&
             stats.total_tokens && *stats.total_tokens == 5,
         "session stats helper aggregates exact token fields only from provider usage");
  expect(stats.reasoning_tokens && *stats.reasoning_tokens == 1 && stats.cache_read_tokens &&
             *stats.cache_read_tokens == 2 && !stats.cache_write_tokens,
         "session stats helper keeps estimated cache token fields out of exact totals");
  expect(stats.estimated_input_bytes && *stats.estimated_input_bytes == 10 && stats.estimated_output_bytes &&
             *stats.estimated_output_bytes == 20 && stats.estimated_total_bytes && *stats.estimated_total_bytes == 30,
         "session stats helper aggregates separate estimated byte totals");
  expect(stats.total_cost_usd && *stats.total_cost_usd > 0.0009L && *stats.total_cost_usd < 0.0011L,
         "session stats helper aggregates exact cost fields without estimated fallback cost");
  expect(stats.exact_usage_entries == 1 && stats.estimated_usage_entries == 1,
         "session stats helper counts exact and estimated usage entries");

  auto const empty_stats = ava::session::compute_session_stats({});
  expect(!empty_stats.input_tokens && !empty_stats.total_cost_usd,
         "session stats helper leaves token and cost totals absent when no entry JSON supplies them");
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
  expect(stats.exact_usage_entries == 2 && stats.estimated_usage_entries == 0,
         "session stats counts mixed exact usage entries");
  expect(stats.known_cost_usd && *stats.known_cost_usd > 0.0009L && *stats.known_cost_usd < 0.0011L,
         "session stats preserves the known portion of incomplete cost totals");
  expect(!stats.total_cost_usd && !stats.cost_complete && stats.unknown_cost_entries == 1,
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
  expect(stats.input_tokens && *stats.input_tokens == 7 && stats.output_tokens && *stats.output_tokens == 3 &&
             stats.total_tokens && *stats.total_tokens == 10,
         "session stats still aggregates legacy top-level assistant token totals");
  expect(stats.exact_usage_entries == 0 && stats.estimated_usage_entries == 0,
         "legacy top-level assistant token stats do not masquerade as usage objects");
  expect(!stats.total_cost_usd && !stats.cost_complete && stats.unknown_cost_entries == 1,
         "legacy top-level assistant tokens without cost make cost stats incomplete");
}

void test_session_replay_validation()
{
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

  auto const valid = ava::session::validate_session_replay(
      valid_entries, ava::session::SessionReplayValidationOptions{.require_structured_tool_results = true});
  expect(valid.ok() && valid.issues.empty(), "session replay validator accepts paired structured tool history");

  auto unsupported_version_entries = valid_entries;
  unsupported_version_entries[1].version = ava::session::kCurrentSessionEntryVersion + 1;
  auto const unsupported_version = ava::session::validate_session_replay(unsupported_version_entries);
  expect(!unsupported_version.ok() &&
             has_replay_issue(unsupported_version, ava::session::SessionReplayIssueKind::UnsupportedEntryVersion),
         "session replay validator flags unsupported in-memory entry versions");

  std::vector<ava::session::SessionEntry> const duplicate_entry_entries = {
      valid_entries[0],
      valid_entries[0],
  };
  auto const duplicate_entry = ava::session::validate_session_replay(duplicate_entry_entries);
  expect(!duplicate_entry.ok() &&
             has_replay_issue(duplicate_entry, ava::session::SessionReplayIssueKind::DuplicateEntryId),
         "session replay validator flags duplicate entry ids");

  std::vector<ava::session::SessionEntry> const unknown_parent_entries = {
      ava::session::SessionEntry{.id = "child",
                                 .parent_id = "missing_parent",
                                 .type = ava::session::EntryType::UserMessage,
                                 .timestamp = "2026-04-29T00:00:00Z",
                                 .data_json = "{\"text\":\"orphan\"}"},
  };
  auto const unknown_parent = ava::session::validate_session_replay(unknown_parent_entries);
  expect(
      !unknown_parent.ok() && has_replay_issue(unknown_parent, ava::session::SessionReplayIssueKind::UnknownParentId),
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
  expect(!result_without_call.ok() &&
             has_replay_issue(result_without_call, ava::session::SessionReplayIssueKind::ToolResultWithoutCall),
         "session replay validator flags tool results without earlier tool calls");

  auto mismatch_entries = valid_entries;
  mismatch_entries.back().data_json =
      "{\"call_id\":\"call_read\",\"name\":\"bash\",\"success\":true,\"result\":\"wrong tool\"}";
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
  auto const missing_structured = ava::session::validate_session_replay(
      missing_structured_entries,
      ava::session::SessionReplayValidationOptions{.require_structured_tool_results = true});
  expect(!missing_structured.ok() &&
             has_replay_issue(missing_structured, ava::session::SessionReplayIssueKind::MissingStructuredToolResult),
         "session replay validator can require structured tool result payloads");

  auto structured_mismatch_entries = valid_entries;
  structured_mismatch_entries.back().data_json =
      "{\"call_id\":\"call_read\",\"name\":\"read_file\",\"success\":true,\"status\":\"success\","
      "\"result\":\"note contents\",\"structured_result\":{\"schema_version\":1,"
      "\"call_id\":\"call_other\",\"tool\":\"read_file\",\"status\":\"success\",\"ok\":true,"
      "\"content_type\":\"text/plain\",\"content\":\"note contents\"}}";
  auto const structured_mismatch = ava::session::validate_session_replay(
      structured_mismatch_entries,
      ava::session::SessionReplayValidationOptions{.require_structured_tool_results = true});
  expect(!structured_mismatch.ok() &&
             has_replay_issue(structured_mismatch, ava::session::SessionReplayIssueKind::StructuredToolResultMismatch),
         "session replay validator flags structured result call/tool/status mismatches");

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
  };
  auto const valid_permission = ava::session::validate_session_replay(valid_permission_entries);
  expect(valid_permission.ok() && valid_permission.issues.empty(),
         "session replay validator accepts complete permission audit decisions");

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
  expect(valid_session_grant.ok() && valid_session_grant.issues.empty(),
         "session replay validator accepts permission outcomes resolved by session grants");

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
  expect(!invalid_permission.ok() &&
             has_replay_issue(invalid_permission, ava::session::SessionReplayIssueKind::InvalidPermissionDecision),
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
  expect(!invalid_risk.ok() &&
             has_replay_issue(invalid_risk, ava::session::SessionReplayIssueKind::InvalidPermissionDecision),
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
  expect(!resolution_without_ask.ok() &&
             has_replay_issue(resolution_without_ask,
                              ava::session::SessionReplayIssueKind::PermissionResolutionWithoutAsk),
         "session replay validator flags resolver outcomes without earlier ask prompts");

  auto mismatched_permission_id_entries = valid_permission_entries;
  mismatched_permission_id_entries.back().data_json =
      "{\"permission_request_id\":\"permreq_other\",\"operation\":\"edit\",\"mode\":\"build\","
      "\"tool_name\":\"write_file\",\"action\":\"ask\","
      "\"reason\":\"target is outside the workspace\","
      "\"risk\":\"high\",\"target_path\":\"/tmp/outside.txt\","
      "\"resolution\":\"deny\",\"resolution_source\":\"resolver\"}";
  auto const mismatched_permission_id = ava::session::validate_session_replay(mismatched_permission_id_entries);
  expect(!mismatched_permission_id.ok() &&
             has_replay_issue(mismatched_permission_id,
                              ava::session::SessionReplayIssueKind::PermissionResolutionWithoutAsk),
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
  expect(!unresolved_permission.ok() &&
             has_replay_issue(unresolved_permission, ava::session::SessionReplayIssueKind::UnresolvedPermissionPrompt),
         "session replay validator flags ask permission prompts without outcomes");

  auto valid_compaction_entries = valid_entries;
  valid_compaction_entries.push_back(ava::session::SessionEntry{
      .id = "compaction",
      .parent_id = "tool_result",
      .type = ava::session::EntryType::Compaction,
      .timestamp = "2026-04-29T00:00:05Z",
      .data_json = "{\"trigger\":\"manual\",\"status\":\"recorded\",\"summary_unavailable\":false,"
                   "\"summary\":\"read_file returned note contents\","
                   "\"instructions\":\"keep the file result\",\"model\":\"gpt-5.5\","
                   "\"threshold_tokens\":100,\"estimated_tokens\":125,"
                   "\"keep_recent_tokens\":64,\"keep_recent_messages\":4,\"max_summary_bytes\":65536}"});
  auto const valid_compaction = ava::session::validate_session_replay(
      valid_compaction_entries, ava::session::SessionReplayValidationOptions{.require_structured_tool_results = true});
  expect(valid_compaction.ok() && valid_compaction.issues.empty(),
         "session replay validator accepts compaction after resolved tool state");

  auto invalid_compaction_entries = valid_entries;
  invalid_compaction_entries.push_back(ava::session::SessionEntry{.id = "compaction_invalid",
                                                                  .parent_id = "tool_result",
                                                                  .type = ava::session::EntryType::Compaction,
                                                                  .timestamp = "2026-04-29T00:00:05Z",
                                                                  .data_json = "{\"status\":\"recorded\","
                                                                               "\"summary_unavailable\":false,"
                                                                               "\"summary\":\"\"}"});
  auto const invalid_compaction = ava::session::validate_session_replay(invalid_compaction_entries);
  expect(!invalid_compaction.ok() &&
             has_replay_issue(invalid_compaction, ava::session::SessionReplayIssueKind::InvalidCompactionEntry),
         "session replay validator flags compaction entries without durable summaries");

  auto malformed_compaction_metadata_entries = valid_entries;
  malformed_compaction_metadata_entries.push_back(
      ava::session::SessionEntry{.id = "compaction_malformed_metadata",
                                 .parent_id = "tool_result",
                                 .type = ava::session::EntryType::Compaction,
                                 .timestamp = "2026-04-29T00:00:05Z",
                                 .data_json = "{\"status\":\"recorded\",\"summary\":\"durable summary\","
                                              "\"summary_unavailable\":false,\"threshold_tokens\":1.5}"});
  auto const malformed_compaction_metadata =
      ava::session::validate_session_replay(malformed_compaction_metadata_entries);
  expect(
      !malformed_compaction_metadata.ok() &&
          has_replay_issue(malformed_compaction_metadata, ava::session::SessionReplayIssueKind::InvalidCompactionEntry),
      "session replay validator flags non-integer compaction token metadata");

  auto unresolved_tool_compaction_entries = valid_entries;
  unresolved_tool_compaction_entries.pop_back();
  unresolved_tool_compaction_entries.push_back(
      ava::session::SessionEntry{.id = "compaction_before_tool_result",
                                 .parent_id = "tool_call",
                                 .type = ava::session::EntryType::Compaction,
                                 .timestamp = "2026-04-29T00:00:04Z",
                                 .data_json = "{\"summary\":\"tool call still pending\"}"});
  auto const unresolved_tool_compaction = ava::session::validate_session_replay(unresolved_tool_compaction_entries);
  expect(!unresolved_tool_compaction.ok() &&
             has_replay_issue(unresolved_tool_compaction,
                              ava::session::SessionReplayIssueKind::CompactionWithUnresolvedToolCall),
         "session replay validator flags compaction before unresolved tool results");

  std::vector<ava::session::SessionEntry> const unresolved_permission_compaction_entries = {
      unresolved_permission_entries[0],
      ava::session::SessionEntry{.id = "compaction_before_permission_resolution",
                                 .parent_id = "permission_unresolved",
                                 .type = ava::session::EntryType::Compaction,
                                 .timestamp = "2026-04-29T00:00:01Z",
                                 .data_json = "{\"summary\":\"permission prompt still pending\"}"},
  };
  auto const unresolved_permission_compaction =
      ava::session::validate_session_replay(unresolved_permission_compaction_entries);
  expect(!unresolved_permission_compaction.ok() &&
             has_replay_issue(unresolved_permission_compaction,
                              ava::session::SessionReplayIssueKind::CompactionWithUnresolvedPermissionPrompt),
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
  expect(valid_model_reasoning.ok() && valid_model_reasoning.issues.empty(),
         "session replay validator accepts durable model and reasoning metadata");

  std::vector<ava::session::SessionEntry> const invalid_model_start_entries = {
      ava::session::SessionEntry{.id = "bad_start",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::SessionStart,
                                 .timestamp = "2026-04-29T00:00:00Z",
                                 .data_json = "{\"mode\":\"build\",\"model\":\"gpt-5.5\"}"},
  };
  auto const invalid_model_start = ava::session::validate_session_replay(invalid_model_start_entries);
  expect(!invalid_model_start.ok() &&
             has_replay_issue(invalid_model_start, ava::session::SessionReplayIssueKind::InvalidModelEntry),
         "session replay validator flags session_start entries without provider/model metadata");

  auto invalid_model_change_entries = valid_model_reasoning_entries;
  invalid_model_change_entries[1].data_json =
      "{\"previous_provider\":\"anthropic\",\"previous_model\":\"claude\","
      "\"provider\":\"kimi\",\"model\":\"kimi-k2-thinking\"}";
  auto const invalid_model_change = ava::session::validate_session_replay(invalid_model_change_entries);
  expect(!invalid_model_change.ok() &&
             has_replay_issue(invalid_model_change, ava::session::SessionReplayIssueKind::InvalidModelEntry),
         "session replay validator flags model_change entries whose previous model does not match active state");

  auto invalid_reasoning_change_entries = valid_model_reasoning_entries;
  invalid_reasoning_change_entries[2].data_json =
      "{\"provider\":\"kimi\",\"model\":\"kimi-k2-thinking\",\"enabled\":true}";
  auto const invalid_reasoning_change = ava::session::validate_session_replay(invalid_reasoning_change_entries);
  expect(!invalid_reasoning_change.ok() &&
             has_replay_issue(invalid_reasoning_change, ava::session::SessionReplayIssueKind::InvalidReasoningEntry),
         "session replay validator flags enabled reasoning_change entries without a level");

  auto mismatched_reasoning_change_entries = valid_model_reasoning_entries;
  mismatched_reasoning_change_entries[2].data_json =
      "{\"provider\":\"openai\",\"model\":\"gpt-5.5\",\"enabled\":true,\"level\":\"low\"}";
  auto const mismatched_reasoning_change = ava::session::validate_session_replay(mismatched_reasoning_change_entries);
  expect(!mismatched_reasoning_change.ok() &&
             has_replay_issue(mismatched_reasoning_change, ava::session::SessionReplayIssueKind::InvalidReasoningEntry),
         "session replay validator flags reasoning_change entries for the wrong active model");

  auto invalid_reasoning_block_entries = valid_model_reasoning_entries;
  invalid_reasoning_block_entries[3].data_json =
      "{\"provider\":\"kimi\",\"model\":\"kimi-k2-thinking\",\"format\":\"reasoning_content\","
      "\"redacted\":false}";
  auto const invalid_reasoning_block = ava::session::validate_session_replay(invalid_reasoning_block_entries);
  expect(!invalid_reasoning_block.ok() &&
             has_replay_issue(invalid_reasoning_block, ava::session::SessionReplayIssueKind::InvalidReasoningEntry),
         "session replay validator flags reasoning_block entries without replayable content");

  expect(ava::session::to_string(ava::session::SessionReplayIssueKind::InvalidCompactionEntry) ==
             "invalid_compaction_entry",
         "session replay issue kind names include compaction validation failures");
  expect(ava::session::to_string(ava::session::SessionReplayIssueKind::UnsupportedEntryVersion) ==
             "unsupported_entry_version",
         "session replay issue kind names include entry version validation failures");
  expect(
      ava::session::to_string(ava::session::SessionReplayIssueKind::InvalidReasoningEntry) == "invalid_reasoning_entry",
      "session replay issue kind names include reasoning validation failures");
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

  auto const old_time = std::filesystem::file_time_type::clock::now() - std::chrono::minutes(2);
  auto const new_time = std::filesystem::file_time_type::clock::now();
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

void test_session_compaction_entry_round_trip()
{
  auto const root = temp_root() / "compaction-round-trip";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
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
                                                   .estimated_tokens = 1300,
                                                   .threshold_tokens = 0,
                                                   .trigger = "manual",
                                                   .recent_context = ""});
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
      unavailable_store, ava::session::ManualCompactionRequest{.summary = "",
                                                               .instructions = "",
                                                               .config = config,
                                                               .estimated_tokens = 0,
                                                               .threshold_tokens = 0,
                                                               .trigger = "manual",
                                                               .recent_context = ""});
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
      tiny_unavailable_store, ava::session::ManualCompactionRequest{.summary = "",
                                                                    .instructions = "",
                                                                    .config = tiny_config,
                                                                    .estimated_tokens = 0,
                                                                    .threshold_tokens = 0,
                                                                    .trigger = "manual",
                                                                    .recent_context = ""});
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
      oversized_summary_store, ava::session::ManualCompactionRequest{.summary = "xx",
                                                                     .instructions = "",
                                                                     .config = tiny_config,
                                                                     .estimated_tokens = 0,
                                                                     .threshold_tokens = 0,
                                                                     .trigger = "manual",
                                                                     .recent_context = ""});
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
      ava::session::SessionEntry{.id = "reasoning_1",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::ReasoningBlock,
                                 .timestamp = "2026-04-29T00:00:02Z",
                                 .data_json = "{\"provider\":\"anthropic\",\"model\":\"claude-sonnet-4-5\","
                                              "\"format\":\"anthropic_thinking\","
                                              "\"text\":\"visible reasoning summary\","
                                              "\"signature\":\"super-secret-signature\"}"},
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

  auto const basic = ava::session::format_session_markdown(entries);
  expect(basic.find("# AVA Session Export") != std::string::npos, "markdown export has deterministic title");
  expect(basic.find("## User") != std::string::npos && basic.find("Hello AVA") != std::string::npos,
         "markdown export renders user messages");
  expect(basic.find("internal_replay") == std::string::npos && basic.find("replay_of") == std::string::npos,
         "markdown export hides internal replay user messages");
  expect(basic.find("## Assistant") != std::string::npos && basic.find("Hello human") != std::string::npos,
         "markdown export renders assistant messages");
  expect(basic.find("## Reasoning") != std::string::npos &&
             basic.find("visible reasoning summary") != std::string::npos &&
             basic.find("Signature present") != std::string::npos &&
             basic.find("super-secret-signature") == std::string::npos,
         "markdown export renders reasoning blocks without leaking provider-private signatures");
  expect(basic.find("Usage:") != std::string::npos && basic.find("input_tokens") != std::string::npos,
         "markdown export renders assistant usage when present");
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
  std::string const escaped_control_markdown = std::string("first\nsecond\tindent") + "\\u0000\\u001B\\u007F\\u000D";
  expect(basic.find(escaped_control_markdown) != std::string::npos,
         "markdown export escapes decoded fenced control bytes while preserving newlines and tabs");
  expect(basic.find('\0') == std::string::npos && basic.find('\x1B') == std::string::npos &&
             basic.find('\x7F') == std::string::npos && basic.find('\r') == std::string::npos,
         "markdown export does not emit raw NUL, escape, DEL, or carriage return bytes");

  auto const with_tools = ava::session::format_session_markdown(
      entries, ava::session::ExportOptions{.include_tool_details = true, .include_metadata = false});
  expect(with_tools.find("## Tool Call") != std::string::npos && with_tools.find("README.md") != std::string::npos &&
             with_tools.find("## Tool Result") != std::string::npos &&
             with_tools.find("tool output") != std::string::npos,
         "markdown export includes tool calls and results when requested");

  auto const without_compactions = ava::session::format_session_markdown(
      entries, ava::session::ExportOptions{
                   .include_tool_details = false, .include_metadata = false, .include_compactions = false});
  expect(without_compactions.find("## Compaction") == std::string::npos &&
             without_compactions.find("Prior summary") == std::string::npos,
         "markdown export can omit compaction entries");

  auto const with_metadata = ava::session::format_session_markdown(
      entries, ava::session::ExportOptions{
                   .include_tool_details = false, .include_metadata = true, .include_compactions = true});
  expect(with_metadata.find("Metadata:") != std::string::npos &&
             with_metadata.find("\"id\":\"user_1\"") != std::string::npos &&
             with_metadata.find("Estimated tokens") != std::string::npos &&
             with_metadata.find("gpt-5.5-mini") != std::string::npos,
         "markdown export includes entry and compaction metadata when requested");
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

  expect(paths.compaction_file == root / "config" / "ava" / "compaction.json",
         "compaction config path follows XDG config home");
  auto missing = ava::session::load_compaction_config(paths);
  expect(missing && missing->model_id == "gpt-5.5" && missing->auto_threshold_tokens == 0,
         "missing compaction config uses safe defaults");
  auto const fallback_threshold = ava::session::effective_auto_threshold_tokens(*missing, std::nullopt);
  expect(fallback_threshold > 0,
         "missing compaction config uses a nonzero effective auto-compaction threshold without model metadata");

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
  std::vector<ava::session::SessionEntry> const entries = {
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
  auto const decision = ava::session::should_auto_compact(entries, config);
  expect(decision.should_compact && decision.estimated_tokens == config.auto_threshold_tokens,
         "auto compaction triggers when estimated tokens reach threshold");
  config.auto_threshold_tokens = decision.estimated_tokens + 1;
  expect(!ava::session::should_auto_compact(entries, config).should_compact,
         "auto compaction does not trigger below threshold");
  config.auto_threshold_tokens = 0;
  expect(!ava::session::should_auto_compact(entries, config).should_compact,
         "auto compaction threshold zero disables automatic compaction");
  config.auto_threshold_tokens_explicit = false;
  std::vector<ava::session::SessionEntry> const fallback_entries = {
      ava::session::SessionEntry{.id = "fallback_big",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::UserMessage,
                                 .timestamp = "2026-04-27T00:00:00Z",
                                 .data_json = "{\"text\":\"" + std::string(fallback_threshold * 4, 'x') + "\"}"}};
  expect(ava::session::should_auto_compact(fallback_entries, config, std::nullopt).should_compact,
         "default auto compaction can trigger when model context-window metadata is absent");
  config.auto_threshold_tokens_explicit = true;
  expect(!ava::session::should_auto_compact(fallback_entries, config, std::nullopt).should_compact,
         "explicit auto_threshold_tokens zero remains disabled without model metadata");
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
  if (!messages) return;
  expect((*messages)[0].role == "user" && (*messages)[0].content.find("latest summary") != std::string::npos &&
             (*messages)[0].content.find("carry this") != std::string::npos,
         "latest compaction summary becomes provider-visible context");
  std::string const joined = (*messages)[0].content + (*messages)[1].content + (*messages)[2].content +
                             (*messages)[3].content + (*messages)[4].content + (*messages)[5].content;
  expect(joined.find("old raw user") == std::string::npos && joined.find("old raw assistant") == std::string::npos &&
             joined.find("old raw tool") == std::string::npos && joined.find("middle raw user") == std::string::npos &&
             joined.find("first summary") == std::string::npos,
         "context reconstruction omits raw messages and tool results before latest compaction");
  expect((*messages)[1].role == "user" && (*messages)[1].content == "new user" && (*messages)[2].role == "user" &&
             (*messages)[2].content == "new user" && (*messages)[3].role == "assistant" &&
             (*messages)[3].content == "new assistant",
         "post-compaction entries include internal replays as normal provider messages");
  expect((*messages)[4].role == "assistant" &&
             (*messages)[4].content.find("Tool call requested by assistant") != std::string::npos &&
             (*messages)[4].content.find("read_file") != std::string::npos && (*messages)[5].role == "user" &&
             (*messages)[5].content.find("note contents") != std::string::npos,
         "post-compaction context includes tool-call metadata before tool result data");
  expect((*messages)[4].content_parts.size() == 1 &&
             (*messages)[4].content_parts[0].type == ava::provider::ContentPartType::ToolUse &&
             (*messages)[4].content_parts[0].tool_call_id == "call_read" &&
             (*messages)[4].content_parts[0].tool_name == "read_file" &&
             (*messages)[4].content_parts[0].input_json.find("note.txt") != std::string::npos &&
             (*messages)[5].content_parts.size() == 1 &&
             (*messages)[5].content_parts[0].type == ava::provider::ContentPartType::ToolResult &&
             (*messages)[5].content_parts[0].tool_call_id == "call_read" &&
             (*messages)[5].content_parts[0].tool_name == "read_file" &&
             (*messages)[5].content_parts[0].text == "note contents" && !(*messages)[5].content_parts[0].is_error,
         "tool-call and tool-result entries carry native provider content parts");
}

void test_tool_content_parts_reconstruction()
{
  std::string const long_result(80, 'r');
  std::vector<ava::session::SessionEntry> const entries = {
      ava::session::SessionEntry{.id = "tool_call",
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

  auto messages = ava::agent::build_provider_messages_from_entries(
      entries, ava::agent::MessageBuildOptions{.max_tool_result_context_bytes = 48});
  expect(messages && messages->size() == 2, "tool entries reconstruct as provider messages");
  if (!messages || messages->size() != 2) return;

  expect((*messages)[0].role == "assistant" &&
             (*messages)[0].content.find("Tool call requested by assistant") != std::string::npos &&
             (*messages)[0].content_parts.size() == 1 &&
             (*messages)[0].content_parts[0].type == ava::provider::ContentPartType::ToolUse &&
             (*messages)[0].content_parts[0].tool_call_id == "call_failed" &&
             (*messages)[0].content_parts[0].tool_name == "bash" &&
             (*messages)[0].content_parts[0].input_json.find("false") != std::string::npos,
         "tool-call entry reconstructs an assistant tool-use content part with fallback text");
  expect((*messages)[1].role == "user" && !(*messages)[1].content.empty() && (*messages)[1].content_parts.size() == 1 &&
             (*messages)[1].content_parts[0].type == ava::provider::ContentPartType::ToolResult &&
             (*messages)[1].content_parts[0].tool_call_id == "call_failed" &&
             (*messages)[1].content_parts[0].tool_name == "bash" && (*messages)[1].content_parts[0].is_error &&
             (*messages)[1].content_parts[0].text.size() == 48 &&
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
  expect(permission_messages && permission_messages->size() == 2,
         "permission decisions are internal metadata during provider replay");
  if (!permission_messages || permission_messages->size() != 2) return;
  expect((*permission_messages)[0].content_parts.size() == 1 && (*permission_messages)[1].content_parts.size() == 1,
         "native tool replay allows internal permission metadata between tool call and result");

  constexpr std::string_view truncation_marker = "\n[AVA: tool result content truncated]";
  std::string const euro = std::string("\xE2") + "\x82" + "\xAC";
  std::string const utf8_result = "abc" + euro + std::string(80, 'x');
  std::vector<ava::session::SessionEntry> const utf8_entries = {
      ava::session::SessionEntry{.id = "utf8_tool_call",
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
  if (!utf8_messages || utf8_messages->size() != 2) return;
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
  expect(malformed_success_messages && malformed_success_messages->size() == 2,
         "malformed bool tool entries reconstruct as provider messages");
  if (!malformed_success_messages || malformed_success_messages->size() != 2) return;
  expect((*malformed_success_messages)[1].content_parts.size() == 1 &&
             !(*malformed_success_messages)[1].content_parts[0].is_error,
         "malformed success bool prefixes do not parse as native error metadata");

  std::vector<ava::session::SessionEntry> const malformed_entries = {
      ava::session::SessionEntry{.id = "malformed_tool_call",
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
  expect(malformed_messages && malformed_messages->size() == 2,
         "malformed tool entries still reconstruct as fallback provider messages");
  if (!malformed_messages || malformed_messages->size() != 2) return;
  expect((*malformed_messages)[0].content.find("cmd") != std::string::npos &&
             (*malformed_messages)[0].content_parts.empty() &&
             (*malformed_messages)[1].content.find("still visible") != std::string::npos &&
             (*malformed_messages)[1].content_parts.empty(),
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
  expect(malformed_id_messages && malformed_id_messages->size() == 2,
         "malformed tool id entries still reconstruct as fallback provider messages");
  if (!malformed_id_messages || malformed_id_messages->size() != 2) return;
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
  expect(duplicate_batch_id_messages && duplicate_batch_id_messages->size() == 5,
         "duplicate same-turn tool ids reconstruct as fallback provider messages");
  if (!duplicate_batch_id_messages || duplicate_batch_id_messages->size() != 5) return;
  expect((*duplicate_batch_id_messages)[1].content_parts.empty() &&
             (*duplicate_batch_id_messages)[2].content_parts.empty() &&
             (*duplicate_batch_id_messages)[3].content_parts.empty() &&
             (*duplicate_batch_id_messages)[4].content_parts.empty(),
         "duplicate same-turn native tool ids fall back to text-only replay");

  std::vector<ava::session::SessionEntry> const reused_id_entries = {
      ava::session::SessionEntry{.id = "valid_tool_call",
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
  if (!reused_id_messages || reused_id_messages->size() != 4) return;
  expect((*reused_id_messages)[1].content_parts.size() == 1 &&
             (*reused_id_messages)[1].content_parts[0].type == ava::provider::ContentPartType::ToolResult &&
             (*reused_id_messages)[2].content_parts.empty() && (*reused_id_messages)[3].content_parts.empty(),
         "native tool-result matching is one-shot and reused native ids fall back to text-only");

  std::vector<ava::session::SessionEntry> const interrupted_entries = {
      ava::session::SessionEntry{.id = "interrupted_tool_call",
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
  expect(interrupted_messages && interrupted_messages->size() == 3,
         "interrupted tool entries still reconstruct as provider messages");
  if (!interrupted_messages || interrupted_messages->size() != 3) return;
  expect((*interrupted_messages)[0].content_parts.empty() && (*interrupted_messages)[2].content_parts.empty(),
         "non-contiguous tool-use/result history falls back to text-only native replay");
}

}  // namespace

void run_session_tests()
{
  test_session_entry_codec_helpers();
  test_session_store_round_trip();
  test_session_stats_helper();
  test_session_stats_omits_incomplete_cost_total();
  test_session_stats_flags_legacy_assistant_tokens_without_cost();
  test_session_replay_validation();
  test_session_resume_and_listing();
  test_session_compaction_entry_round_trip();
  test_session_markdown_export();
  test_compaction_config_and_thresholds();
  test_compaction_context_reconstruction();
  test_tool_content_parts_reconstruction();
}
