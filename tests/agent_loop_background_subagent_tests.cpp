#include "sys.h"
#include "tests/agent_loop_test_declarations.h"
#include "tests/support/agent_loop_test_support.h"
#include "tests/support/fake_transport.h"
#include "tests/support/test_harness.h"
#include "ava/app/command_jobs.h"
#include "ava/agent/agent_loop.h"
#include "ava/session/assistant_output.h"
#include "ava/session/session_store.h"
#include "ava/provider/openai_provider.h"
#include "ava/lsp/lsp_client.h"
#include "ava/core/result.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <string>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

using agent_loop_test::BlockingBackgroundTransport;
using agent_loop_test::SharedFakeTransport;
using agent_loop_test::sse_response;
using agent_loop_test::TraceCollector;

namespace {

class NoopDiagnosticsProvider final : public ava::lsp::DiagnosticsProvider
{
 public:
  [[nodiscard]] ava::core::Result<std::vector<ava::lsp::Diagnostic>> diagnostics(std::filesystem::path const&, ava::lsp::CancelCallback = nullptr) override
  {
    return std::vector<ava::lsp::Diagnostic>{};
  }
};

}  // namespace

void test_agent_loop_background_task_starts_child_session()
{
  auto const root = create_empty_root("agent-task-background");

  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  auto const session_root = root / "sessions";
  ava::session::SessionStore store(ava::session::SessionStoreOptions{.root_dir = session_root, .workspace_dir = workspace, .session_id = "parent-bg"});
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  std::mutex session_mutex;
  auto registry = std::make_shared<ava::agent::BackgroundJobRegistry>();
  auto trace_collector = std::make_shared<TraceCollector>();
  auto observation = std::make_shared<ava::observability::RunObservation>(trace_collector);
  auto background_state = std::make_shared<BlockingBackgroundTransport::State>();
  ava::tests::FakeTransport transport({sse_response("data: {\"type\":\"response.function_call.added\",\"call_id\":\"call_task\",\"name\":\"task\"}\n\n"
                                                    "data: "
                                                    "{\"type\":\"response.function_call_arguments.delta\",\"call_id\":\"call_task\",\"delta\":\"{"
                                                    "\\\"description\\\":\\\"Explore async\\\",\\\"prompt\\\":\\\"Return background child.\\\","
                                                    "\\\"subagent_type\\\":\\\"general\\\",\\\"background\\\":true}\"}\n\n"
                                                    "data: [DONE]\n\n"),
                                       sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"queued\"}\n\n"
                                                    "data: [DONE]\n\n")});
  auto parent_append = append_route_for_test(store);
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .provider_id = "openai",
      .model_id = "gpt-5.5",
      .system_prompt = "system prompt",
      .access_token = "token",
      .permission_resolver = [](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        return ava::permissions::PermissionResolution::Allow;
      },
      .lsp_diagnostics_provider = std::make_shared<NoopDiagnosticsProvider>(),
      .background_provider_factory = []() -> ava::core::Result<std::unique_ptr<ava::provider::Provider>> {
        std::unique_ptr<ava::provider::Provider> provider = std::make_unique<ava::provider::OpenAIProvider>("https://api.example.test");
        return provider;
      },
      .background_transport_factory = [background_state]() -> ava::core::Result<std::unique_ptr<ava::provider::Transport>> {
        std::unique_ptr<ava::provider::Transport> transport = std::make_unique<BlockingBackgroundTransport>(
            background_state, sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"background child\"}\n\n"
                                           "data: [DONE]\n\n"));
        return transport;
      },
      .background_jobs = registry,
      .session_mutex = &session_mutex,
      .append_entry = parent_append,
      .append_batch = append_batch_route_for_test(store),
      .session_read_authority = read_authority_for_test(store),
      .observation = observation});

  auto result = loop.run_turn("delegate background", store, provider, transport);
  expect(result && result->final_text == "queued" && result->tool_calls == 1 && transport.requests().size() == 2,
         "agent loop starts background task and continues parent turn immediately");
  if (transport.requests().size() == 2)
  {
    expect(transport.requests()[1].body.find("\\\"state\\\":\\\"running\\\"") != std::string::npos, "parent continuation receives running task state");
    expect(transport.requests()[1].body.find("\\\"job_id\\\":\\\"job_") != std::string::npos, "parent continuation receives registry job id");
  }

  auto running_jobs = registry->snapshot();
  expect(running_jobs.size() == 1 && running_jobs.front().state == ava::agent::BackgroundJobState::Running &&
             running_jobs.front().child_session_id.starts_with("session_"),
         "background task appears as running in the registry");
  expect(background_state->wait_for_request(std::chrono::milliseconds(1000)), "background child reaches provider transport while registered");
  auto background_requests = background_state->requests_snapshot();
  bool const saw_background_request_without_lsp = !background_requests.empty() &&
                                                  background_requests.front().body.find("\"name\":\"lsp_diagnostics\"") == std::string::npos &&
                                                  background_requests.front().body.find("\"name\":\"lsp_workspace_symbols\"") == std::string::npos;

  bool foreground_resume_blocked = false;
  if (!running_jobs.empty())
  {
    ava::session::SessionStore competing_parent(
        ava::session::SessionStoreOptions{.root_dir = session_root, .workspace_dir = workspace, .session_id = "parent-bg-contender"});
    auto const resume_arguments = std::string("{\\\"description\\\":\\\"Resume running child\\\",\\\"prompt\\\":\\\"Compete.\\\",") +
                                  "\\\"subagent_type\\\":\\\"general\\\",\\\"task_id\\\":\\\"" + running_jobs.front().child_session_id + "\\\"}";
    ava::tests::FakeTransport competing_transport(
        {sse_response("data: {\"type\":\"response.function_call.added\",\"call_id\":\"call_compete\",\"name\":\"task\"}\n\n"
                      "data: {\"type\":\"response.function_call_arguments.delta\",\"call_id\":\"call_compete\",\"delta\":\"" +
                      resume_arguments +
                      "\"}\n\n"
                      "data: [DONE]\n\n"),
         sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"resume was blocked\"}\n\n"
                      "data: [DONE]\n\n")});
    ava::agent::AgentLoop competing_loop(ava::agent::AgentLoopOptions{
        .workspace_dir = workspace,
        .mode = ava::agent::Mode::Build,
        .provider_id = "openai",
        .model_id = "gpt-5.5",
        .system_prompt = "system prompt",
        .access_token = "token",
        .permission_resolver = [](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
          return ava::permissions::PermissionResolution::Allow;
        },
        .append_entry = append_route_for_test(competing_parent),
        .append_batch = append_batch_route_for_test(competing_parent),
        .session_read_authority = read_authority_for_test(competing_parent),
    });
    auto competing_result = competing_loop.run_turn("resume running child", competing_parent, provider, competing_transport);
    foreground_resume_blocked = competing_result && competing_result->final_text == "resume was blocked" && competing_transport.requests().size() == 2 &&
                                competing_transport.requests()[1].body.find("already owned") != std::string::npos;
  }
  expect(foreground_resume_blocked, "foreground task_id resume fails while the background child owns its session lease");

  background_state->release_success();
  ava::core::Result<ava::agent::BackgroundJobSnapshot> completed =
      std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "missing background job"));
  if (!running_jobs.empty())
  {
    completed = registry->wait(running_jobs.front().job_id, std::chrono::milliseconds(1000));
  }
  expect(completed && completed->state == ava::agent::BackgroundJobState::Completed && completed->final_text == "background child",
         "background task transitions to completed in the registry");
  registry->join_finished();
  bool saw_background_answer = false;
  if (completed)
  {
    auto child_store = ava::session::SessionStore::open(workspace, completed->child_session_id, session_root);
    if (child_store)
    {
      auto child_entries = child_store->load();
      if (child_entries)
      {
        auto const projection = ava::session::classify_assistant_output(*child_entries);
        for (auto const& turn : projection.turns)
          for (auto const& item : turn.items)
            if (auto const* text = std::get_if<ava::session::AssistantOutputText>(&item.item.payload))
              saw_background_answer = saw_background_answer || text->text == "background child";
      }
    }
  }
  expect(saw_background_request_without_lsp, "background subagents do not inherit the parent LSP provider");
  expect(saw_background_answer, "background subagents write completion to the child session");
  std::lock_guard trace_lock(trace_collector->mutex);
  auto trace = ava::observability::validate_and_score_trace(trace_collector->events);
  std::map<std::string, unsigned> starts, terminals;
  bool child_parent_correlation = false;
  for (auto const& event : trace_collector->events)
  {
    starts[event.run_id] += event.type == ava::observability::TraceEventType::AgentRunStart;
    terminals[event.run_id] += event.type == ava::observability::TraceEventType::AgentRunTerminal;
    child_parent_correlation =
        child_parent_correlation || (!event.parent_run_id.empty() && event.parent_session_id == "parent-bg" && event.session_id != event.parent_session_id);
  }
  expect(trace.valid && starts.size() == 2 && starts == terminals && child_parent_correlation,
         "observed background task has separate parent/child lifecycles and typed parent correlation");
}

void test_agent_loop_background_task_failure_records_parent_and_child_errors()
{
  auto const root = create_empty_root("agent-task-background-failure");

  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  auto const session_root = root / "sessions";
  ava::session::SessionStore store(ava::session::SessionStoreOptions{.root_dir = session_root, .workspace_dir = workspace, .session_id = "parent-bg-fail"});
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  std::mutex session_mutex;
  auto background_responses = std::make_shared<std::vector<ava::provider::HttpResponse>>();
  auto background_requests = std::make_shared<std::vector<ava::provider::HttpRequest>>();
  auto background_mutex = std::make_shared<std::mutex>();
  auto registry = std::make_shared<ava::agent::BackgroundJobRegistry>();
  ava::tests::FakeTransport transport({sse_response("data: {\"type\":\"response.function_call.added\",\"call_id\":\"call_task\",\"name\":\"task\"}\n\n"
                                                    "data: "
                                                    "{\"type\":\"response.function_call_arguments.delta\",\"call_id\":\"call_task\",\"delta\":\"{"
                                                    "\\\"description\\\":\\\"Fail async\\\",\\\"prompt\\\":\\\"This background request will fail.\\\","
                                                    "\\\"subagent_type\\\":\\\"general\\\",\\\"background\\\":true}\"}\n\n"
                                                    "data: [DONE]\n\n"),
                                       sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"queued failure\"}\n\n"
                                                    "data: [DONE]\n\n")});
  auto parent_append = append_route_for_test(store);
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .provider_id = "openai",
      .model_id = "gpt-5.5",
      .system_prompt = "system prompt",
      .access_token = "token",
      .permission_resolver = [](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        return ava::permissions::PermissionResolution::Allow;
      },
      .background_provider_factory = []() -> ava::core::Result<std::unique_ptr<ava::provider::Provider>> {
        std::unique_ptr<ava::provider::Provider> provider = std::make_unique<ava::provider::OpenAIProvider>("https://api.example.test");
        return provider;
      },
      .background_transport_factory = [background_responses, background_requests,
                                       background_mutex]() -> ava::core::Result<std::unique_ptr<ava::provider::Transport>> {
        std::unique_ptr<ava::provider::Transport> transport =
            std::make_unique<SharedFakeTransport>(background_responses, background_requests, background_mutex);
        return transport;
      },
      .background_jobs = registry,
      .session_mutex = &session_mutex,
      .append_entry = parent_append,
      .append_batch = append_batch_route_for_test(store),
      .session_read_authority = read_authority_for_test(store),
  });

  auto result = loop.run_turn("delegate failing background", store, provider, transport);
  expect(result && result->final_text == "queued failure" && result->tool_calls == 1, "agent loop can queue a background task that later fails");

  auto jobs = registry->snapshot();
  expect(jobs.size() == 1, "failed background task is registered");
  ava::core::Result<ava::agent::BackgroundJobSnapshot> failed = std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "missing background job"));
  if (!jobs.empty())
  {
    failed = registry->wait(jobs.front().job_id, std::chrono::milliseconds(1000));
  }
  expect(failed && failed->state == ava::agent::BackgroundJobState::Failed && failed->error &&
             failed->error->find("fake transport has no response") != std::string::npos,
         "failed background task is marked failed in the registry");
  registry->join_finished();

  bool saw_background_request = false;
  {
    std::lock_guard lock(*background_mutex);
    saw_background_request = !background_requests->empty();
  }
  auto parent_entries = store.load();
  bool const saw_parent_error = parent_entries && std::ranges::any_of(*parent_entries, [](ava::session::SessionEntry const& entry) {
                                  return entry.type == ava::session::EntryType::Error &&
                                         entry.data_json.find("fake transport has no response") != std::string::npos &&
                                         entry.data_json.find("background_task_id") != std::string::npos;
                                });
  bool saw_child_error = false;
  if (failed)
  {
    auto child_store = ava::session::SessionStore::open(workspace, failed->child_session_id, session_root);
    if (child_store)
    {
      auto child_entries = child_store->load();
      saw_child_error = child_entries && std::ranges::any_of(*child_entries, [](ava::session::SessionEntry const& entry) {
                          return entry.type == ava::session::EntryType::Error && entry.data_json.find("subagent job failed") != std::string::npos &&
                                 entry.data_json.find("fake transport has no response") == std::string::npos;
                        });
    }
  }
  expect(saw_background_request, "background failure test reaches the child provider transport");
  expect(!saw_parent_error, "background task failures are not appended directly into the parent session");
  expect(saw_child_error, "background task failures are persisted in the child session with bounded sanitized content");
}

void test_agent_loop_background_task_cancel_requests_child_cancellation()
{
  auto const root = create_empty_root("agent-task-background-cancel");

  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  auto const session_root = root / "sessions";
  ava::session::SessionStore store(ava::session::SessionStoreOptions{.root_dir = session_root, .workspace_dir = workspace, .session_id = "parent-bg-cancel"});
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  auto registry = std::make_shared<ava::agent::BackgroundJobRegistry>();
  auto background_state = std::make_shared<BlockingBackgroundTransport::State>();
  ava::tests::FakeTransport transport({sse_response("data: {\"type\":\"response.function_call.added\",\"call_id\":\"call_task\",\"name\":\"task\"}\n\n"
                                                    "data: "
                                                    "{\"type\":\"response.function_call_arguments.delta\",\"call_id\":\"call_task\",\"delta\":\"{"
                                                    "\\\"description\\\":\\\"Cancel async\\\",\\\"prompt\\\":\\\"Wait until canceled.\\\","
                                                    "\\\"subagent_type\\\":\\\"general\\\",\\\"background\\\":true}\"}\n\n"
                                                    "data: [DONE]\n\n"),
                                       sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"queued cancel\"}\n\n"
                                                    "data: [DONE]\n\n")});
  auto parent_append = append_route_for_test(store);
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .provider_id = "openai",
      .model_id = "gpt-5.5",
      .system_prompt = "system prompt",
      .access_token = "token",
      .permission_resolver = [](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        return ava::permissions::PermissionResolution::Allow;
      },
      .background_provider_factory = []() -> ava::core::Result<std::unique_ptr<ava::provider::Provider>> {
        std::unique_ptr<ava::provider::Provider> provider = std::make_unique<ava::provider::OpenAIProvider>("https://api.example.test");
        return provider;
      },
      .background_transport_factory = [background_state]() -> ava::core::Result<std::unique_ptr<ava::provider::Transport>> {
        std::unique_ptr<ava::provider::Transport> transport = std::make_unique<BlockingBackgroundTransport>(
            background_state, sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"should not complete\"}\n\n"
                                           "data: [DONE]\n\n"));
        return transport;
      },
      .background_jobs = registry,
      .append_entry = parent_append,
      .append_batch = append_batch_route_for_test(store),
      .session_read_authority = read_authority_for_test(store),
  });

  auto result = loop.run_turn("delegate cancelable background", store, provider, transport);
  expect(result && result->final_text == "queued cancel" && result->tool_calls == 1, "agent loop can queue a cancelable background task");
  auto jobs = registry->snapshot();
  expect(jobs.size() == 1 && jobs.front().state == ava::agent::BackgroundJobState::Running, "cancelable background task is running in registry");
  expect(background_state->wait_for_request(std::chrono::milliseconds(1000)), "cancel test background child reaches provider transport");
  if (!jobs.empty())
  {
    auto canceled = registry->cancel(jobs.front().job_id);
    background_state->notify();
    expect(canceled && canceled->cancel_requested, "background registry cancel requests stop");
    expect(background_state->wait_for_cancel(std::chrono::milliseconds(1000)), "background child transport observes cancellation");
    auto final = registry->wait(jobs.front().job_id, std::chrono::milliseconds(1000));
    expect(final && final->state == ava::agent::BackgroundJobState::Canceled, "background registry marks canceled child jobs canceled");
    expect(final && !final->error, "background registry canceled job snapshots do not carry failure errors");
    registry->join_finished();

    bool saw_child_cancel = false;
    if (final)
    {
      auto child_store = ava::session::SessionStore::open(workspace, final->child_session_id, session_root);
      if (child_store)
      {
        auto child_entries = child_store->load();
        saw_child_cancel = child_entries && std::ranges::any_of(*child_entries, [](ava::session::SessionEntry const& entry) {
                             return entry.type == ava::session::EntryType::Cancel && entry.data_json.find("cancel_requested") != std::string::npos;
                           });
      }
    }
    expect(saw_child_cancel, "canceled background child records cancellation in its child session");
  }
}

void test_agent_loop_background_task_requires_registry_owner()
{
  auto const root = create_empty_root("agent-task-background-no-registry");

  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  ava::session::SessionStore store(ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "no-registry"});
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response("data: {\"type\":\"response.function_call.added\",\"call_id\":\"call_task\",\"name\":\"task\"}\n\n"
                                                    "data: "
                                                    "{\"type\":\"response.function_call_arguments.delta\",\"call_id\":\"call_task\",\"delta\":\"{"
                                                    "\\\"description\\\":\\\"No registry\\\",\\\"prompt\\\":\\\"Try background.\\\","
                                                    "\\\"subagent_type\\\":\\\"general\\\",\\\"background\\\":true}\"}\n\n"
                                                    "data: [DONE]\n\n"),
                                       sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"handled\"}\n\n"
                                                    "data: [DONE]\n\n")});
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .provider_id = "openai",
      .model_id = "gpt-5.5",
      .system_prompt = "system prompt",
      .access_token = "token",
      .permission_resolver = [](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        return ava::permissions::PermissionResolution::Allow;
      },
      .background_provider_factory = []() -> ava::core::Result<std::unique_ptr<ava::provider::Provider>> {
        std::unique_ptr<ava::provider::Provider> provider = std::make_unique<ava::provider::OpenAIProvider>("https://api.example.test");
        return provider;
      },
      .background_transport_factory = []() -> ava::core::Result<std::unique_ptr<ava::provider::Transport>> {
        std::unique_ptr<ava::provider::Transport> transport = std::make_unique<ava::tests::FakeTransport>(std::vector<ava::provider::HttpResponse>{});
        return transport;
      },
      .append_entry = append_route_for_test(store),
      .append_batch = append_batch_route_for_test(store),
      .session_read_authority = read_authority_for_test(store),
  });

  auto result = loop.run_turn("delegate unavailable background", store, provider, transport);
  expect(result && result->final_text == "handled" && transport.requests().size() == 2,
         "agent loop continues after unavailable background registry tool error");
  if (transport.requests().size() == 2)
  {
    expect(transport.requests()[1].body.find("background task subagents are unavailable") != std::string::npos,
           "background task requires an explicit registry owner");
  }
}

void test_agent_loop_coordinator_start_journal_failure_rolls_back_child()
{
  auto const root = temp_root() / "agent-task-background-journal-failure";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  ava::agent::SubagentCoordinatorOptions coordinator_options;
  coordinator_options.ava_state_dir = root / "state";
  coordinator_options.journal_append_preflight = [](ava::agent::JobJournalRecord const& record) -> ava::core::VoidResult {
    if (record.kind == ava::agent::JobJournalTransitionKind::Started)
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "injected started journal failure"));
    return {};
  };
  auto coordinator = ava::agent::SubagentCoordinator::create(std::move(coordinator_options));
  expect(coordinator.has_value(), "journal-failure rollback fixture creates coordinator");
  if (!coordinator)
    return;
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  auto const session_root = root / "sessions";
  ava::session::SessionStore store(
      ava::session::SessionStoreOptions{.root_dir = session_root, .workspace_dir = workspace, .session_id = "parent-journal-failure"});
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response("data: {\"type\":\"response.function_call.added\",\"call_id\":\"call_task\",\"name\":\"task\"}\n\n"
                                                    "data: "
                                                    "{\"type\":\"response.function_call_arguments.delta\",\"call_id\":\"call_task\",\"delta\":\"{"
                                                    "\\\"description\\\":\\\"Reject start\\\",\\\"prompt\\\":\\\"Never run.\\\","
                                                    "\\\"subagent_type\\\":\\\"general\\\",\\\"background\\\":true}\"}\n\n"
                                                    "data: [DONE]\n\n"),
                                       sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"start rejected\"}\n\n"
                                                    "data: [DONE]\n\n")});
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .provider_id = "openai",
      .model_id = "gpt-5.5",
      .system_prompt = "system prompt",
      .access_token = "token",
      .permission_resolver = [](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        return ava::permissions::PermissionResolution::Allow;
      },
      .background_provider_factory = []() -> ava::core::Result<std::unique_ptr<ava::provider::Provider>> {
        std::unique_ptr<ava::provider::Provider> provider = std::make_unique<ava::provider::OpenAIProvider>("https://api.example.test");
        return provider;
      },
      .background_transport_factory = []() -> ava::core::Result<std::unique_ptr<ava::provider::Transport>> {
        std::unique_ptr<ava::provider::Transport> transport = std::make_unique<ava::tests::FakeTransport>(std::vector<ava::provider::HttpResponse>{});
        return transport;
      },
      .subagent_coordinator = *coordinator,
      .append_entry = append_route_for_test(store),
      .append_batch = append_batch_route_for_test(store),
      .session_read_authority = read_authority_for_test(store),
  });

  auto result = loop.run_turn("delegate rejected background", store, provider, transport);
  expect(result && result->final_text == "start rejected" && transport.requests().size() == 2,
         "parent continues after coordinator rejects background publication");
  bool saw_failure = transport.requests().size() == 2 && transport.requests()[1].body.find("injected started journal failure") != std::string::npos;
  std::size_t session_files = 0;
  if (std::filesystem::exists(session_root))
    for (auto const& entry : std::filesystem::recursive_directory_iterator(session_root))
      session_files += entry.is_regular_file() && entry.path().extension() == ".jsonl";
  expect(saw_failure, "coordinator start failure is returned to the parent tool continuation");
  expect(session_files == 1, "journal failure rolls back the newly created child session file");
  expect((*coordinator)->list("parent-journal-failure").empty(), "journal failure prevents live worker publication");
}
