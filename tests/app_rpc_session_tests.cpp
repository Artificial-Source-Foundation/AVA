#include "sys.h"
#include "tests/app_rpc_test_cases.h"
#include "tests/support/app_runtime_support.h"
#include "tests/support/fake_transport.h"
#include "tests/support/test_harness.h"
#include "ava/app/rpc_mode.h"
#include "ava/app/runtime.h"
#include "ava/app/runtime/Session.h"
#include "ava/agent/background_job_registry.h"
#include "ava/agent/subagent_job.h"
#include "ava/session/attachments.h"
#include "ava/session/record.h"
#include "ava/session/session_metadata.h"
#include "ava/session/session_store.h"
#include "ava/session/validation.h"
#include "ava/provider/openai_provider.h"
#include "ava/core/error.h"
#include "ava/core/result.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <istream>
#include <memory>
#include <mutex>
#include <optional>
#include <ostream>
#include <sstream>
#include <stop_token>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace ava::tests::app_rpc_test {

void test_app_rpc_malformed_line_recovery_and_unknown_command()
{
  auto const root = create_empty_root("app-rpc-recovery");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::runtime::RuntimeOpenContext open_context;
  open_context.workspace_dir = workspace;
  open_context.current_dir = workspace;
  open_context.paths = paths;
  auto session = ava::app::runtime::Session::open_runtime_session(open_context);
  expect(session.has_value(), "RPC recovery test opens runtime session");
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  std::istringstream in(
      "not json\n{\"id\":\"s1\",\"type\":\"get_state\"}\n"
      "{\"id\":\"u1\",\"type\":\"unknown\"}\n");
  std::ostringstream out;
  ava::app::runtime::session_ts unlocked_session(std::move(*session));
  auto result =
      ava::app::run_rpc_loop(unlocked_session, open_context, provider, transport, ava::app::runtime::RunOptions{}, in, out, ava::app::rpc::RpcInputWake{});
  auto const jsonl = out.str();
  expect(result.has_value(), "RPC loop continues after malformed and unknown commands");
  expect(std::count(jsonl.begin(), jsonl.end(), '\n') == 3 && jsonl.find("\"id\":\"\"") != std::string::npos &&
             jsonl.find("malformed RPC JSON object") != std::string::npos && jsonl.find("\"id\":\"s1\"") != std::string::npos &&
             jsonl.find("\"session_id\":\"") != std::string::npos && jsonl.find("\"id\":\"u1\"") != std::string::npos &&
             jsonl.find("unknown RPC command type") != std::string::npos,
         "RPC loop writes error responses and recovers for subsequent JSONL records");
}

void test_app_rpc_state_list_sessions_and_open_session()
{
  auto const root = create_empty_root("app-rpc-state");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::runtime::RuntimeOpenContext open_context;
  open_context.workspace_dir = workspace;
  open_context.current_dir = workspace;
  open_context.paths = paths;
  auto first = ava::app::runtime::Session::open_runtime_session(open_context);
  auto second = ava::app::runtime::Session::open_runtime_session(open_context);
  expect(first.has_value() && second.has_value(), "RPC state test opens multiple sessions");
  if (!first || !second)
    return;
  auto const first_id = first->store.session_id();
  auto const second_id = second->store.session_id();
  first = std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "release target runtime before RPC switch"));

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  std::istringstream in(
      "{\"id\":\"state\",\"type\":\"get_state\"}\n"
      "{\"id\":\"list\",\"type\":\"list_sessions\"}\n"
      "{\"id\":\"open\",\"type\":\"open_session\",\"session_id\":\"" +
      first_id + "\"}\n");
  std::ostringstream out;
  ava::app::runtime::session_ts unlocked_second(std::move(*second));
  auto result =
      ava::app::run_rpc_loop(unlocked_second, open_context, provider, transport, ava::app::runtime::RunOptions{}, in, out, ava::app::rpc::RpcInputWake{});
  auto const jsonl = out.str();
  expect(result.has_value(), "RPC state/list/open loop completes successfully");
  expect(jsonl.find("\"id\":\"state\"") != std::string::npos && jsonl.find(second_id) != std::string::npos &&
             jsonl.find("\"id\":\"list\"") != std::string::npos && jsonl.find(first_id) != std::string::npos &&
             jsonl.find("\"id\":\"open\"") != std::string::npos,
         "RPC state, list_sessions, and open_session return session metadata");
  expect(ava::app::runtime::session_ts::rat(unlocked_second)->store.session_id() == first_id, "RPC open_session switches the active runtime session");
}

void test_app_rpc_job_controls_are_active_safe_and_redacted()
{
  auto const root = temp_root() / "app-rpc-job-controls";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  ava::app::runtime::RuntimeOpenContext open_context;
  open_context.workspace_dir = workspace;
  open_context.current_dir = workspace;
  open_context.paths = paths;
  auto session = ava::app::runtime::Session::open_runtime_session(open_context);
  expect(session.has_value(), "RPC job fixture opens runtime session");
  if (!session || !session->subagent_coordinator())
    return;

  struct WorkerState
  {
    std::mutex mutex;
    std::condition_variable changed;
    bool started = false;
    bool release = false;
    ava::agent::BackgroundJobCompletion run(ava::agent::BackgroundJobContext const& context)
    {
      std::stop_callback wake(context.stop_token, [&] { changed.notify_all(); });
      std::unique_lock lock(mutex);
      started = true;
      changed.notify_all();
      changed.wait(lock, [&] { return release || context.stop_token.stop_requested(); });
      if (context.stop_token.stop_requested())
        return {.state = ava::agent::BackgroundJobState::Canceled,
                .final_text = {},
                .stop_reason = "canceled",
                .error = std::nullopt,
                .provider_iterations = 0,
                .tool_calls = 0,
                .tool_iterations = 0};
      return {.state = ava::agent::BackgroundJobState::Completed,
              .final_text = "bounded RPC result",
              .stop_reason = "completed",
              .error = std::nullopt,
              .provider_iterations = 0,
              .tool_calls = 0,
              .tool_iterations = 0};
    }
    bool wait_started()
    {
      std::unique_lock lock(mutex);
      return changed.wait_for(lock, std::chrono::seconds(1), [&] { return started; });
    }
    void finish()
    {
      std::lock_guard lock(mutex);
      release = true;
      changed.notify_all();
    }
  };

  auto coordinator = session->subagent_coordinator();
  auto const owner = session->store.session_id();
  auto promoted_state = std::make_shared<WorkerState>();
  auto canceled_state = std::make_shared<WorkerState>();
  auto promotable = coordinator->start(owner, ava::agent::SubagentJobMode::Foreground, {.child_session_id = "rpc_promotable"},
                                       [promoted_state](auto const& context) { return promoted_state->run(context); });
  auto cancelable = coordinator->start(owner, ava::agent::SubagentJobMode::Foreground, {.child_session_id = "rpc_cancelable"},
                                       [canceled_state](auto const& context) { return canceled_state->run(context); });
  auto failed = coordinator->start(owner, ava::agent::SubagentJobMode::Foreground, {.child_session_id = "rpc_failed"}, [](auto const&) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Provider, "credential=rpc-secret raw provider body");
    error.with_context("command", "curl --token rpc-secret");
    return ava::agent::BackgroundJobCompletion{.state = ava::agent::BackgroundJobState::Failed,
                                               .final_text = {},
                                               .stop_reason = "failed",
                                               .error = std::move(error),
                                               .provider_iterations = 0,
                                               .tool_calls = 0,
                                               .tool_iterations = 0};
  });
  expect(promotable && cancelable && failed && promoted_state->wait_started() && canceled_state->wait_started(), "RPC job fixture starts controlled jobs");
  if (!promotable || !cancelable || !failed)
    return;
  static_cast<void>(coordinator->wait(owner, failed->job.identity.job_id, std::chrono::seconds(1)));

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  BlockingResponseTransport transport(sse_response(final_text_sse("parent terminal")));
  ava::app::runtime::RunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult rpc_result;
  ava::app::runtime::session_ts unlocked_session(std::move(*session));
  std::jthread rpc_thread([&] {
    rpc_result = ava::app::run_rpc_loop(unlocked_session, open_context, provider, transport, runtime_options, in, out, [&] noexcept { input_buffer.close(); });
  });
  input_buffer.push("{\"id\":\"prompt\",\"type\":\"prompt\",\"message\":\"keep active\"}\n");
  expect(transport.wait_for_request(std::chrono::seconds(2)), "RPC job fixture has an active parent prompt");
  auto const promotable_id = promotable->job.identity.job_id;
  auto const cancelable_id = cancelable->job.identity.job_id;
  auto const failed_id = failed->job.identity.job_id;
  input_buffer.push("{\"id\":\"jobs\",\"type\":\"list_jobs\"}\n");
  input_buffer.push("{\"id\":\"status\",\"type\":\"get_job\",\"job_id\":\"" + promotable_id + "\"}\n");
  input_buffer.push("{\"id\":\"wait\",\"type\":\"wait_job\",\"job_id\":\"" + promotable_id + "\",\"timeout_ms\":1}\n");
  input_buffer.push("{\"id\":\"not-ready\",\"type\":\"get_job_result\",\"job_id\":\"" + promotable_id + "\"}\n");
  input_buffer.push("{\"id\":\"promote\",\"type\":\"promote_job\",\"job_id\":\"" + promotable_id + "\"}\n");
  input_buffer.push("{\"id\":\"cancel-job\",\"type\":\"cancel_job\",\"job_id\":\"" + cancelable_id + "\"}\n");
  input_buffer.push("{\"id\":\"failed-result\",\"type\":\"get_job_result\",\"job_id\":\"" + failed_id + "\"}\n");
  bool const controls_completed = output_buffer.wait_contains("\"id\":\"failed-result\"", std::chrono::seconds(2));
  auto active_output = output_buffer.str();
  expect(controls_completed && active_output.find("\"id\":\"promote\"") != std::string::npos &&
             active_output.find("\"was_promoted\":true") != std::string::npos && active_output.find("\"id\":\"cancel-job\"") != std::string::npos &&
             active_output.find("\"timed_out\":true") != std::string::npos && active_output.find("\"code\":\"job_not_ready\"") != std::string::npos &&
             active_output.find("\"message\":\"subagent job failed\"") != std::string::npos && active_output.find("rpc-secret") == std::string::npos &&
             active_output.find("raw provider body") == std::string::npos && active_output.find("\"code\":\"active_run\"") == std::string::npos,
         "RPC job status, wait, result, cancel, and promote remain active-safe and redact provider/error context");

  promoted_state->finish();
  static_cast<void>(coordinator->wait(owner, promotable_id, std::chrono::seconds(1)));
  transport.release();
  input_buffer.close();
  rpc_thread.join();
  expect(rpc_result.has_value(), "RPC job active-safe loop shuts down cleanly");
}

void test_app_rpc_current_session_reads_reject_path_replacement()
{
  auto const root = create_empty_root("app-rpc-current-session-replacement");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::runtime::RuntimeOpenContext open_context;
  open_context.workspace_dir = workspace;
  open_context.current_dir = workspace;
  open_context.paths = paths;
  auto session = ava::app::runtime::Session::open_runtime_session(open_context);
  expect(session.has_value(), "replacement-safe current-session RPC test opens runtime session");
  if (!session)
    return;
  expect(session
             ->append_owned(ava::session::SessionEntry{.id = "original_rpc_history",
                                                       .parent_id = "",
                                                       .type = ava::session::EntryType::UserMessage,
                                                       .timestamp = "2026-05-02T00:00:00Z",
                                                       .data_json = "{\"text\":\"ORIGINAL_RPC_HISTORY\"}"})
             .has_value(),
         "replacement-safe current-session RPC test seeds original history");

  auto replacement = ava::session::serialize_session_entry_line(ava::session::SessionEntry{.id = "replacement_rpc_history",
                                                                                           .parent_id = "",
                                                                                           .type = ava::session::EntryType::UserMessage,
                                                                                           .timestamp = "2026-05-02T00:00:01Z",
                                                                                           .data_json = "{\"text\":\"RPC_REPLACEMENT_CANARY\"}"});
  expect(replacement.has_value(), "replacement-safe current-session RPC test serializes replacement history");
  if (!replacement)
    return;
  bool replaced = false;
  auto const session_path = session->store.session_path();
  session->store.set_after_lease_bound_read_for_test([&, session_path] {
    if (replaced)
      return;
    replaced = true;
    std::filesystem::rename(session_path, session_path.string() + ".parked");
    std::ofstream file(session_path, std::ios::binary | std::ios::trunc);
    file << *replacement << '\n';
  });

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  std::istringstream in(
      "{\"id\":\"messages\",\"type\":\"get_messages\"}\n"
      "{\"id\":\"metadata\",\"type\":\"session_metadata\"}\n"
      "{\"id\":\"stats\",\"type\":\"get_session_stats\"}\n"
      "{\"id\":\"validate\",\"type\":\"validate_session\"}\n");
  std::ostringstream out;
  ava::app::runtime::session_ts unlocked_session(std::move(*session));
  auto result =
      ava::app::run_rpc_loop(unlocked_session, open_context, provider, transport, ava::app::runtime::RunOptions{}, in, out, ava::app::rpc::RpcInputWake{});
  auto const jsonl = out.str();
  auto pathname_entries = ava::app::runtime::session_ts::rat(unlocked_session)->store.load();
  expect(result && replaced && jsonl.find("replaced") != std::string::npos && jsonl.find("RPC_REPLACEMENT_CANARY") == std::string::npos && pathname_entries &&
             pathname_entries->size() == 1 && pathname_entries->front().data_json.find("RPC_REPLACEMENT_CANARY") != std::string::npos,
         "current-session RPC messages, metadata, stats, and validation fail closed after authority binding without serializing replacement content");
}

void test_app_rpc_session_metadata_name_and_labels()
{
  auto const root = create_empty_root("app-rpc-session-metadata");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::runtime::RuntimeOpenContext open_context;
  open_context.workspace_dir = workspace;
  open_context.current_dir = workspace;
  open_context.paths = paths;
  auto session = ava::app::runtime::Session::open_runtime_session(open_context);
  expect(session.has_value(), "RPC session metadata test opens runtime session");
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  std::istringstream in(
      "{\"id\":\"initial\",\"type\":\"session_metadata\"}\n"
      "{\"id\":\"name\",\"type\":\"set_session_name\",\"session_name\":\"Auth follow-up\"}\n"
      "{\"id\":\"labels\",\"type\":\"set_session_labels\",\"labels\":[\"auth\",\"bug\"]}\n"
      "{\"id\":\"after\",\"type\":\"session_metadata\"}\n"
      "{\"id\":\"bad\",\"type\":\"set_session_labels\",\"labels\":[\"dup\",\"dup\"]}\n");
  std::ostringstream out;
  ava::app::runtime::session_ts unlocked_session(std::move(*session));
  auto result =
      ava::app::run_rpc_loop(unlocked_session, open_context, provider, transport, ava::app::runtime::RunOptions{}, in, out, ava::app::rpc::RpcInputWake{});
  auto const jsonl = out.str();
  auto metadata = ava::session::load_session_metadata(ava::app::runtime::session_ts::rat(unlocked_session)->store);
  auto entries = ava::app::runtime::session_ts::rat(unlocked_session)->store.load();
  auto validation = entries ? ava::session::validate_session_replay(*entries) : ava::session::SessionReplayValidation{};

  expect(result.has_value() && metadata && metadata->name == "Auth follow-up" && metadata->labels.size() == 2 && metadata->labels[0] == "auth" &&
             metadata->labels[1] == "bug" && metadata->actor == "rpc" && validation.ok(),
         "RPC session metadata commands persist append-only name and label entries");
  expect(jsonl.find("\"id\":\"initial\"") != std::string::npos && jsonl.find("\"name\":\"\"") != std::string::npos &&
             jsonl.find("\"id\":\"name\"") != std::string::npos && jsonl.find("\"name\":\"Auth follow-up\"") != std::string::npos &&
             jsonl.find("\"labels\":[\"auth\",\"bug\"]") != std::string::npos && jsonl.find("\"actor\":\"rpc\"") != std::string::npos &&
             jsonl.find("session labels must be unique") != std::string::npos,
         "RPC session metadata responses expose current name/labels/actor and reject invalid labels");
}

void test_app_rpc_session_tree_command_and_switch_navigation()
{
  auto const root = create_empty_root("app-rpc-session-tree");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::runtime::RuntimeOpenContext open_context;
  open_context.workspace_dir = workspace;
  open_context.current_dir = workspace;
  open_context.paths = paths;
  auto parent = ava::app::runtime::Session::open_runtime_session(open_context);
  auto child = ava::app::runtime::Session::open_runtime_session(open_context);
  expect(parent.has_value() && child.has_value(), "RPC session_tree test opens parent and child sessions");
  if (!parent || !child)
    return;
  auto parent_entries = parent->store.load();
  expect(parent_entries && !parent_entries->empty(), "RPC session_tree test loads parent start entry");
  if (!parent_entries || parent_entries->empty())
    return;

  ava::session::SessionMetadataUpdate parent_metadata;
  parent_metadata.name = "Parent";
  parent_metadata.labels = std::vector<std::string>{"root"};
  parent_metadata.branch_origin = "root";
  parent_metadata.actor = "test";
  auto const parent_id = parent->store.session_id();
  auto parent_meta = parent->append_runtime_session_metadata(std::move(parent_metadata));

  ava::session::SessionMetadataUpdate child_metadata;
  child_metadata.name = "Child";
  child_metadata.labels = std::vector<std::string>{"branch"};
  child_metadata.archived = true;
  child_metadata.parent_session_id = parent_id;
  child_metadata.source_session_id = parent_id;
  child_metadata.branch_from_entry_id = parent_entries->front().id;
  child_metadata.branch_origin = "fork";
  child_metadata.actor = "test";
  auto const child_id = child->store.session_id();
  auto child_meta = child->append_runtime_session_metadata(std::move(child_metadata));
  expect(parent_meta && child_meta, "RPC session_tree test persists branch metadata");
  parent = std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "release parent runtime before RPC switch"));

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  auto const requests = std::string("{\"id\":\"tree\",\"type\":\"session_tree\"}\n") + "{\"id\":\"switch\",\"type\":\"switch_session\",\"session_id\":\"" +
                        parent_id + "\"}\n" + "{\"id\":\"tree2\",\"type\":\"session_tree\"}\n";
  std::istringstream in(requests);
  std::ostringstream out;
  ava::app::runtime::session_ts unlocked_child(std::move(*child));
  auto result =
      ava::app::run_rpc_loop(unlocked_child, open_context, provider, transport, ava::app::runtime::RunOptions{}, in, out, ava::app::rpc::RpcInputWake{});
  auto const jsonl = out.str();
  expect(result.has_value(), "RPC session_tree loop completes successfully");
  expect(jsonl.find("\"id\":\"tree\"") != std::string::npos && jsonl.find("\"current_session_id\":\"" + child_id + "\"") != std::string::npos &&
             jsonl.find("\"path\":[\"" + parent_id + "\",\"" + child_id + "\"]") != std::string::npos &&
             jsonl.find("\"parent_session_id\":\"" + parent_id + "\"") != std::string::npos &&
             jsonl.find("\"children\":[\"" + child_id + "\"]") != std::string::npos && jsonl.find("\"labels\":[\"branch\"]") != std::string::npos &&
             jsonl.find("\"labels_updated\":\"") != std::string::npos && jsonl.find("\"archived\":true") != std::string::npos &&
             jsonl.find("\"actor\":\"test\"") != std::string::npos,
         "RPC session_tree returns current path, children, labels, archive state, actor, and provenance metadata");
  expect(jsonl.find("\"id\":\"switch\"") != std::string::npos && jsonl.find("\"current_session_id\":\"" + parent_id + "\"") != std::string::npos &&
             ava::app::runtime::session_ts::rat(unlocked_child)->store.session_id() == parent_id,
         "RPC switch_session navigates the active session used by following tree calls");
}

void test_app_rpc_session_fork_and_clone_commands()
{
  auto const root = create_empty_root("app-rpc-session-branch");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::runtime::RuntimeOpenContext open_context;
  open_context.workspace_dir = workspace;
  open_context.current_dir = workspace;
  open_context.paths = paths;
  auto session = ava::app::runtime::Session::open_runtime_session(open_context);
  expect(session.has_value(), "RPC session branch test opens runtime session");
  if (!session)
    return;
  auto const source_id = session->store.session_id();
  auto source_entries = session->store.load();
  expect(source_entries && !source_entries->empty(), "RPC session branch test loads source start entry");
  if (!source_entries || source_entries->empty())
    return;
  auto const branch_from_entry_id = source_entries->front().id;
  auto const source_count = source_entries->size();
  auto const valid_source_bytes = app_read_binary_file(session->store.session_path());
  {
    std::ofstream file(session->store.session_path(), std::ios::binary | std::ios::app);
    file << "{\"version\":3,\"id\":\"rpc-current-torn";
  }

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  auto const requests = std::string(
                            "{\"id\":\"fork\",\"type\":\"fork_session\","
                            "\"branch_from_entry_id\":\"") +
                        branch_from_entry_id +
                        "\",\"session_name\":\"Forked\",\"labels\":[\"forked\"]}\n"
                        "{\"id\":\"fork_meta\",\"type\":\"session_metadata\"}\n"
                        "{\"id\":\"clone\",\"type\":\"clone_session\",\"session_id\":\"" +
                        source_id +
                        "\",\"session_name\":\"Cloned\"}\n"
                        "{\"id\":\"clone_meta\",\"type\":\"session_metadata\"}\n";
  std::istringstream in(requests);
  std::ostringstream out;
  ava::app::runtime::session_ts unlocked_session(std::move(*session));
  auto result =
      ava::app::run_rpc_loop(unlocked_session, open_context, provider, transport, ava::app::runtime::RunOptions{}, in, out, ava::app::rpc::RpcInputWake{});
  auto const jsonl = out.str();
  auto source_store = ava::session::SessionStore::open(workspace, source_id, paths.sessions_dir);
  bool source_unchanged = false;
  if (source_store)
  {
    auto source_after = source_store->load();
    source_unchanged = source_after && source_after->size() == source_count && app_read_binary_file(source_store->session_path()) == valid_source_bytes;
  }
  expect(result.has_value(), "RPC fork/clone loop completes successfully");
  expect(source_unchanged, "RPC fork uses its existing lease to recover the current source and later branching does not add source entries");
  expect(jsonl.find("\"id\":\"fork\"") != std::string::npos && jsonl.find("\"id\":\"fork_meta\"") != std::string::npos &&
             jsonl.find("\"created\":true") != std::string::npos && jsonl.find("\"name\":\"Forked\"") != std::string::npos &&
             jsonl.find("\"labels\":[\"forked\"]") != std::string::npos && jsonl.find("\"parent_session_id\":\"" + source_id + "\"") != std::string::npos &&
             jsonl.find("\"branch_from_entry_id\":\"" + branch_from_entry_id + "\"") != std::string::npos &&
             jsonl.find("\"branch_origin\":\"fork\"") != std::string::npos,
         "RPC fork_session creates and switches to a fork with provenance metadata");
  expect(jsonl.find("\"id\":\"clone\"") != std::string::npos && jsonl.find("\"id\":\"clone_meta\"") != std::string::npos &&
             jsonl.find("\"name\":\"Cloned\"") != std::string::npos && jsonl.find("\"branch_origin\":\"clone\"") != std::string::npos,
         "RPC clone_session creates and switches to a clone with provenance metadata");
  auto active_destination_contender = ava::session::SessionLease::acquire(ava::app::runtime::session_ts::rat(unlocked_session)->store.session_path());
  expect(!active_destination_contender && active_destination_contender.error().message().find("already owned") != std::string::npos,
         "RPC fork/clone transfers the active destination lease directly into the replacement runtime");
}

void test_app_rpc_branch_construction_failure_rolls_back_created_file()
{
  auto const root = create_empty_root("app-rpc-branch-rollback");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::runtime::RuntimeOpenContext open_context;
  open_context.workspace_dir = workspace;
  open_context.current_dir = workspace;
  open_context.paths = paths;
  auto source = ava::app::runtime::Session::open_runtime_session(open_context);
  expect(source.has_value(), "RPC rollback test opens an active source session");
  if (!source)
    return;
  auto entries = source->store.load();
  expect(entries && !entries->empty(), "RPC rollback test loads the source start entry");
  if (!entries || entries->empty())
    return;
  auto appended =
      source->append_owned(ava::session::SessionEntry{.id = "entry_rpc_rollback_attachment",
                                                      .parent_id = entries->back().id,
                                                      .type = ava::session::EntryType::UserMessage,
                                                      .timestamp = "2026-07-16T00:00:00Z",
                                                      .data_json = "{\"text\":\"attachment\",\"attachments\":[{\"id\":\"rpc_rollback_img\","
                                                                   "\"type\":\"image\",\"mime_type\":\"image/png\",\"byte_size\":5,"
                                                                   "\"sha256\":\"2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824\","
                                                                   "\"storage_path\":\"attachments/rollback.txt\"}]}",
                                                      .version = 2});
  auto const source_attachment = ava::session::attachment_storage_root(source->store) / "attachments" / "rollback.txt";
  write_app_test_file(source_attachment, "hello");
  expect(appended.has_value(), "RPC rollback test appends a copyable source attachment reference");
  if (!appended)
    return;

  auto const source_id = source->store.session_id();
  auto const source_path = source->store.session_path();
  std::filesystem::create_directories(paths.models_file);
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  std::istringstream in("{\"id\":\"fork-rollback\",\"type\":\"fork_session\"}\n");
  std::ostringstream out;
  ava::app::runtime::session_ts unlocked_source(std::move(*source));
  auto result =
      ava::app::run_rpc_loop(unlocked_source, open_context, provider, transport, ava::app::runtime::RunOptions{}, in, out, ava::app::rpc::RpcInputWake{});
  auto const jsonl = out.str();
  auto const marker = std::string("created_session_id: ");
  auto const marker_offset = jsonl.find(marker);
  std::optional<std::string> created_id;
  if (marker_offset != std::string::npos)
  {
    auto const value_start = marker_offset + marker.size();
    auto const value_end = jsonl.find("\\n", value_start);
    created_id = jsonl.substr(value_start, value_end == std::string::npos ? std::string::npos : value_end - value_start);
  }
  bool destination_jsonl_removed = false;
  bool destination_attachment_retained = false;
  if (created_id)
  {
    auto destination =
        ava::session::SessionStore(ava::session::SessionStoreOptions{.root_dir = paths.sessions_dir, .workspace_dir = workspace, .session_id = *created_id});
    auto const destination_attachment = ava::session::attachment_storage_root(destination) / "attachments" / "rollback.txt";
    destination_jsonl_removed = !std::filesystem::exists(destination.session_path());
    destination_attachment_retained = app_read_binary_file(destination_attachment) == "hello";
  }
  auto source_contender = ava::session::SessionLease::acquire(source_path);
  expect(result && created_id && jsonl.find("\"id\":\"fork-rollback\"") != std::string::npos && jsonl.find("\"success\":false") != std::string::npos &&
             jsonl.find("rollback_attachment_disposition: preserved") != std::string::npos && destination_jsonl_removed && destination_attachment_retained &&
             ava::app::runtime::session_ts::rat(unlocked_source)->store.session_id() == source_id && !source_contender &&
             source_contender.error().message().find("already owned") != std::string::npos,
         "RPC branch runtime-construction failure preserves the primary error, removes only destination JSONL, retains copied attachments, and leaves the "
         "source active");
}

void test_app_rpc_noncurrent_branch_source_recovers_torn_tail()
{
  auto const root = create_empty_root("app-rpc-noncurrent-torn-branch");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::runtime::RuntimeOpenContext open_context;
  open_context.workspace_dir = workspace;
  open_context.current_dir = workspace;
  open_context.paths = paths;
  auto source = ava::app::runtime::Session::open_runtime_session(open_context);
  expect(source.has_value(), "RPC noncurrent torn branch test opens source runtime");
  if (!source)
    return;
  auto const source_id = source->store.session_id();
  auto const source_path = source->store.session_path();
  auto const valid_source_bytes = app_read_binary_file(source_path);
  source = std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "release source runtime before RPC branch"));

  auto current = ava::app::runtime::Session::open_runtime_session(open_context);
  expect(current.has_value(), "RPC noncurrent torn branch test opens a different current runtime");
  if (!current)
    return;
  {
    std::ofstream file(source_path, std::ios::binary | std::ios::app);
    file << "{\"version\":3,\"id\":\"rpc-torn";
  }

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  std::istringstream in("{\"id\":\"clone\",\"type\":\"clone_session\",\"session_id\":\"" + source_id + "\"}\n");
  std::ostringstream out;
  ava::app::runtime::session_ts unlocked_current(std::move(*current));
  auto result =
      ava::app::run_rpc_loop(unlocked_current, open_context, provider, transport, ava::app::runtime::RunOptions{}, in, out, ava::app::rpc::RpcInputWake{});
  ava::app::runtime::session_ts::rat current_r(unlocked_current);
  auto cloned_entries = current_r->store.load();
  auto cloned_metadata = ava::session::load_session_metadata(current_r->store);
  expect(result && out.str().find("\"id\":\"clone\"") != std::string::npos && current_r->store.session_id() != source_id && cloned_entries &&
             cloned_entries->size() == 2 && cloned_metadata && cloned_metadata->source_session_id == source_id && cloned_metadata->branch_origin == "clone" &&
             app_read_binary_file(source_path) == valid_source_bytes,
         "RPC branching temporarily leases and recovers a different source before holding it through clone creation");
}

void test_app_rpc_summarize_branch_appends_to_source_session()
{
  auto const root = create_empty_root("app-rpc-branch-summary");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::runtime::RuntimeOpenContext open_context;
  open_context.workspace_dir = workspace;
  open_context.current_dir = workspace;
  open_context.paths = paths;
  auto session = ava::app::runtime::Session::open_runtime_session(open_context);
  expect(session.has_value(), "RPC summarize_branch test opens runtime session");
  if (!session)
    return;
  auto const source_id = session->store.session_id();
  auto source_entries = session->store.load();
  expect(source_entries && !source_entries->empty(), "RPC summarize_branch test loads source start entry");
  if (!source_entries || source_entries->empty())
    return;
  auto const source_count = source_entries->size();
  auto const root_entry_id = source_entries->front().id;
  auto const tip_entry_id = source_entries->back().id;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  auto const requests = std::string("{\"id\":\"summary\",\"type\":\"summarize_branch\",") + "\"branch_root_entry_id\":\"" + root_entry_id +
                        "\","
                        "\"branch_tip_entry_id\":\"" +
                        tip_entry_id +
                        "\","
                        "\"summary\":\"Abandoned path was not needed.\","
                        "\"provider\":\"openai\",\"model\":\"gpt-test\",\"reason\":\"test\"}\n"
                        "{\"id\":\"stats\",\"type\":\"get_session_stats\"}\n";
  std::istringstream in(requests);
  std::ostringstream out;
  ava::core::VoidResult latched;
  ava::app::runtime::session_ts unlocked_session(std::move(*session));
  {
    auto result =
        ava::app::run_rpc_loop(unlocked_session, open_context, provider, transport, ava::app::runtime::RunOptions{}, in, out, ava::app::rpc::RpcInputWake{});
    ava::app::runtime::session_ts::rat session_r(unlocked_session);
    auto const jsonl = out.str();
    auto source_store = ava::session::SessionStore::open(workspace, source_id, paths.sessions_dir);
    bool source_has_summary = false;
    if (source_store)
    {
      auto source_after = source_store->load();
      source_has_summary = source_after && source_after->size() == source_count + 1 && source_after->back().type == ava::session::EntryType::BranchSummary &&
                           source_after->back().parent_id == tip_entry_id;
    }
    expect(result.has_value(), "RPC summarize_branch loop completes successfully");
    expect(source_has_summary && session_r->store.session_id() == source_id, "RPC summarize_branch appends to the source session without switching sessions");
    expect(jsonl.find("\"id\":\"summary\"") != std::string::npos && jsonl.find("\"source_session_id\":\"" + source_id + "\"") != std::string::npos &&
               jsonl.find("\"type\":\"branch_summary\"") != std::string::npos && jsonl.find("Abandoned path was not needed.") != std::string::npos &&
               jsonl.find("\"branch_summary\":1") != std::string::npos,
           "RPC summarize_branch returns the persisted branch summary entry and updated stats expose the count");

    auto invalid = ava::session::SessionEntry{
        .id = "summary-route-latch", .parent_id = "", .type = ava::session::EntryType::Error, .timestamp = ava::session::now_timestamp(), .data_json = ""};
    latched = session_r->run_controller()
                  ? session_r->run_controller()->append(std::move(invalid))
                  : ava::core::VoidResult(std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "missing summary route controller")));
  }
  std::istringstream blocked_in(std::string("{\"id\":\"blocked-summary\",\"type\":\"summarize_branch\",") + "\"branch_root_entry_id\":\"" + root_entry_id +
                                "\",\"branch_tip_entry_id\":\"" + tip_entry_id +
                                "\",\"summary\":\"must not bypass latch\",\"provider\":\"openai\",\"model\":\"gpt-test\",\"reason\":\"test\"}\n");
  std::ostringstream blocked_out;
  auto blocked_loop = ava::app::run_rpc_loop(unlocked_session, open_context, provider, transport, ava::app::runtime::RunOptions{}, blocked_in, blocked_out,
                                             ava::app::rpc::RpcInputWake{});
  ava::app::runtime::session_ts::rat session_r(unlocked_session);
  auto blocked_entries = session_r->store.load();
  expect(!latched && blocked_loop && blocked_entries && blocked_entries->size() == source_count + 1 &&
             blocked_out.str().find("blocked-summary") != std::string::npos && blocked_out.str().find("append_commit_state") != std::string::npos,
         "current-session RPC summaries perform lease-bound reads but append only through the owner route and cannot bypass its persistence latch");
}

}  // namespace ava::tests::app_rpc_test
