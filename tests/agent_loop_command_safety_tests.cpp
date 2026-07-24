#include "sys.h"
#include "tests/agent_loop_test_declarations.h"
#include "tests/support/agent_loop_test_support.h"
#include "tests/support/fake_transport.h"
#include "tests/support/test_harness.h"
#include "ava/agent/agent_loop.h"
#include "ava/agent/agent_loop_session.h"
#include "ava/agent/assistant_turn.h"
#include "ava/session/session_store.h"
#include "ava/provider/openai_provider.h"
#include "ava/core/result.h"

#include <algorithm>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
#include <sys/stat.h>

using agent_loop_test::sse_response;
using agent_loop_test::tool_call_sse;
using agent_loop_test::TraceCollector;

void test_agent_loop_model_command_deny_preflight_blocks_auto_allow_without_process()
{
  auto const test_root = temp_root();
  expect(::chmod(test_root.c_str(), S_IRWXU) == 0, "model command deny preflight test secures its test-root ancestor");
  auto const root = test_root / "agent-model-command-preflight-deny";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  expect(::chmod(root.c_str(), S_IRWXU) == 0 && ::chmod(workspace.c_str(), S_IRWXU) == 0,
         "model command deny preflight fixture keeps sealed planning roots owner-only");

  ava::session::SessionStore store(
      ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "model-preflight-deny"});
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response(tool_call_sse("call_auto_allow", "bash", R"({"command":"ls"})") + "data: [DONE]\n\n"),
                                       sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"persistent deny handled\"}\n\n"
                                                    "data: [DONE]\n\n")});
  auto collector = std::make_shared<TraceCollector>();
  auto observation = std::make_shared<ava::observability::RunObservation>(collector);
  int interactive_prompts = 0;
  int deny_preflights = 0;
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
      .workspace_dir = workspace,
      .anchor_set = command_anchors_for_test(workspace, store.session_path().parent_path() / "spill"),
      .mode = ava::agent::Mode::Build,
      .provider_id = "openai",
      .model_id = "gpt-5.5",
      .system_prompt = "system prompt",
      .access_token = "token",
      .permission_resolver =
          [&interactive_prompts](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        ++interactive_prompts;
        return ava::permissions::PermissionResolution::Allow;
      },
      .command_deny_preflight =
          [&deny_preflights](ava::permissions::PermissionPrompt const& prompt) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        ++deny_preflights;
        expect(prompt.operation == ava::permissions::Operation::RunCommand && prompt.command == "ls" && prompt.command_metadata &&
                   prompt.command_metadata->level == ava::command::CommandLevel::Standard &&
                   prompt.command_metadata->containment_status == ava::permissions::CommandContainmentStatus::NotRequired,
               "model command deny preflight receives the auto-Allow standard command's sealed metadata");
        ava::permissions::PermissionResolutionDecision denied(ava::permissions::PermissionResolution::Deny, "external persistent Deny");
        denied.resolution_source = "persistent_rule";
        denied.rule_id = "rule_model_deny";
        denied.authoritative = true;
        return denied;
      },
      .append_entry = append_route_for_test(store),
      .append_batch = append_batch_route_for_test(store),
      .session_read_authority = read_authority_for_test(store),
      .observation = observation,
  });

  auto result = loop.run_turn("run inspection", store, provider, transport);
  bool process_started = false;
  {
    std::lock_guard lock(collector->mutex);
    process_started = std::ranges::any_of(
        collector->events, [](ava::observability::TraceEvent const& event) { return event.type == ava::observability::TraceEventType::ProcessStart; });
  }
  expect(result && result->final_text == "persistent deny handled" && deny_preflights == 1 && interactive_prompts == 0 && result->tool_timeline.size() == 1 &&
             result->tool_timeline.front().status == ava::agent::ToolTimelineStatus::Error && !process_started,
         "an authoritative model-command Deny preflight overrides Standard auto-Allow before prompt or process side effects");
}

void test_agent_loop_model_command_rejects_authority_workspace_before_permission_or_process()
{
  auto const test_root = temp_root();
  expect(::chmod(test_root.c_str(), S_IRWXU) == 0, "model authority-root test secures its test-root ancestor");
  auto const root = test_root / "agent-model-authority-root";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "ava-authority";
  std::filesystem::create_directories(workspace);
  expect(::chmod(root.c_str(), S_IRWXU) == 0 && ::chmod(workspace.c_str(), S_IRWXU) == 0,
         "model authority-root fixture keeps sealed planning roots owner-only");

  ava::session::SessionStore store(
      ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "model-authority-root"});
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response(tool_call_sse("call_authority", "bash", R"({"command":"ls"})") + "data: [DONE]\n\n"),
                                       sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"authority rejected\"}\n\n"
                                                    "data: [DONE]\n\n")});
  auto collector = std::make_shared<TraceCollector>();
  auto observation = std::make_shared<ava::observability::RunObservation>(collector);
  int prompts = 0;
  int preflights = 0;
  std::vector<std::filesystem::path> duplicate_roots(65, workspace);
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
      .workspace_dir = workspace,
      .anchor_set = command_anchors_for_test(workspace, store.session_path().parent_path() / "spill"),
      .mode = ava::agent::Mode::Build,
      .provider_id = "openai",
      .model_id = "gpt-5.5",
      .system_prompt = "system prompt",
      .access_token = "token",
      .ava_authority_roots = std::move(duplicate_roots),
      .permission_resolver = [&prompts](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        ++prompts;
        return ava::permissions::PermissionResolution::Allow;
      },
      .command_deny_preflight = [&preflights](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        ++preflights;
        return ava::permissions::PermissionResolution::Allow;
      },
      .append_entry = append_route_for_test(store),
      .append_batch = append_batch_route_for_test(store),
      .session_read_authority = read_authority_for_test(store),
      .observation = observation,
  });

  auto result = loop.run_turn("inspect authority", store, provider, transport);
  bool process_started = false;
  {
    std::lock_guard lock(collector->mutex);
    process_started = std::ranges::any_of(
        collector->events, [](ava::observability::TraceEvent const& event) { return event.type == ava::observability::TraceEventType::ProcessStart; });
  }
  auto const detail = result && !result->tool_timeline.empty() ? result->tool_timeline.front().result_summary : std::string{};
  expect(result && result->final_text == "authority rejected" && prompts == 0 && preflights == 0 && !process_started &&
             detail.find("must not overlap with any AVA authority root") != std::string::npos,
         "model ToolContexts deduplicate bounded AVA authority roots and reject overlapping workspaces before prompts or processes");
}

void test_agent_loop_truncates_tool_context()
{
  auto const root = create_empty_root("agent-tool-truncate");

  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  {
    std::ofstream large(workspace / "large.txt", std::ios::binary | std::ios::trunc);
    large << std::string(12 * 1024, 'x');
  }
  ava::session::SessionStore store(ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "truncate"});
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response("data: {\"type\":\"response.function_call.added\",\"call_id\":\"call_large\",\"name\":\"read_file\"}\n\n"
                                                    "data: "
                                                    "{\"type\":\"response.function_call_arguments.delta\",\"call_id\":\"call_large\",\"delta\":\"{\\\"path\\\":"
                                                    "\\\"large.txt\\\"}\"}\n\n"
                                                    "data: [DONE]\n\n"),
                                       sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"ok\"}\n\n"
                                                    "data: [DONE]\n\n")});
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .provider_id = "openai",
      .model_id = "gpt-5.5",
      .system_prompt = "system prompt",
      .access_token = "token",
      .max_tool_result_context_bytes = 8 * 1024,
      .append_entry = append_route_for_test(store),
      .append_batch = append_batch_route_for_test(store),
      .session_read_authority = read_authority_for_test(store),
  });
  auto result = loop.run_turn("read large", store, provider, transport);
  expect(result && transport.requests().size() == 2 && transport.requests()[1].body.find("tool result content truncated") != std::string::npos,
         "agent loop truncates native tool results before OpenAI continuation");
}

void test_unresolved_committed_function_recovery_never_partially_appends_after_a_closed_window()
{
  auto store = ava::session::SessionStore::create_ephemeral(create_empty_root("unresolved-committed-function-recovery"));
  auto target = store ? ava::session::SessionAppendTarget::create_ephemeral(*store)
                      : ava::core::Result<std::shared_ptr<ava::session::SessionAppendTarget>>(std::unexpected(store.error()));
  if (!store || !target)
    return;

  auto function_turn = [](std::string call_id) {
    ava::agent::ParsedAssistantTurn turn;
    turn.ordered_items.push_back(ava::agent::OrderedAssistantItem{
        .sequence = 0,
        .item = ava::agent::AssistantFunctionCallItem{
            .metadata = {}, .tool_call = ava::agent::ProviderToolCall{.id = std::move(call_id), .name = "read_file", .arguments_json = "{}"}}});
    turn.finish_reason = ava::provider::ProviderFinishReason::ToolCalls;
    return turn;
  };
  auto append_batch = [append_target = *target](std::vector<ava::session::SessionEntry> entries) { return append_target->append_batch(std::move(entries)); };
  auto first = ava::agent::append_assistant_turn(append_batch, function_turn("closed_call"), "openai", "gpt-5.5", {}, std::nullopt);
  auto boundary = first ? (*target)->append(ava::session::SessionEntry{.id = "closed_window_user",
                                                                       .parent_id = "",
                                                                       .type = ava::session::EntryType::UserMessage,
                                                                       .timestamp = ava::session::now_timestamp(),
                                                                       .data_json = "{\"text\":\"later turn\"}"})
                        : ava::core::VoidResult(std::unexpected(first.error()));
  auto current = boundary ? ava::agent::append_assistant_turn(append_batch, function_turn("current_call"), "openai", "gpt-5.5", {}, std::nullopt)
                          : ava::core::Result<ava::agent::PersistedAssistantTurn>(std::unexpected(boundary.error()));
  auto authority = current ? (*target)->read_authority() : ava::core::Result<ava::session::SessionReadAuthority>(std::unexpected(current.error()));
  std::size_t synthetic_append_attempts = 0;
  ava::agent::SessionAppendSink sink = [append_target = *target, &synthetic_append_attempts](ava::session::SessionEntry entry) {
    ++synthetic_append_attempts;
    return append_target->append(std::move(entry));
  };
  auto const before = store->load();
  auto reconciled = authority
                        ? ava::agent::reconcile_unresolved_committed_function_calls(*authority, sink, ava::session::legacy_unbounded_session_read_limits())
                        : ava::core::VoidResult(std::unexpected(authority.error()));
  auto const after = store->load();
  expect(first && boundary && current && authority && !reconciled && reconciled.error().message().find("active EOF tool-result window") != std::string::npos &&
             synthetic_append_attempts == 0 && before && after && after->size() == before->size() &&
             std::ranges::equal(
                 *before, *after, {}, [](auto const& entry) { return entry.id; }, [](auto const& entry) { return entry.id; }),
         "closed unresolved v4 calls reject the whole recovery batch before any synthetic result can mutate a later current call");
}
