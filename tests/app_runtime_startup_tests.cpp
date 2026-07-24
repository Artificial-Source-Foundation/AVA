#include "sys.h"
#include "tests/support/app_runtime_support.h"
#include "tests/support/fake_transport.h"
#include "tests/support/test_harness.h"
#include "ava/app/commands.h"
#include "ava/app/project_trust.h"
#include "ava/app/rpc_mode.h"
#include "ava/app/runtime.h"
#include "ava/app/runtime/OpenOptions.h"
#include "ava/app/runtime/Session.h"
#include "ava/agent/agent_loop_session.h"
#include "ava/agent/message_builder.h"
#include "ava/agent/mode.h"
#include "ava/agent/tool_types.h"
#include "ava/session/assistant_output.h"
#include "ava/session/record.h"
#include "ava/session/session_metadata.h"
#include "ava/session/session_store.h"
#include "ava/session/validation.h"
#include "ava/provider/openai_provider.h"
#include "ava/provider/provider.h"
#include "ava/core/error.h"
#include "ava/core/json.h"
#include "ava/core/result.h"

#include <algorithm>
#include <expected>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>
#ifndef AVA_FAKE_MCP_SERVER_PATH
#define AVA_FAKE_MCP_SERVER_PATH ""
#endif

namespace ava::tests::app_runtime_tests {

using namespace ava::tests;

void test_app_runtime_open_session_and_context_prompt()
{
  auto root = create_empty_root("app-runtime-open");

  auto workspace = root / "workspace";
  auto const current = workspace / "src";
  auto const paths = app_test_paths(root);
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

  ava::app::runtime::OpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = current;
  open_options.mode = ava::agent::Mode::Plan;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "runtime session opens with selected model, prompt, and context");
  if (!session)
    return;

  expect(session->created && session->mode == ava::agent::Mode::Plan && session->model.model_id == "gpt-5.5",
         "runtime session records created state, mode, and model");
  expect(session->context_sources.size() == 3, "runtime session records workspace and global context metadata");
  expect(!session->base_prompt.from_override && !session->base_prompt.source_path && session->base_prompt.byte_count > 0 &&
             session->base_prompt.content_fingerprint != 0,
         "runtime session records selected base prompt metadata without storing prompt text twice");
  expect(session->system_prompt.find("Plan before changing files") != std::string::npos &&
             session->system_prompt.find("workspace runtime instructions") != std::string::npos &&
             session->system_prompt.find("nested runtime instructions") != std::string::npos &&
             session->system_prompt.find("global runtime instructions") != std::string::npos,
         "runtime session system prompt combines selected prompt and formatted context");
  auto entries = session->store.load();
  expect(entries && entries->size() == 1 && (*entries)[0].type == ava::session::EntryType::SessionStart &&
             (*entries)[0].data_json.find("\"context_sources\":3") != std::string::npos,
         "runtime session appends session_start on creation");

  auto const session_id = session->store.session_id();
  ava::app::runtime::OpenOptions reopen_options;
  reopen_options.workspace_dir = workspace;
  reopen_options.current_dir = current;
  reopen_options.requested_session_id = session_id.substr(0, 12);
  reopen_options.mode = ava::agent::Mode::Plan;
  reopen_options.paths = paths;
  session = std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "release runtime before reopen"));
  auto reopened = ava::app::open_runtime_session(reopen_options);
  expect(reopened && !reopened->created && reopened->store.session_id() == session_id,
         "runtime session resolves requested session id prefixes without creating a new session");
  if (reopened)
  {
    auto reopened_entries = reopened->store.load();
    expect(reopened_entries && reopened_entries->size() == 1, "runtime reopened session does not append another session_start");
  }
}

void test_app_runtime_no_session_mode()
{
  auto const root = create_empty_root("app-runtime-no-session");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::runtime::OpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Build;
  open_options.paths = paths;
  open_options.sessionless = true;

  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value() && session->sessionless && session->store.is_ephemeral(), "runtime opens no-session mode with an ephemeral store");
  if (!session)
    return;

  auto entries = session->store.load();
  expect(entries && entries->size() == 1 && (*entries)[0].type == ava::session::EntryType::SessionStart,
         "runtime no-session mode records session_start in memory");
  expect(!std::filesystem::exists(session->store.session_path()), "runtime no-session mode does not create a resumable JSONL file");

  auto listed = ava::session::SessionStore::list_sessions(workspace, paths.sessions_dir);
  expect(listed && listed->empty(), "runtime no-session mode does not appear in persisted session listings");

  auto requested_conflict = open_options;
  requested_conflict.requested_session_id = session->store.session_id();
  auto requested_result = ava::app::open_runtime_session(requested_conflict);
  expect(!requested_result && requested_result.error().message().find("no-session") != std::string::npos,
         "runtime rejects no-session with requested session resume");

  auto continue_conflict = open_options;
  continue_conflict.continue_last_session = true;
  auto continue_result = ava::app::open_runtime_session(continue_conflict);
  expect(!continue_result && continue_result.error().message().find("no-session") != std::string::npos, "runtime rejects no-session with continue");
}

void test_app_runtime_session_startup_options()
{
  auto const root = create_empty_root("app-runtime-session-startup-options");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::runtime::OpenOptions named_options;
  named_options.workspace_dir = workspace;
  named_options.current_dir = workspace;
  named_options.mode = ava::agent::Mode::Build;
  named_options.paths = paths;
  named_options.initial_session_name = "named startup";

  auto named = ava::app::open_runtime_session(named_options);
  expect(named.has_value() && named->created, "runtime opens a named startup session");
  if (!named)
    return;

  auto const named_session_id = named->store.session_id();
  auto named_metadata = ava::session::load_session_metadata(named->store);
  expect(named_metadata && named_metadata->name == "named startup" && named_metadata->actor == "cli", "runtime startup --name records session metadata");
  auto named_entries = named->store.load();
  expect(named_entries && named_entries->size() == 2 && (*named_entries)[0].type == ava::session::EntryType::SessionStart &&
             (*named_entries)[1].type == ava::session::EntryType::SessionMetadata,
         "runtime startup --name appends metadata after session_start");

  auto custom_paths = paths;
  custom_paths.sessions_dir = root / "custom-sessions";
  ava::app::runtime::OpenOptions custom_options;
  custom_options.workspace_dir = workspace;
  custom_options.current_dir = workspace;
  custom_options.paths = custom_paths;
  auto custom = ava::app::open_runtime_session(custom_options);
  expect(custom.has_value() && custom->store.session_path().string().find(custom_paths.sessions_dir.string()) == 0,
         "runtime opens sessions under a custom session directory");
  auto default_sessions = ava::session::SessionStore::list_sessions(workspace, paths.sessions_dir);
  auto custom_sessions = ava::session::SessionStore::list_sessions(workspace, custom_paths.sessions_dir);
  expect(default_sessions && default_sessions->size() == 1 && default_sessions->front().session_id == named_session_id,
         "runtime custom session directory leaves default session listing unchanged");
  expect(custom_sessions && custom_sessions->size() == 1 && custom_sessions->front().session_id == custom->store.session_id(),
         "runtime custom session directory has its own session listing");

  ava::app::runtime::OpenOptions active_source_fork_options;
  active_source_fork_options.workspace_dir = workspace;
  active_source_fork_options.current_dir = workspace;
  active_source_fork_options.paths = paths;
  active_source_fork_options.fork_session_id = named_session_id;
  auto active_source_fork = ava::app::open_runtime_session(active_source_fork_options);
  expect(!active_source_fork && active_source_fork.error().message().find("already owned") != std::string::npos,
         "runtime --fork reports an actionable lease conflict while another runtime owns the source");

  named = std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "release source runtime before startup fork"));

  ava::app::runtime::OpenOptions fork_options;
  fork_options.workspace_dir = workspace;
  fork_options.current_dir = workspace;
  fork_options.paths = paths;
  fork_options.fork_session_id = named_session_id.substr(0, 12);
  fork_options.initial_session_name = "forked startup";
  auto forked = ava::app::open_runtime_session(fork_options);
  expect(forked.has_value() && forked->created && forked->store.session_id() != named_session_id, "runtime --fork creates a new session from a source prefix");
  if (forked)
  {
    auto fork_metadata = ava::session::load_session_metadata(forked->store);
    expect(fork_metadata && fork_metadata->name == "forked startup" && fork_metadata->parent_session_id == named_session_id &&
               fork_metadata->source_session_id == named_session_id && fork_metadata->branch_origin == "fork" && fork_metadata->actor == "cli" &&
               !fork_metadata->branch_from_entry_id.empty(),
           "runtime --fork records branch metadata and startup name");
    auto fork_entries = forked->store.load();
    if (fork_entries && named_entries && named_entries->size() > 1)
    {
      auto const start_count = std::ranges::count_if(*fork_entries, [](auto const& entry) { return entry.type == ava::session::EntryType::SessionStart; });
      expect(start_count == 1 && fork_entries->size() == 3 && fork_entries->back().type == ava::session::EntryType::SessionMetadata,
             "runtime --fork copies source history and does not append an extra session_start");
      expect(fork_metadata && fork_metadata->branch_from_entry_id == (*named_entries)[1].id,
             "runtime --fork records the latest copied source entry as the branch point");
    }
  }

  auto fork_requested_conflict = fork_options;
  fork_requested_conflict.requested_session_id = named_session_id;
  auto fork_requested_result = ava::app::open_runtime_session(fork_requested_conflict);
  expect(!fork_requested_result && fork_requested_result.error().message().find("fork") != std::string::npos,
         "runtime rejects --fork with requested session resume");

  auto fork_continue_conflict = fork_options;
  fork_continue_conflict.continue_last_session = true;
  auto fork_continue_result = ava::app::open_runtime_session(fork_continue_conflict);
  expect(!fork_continue_result && fork_continue_result.error().message().find("fork") != std::string::npos, "runtime rejects --fork with continue");

  auto fork_no_session_conflict = fork_options;
  fork_no_session_conflict.sessionless = true;
  auto fork_no_session_result = ava::app::open_runtime_session(fork_no_session_conflict);
  expect(!fork_no_session_result && fork_no_session_result.error().message().find("no-session") != std::string::npos, "runtime rejects --fork with no-session");
}

void test_app_runtime_recovers_torn_tail_before_resume_and_startup_fork()
{
  for (std::string const mode : {"exact", "prefix", "continue"})
  {
    auto const root = create_empty_root("app-runtime-torn-resume-" + mode);

    auto const workspace = root / "workspace";
    auto const paths = app_test_paths(root);
    std::filesystem::create_directories(workspace);

    ava::app::runtime::OpenOptions seed_options;
    seed_options.workspace_dir = workspace;
    seed_options.current_dir = workspace;
    seed_options.paths = paths;
    auto seeded = ava::app::open_runtime_session(seed_options);
    expect(seeded.has_value(), "runtime torn resume test creates source session for " + mode);
    if (!seeded)
      continue;
    auto const session_id = seeded->store.session_id();
    auto const session_path = seeded->store.session_path();
    auto const valid_bytes = app_read_binary_file(session_path);
    {
      std::ofstream file(session_path, std::ios::binary | std::ios::app);
      file << "{\"version\":3,\"id\":\"torn";
    }
    seeded = std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "release torn source before resume"));

    ava::app::runtime::OpenOptions resume_options = seed_options;
    if (mode == "continue")
      resume_options.continue_last_session = true;
    else
    {
      resume_options.requested_session_id = mode == "exact" ? session_id : session_id.substr(0, 12);
      resume_options.exact_session_id = mode == "exact";
    }
    auto resumed = ava::app::open_runtime_session(resume_options);
    auto loaded = resumed ? resumed->store.load() : ava::core::Result<std::vector<ava::session::SessionEntry>>(std::unexpected(resumed.error()));
    expect(resumed && resumed->store.session_id() == session_id && loaded && loaded->size() == 1 && app_read_binary_file(session_path) == valid_bytes,
           "runtime " + mode + " resume acquires the lease, quarantines the torn tail, and then loads validated history");
  }

  auto const root = create_empty_root("app-runtime-torn-startup-fork");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  ava::app::runtime::OpenOptions seed_options;
  seed_options.workspace_dir = workspace;
  seed_options.current_dir = workspace;
  seed_options.paths = paths;
  auto source = ava::app::open_runtime_session(seed_options);
  expect(source.has_value(), "startup fork torn recovery test creates source session");
  if (!source)
    return;
  auto const source_id = source->store.session_id();
  auto const source_path = source->store.session_path();
  auto const valid_source_bytes = app_read_binary_file(source_path);
  {
    std::ofstream file(source_path, std::ios::binary | std::ios::app);
    file << "{\"version\":3,\"id\":\"fork-torn";
  }
  source = std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "release torn source before startup fork"));

  auto fork_options = seed_options;
  fork_options.fork_session_id = source_id.substr(0, 12);
  auto forked = ava::app::open_runtime_session(fork_options);
  auto fork_entries = forked ? forked->store.load() : ava::core::Result<std::vector<ava::session::SessionEntry>>(std::unexpected(forked.error()));
  expect(forked && forked->created && fork_entries && fork_entries->size() == 2 && app_read_binary_file(source_path) == valid_source_bytes,
         "startup --fork temporarily leases and recovers its source before holding the lease through branch creation");

  auto no_recovery_artifacts = [](std::filesystem::path const& session_path) {
    auto const final_prefix = session_path.filename().string() + ".torn-tail.";
    auto const temporary_prefix = "." + session_path.filename().string() + ".torn-tail.tmp.";
    std::error_code iter_error;
    for (std::filesystem::directory_iterator iterator(session_path.parent_path(), iter_error), end; !iter_error && iterator != end;
         iterator.increment(iter_error))
    {
      auto const name = iterator->path().filename().string();
      if (name.starts_with(final_prefix) || name.starts_with(temporary_prefix))
        return false;
    }
    return true;
  };

  auto const bounded_root = create_empty_root("app-runtime-bounded-torn-resume");

  auto const bounded_workspace = bounded_root / "workspace";
  auto const bounded_paths = app_test_paths(bounded_root);
  std::filesystem::create_directories(bounded_workspace);
  ava::app::runtime::OpenOptions bounded_seed_options;
  bounded_seed_options.workspace_dir = bounded_workspace;
  bounded_seed_options.current_dir = bounded_workspace;
  bounded_seed_options.paths = bounded_paths;
  auto byte_limited_seed = ava::app::open_runtime_session(bounded_seed_options);
  expect(byte_limited_seed.has_value(), "bounded runtime recovery test creates byte-limited source");
  if (!byte_limited_seed)
    return;
  auto const byte_limited_id = byte_limited_seed->store.session_id();
  auto const byte_limited_path = byte_limited_seed->store.session_path();
  auto byte_limited_bytes = app_read_binary_file(byte_limited_path);
  byte_limited_bytes += "{\"version\":3,\"id\":\"bounded-byte-torn";
  {
    std::ofstream file(byte_limited_path, std::ios::binary | std::ios::trunc);
    file << byte_limited_bytes;
  }
  byte_limited_seed = std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "release byte-limited runtime"));
  auto byte_limited_options = bounded_seed_options;
  byte_limited_options.requested_session_id = byte_limited_id;
  byte_limited_options.exact_session_id = true;
  byte_limited_options.session_read_limits =
      ava::session::SessionReadLimits{.max_file_bytes = byte_limited_bytes.size() - 1, .max_line_bytes = byte_limited_bytes.size() - 1, .max_entries = 8};
  auto byte_limited_resume = ava::app::open_runtime_session(byte_limited_options);
  expect(!byte_limited_resume && byte_limited_resume.error().message().find("byte limit") != std::string::npos &&
             app_read_binary_file(byte_limited_path) == byte_limited_bytes && no_recovery_artifacts(byte_limited_path),
         "bounded runtime/ACP-style recovery rejects an oversized source unchanged without quarantine");

  auto entry_limited_seed = ava::app::open_runtime_session(bounded_seed_options);
  expect(entry_limited_seed.has_value(), "bounded runtime recovery test creates entry-limited source");
  if (!entry_limited_seed)
    return;
  auto const entry_limited_id = entry_limited_seed->store.session_id();
  auto const entry_limited_path = entry_limited_seed->store.session_path();
  auto appended_entry = entry_limited_seed->append_owned(ava::session::SessionEntry{.id = "bounded_second_entry",
                                                                                    .parent_id = "",
                                                                                    .type = ava::session::EntryType::UserMessage,
                                                                                    .timestamp = "2026-07-14T00:00:00Z",
                                                                                    .data_json = "{\"text\":\"second\"}"});
  expect(appended_entry.has_value(), "bounded runtime recovery test appends a second complete entry");
  auto entry_limited_bytes = app_read_binary_file(entry_limited_path);
  entry_limited_bytes += "{\"version\":3,\"id\":\"bounded-entry-torn";
  {
    std::ofstream file(entry_limited_path, std::ios::binary | std::ios::trunc);
    file << entry_limited_bytes;
  }
  entry_limited_seed = std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "release entry-limited runtime"));
  auto entry_limited_options = bounded_seed_options;
  entry_limited_options.requested_session_id = entry_limited_id;
  entry_limited_options.exact_session_id = true;
  entry_limited_options.session_read_limits = ava::session::SessionReadLimits{.max_file_bytes = 4096, .max_line_bytes = 2048, .max_entries = 1};
  auto entry_limited_resume = ava::app::open_runtime_session(entry_limited_options);
  expect(!entry_limited_resume && entry_limited_resume.error().message().find("entry count") != std::string::npos &&
             app_read_binary_file(entry_limited_path) == entry_limited_bytes && no_recovery_artifacts(entry_limited_path),
         "bounded runtime/ACP-style recovery rejects an over-entry source unchanged without quarantine");
}

void test_app_runtime_reconciles_committed_function_calls_on_resume()
{
  auto const root = create_empty_root("app-runtime-committed-function-reconciliation");
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::runtime::OpenOptions options;
  options.workspace_dir = workspace;
  options.current_dir = workspace;
  options.paths = paths;
  auto seeded = ava::app::open_runtime_session(options);
  expect(seeded.has_value(), "committed-function reconciliation fixture opens a session");
  if (!seeded)
    return;
  auto const session_id = seeded->store.session_id();
  auto function = [](std::string id, std::string call_id, std::size_t sequence) {
    auto data = ava::session::serialize_assistant_output_item_data_json(ava::session::AssistantOutputItem{
        .assistant_turn_id = "reconcile-turn",
        .sequence = sequence,
        .kind = ava::session::AssistantOutputItemKind::FunctionCall,
        .provider_item_id = "provider-" + std::to_string(sequence),
        .provider_output_index = sequence,
        .payload = ava::session::AssistantOutputFunctionCall{.call_id = std::move(call_id), .name = "read_file", .arguments_json = R"({"path":"note.txt"})"}});
    return ava::session::SessionEntry{.id = std::move(id),
                                      .parent_id = "",
                                      .type = ava::session::EntryType::AssistantOutputItem,
                                      .timestamp = ava::session::now_timestamp(),
                                      .data_json = data.value_or("{}")};
  };
  auto commit_data = ava::session::serialize_assistant_turn_commit_data_json(ava::session::AssistantTurnCommit{.assistant_turn_id = "reconcile-turn",
                                                                                                               .item_count = 2,
                                                                                                               .provider = "openai",
                                                                                                               .model = "gpt-5.5",
                                                                                                               .finish_reason = "tool_calls",
                                                                                                               .usage_json = std::nullopt});
  auto first = function("reconcile-function-one", "reconcile-call-one", 0);
  auto second = function("reconcile-function-two", "reconcile-call-two", 1);
  auto committed = seeded->append_owned(first);
  committed = committed ? seeded->append_owned(second) : std::move(committed);
  committed = committed ? seeded->append_owned(ava::session::SessionEntry{.id = "reconcile-commit",
                                                                          .parent_id = "",
                                                                          .type = ava::session::EntryType::AssistantTurnCommit,
                                                                          .timestamp = ava::session::now_timestamp(),
                                                                          .data_json = commit_data.value_or("{}")})
                        : std::move(committed);
  auto partial_result = committed
                            ? ava::agent::append_tool_result(
                                  seeded->owner_append_route(),
                                  ava::agent::ToolDispatchResult{
                                      .call_id = "reconcile-call-one", .name = "read_file", .success = true, .result_text = R"({"ok":true,"path":"note.txt"})"},
                                  "reconcile-function-one")
                            : ava::core::VoidResult(std::unexpected(committed.error()));
  expect(committed && partial_result, "committed-function reconciliation fixture writes a committed turn with one preexisting exact result");
  seeded = std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "release reconciliation fixture before resume"));

  auto resume = options;
  resume.requested_session_id = session_id;
  resume.exact_session_id = true;
  auto resumed = ava::app::open_runtime_session(resume);
  auto entries = resumed ? resumed->store.load() : ava::core::Result<std::vector<ava::session::SessionEntry>>(std::unexpected(resumed.error()));
  std::size_t first_results = 0;
  std::size_t second_results = 0;
  bool saw_unknown_nonretriable = false;
  if (entries)
  {
    for (auto const& entry : *entries)
    {
      if (entry.type != ava::session::EntryType::ToolResult)
        continue;
      auto const call_id = ava::core::json::string_field(entry.data_json, "call_id").value_or("");
      first_results += call_id == "reconcile-call-one";
      second_results += call_id == "reconcile-call-two";
      saw_unknown_nonretriable =
          saw_unknown_nonretriable ||
          (call_id == "reconcile-call-two" && entry.data_json.find("execution_outcome_unknown") != std::string::npos &&
           entry.data_json.find("Do not retry automatically") != std::string::npos && entry.data_json.find("reconcile-function-two") != std::string::npos);
    }
  }
  auto validation = entries ? ava::session::validate_session_replay(*entries) : ava::session::SessionReplayValidation{};
  auto messages = entries ? ava::agent::build_provider_messages_from_entries(*entries, ava::agent::MessageBuildOptions{})
                          : ava::core::Result<std::vector<ava::provider::ChatMessage>>(std::unexpected(resumed.error()));
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  auto request = messages ? provider.build_request(ava::provider::ProviderRequest{.provider_id = "openai",
                                                                                  .model_id = "gpt-5.5",
                                                                                  .system_prompt = "system",
                                                                                  .messages = std::move(*messages),
                                                                                  .tools_json = {},
                                                                                  .stream = true},
                                                   "token")
                          : ava::core::Result<ava::provider::HttpRequest>(std::unexpected(messages.error()));
  expect(resumed && entries && first_results == 1 && second_results == 1 && saw_unknown_nonretriable && validation.ok() && request,
         "resume closes only unresolved committed v4 functions, preserves exact bindings, validates replay, and builds the next provider request");

  resumed = std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "release reconciled runtime before idempotence check"));
  auto reopened = ava::app::open_runtime_session(resume);
  auto reopened_entries = reopened ? reopened->store.load() : ava::core::Result<std::vector<ava::session::SessionEntry>>(std::unexpected(reopened.error()));
  std::size_t second_results_after_reopen = 0;
  if (reopened_entries)
    for (auto const& entry : *reopened_entries)
      second_results_after_reopen +=
          entry.type == ava::session::EntryType::ToolResult && ava::core::json::string_field(entry.data_json, "call_id").value_or("") == "reconcile-call-two";
  expect(reopened && reopened_entries && second_results_after_reopen == 1,
         "reopening an already reconciled committed function turn never writes a duplicate synthetic result");

  auto zero_result_seed = ava::app::open_runtime_session(options);
  expect(zero_result_seed.has_value(), "zero-result reconciliation fixture opens a second session");
  if (!zero_result_seed)
    return;
  auto const zero_result_session_id = zero_result_seed->store.session_id();
  auto zero_commit_data = ava::session::serialize_assistant_turn_commit_data_json(ava::session::AssistantTurnCommit{.assistant_turn_id = "reconcile-turn",
                                                                                                                    .item_count = 1,
                                                                                                                    .provider = "openai",
                                                                                                                    .model = "gpt-5.5",
                                                                                                                    .finish_reason = "tool_calls",
                                                                                                                    .usage_json = std::nullopt});
  auto zero_committed = zero_result_seed->append_owned(function("zero-result-function", "zero-result-call", 0));
  zero_committed = zero_committed ? zero_result_seed->append_owned(ava::session::SessionEntry{.id = "zero-result-commit",
                                                                                              .parent_id = "",
                                                                                              .type = ava::session::EntryType::AssistantTurnCommit,
                                                                                              .timestamp = ava::session::now_timestamp(),
                                                                                              .data_json = zero_commit_data.value_or("{}")})
                                  : std::move(zero_committed);
  expect(zero_committed.has_value(), "zero-result reconciliation fixture writes a committed v4 function without any result");
  zero_result_seed = std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "release zero-result fixture before resume"));
  auto zero_resume = options;
  zero_resume.requested_session_id = zero_result_session_id;
  zero_resume.exact_session_id = true;
  auto zero_result_reopened = ava::app::open_runtime_session(zero_resume);
  auto zero_entries = zero_result_reopened ? zero_result_reopened->store.load()
                                           : ava::core::Result<std::vector<ava::session::SessionEntry>>(std::unexpected(zero_result_reopened.error()));
  std::size_t zero_synthetic_results = 0;
  if (zero_entries)
    for (auto const& entry : *zero_entries)
      zero_synthetic_results += entry.type == ava::session::EntryType::ToolResult &&
                                ava::core::json::string_field(entry.data_json, "call_id").value_or("") == "zero-result-call" &&
                                entry.data_json.find("execution_outcome_unknown") != std::string::npos;
  auto zero_validation = zero_entries ? ava::session::validate_session_replay(*zero_entries) : ava::session::SessionReplayValidation{};
  expect(zero_result_reopened && zero_entries && zero_synthetic_results == 1 && zero_validation.ok(),
         "resume closes a committed v4 function with zero prior results without re-executing it");

  auto invalid_seed = ava::app::open_runtime_session(options);
  expect(invalid_seed.has_value(), "invalid exact-result reconciliation fixture opens a third session");
  if (!invalid_seed)
    return;
  auto const invalid_session_id = invalid_seed->store.session_id();
  auto const invalid_session_path = invalid_seed->store.session_path();
  auto invalid_committed = invalid_seed->append_owned(function("invalid-window-function", "invalid-window-call", 0));
  invalid_committed = invalid_committed ? invalid_seed->append_owned(ava::session::SessionEntry{.id = "invalid-window-commit",
                                                                                                .parent_id = "",
                                                                                                .type = ava::session::EntryType::AssistantTurnCommit,
                                                                                                .timestamp = ava::session::now_timestamp(),
                                                                                                .data_json = zero_commit_data.value_or("{}")})
                                        : std::move(invalid_committed);
  invalid_committed = invalid_committed ? invalid_seed->append_owned(ava::session::SessionEntry{.id = "invalid-window-user",
                                                                                                .parent_id = "",
                                                                                                .type = ava::session::EntryType::UserMessage,
                                                                                                .timestamp = ava::session::now_timestamp(),
                                                                                                .data_json = "{\"text\":\"later input\"}"})
                                        : std::move(invalid_committed);
  auto invalid_result = invalid_committed
                            ? ava::agent::append_tool_result(invalid_seed->owner_append_route(),
                                                             {.call_id = "invalid-window-call", .name = "read_file", .success = true, .result_text = "late"},
                                                             "invalid-window-function")
                            : ava::core::VoidResult(std::unexpected(invalid_committed.error()));
  auto const bytes_before_invalid_resume = app_read_binary_file(invalid_session_path);
  invalid_seed = std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "release invalid reconciliation fixture before resume"));
  auto invalid_resume = options;
  invalid_resume.requested_session_id = invalid_session_id;
  invalid_resume.exact_session_id = true;
  auto invalid_reopened = ava::app::open_runtime_session(invalid_resume);
  expect(invalid_committed && invalid_result && !invalid_reopened && app_read_binary_file(invalid_session_path) == bytes_before_invalid_resume,
         "runtime reconciliation rejects an out-of-window exact v4 result before appending any synthetic result");
}

void test_app_runtime_cli_prompt_overrides()
{
  auto const root = create_empty_root("app-runtime-cli-prompt-overrides");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  write_app_test_file(workspace / "AGENTS.md", "workspace context should remain after cli prompt override\n");
  write_app_test_file(paths.prompts_dir / "openai" / "gpt-5.5" / "plan.txt", "provider prompt override should be replaced\n");
  write_app_test_file(paths.ava_config_dir / "SYSTEM.md", "global system prompt should be replaced\n");
  write_app_test_file(paths.ava_config_dir / "APPEND_SYSTEM.md", "global append prompt should be replaced\n");
  write_app_test_file(workspace / ".ava" / "SYSTEM.md", "project system prompt should be replaced\n");
  write_app_test_file(workspace / ".ava" / "APPEND_SYSTEM.md", "project append prompt should be replaced\n");
  auto trusted = ava::app::set_project_trust_decision(paths, workspace, true);
  expect(trusted.has_value(),
         trusted ? "cli prompt override test trusts project resources" : "cli prompt override test trusts project resources: " + trusted.error().format());

  ava::app::runtime::OpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Plan;
  open_options.paths = paths;
  open_options.prompt_overrides.system_prompt = "cli system prompt";
  open_options.prompt_overrides.append_system_prompts = {"cli append prompt one", "cli append prompt two"};
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "runtime session opens with cli prompt overrides");
  if (!session)
    return;

  expect(session->base_prompt.from_override && !session->base_prompt.source_path &&
             session->base_prompt.byte_count == std::string_view("cli system prompt").size() && session->base_prompt.content_fingerprint != 0,
         "cli --system-prompt is recorded as base prompt metadata");
  expect(session->prompt_overrides.system_prompt && *session->prompt_overrides.system_prompt == "cli system prompt" &&
             session->prompt_overrides.append_system_prompts.size() == 2,
         "runtime session retains cli prompt overrides for reloads");
  expect(session->system_prompt.find("cli system prompt") != std::string::npos && session->system_prompt.find("cli append prompt one") != std::string::npos &&
             session->system_prompt.find("cli append prompt two") != std::string::npos &&
             session->system_prompt.find("workspace context should remain") != std::string::npos &&
             session->system_prompt.find("provider prompt override should be replaced") == std::string::npos &&
             session->system_prompt.find("global system prompt should be replaced") == std::string::npos &&
             session->system_prompt.find("project system prompt should be replaced") == std::string::npos &&
             session->system_prompt.find("global append prompt should be replaced") == std::string::npos &&
             session->system_prompt.find("project append prompt should be replaced") == std::string::npos,
         "cli prompt overrides replace selected system/append prompt resources while preserving context");

  auto const system_source_count =
      std::ranges::count_if(session->freshness_sources, [](auto const& source) { return source.kind == ava::app::runtime::FreshnessSourceKind::SystemPrompt; });
  auto const append_source_count = std::ranges::count_if(
      session->freshness_sources, [](auto const& source) { return source.kind == ava::app::runtime::FreshnessSourceKind::AppendSystemPrompt; });
  expect(system_source_count == 1 && append_source_count == 2, "cli prompt overrides are tracked as system prompt freshness sources");

  auto context = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/context --system-prompt"});
  expect(context && context->handled && !context->output.empty() && context->output[0].find("system_prompt_sources=3") != std::string::npos &&
             context->output[0].find("system_prompt  cli  --system-prompt  <inline>") != std::string::npos &&
             context->output[0].find("status=inline") != std::string::npos && context->output[0].find("SYSTEM.md") == std::string::npos,
         "context freshness reports cli system prompt overrides as inline sources");
  auto append_context = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/context --append-system-prompt"});
  expect(append_context && append_context->handled && !append_context->output.empty() &&
             append_context->output[0].find("append_system_prompt  cli  --append-system-prompt/1  <inline>") != std::string::npos &&
             append_context->output[0].find("append_system_prompt  cli  --append-system-prompt/2  <inline>") != std::string::npos &&
             append_context->output[0].find("APPEND_SYSTEM.md") == std::string::npos,
         "context freshness reports repeated cli append prompt overrides as inline sources");

  auto switched_mode = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/mode"});
  expect(switched_mode && switched_mode->handled && session->mode == ava::agent::Mode::Build &&
             session->system_prompt.find("cli system prompt") != std::string::npos &&
             session->system_prompt.find("cli append prompt two") != std::string::npos &&
             session->system_prompt.find("Implement changes directly") == std::string::npos,
         "mode reloads preserve cli system prompt overrides");
}

}  // namespace ava::tests::app_runtime_tests
