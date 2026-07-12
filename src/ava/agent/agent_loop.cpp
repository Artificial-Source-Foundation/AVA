#include "sys.h"
#include "ava/agent/agent_loop.h"
#include "ava/agent/agent_loop_session.h"
#include "ava/agent/assistant_turn.h"
#include "ava/agent/message_builder.h"
#include "ava/agent/provider_output_validation.h"
#include "ava/agent/stream_bridge.h"
#include "ava/agent/tool_dispatch_common.h"
#include "ava/agent/tool_dispatcher.h"
#include "ava/agent/tool_result.h"
#include "ava/agent/tool_scheduler.h"
#include "ava/agent/tool_summaries.h"
#include "ava/agent/tool_timeline.h"
#include "ava/agent/usage_accounting.h"
#include "ava/session/attachments.h"
#include "ava/session/session_metadata.h"
#include "ava/provider/provider_utils.h"
#include "ava/core/json.h"

#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <string_view>
#include <utility>
#include <vector>

namespace ava::agent {
namespace {

std::string dispatch_error_result_json(ProviderToolCall const& call, ava::core::Error const& error)
{
  return "{\"tool\":\"" + ava::core::json::escape(call.name) + "\",\"ok\":false,\"error\":{\"category\":\"" +
         ava::core::json::escape(ava::core::to_string(error.category())) + "\",\"message\":\"" + ava::core::json::escape(error.message()) +
         "\",\"details\":\"" + ava::core::json::escape(error.format()) + "\"}}";
}

ToolDispatchResult synthetic_failed_dispatch_result(ProviderToolCall const& call, ava::core::Error const& error)
{
  return with_tool_result_payload(
      ToolDispatchResult{.call_id = call.id, .name = call.name, .success = false, .result_text = dispatch_error_result_json(call, error)});
}

bool is_canceled(AgentLoopOptions const& options)
{
  return options.cancel_requested && options.cancel_requested();
}

ava::core::VoidResult check_canceled(AgentLoopOptions const& options, ava::session::SessionStore& store, std::string_view boundary)
{
  if (!is_canceled(options))
    return {};
  static_cast<void>(append_cancel(store, boundary));
  auto error = ava::core::Error(ava::core::ErrorCategory::Unknown, "agent loop canceled");
  error.with_context("boundary", std::string(boundary));
  return std::unexpected(std::move(error));
}

bool error_has_context(ava::core::Error const& error, std::string_view key, std::string_view value)
{
  return std::ranges::any_of(error.context(), [&](ava::core::ErrorContext const& item) { return item.key == key && item.value == value; });
}

bool is_scheduler_canceled_error(ava::core::Error const& error)
{
  return error_has_context(error, "canceled", "true");
}

ava::core::VoidResult attach_verified_image_payloads(ava::provider::ProviderRequest& request, ava::session::SessionStore const& store)
{
  for (auto& message : request.messages)
  {
    for (auto& part : message.content_parts)
    {
      if (part.type != ava::provider::ContentPartType::Image)
        continue;
      ava::session::ImageAttachmentRef const attachment{
          .id = part.attachment_id, .mime_type = part.mime_type, .storage_path = part.storage_path, .sha256 = part.sha256, .byte_size = part.byte_size};
      auto loaded = ava::session::load_image_attachment(store, attachment);
      if (!loaded)
        return std::unexpected(std::move(loaded.error()));
      part.data_base64 = ava::provider::base64_encode(loaded->bytes);
    }
  }
  return {};
}

bool contains_tool_name(std::vector<std::string> const& tools, std::string_view name)
{
  return std::find(tools.begin(), tools.end(), name) != tools.end();
}

void add_excluded_tool(ToolVisibilityOptions& visibility, std::string_view name)
{
  if (!contains_tool_name(visibility.excluded_tools, name))
    visibility.excluded_tools.emplace_back(name);
}

ToolVisibilityOptions subagent_tool_visibility(ToolVisibilityOptions parent, SubagentToolPreset tool_preset)
{
  add_excluded_tool(parent, "task");
  if (tool_preset != SubagentToolPreset::ReadOnly)
    return parent;

  std::vector<std::string> const read_only_tools{"read_file", "list_directory", "glob", "grep"};
  if (parent.included_tools.empty())
  {
    parent.included_tools = read_only_tools;
    return parent;
  }

  std::vector<std::string> narrowed;
  for (auto const& tool : parent.included_tools)
  {
    if (contains_tool_name(read_only_tools, tool))
      narrowed.push_back(tool);
  }
  parent.included_tools = std::move(narrowed);
  return parent;
}

std::string subagent_system_prompt(std::string base, std::string_view role_prompt)
{
  auto const role = role_prompt.empty()
                        ? std::string("You are AVA's subagent. Complete the delegated task and return only the result needed by the parent agent.")
                        : std::string(role_prompt);
  if (base.empty())
    return role;
  base += "\n\n";
  base += role;
  return base;
}

bool permission_decision_cannot_ask(ava::tools::ToolContext const& context, ava::permissions::Operation operation, std::filesystem::path const& target_path)
{
  auto const decision = ava::permissions::decide(ava::permissions::PermissionRequest{
      .operation = operation,
      .mode = context.mode,
      .workspace_dir = context.workspace_dir,
      .target_path = target_path,
      .command = "",
  });
  return decision.action != ava::permissions::PermissionAction::Ask;
}

bool preflight_read_file_parallel_ready(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  auto path = tool_dispatch::required_safe_string_arg(call.arguments_json, "path", call.name);
  if (!path)
    return false;
  return permission_decision_cannot_ask(context, ava::permissions::Operation::ReadFile, tool_dispatch::workspace_path(context, *path));
}

bool preflight_list_directory_entries_cannot_ask(ava::tools::ToolContext const& context, std::filesystem::path const& path)
{
  std::error_code status_error;
  if (!std::filesystem::exists(path, status_error) || status_error)
    return true;
  if (!std::filesystem::is_directory(path, status_error) || status_error)
    return true;

  std::error_code iter_error;
  for (std::filesystem::directory_iterator it(path, iter_error), end; !iter_error && it != end; it.increment(iter_error))
  {
    if (!permission_decision_cannot_ask(context, ava::permissions::Operation::ReadFile, it->path()))
      return false;
  }
  return !iter_error;
}

bool preflight_list_directory_parallel_ready(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  auto path_value = ava::core::json::string_field(call.arguments_json, "path");
  if (path_value)
  {
    if (auto safe = tool_dispatch::reject_control_arg(*path_value, "path", call.name); !safe)
      return false;
  }
  auto const path = tool_dispatch::workspace_path(context, path_value.value_or("."));
  if (!permission_decision_cannot_ask(context, ava::permissions::Operation::SearchFiles, path))
    return false;
  return preflight_list_directory_entries_cannot_ask(context, path);
}

bool preflight_glob_parallel_ready(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  auto pattern = tool_dispatch::required_safe_string_arg(call.arguments_json, "pattern", call.name);
  if (!pattern)
    return false;
  if (auto no_ignore_allowed = tool_dispatch::reject_provider_no_ignore(call.arguments_json, call.name); !no_ignore_allowed)
    return false;
  return permission_decision_cannot_ask(context, ava::permissions::Operation::SearchFiles, context.workspace_dir);
}

bool preflight_grep_parallel_ready(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  auto pattern = tool_dispatch::required_text_arg(call.arguments_json, "pattern", call.name);
  if (!pattern)
    return false;
  auto const include_value = ava::core::json::string_field(call.arguments_json, "include");
  if (include_value)
  {
    if (auto safe = tool_dispatch::reject_control_arg(*include_value, "include", call.name); !safe)
      return false;
  }
  if (auto no_ignore_allowed = tool_dispatch::reject_provider_no_ignore(call.arguments_json, call.name); !no_ignore_allowed)
    return false;
  if (auto literal = tool_dispatch::optional_bool_arg(call.arguments_json, "literal", true, call.name); !literal)
    return false;
  if (auto case_insensitive = tool_dispatch::optional_bool_arg(call.arguments_json, "case_insensitive", false, call.name); !case_insensitive)
    return false;
  return permission_decision_cannot_ask(context, ava::permissions::Operation::SearchFiles, context.workspace_dir);
}

bool preflight_parallel_ready(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  if (call.name == "read_file")
    return preflight_read_file_parallel_ready(context, call);
  if (call.name == "list_directory")
    return preflight_list_directory_parallel_ready(context, call);
  if (call.name == "glob")
    return preflight_glob_parallel_ready(context, call);
  if (call.name == "grep")
    return preflight_grep_parallel_ready(context, call);
  return false;
}

void mark_parallel_ready_slots(std::vector<ToolScheduleSlot>& schedule, ava::tools::ToolContext const& context)
{
  for (auto& slot : schedule)
  {
    if (slot.classification.eligibility != ToolScheduleEligibility::ReadOnlyCandidate)
      continue;
    if (preflight_parallel_ready(context, slot.call))
      slot.parallel_readiness = ToolScheduleParallelReadiness::PreflightProvenNonInteractive;
  }
}

bool is_parallel_ready_slot(ToolScheduleSlot const& slot) noexcept
{
  return slot.classification.eligibility == ToolScheduleEligibility::ReadOnlyCandidate &&
         slot.parallel_readiness == ToolScheduleParallelReadiness::PreflightProvenNonInteractive;
}

struct BufferedToolCallbacks
{
  std::vector<ava::tools::PermissionAuditEvent> permission_audits;
  std::vector<ava::tools::ToolProgressEvent> progress_events;
};

struct AgentTraceScope
{
  ava::session::SessionStore& store;
  std::shared_ptr<ava::observability::RunObservation> observation;
  ava::observability::TraceContext const& context;
  ava::observability::TraceOutcome outcome = ava::observability::TraceOutcome::Failed;

  std::uint64_t attachment_generation = 0;

  AgentTraceScope(ava::session::SessionStore& store_in, std::shared_ptr<ava::observability::RunObservation> observation_in,
                  ava::observability::TraceContext const& context_in, std::uint64_t generation) noexcept
      : store(store_in), observation(std::move(observation_in)), context(context_in), attachment_generation(generation)
  {
  }
  ~AgentTraceScope() noexcept
  {
    if (observation)
      observation->emit(ava::observability::TraceEventType::AgentRunTerminal, context, [this](auto& event) {
        event.phase = ava::observability::TracePhase::Run;
        event.outcome = outcome;
      });
    // Do not let stale background copies clear a newer attachment.
    store.clear_run_observation(attachment_generation);
  }
};

bool is_terminal_canceled_error(ava::core::Error const& error)
{
  return error.message() == "agent loop canceled" || error.message() == "transport retry canceled" || error.message() == "transport request canceled" ||
         is_scheduler_canceled_error(error);
}

ava::observability::TraceOutcome terminal_outcome(ava::core::Error const& error)
{
  if (is_terminal_canceled_error(error))
    return ava::observability::TraceOutcome::Canceled;
  switch (error.category())
  {
    case ava::core::ErrorCategory::Provider:
      return ava::observability::TraceOutcome::ProviderError;
    case ava::core::ErrorCategory::Tool:
      return ava::observability::TraceOutcome::ToolError;
    case ava::core::ErrorCategory::Session:
    case ava::core::ErrorCategory::Io:
      return ava::observability::TraceOutcome::SessionError;
    default:
      return ava::observability::TraceOutcome::Failed;
  }
}

void append_background_error_best_effort(ava::session::SessionStore& store, ava::core::Error const& error)
{
  static_cast<void>(append_error(store, error));
}

void append_background_parent_error_best_effort(ava::session::SessionStore store, std::string const& task_id, std::string const& subagent_type,
                                                ava::core::Error error)
{
  error.with_context("background_task_id", task_id);
  error.with_context("subagent_type", subagent_type);
  static_cast<void>(append_error(store, error));
}

BackgroundJobCompletion background_failure_completion(BackgroundJobContext const& context, ava::core::Error const& error)
{
  if (context.stop_token.stop_requested())
  {
    return BackgroundJobCompletion{.state = BackgroundJobState::Canceled, .final_text = "", .stop_reason = "canceled", .error = error};
  }
  return BackgroundJobCompletion{.state = BackgroundJobState::Failed, .final_text = "", .stop_reason = "failed", .error = error};
}

}  // namespace

AgentLoop::AgentLoop(AgentLoopOptions options) : options_(std::move(options))
{
}

std::string to_string(ToolTimelineStatus status)
{
  switch (status)
  {
    case ToolTimelineStatus::Running:
      return "running";
    case ToolTimelineStatus::Success:
      return "success";
    case ToolTimelineStatus::Canceled:
      return "canceled";
    case ToolTimelineStatus::Error:
      return "error";
  }
  return "unknown";
}

ava::core::Result<AgentLoopResult> AgentLoop::run_turn(std::string const& user_message, ava::session::SessionStore& store,
                                                       ava::provider::Provider const& provider, ava::provider::Transport& transport)
{
  return run_turn(user_message, {}, store, provider, transport);
}

ava::core::Result<AgentLoopResult> AgentLoop::run_turn(std::string const& user_message, std::vector<ava::session::ImageAttachmentRef> const& image_attachments,
                                                       ava::session::SessionStore& store, ava::provider::Provider const& provider,
                                                       ava::provider::Transport& transport)
{
  ava::observability::TraceContext trace_context;
  std::optional<AgentTraceScope> trace_scope;
  if (options_.observation && options_.observation->enabled())
  {
    try
    {
      trace_context = options_.trace_context;
      trace_context.session_id = trace_context.session_id.empty() ? store.session_id() : trace_context.session_id;
      trace_context.provider_id = trace_context.provider_id.empty() ? options_.provider_id : trace_context.provider_id;
      // Empty/failed IDs are still isolated at RunObservation; do not let them
      // prevent the authoritative run.
      if (trace_context.run_id.empty())
        trace_context.run_id = options_.observation->next_id("run");
      if (trace_context.turn_id.empty())
        trace_context.turn_id = options_.observation->next_id("turn");
      auto const attachment_generation = store.set_run_observation(options_.observation, trace_context);
      // A failed observer attachment must not orphan the lifecycle: generation
      // zero simply makes scope cleanup a no-op after it emits the terminal.
      trace_scope.emplace(store, options_.observation, trace_context, attachment_generation);
      options_.observation->emit(ava::observability::TraceEventType::AgentRunStart, trace_context,
                                 [](auto& event) { event.phase = ava::observability::TracePhase::Run; });
    }
    catch (...)
    {
      options_.observation->account_external_failure();
    }
  }
  auto result = run_turn_impl(user_message, image_attachments, store, provider, transport, trace_context);
  if (trace_scope)
    trace_scope->outcome = result ? ava::observability::TraceOutcome::Completed : terminal_outcome(result.error());
  return result;
}

ava::core::Result<AgentLoopResult> AgentLoop::run_turn_impl(std::string const& user_message,
                                                            std::vector<ava::session::ImageAttachmentRef> const& image_attachments,
                                                            ava::session::SessionStore& store, ava::provider::Provider const& provider,
                                                            ava::provider::Transport& transport, ava::observability::TraceContext const& trace_context)
{
  ava::provider::Transport* effective_transport = &transport;
  std::optional<ava::provider::ObservedTransport> observed_transport;
  if (options_.observation && options_.observation->enabled())
  {
    try
    {
      observed_transport.emplace(transport, ava::provider::TransportObservation{.observation = options_.observation, .context = trace_context});
      effective_transport = &*observed_transport;
    }
    catch (...)
    {
      options_.observation->account_external_failure();
    }
  }
  auto check_canceled_locked = [&](std::string_view boundary) -> ava::core::VoidResult {
    if (options_.session_mutex)
    {
      std::lock_guard lock(*options_.session_mutex);
      return check_canceled(options_, store, boundary);
    }
    return check_canceled(options_, store, boundary);
  };
  auto append_user_message_locked = [&](std::string const& text,
                                        std::vector<ava::session::ImageAttachmentRef> const& attachments) -> ava::core::Result<std::string> {
    if (options_.session_mutex)
    {
      std::lock_guard lock(*options_.session_mutex);
      return append_user_message(store, text, attachments);
    }
    return append_user_message(store, text, attachments);
  };
  auto build_messages_locked = [&]() -> ava::core::Result<BuiltProviderMessages> {
    if (options_.session_mutex)
    {
      std::lock_guard lock(*options_.session_mutex);
      return build_messages(store, options_.max_tool_result_context_bytes);
    }
    return build_messages(store, options_.max_tool_result_context_bytes);
  };
  auto append_assistant_message_locked = [&](std::string const& text, std::size_t tool_call_count, ava::provider::TokenUsage const& usage,
                                             std::optional<long double> const& cost_usd) -> ava::core::VoidResult {
    if (options_.session_mutex)
    {
      std::lock_guard lock(*options_.session_mutex);
      return append_assistant_message(store, text, tool_call_count, usage, cost_usd);
    }
    return append_assistant_message(store, text, tool_call_count, usage, cost_usd);
  };
  auto append_reasoning_blocks_locked = [&](std::vector<ParsedReasoningBlock> const& blocks) -> ava::core::VoidResult {
    auto append_all = [&]() -> ava::core::VoidResult {
      for (auto const& block : blocks)
      {
        if (auto appended = append_reasoning_block(store, block, options_.provider_id, options_.model_id); !appended)
        {
          return appended;
        }
      }
      return {};
    };
    if (options_.session_mutex)
    {
      std::lock_guard lock(*options_.session_mutex);
      return append_all();
    }
    return append_all();
  };
  auto append_tool_call_locked = [&](ProviderToolCall const& call) -> ava::core::VoidResult {
    if (options_.session_mutex)
    {
      std::lock_guard lock(*options_.session_mutex);
      return append_tool_call(store, call);
    }
    return append_tool_call(store, call);
  };
  auto append_tool_result_locked = [&](ToolDispatchResult const& dispatch_result) -> ava::core::VoidResult {
    if (options_.session_mutex)
    {
      std::lock_guard lock(*options_.session_mutex);
      return append_tool_result(store, dispatch_result);
    }
    return append_tool_result(store, dispatch_result);
  };
  auto append_permission_decision_locked = [&](ava::tools::PermissionAuditEvent const& event) -> ava::core::VoidResult {
    if (options_.session_mutex)
    {
      std::lock_guard lock(*options_.session_mutex);
      return append_permission_decision(store, event);
    }
    return append_permission_decision(store, event);
  };
  auto append_error_locked = [&](ava::core::Error const& error) -> ava::core::VoidResult {
    if (options_.session_mutex)
    {
      std::lock_guard lock(*options_.session_mutex);
      return append_error(store, error);
    }
    return append_error(store, error);
  };
  struct ActiveTurnUserMessage
  {
    std::string id;
    std::string text;
    std::vector<ava::session::ImageAttachmentRef> image_attachments;
  };
  std::vector<ActiveTurnUserMessage> active_turn_user_messages;
  auto replayable_active_turn_texts = [&]() {
    std::vector<std::string> messages;
    messages.reserve(active_turn_user_messages.size());
    for (auto const& message : active_turn_user_messages) messages.push_back(message.text);
    return messages;
  };
  auto compact_context = [&](std::string_view trigger) -> ava::core::Result<bool> {
    if (!options_.compact_context)
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::Provider, "context compaction is unavailable");
      error.with_context("trigger", std::string(trigger));
      return std::unexpected(std::move(error));
    }
    auto const replayed_messages = replayable_active_turn_texts();
    return options_.compact_context(store, trigger, replayed_messages);
  };
  auto append_active_turn_user_message_locked = [&](std::string const& text,
                                                    std::vector<ava::session::ImageAttachmentRef> const& attachments) -> ava::core::VoidResult {
    auto appended = append_user_message_locked(text, attachments);
    if (!appended)
      return std::unexpected(std::move(appended.error()));
    active_turn_user_messages.push_back(ActiveTurnUserMessage{.id = *appended, .text = text, .image_attachments = attachments});
    return {};
  };
  auto replay_active_turn_user_messages_locked = [&]() -> ava::core::VoidResult {
    for (auto const& message : active_turn_user_messages)
    {
      auto replayed = [&]() -> ava::core::VoidResult {
        if (options_.session_mutex)
        {
          std::lock_guard lock(*options_.session_mutex);
          return append_replay_user_message(store, message.text, message.image_attachments, message.id);
        }
        return append_replay_user_message(store, message.text, message.image_attachments, message.id);
      }();
      if (!replayed)
        return replayed;
    }
    return {};
  };
  bool context_overflow_retry_used = false;
  bool skip_auto_compaction_after_overflow_retry = false;
  auto prepare_context_overflow_retry = [&](ava::core::Error const& error) -> ava::core::Result<bool> {
    if (!ava::provider::is_context_overflow_error(error) || context_overflow_retry_used || !options_.compact_context)
    {
      return false;
    }
    context_overflow_retry_used = true;
    if (auto not_canceled = check_canceled_locked("before_context_overflow_compaction"); !not_canceled)
    {
      return std::unexpected(std::move(not_canceled.error()));
    }
    auto compacted = compact_context("context_overflow");
    if (!compacted)
    {
      auto compact_error = ava::core::Error(ava::core::ErrorCategory::Provider, "context overflow compaction failed");
      compact_error.with_context("provider_error", error.format());
      compact_error.with_context("compaction_error", compacted.error().format());
      return std::unexpected(std::move(compact_error));
    }
    if (*compacted)
    {
      if (auto replayed = replay_active_turn_user_messages_locked(); !replayed)
      {
        return std::unexpected(std::move(replayed.error()));
      }
      skip_auto_compaction_after_overflow_retry = true;
    }
    if (auto not_canceled = check_canceled_locked("after_context_overflow_compaction"); !not_canceled)
    {
      return std::unexpected(std::move(not_canceled.error()));
    }
    return true;
  };

  if (auto not_canceled = check_canceled_locked("before_turn_start"); !not_canceled)
  {
    return std::unexpected(std::move(not_canceled.error()));
  }
  bool pre_turn_compacted = false;
  if (options_.compact_context)
  {
    auto compacted = compact_context("auto");
    if (!compacted)
      return std::unexpected(std::move(compacted.error()));
    pre_turn_compacted = *compacted;
    if (auto not_canceled = check_canceled_locked("after_pre_turn_auto_compaction"); !not_canceled)
    {
      return std::unexpected(std::move(not_canceled.error()));
    }
  }
  if (auto appended = append_active_turn_user_message_locked(user_message, image_attachments); !appended)
    return std::unexpected(appended.error());

  AgentLoopResult result;
  auto const subagents = options_.subagents.empty() ? builtin_subagents() : options_.subagents;
  auto run_task_subagent = [&](ava::tools::TaskSubagentRequest const& request) -> ava::core::Result<ava::tools::TaskSubagentResult> {
    auto const session_root = store.session_path().parent_path().parent_path();
    if (request.background && request.task_id)
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "background task cannot resume an existing session");
      error.with_context("task_id", *request.task_id);
      return std::unexpected(std::move(error));
    }
    if (request.background && (!options_.background_provider_factory || !options_.background_transport_factory || !options_.background_jobs))
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::Tool, "background task subagents are unavailable");
      error.with_context("subagent_type", request.subagent_type);
      return std::unexpected(std::move(error));
    }

    auto child_store_result = request.task_id ? ava::session::SessionStore::open(options_.workspace_dir, *request.task_id, session_root)
                                              : ava::session::SessionStore::create(options_.workspace_dir, session_root);
    if (!child_store_result)
      return std::unexpected(std::move(child_store_result.error()));
    auto child_store = std::move(*child_store_result);

    if (!request.task_id)
    {
      auto name = request.description + " (@" + request.subagent_type + " subagent)";
      if (name.size() > ava::session::kMaxSessionNameBytes)
      {
        name.resize(ava::session::kMaxSessionNameBytes);
      }
      auto metadata = ava::session::append_session_metadata(
          child_store, ava::session::SessionMetadataUpdate{.name = std::move(name), .parent_session_id = store.session_id(), .actor = "subagent"});
      if (!metadata)
        return std::unexpected(std::move(metadata.error()));
    }

    auto child_options = options_;
    child_options.system_prompt = subagent_system_prompt(options_.system_prompt, request.subagent_system_prompt);
    child_options.tool_visibility = subagent_tool_visibility(options_.tool_visibility, request.tool_preset);
    child_options.max_tool_iterations = std::min<std::size_t>(child_options.max_tool_iterations, 6);
    child_options.on_tool_event = nullptr;
    child_options.on_tool_progress = nullptr;
    child_options.on_stream_event = nullptr;
    child_options.take_steering_messages = nullptr;
    child_options.compact_context = nullptr;
    child_options.background_provider_factory = nullptr;
    child_options.background_transport_factory = nullptr;
    child_options.background_jobs = nullptr;
    // A child owns a fresh lifecycle/session identity. Parent IDs are typed
    // correlation metadata only and never become child lifecycle IDs.
    child_options.trace_context = {.run_id = {},
                                   .turn_id = {},
                                   .session_id = {},
                                   .provider_id = options_.provider_id,
                                   .parent_run_id = trace_context.run_id,
                                   .parent_turn_id = trace_context.turn_id,
                                   .parent_session_id = store.session_id()};

    if (request.background)
    {
      auto const task_id = child_store.session_id();
      auto const session_path = child_store.session_path();
      auto background_provider = options_.background_provider_factory();
      if (!background_provider)
        return std::unexpected(std::move(background_provider.error()));
      auto background_transport = options_.background_transport_factory();
      if (!background_transport)
        return std::unexpected(std::move(background_transport.error()));
      auto provider_instance = std::move(*background_provider);
      auto transport_instance = std::move(*background_transport);
      auto parent_store = store.detached_copy_for_background_persistence();
      child_options.permission_resolver = nullptr;
      child_options.question_resolver = nullptr;
      child_options.session_mutex = nullptr;
      child_options.lsp_diagnostics_provider = nullptr;
      struct BackgroundTaskRunState
      {
        ava::session::SessionStore child_store;
        AgentLoopOptions child_options;
        std::string prompt;
        ava::session::SessionStore parent_store;
        std::string task_id;
        std::string subagent_type;
        std::unique_ptr<ava::provider::Provider> provider_instance;
        std::unique_ptr<ava::provider::Transport> transport_instance;
      };
      auto run_state = std::make_shared<BackgroundTaskRunState>(BackgroundTaskRunState{.child_store = std::move(child_store),
                                                                                       .child_options = std::move(child_options),
                                                                                       .prompt = request.prompt,
                                                                                       .parent_store = std::move(parent_store),
                                                                                       .task_id = task_id,
                                                                                       .subagent_type = request.subagent_type,
                                                                                       .provider_instance = std::move(provider_instance),
                                                                                       .transport_instance = std::move(transport_instance)});
      auto job = options_.background_jobs->start(
          BackgroundJobStartOptions{.title = request.description,
                                    .description = request.prompt,
                                    .subagent_type = request.subagent_type,
                                    .child_session_id = task_id,
                                    .child_session_path = session_path},
          [run_state = std::move(run_state)](BackgroundJobContext const& context) mutable {
            run_state->child_options.cancel_requested = [stop_token = context.stop_token] { return stop_token.stop_requested(); };
            AgentLoop child_loop(std::move(run_state->child_options));
            auto child_result = child_loop.run_turn(run_state->prompt, run_state->child_store, *run_state->provider_instance, *run_state->transport_instance);
            if (!child_result)
            {
              auto error = child_result.error();
              if (!context.stop_token.stop_requested())
              {
                append_background_error_best_effort(run_state->child_store, error);
                append_background_parent_error_best_effort(std::move(run_state->parent_store), run_state->task_id, run_state->subagent_type, error);
              }
              return background_failure_completion(context, error);
            }
            return BackgroundJobCompletion{.state = BackgroundJobState::Completed,
                                           .final_text = child_result->final_text,
                                           .stop_reason = child_result->stop_reason.empty() ? "completed" : child_result->stop_reason};
          });
      if (!job)
        return std::unexpected(std::move(job.error()));
      return ava::tools::TaskSubagentResult{.task_id = task_id,
                                            .job_id = job->job_id,
                                            .session_path = session_path,
                                            .subagent_type = request.subagent_type,
                                            .state = to_string(job->state),
                                            .final_text = "",
                                            .stop_reason = "background",
                                            .provider_iterations = 0,
                                            .tool_calls = 0,
                                            .tool_iterations = 0};
    }

    AgentLoop child_loop(std::move(child_options));
    auto child_result = child_loop.run_turn(request.prompt, child_store, provider, transport);
    if (!child_result)
      return std::unexpected(std::move(child_result.error()));
    return ava::tools::TaskSubagentResult{.task_id = child_store.session_id(),
                                          .job_id = "",
                                          .session_path = child_store.session_path(),
                                          .subagent_type = request.subagent_type,
                                          .final_text = child_result->final_text,
                                          .stop_reason = child_result->stop_reason,
                                          .provider_iterations = child_result->provider_iterations,
                                          .tool_calls = child_result->tool_calls,
                                          .tool_iterations = child_result->tool_iterations};
  };
  ava::tools::ToolContext tool_context{.workspace_dir = options_.workspace_dir,
                                       .spill_dir = store.session_path().parent_path() / "spill",
                                       .mode = options_.mode,
                                       .permission_resolver = options_.permission_resolver,
                                       .permission_audit_sink = append_permission_decision_locked,
                                       .progress_sink = [this](ava::tools::ToolProgressEvent const& event) -> ava::core::VoidResult {
                                         return publish_tool_progress(
                                             options_,
                                             ToolProgressEntry{.call_id = event.call_id, .name = event.tool_name, .text = event.text, .status = event.status});
                                       },
                                       .cancel_requested = options_.cancel_requested,
                                       .question_resolver = options_.question_resolver,
                                       .task_subagent_runner = run_task_subagent,
                                       .subagents = subagents,
                                       .lsp_diagnostics_provider = options_.lsp_diagnostics_provider,
                                       .plugin_global_plugins_dir = options_.plugin_global_plugins_dir,
                                       .plugin_project_plugins_dir = options_.plugin_project_plugins_dir,
                                       .plugin_enablement_file = options_.plugin_enablement_file,
                                       .include_project_plugins = options_.include_project_resources,
                                       .include_project_mcp_config = options_.include_project_resources,
                                       .include_project_skills = options_.include_project_resources,
                                       .session_id = store.session_id(),
                                       .provider_id = options_.provider_id,
                                       .model_id = options_.model_id,
                                       .current_dir = options_.current_dir.empty() ? options_.workspace_dir : options_.current_dir,
                                       .tool_visibility = options_.tool_visibility};
  if (options_.observation && options_.observation->enabled())
  {
    try
    {
      tool_context.observation = options_.observation;
      tool_context.trace_context = trace_context;
    }
    catch (...)
    {
      tool_context.observation.reset();
      tool_context.trace_context = {};
      options_.observation->account_external_failure();
    }
  }
  std::optional<ToolDispatcher> dispatcher_storage;
  try
  {
    dispatcher_storage.emplace(tool_context);
  }
  catch (...)
  {
    // ToolDispatcher owns a copy of ToolContext. If only that observation
    // setup cannot be prepared, retry with the exact baseline context.
    if (!tool_context.observation)
      throw;
    auto observation = std::move(tool_context.observation);
    tool_context.trace_context = {};
    observation->account_external_failure();
    dispatcher_storage.emplace(tool_context);
  }
  ToolDispatcher const& dispatcher = *dispatcher_storage;

  std::size_t tool_iterations = 0;
  bool accumulated_cost_known = true;
  while (true)
  {
    if (auto not_canceled = check_canceled_locked("before_provider_call"); !not_canceled)
    {
      return std::unexpected(std::move(not_canceled.error()));
    }

    if (options_.take_steering_messages)
    {
      auto steering_messages = options_.take_steering_messages();
      if (!steering_messages)
        return std::unexpected(std::move(steering_messages.error()));
      for (auto const& steering_message : *steering_messages)
      {
        if (auto appended = append_active_turn_user_message_locked(steering_message, {}); !appended)
        {
          return std::unexpected(appended.error());
        }
      }
    }

    if (skip_auto_compaction_after_overflow_retry)
    {
      skip_auto_compaction_after_overflow_retry = false;
    }
    else if (options_.compact_context && result.provider_iterations == 0 && !pre_turn_compacted)
    {
      auto compacted = compact_context("auto");
      if (!compacted)
        return std::unexpected(std::move(compacted.error()));
      if (*compacted)
      {
        if (auto replayed = replay_active_turn_user_messages_locked(); !replayed)
        {
          return std::unexpected(std::move(replayed.error()));
        }
      }
      if (auto not_canceled = check_canceled_locked("after_auto_compaction"); !not_canceled)
      {
        return std::unexpected(std::move(not_canceled.error()));
      }
    }

    auto messages = build_messages_locked();
    if (!messages)
      return std::unexpected(messages.error());
    auto const tool_schemas = options_.model_supports_tools ? ToolDispatcher::tool_schemas_json(tool_context) : std::vector<std::string>{};
    ava::provider::ProviderRequest provider_request{.provider_id = options_.provider_id,
                                                    .model_id = options_.model_id,
                                                    .system_prompt = options_.system_prompt,
                                                    .messages = messages->messages,
                                                    .tools_json = tool_schemas,
                                                    .stream = options_.stream && options_.model_supports_streaming,
                                                    .max_output_tokens = options_.model_max_output_tokens,
                                                    .reasoning = options_.reasoning};
    bool const model_supports_images =
        std::find(options_.model_input_modalities.begin(), options_.model_input_modalities.end(), "image") != options_.model_input_modalities.end();
    if (auto valid_images = ava::provider::validate_image_content_parts(provider_request, model_supports_images); !valid_images)
    {
      static_cast<void>(append_error_locked(valid_images.error()));
      return std::unexpected(std::move(valid_images.error()));
    }
    if (auto attached_images = attach_verified_image_payloads(provider_request, store); !attached_images)
    {
      static_cast<void>(append_error_locked(attached_images.error()));
      return std::unexpected(std::move(attached_images.error()));
    }
    ava::provider::ProviderAuthContext const auth_context{
        .access_token = options_.access_token,
        .credential_type = options_.openai_oauth && options_.credential_type == "bearer" ? "oauth" : options_.credential_type,
        .account_id = options_.openai_account_id};
    auto request = provider.build_request(provider_request, auth_context);
    if (!request)
    {
      if (auto retry = prepare_context_overflow_retry(request.error()); !retry)
      {
        return std::unexpected(std::move(retry.error()));
      }
      else if (*retry)
      {
        continue;
      }
      static_cast<void>(append_error_locked(request.error()));
      return std::unexpected(request.error());
    }
    result.used_compacted_context = result.used_compacted_context || messages->used_compacted_context;
    if (result.provider_iterations == 0)
    {
      result.initial_context_messages = provider_request.messages.size();
    }
    std::vector<ava::provider::StreamEvent> provider_events;
    std::size_t streamed_assistant_text_bytes = 0;
    std::map<std::string, std::size_t> streamed_tool_argument_bytes;
    bool processed_stream_chunks = false;
    auto append_stream_events = [&](std::vector<ava::provider::StreamEvent> new_events, bool publish_all_events = true) -> ava::core::VoidResult {
      for (auto& event : new_events)
      {
        if (options_.max_provider_events > 0 && provider_events.size() >= options_.max_provider_events)
        {
          return std::unexpected(output_limit_error("provider output event limit exceeded", "max_provider_events", options_.max_provider_events));
        }
        if (event.type == ava::provider::StreamEventType::TextDelta)
        {
          if (would_exceed(streamed_assistant_text_bytes, event.text.size(), options_.max_assistant_text_bytes))
          {
            return std::unexpected(output_limit_error("assistant text byte limit exceeded", "max_assistant_text_bytes", options_.max_assistant_text_bytes));
          }
          streamed_assistant_text_bytes += event.text.size();
        }
        else if (event.type == ava::provider::StreamEventType::ReasoningStart || event.type == ava::provider::StreamEventType::ReasoningDelta ||
                 event.type == ava::provider::StreamEventType::ReasoningEnd)
        {
          auto const event_bytes = event.type == ava::provider::StreamEventType::ReasoningEnd
                                       ? event.reasoning_signature.size() + event.reasoning_redacted_data.size()
                                       : event.text.size() + event.reasoning_signature.size() + event.reasoning_redacted_data.size();
          if (would_exceed(streamed_assistant_text_bytes, event_bytes, options_.max_assistant_text_bytes))
          {
            return std::unexpected(output_limit_error("reasoning byte limit exceeded", "max_assistant_text_bytes", options_.max_assistant_text_bytes));
          }
          streamed_assistant_text_bytes += event_bytes;
        }
        else if (event.type == ava::provider::StreamEventType::ToolCallStart)
        {
          if (auto valid_id = validate_provider_tool_call_id(event.tool_call_id); !valid_id)
          {
            return std::unexpected(std::move(valid_id.error()));
          }
        }
        else if (event.type == ava::provider::StreamEventType::ToolCallDelta)
        {
          if (auto valid_id = validate_provider_tool_call_id(event.tool_call_id); !valid_id)
          {
            return std::unexpected(std::move(valid_id.error()));
          }
          auto& bytes = streamed_tool_argument_bytes[event.tool_call_id];
          if (would_exceed(bytes, event.text.size(), options_.max_tool_argument_bytes))
          {
            return std::unexpected(output_limit_error("tool argument byte limit exceeded", "max_tool_argument_bytes", options_.max_tool_argument_bytes));
          }
          bytes += event.text.size();
        }
        // Trace parser output before publishing the product event: an observer
        // failure is isolated and cannot suppress or reorder product output.
        if (options_.observation)
        {
          options_.observation->emit(ava::observability::TraceEventType::ProviderStreamEvent, trace_context, [&event](auto& trace) {
            trace.phase = ava::observability::TracePhase::Provider;
            switch (event.type)
            {
              case ava::provider::StreamEventType::TextDelta:
                trace.outcome = ava::observability::TraceOutcome::TextDelta;
                break;
              case ava::provider::StreamEventType::ReasoningStart:
                trace.outcome = ava::observability::TraceOutcome::ReasoningStart;
                break;
              case ava::provider::StreamEventType::ReasoningDelta:
                trace.outcome = ava::observability::TraceOutcome::ReasoningDelta;
                break;
              case ava::provider::StreamEventType::ReasoningEnd:
                trace.outcome = ava::observability::TraceOutcome::ReasoningEnd;
                break;
              case ava::provider::StreamEventType::ToolCallStart:
                trace.outcome = ava::observability::TraceOutcome::ToolCallStart;
                break;
              case ava::provider::StreamEventType::ToolCallDelta:
                trace.outcome = ava::observability::TraceOutcome::ToolCallDelta;
                break;
              case ava::provider::StreamEventType::ToolCallEnd:
                trace.outcome = ava::observability::TraceOutcome::ToolCallEnd;
                break;
              case ava::provider::StreamEventType::Done:
                trace.outcome = ava::observability::TraceOutcome::Done;
                break;
              case ava::provider::StreamEventType::Error:
                trace.outcome = ava::observability::TraceOutcome::Error;
                break;
            }
            trace.fields = {{.key = "text_bytes", .value = std::to_string(event.text.size())},
                            {.key = "tool_name", .value = "[omitted]", .provenance = ava::observability::FieldProvenance::Content},
                            {.key = "usage_present", .value = event.usage ? "true" : "false"}};
          });
        }
        bool const should_publish = publish_all_events || event.type == ava::provider::StreamEventType::ReasoningStart ||
                                    event.type == ava::provider::StreamEventType::ReasoningDelta || event.type == ava::provider::StreamEventType::ReasoningEnd;
        if (should_publish)
        {
          if (auto published = publish_stream_event(options_, event); !published)
          {
            return std::unexpected(std::move(published.error()));
          }
        }
        provider_events.push_back(std::move(event));
      }
      return {};
    };

    if (provider_request.stream && effective_transport->supports_streaming())
    {
      auto stream_parser = provider.create_stream_parser();
      auto response = effective_transport->send_streaming(
          *request,
          [&](std::string_view chunk) -> ava::core::VoidResult {
            processed_stream_chunks = true;
            if (is_canceled(options_))
            {
              return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "agent loop canceled"));
            }
            auto parsed = stream_parser->append(chunk);
            if (!parsed)
              return std::unexpected(std::move(parsed.error()));
            return append_stream_events(std::move(*parsed));
          },
          [&options = options_]() { return is_canceled(options); });
      if (!response)
      {
        if (is_canceled(options_))
        {
          if (auto not_canceled = check_canceled_locked("during_provider_stream"); !not_canceled)
          {
            return std::unexpected(std::move(not_canceled.error()));
          }
        }
        if (auto retry = prepare_context_overflow_retry(response.error()); !retry)
        {
          return std::unexpected(std::move(retry.error()));
        }
        else if (*retry)
        {
          continue;
        }
        static_cast<void>(append_error_locked(response.error()));
        return std::unexpected(response.error());
      }
      if (auto not_canceled = check_canceled_locked("after_provider_call"); !not_canceled)
      {
        return std::unexpected(std::move(not_canceled.error()));
      }
      if (response->status_code < 200 || response->status_code >= 300)
      {
        auto events = provider.parse_response(*response, provider_request.stream);
        if (!events)
        {
          if (auto retry = prepare_context_overflow_retry(events.error()); !retry)
          {
            return std::unexpected(std::move(retry.error()));
          }
          else if (*retry)
          {
            continue;
          }
          static_cast<void>(append_error_locked(events.error()));
          return std::unexpected(events.error());
        }
        if (auto appended = append_stream_events(std::move(*events)); !appended)
        {
          if (auto retry = prepare_context_overflow_retry(appended.error()); !retry)
          {
            return std::unexpected(std::move(retry.error()));
          }
          else if (*retry)
          {
            continue;
          }
          static_cast<void>(append_error_locked(appended.error()));
          return std::unexpected(std::move(appended.error()));
        }
      }
      else if (!processed_stream_chunks && provider_events.empty() && !response->body.empty())
      {
        auto events = provider.parse_response(*response, provider_request.stream);
        if (!events)
        {
          if (auto retry = prepare_context_overflow_retry(events.error()); !retry)
          {
            return std::unexpected(std::move(retry.error()));
          }
          else if (*retry)
          {
            continue;
          }
          static_cast<void>(append_error_locked(events.error()));
          return std::unexpected(events.error());
        }
        if (auto appended = append_stream_events(std::move(*events)); !appended)
        {
          if (is_canceled(options_))
          {
            if (auto not_canceled = check_canceled_locked("during_provider_stream"); !not_canceled)
            {
              return std::unexpected(std::move(not_canceled.error()));
            }
          }
          if (auto retry = prepare_context_overflow_retry(appended.error()); !retry)
          {
            return std::unexpected(std::move(retry.error()));
          }
          else if (*retry)
          {
            continue;
          }
          static_cast<void>(append_error_locked(appended.error()));
          return std::unexpected(std::move(appended.error()));
        }
      }
      else
      {
        auto parsed = stream_parser->finish();
        if (!parsed)
        {
          if (auto retry = prepare_context_overflow_retry(parsed.error()); !retry)
          {
            return std::unexpected(std::move(retry.error()));
          }
          else if (*retry)
          {
            continue;
          }
          static_cast<void>(append_error_locked(parsed.error()));
          return std::unexpected(parsed.error());
        }
        if (auto appended = append_stream_events(std::move(*parsed)); !appended)
        {
          if (is_canceled(options_))
          {
            if (auto not_canceled = check_canceled_locked("during_provider_stream"); !not_canceled)
            {
              return std::unexpected(std::move(not_canceled.error()));
            }
          }
          if (auto retry = prepare_context_overflow_retry(appended.error()); !retry)
          {
            return std::unexpected(std::move(retry.error()));
          }
          else if (*retry)
          {
            continue;
          }
          static_cast<void>(append_error_locked(appended.error()));
          return std::unexpected(std::move(appended.error()));
        }
      }
    }
    else
    {
      auto response = effective_transport->send(*request, [&options = options_]() { return is_canceled(options); });
      if (!response)
      {
        if (is_canceled(options_))
        {
          if (auto not_canceled = check_canceled_locked("during_provider_request"); !not_canceled)
          {
            return std::unexpected(std::move(not_canceled.error()));
          }
        }
        if (auto retry = prepare_context_overflow_retry(response.error()); !retry)
        {
          return std::unexpected(std::move(retry.error()));
        }
        else if (*retry)
        {
          continue;
        }
        static_cast<void>(append_error_locked(response.error()));
        return std::unexpected(response.error());
      }
      if (auto not_canceled = check_canceled_locked("after_provider_call"); !not_canceled)
      {
        return std::unexpected(std::move(not_canceled.error()));
      }
      auto events = provider.parse_response(*response, provider_request.stream);
      if (!events)
      {
        if (auto retry = prepare_context_overflow_retry(events.error()); !retry)
        {
          return std::unexpected(std::move(retry.error()));
        }
        else if (*retry)
        {
          continue;
        }
        static_cast<void>(append_error_locked(events.error()));
        return std::unexpected(events.error());
      }
      if (auto appended = append_stream_events(std::move(*events), false); !appended)
      {
        if (auto retry = prepare_context_overflow_retry(appended.error()); !retry)
        {
          return std::unexpected(std::move(retry.error()));
        }
        else if (*retry)
        {
          continue;
        }
        static_cast<void>(append_error_locked(appended.error()));
        return std::unexpected(std::move(appended.error()));
      }
    }

    auto turn = parse_assistant_turn(provider_events, ProviderOutputLimits{.max_events = options_.max_provider_events,
                                                                           .max_assistant_text_bytes = options_.max_assistant_text_bytes,
                                                                           .max_tool_argument_bytes = options_.max_tool_argument_bytes});
    if (!turn)
    {
      if (auto retry = prepare_context_overflow_retry(turn.error()); !retry)
      {
        return std::unexpected(std::move(retry.error()));
      }
      else if (*retry)
      {
        continue;
      }
      static_cast<void>(append_error_locked(turn.error()));
      return std::unexpected(turn.error());
    }
    if (auto not_canceled = check_canceled_locked("before_assistant_append"); !not_canceled)
    {
      return std::unexpected(std::move(not_canceled.error()));
    }

    ++result.provider_iterations;
    auto usage = turn->usage ? with_total_tokens(*turn->usage) : estimate_usage_from_turn(request->body, *turn);
    auto const cost_usd =
        options_.model_pricing && !usage.estimated ? ava::config::usage_cost_usd(*options_.model_pricing, usage) : std::optional<long double>{};
    accumulate_usage(result.usage, usage);
    if (cost_usd && accumulated_cost_known)
    {
      result.cost_usd = result.cost_usd.value_or(0.0L) + *cost_usd;
    }
    else
    {
      accumulated_cost_known = false;
      result.cost_usd = std::nullopt;
    }
    if (auto appended = append_reasoning_blocks_locked(turn->reasoning_blocks); !appended)
    {
      return std::unexpected(appended.error());
    }
    if (auto appended = append_assistant_message_locked(turn->text, turn->tool_calls.size(), usage, cost_usd); !appended)
    {
      return std::unexpected(appended.error());
    }

    if (turn->tool_calls.empty())
    {
      result.final_text = turn->text;
      result.tool_iterations = tool_iterations;
      result.stop_reason = turn->stop_reason.empty() ? "completed" : turn->stop_reason;
      return result;
    }

    auto dispatch_and_commit_tool = [&](ProviderToolCall const& call) -> ava::core::Result<ToolDispatchResult> {
      if (auto not_canceled = check_canceled_locked("before_tool_dispatch"); !not_canceled)
      {
        return std::unexpected(std::move(not_canceled.error()));
      }
      if (auto not_canceled = check_canceled_locked("before_tool_call_append"); !not_canceled)
      {
        return std::unexpected(std::move(not_canceled.error()));
      }
      if (auto appended = append_tool_call_locked(call); !appended)
        return std::unexpected(appended.error());
      ToolTimelineEntry timeline_entry{.status = ToolTimelineStatus::Running,
                                       .call_id = call.id,
                                       .name = call.name,
                                       .argument_summary = summarize_tool_arguments(call),
                                       .result_summary = "",
                                       .arguments_json = call.arguments_json};
      publish_tool_event(options_, timeline_entry);
      auto dispatch = dispatcher.dispatch(call);
      auto dispatch_result = dispatch ? *dispatch : synthetic_failed_dispatch_result(call, dispatch.error());
      dispatch_result.payload.summary = summarize_tool_result(dispatch_result);
      if (auto appended = append_tool_result_locked(dispatch_result); !appended)
      {
        return std::unexpected(appended.error());
      }
      if (!dispatch)
      {
        timeline_entry.status = ToolTimelineStatus::Error;
      }
      else if (dispatch_result.payload.status == ToolResultStatus::Canceled)
      {
        timeline_entry.status = ToolTimelineStatus::Canceled;
      }
      else
      {
        timeline_entry.status = dispatch_result.success ? ToolTimelineStatus::Success : ToolTimelineStatus::Error;
      }
      timeline_entry.result_summary = dispatch_result.payload.summary;
      populate_tool_timeline_metadata(timeline_entry, dispatch_result);
      result.tool_timeline.push_back(timeline_entry);
      publish_tool_event(options_, timeline_entry);
      ++result.tool_calls;
      if (auto not_canceled = check_canceled_locked("after_tool_dispatch"); !not_canceled)
      {
        return std::unexpected(std::move(not_canceled.error()));
      }
      return dispatch_result;
    };

    auto commit_buffered_tool = [&](ProviderToolCall const& call, ToolDispatchResult dispatch_result,
                                    BufferedToolCallbacks const& callbacks) -> ava::core::VoidResult {
      dispatch_result.payload.summary = summarize_tool_result(dispatch_result);
      if (auto appended = append_tool_call_locked(call); !appended)
        return std::unexpected(appended.error());
      ToolTimelineEntry timeline_entry{.status = ToolTimelineStatus::Running,
                                       .call_id = call.id,
                                       .name = call.name,
                                       .argument_summary = summarize_tool_arguments(call),
                                       .result_summary = "",
                                       .arguments_json = call.arguments_json};
      publish_tool_event(options_, timeline_entry);
      for (auto const& event : callbacks.permission_audits)
      {
        if (auto appended = append_permission_decision_locked(event); !appended)
        {
          return std::unexpected(appended.error());
        }
      }
      for (auto const& event : callbacks.progress_events)
      {
        if (auto published = publish_tool_progress(
                options_, ToolProgressEntry{.call_id = event.call_id, .name = event.tool_name, .text = event.text, .status = event.status});
            !published)
        {
          return std::unexpected(std::move(published.error()));
        }
      }
      if (auto appended = append_tool_result_locked(dispatch_result); !appended)
      {
        return std::unexpected(appended.error());
      }
      if (dispatch_result.payload.status == ToolResultStatus::Canceled)
      {
        timeline_entry.status = ToolTimelineStatus::Canceled;
      }
      else
      {
        timeline_entry.status = dispatch_result.success ? ToolTimelineStatus::Success : ToolTimelineStatus::Error;
      }
      timeline_entry.result_summary = dispatch_result.payload.summary;
      populate_tool_timeline_metadata(timeline_entry, dispatch_result);
      result.tool_timeline.push_back(timeline_entry);
      publish_tool_event(options_, timeline_entry);
      ++result.tool_calls;
      return {};
    };

    auto run_parallel_epoch_and_commit = [&](std::span<ToolScheduleSlot const> epoch) -> ava::core::VoidResult {
      if (auto not_canceled = check_canceled_locked("before_parallel_tool_epoch"); !not_canceled)
      {
        return std::unexpected(std::move(not_canceled.error()));
      }

      auto const max_workers = std::max<std::size_t>(1, options_.parallel_read_search_max_workers);
      std::stop_source schedule_stop_source;
      std::vector<BufferedToolCallbacks> callbacks_by_provider_index(turn->tool_calls.size());
      std::vector<std::optional<ToolDispatchResult>> dispatch_results_by_provider_index(turn->tool_calls.size());
      std::mutex buffered_results_mutex;
      auto commit_recorded_prefix = [&]() -> ava::core::VoidResult {
        for (auto const& slot : epoch)
        {
          if (slot.provider_index >= dispatch_results_by_provider_index.size() || !dispatch_results_by_provider_index[slot.provider_index])
          {
            break;
          }
          auto const& callbacks = callbacks_by_provider_index[slot.provider_index];
          auto dispatch_result = *dispatch_results_by_provider_index[slot.provider_index];
          if (auto committed = commit_buffered_tool(slot.call, std::move(dispatch_result), callbacks); !committed)
          {
            return std::unexpected(std::move(committed.error()));
          }
        }
        return {};
      };
      auto scheduled = run_parallel_tool_schedule(
          epoch,
          [&](ToolScheduleSlot const& slot, std::stop_token stop_token) -> ava::core::Result<ToolDispatchResult> {
            BufferedToolCallbacks callbacks;
            auto worker_context = tool_context;
            // Parallel slots are preflighted as non-interactive. Drop live
            // resolvers in the worker so a filesystem race that turns an
            // Allow/Deny decision into Ask fails closed instead of prompting
            // from a worker thread.
            worker_context.permission_resolver = nullptr;
            worker_context.question_resolver = nullptr;
            worker_context.task_subagent_runner = nullptr;
            worker_context.lsp_diagnostics_provider = nullptr;
            worker_context.permission_audit_sink = [&callbacks](ava::tools::PermissionAuditEvent const& event) -> ava::core::VoidResult {
              callbacks.permission_audits.push_back(event);
              return {};
            };
            worker_context.progress_sink = [&callbacks](ava::tools::ToolProgressEvent const& event) -> ava::core::VoidResult {
              callbacks.progress_events.push_back(event);
              return {};
            };
            worker_context.cancel_requested = [base_cancel = options_.cancel_requested, stop_token, &schedule_stop_source] {
              if (stop_token.stop_requested())
                return true;
              if (base_cancel && base_cancel())
              {
                schedule_stop_source.request_stop();
                return true;
              }
              return false;
            };

            auto dispatch = dispatcher.dispatch_with_context(std::move(worker_context), slot.call);
            auto dispatch_result = dispatch ? *dispatch : synthetic_failed_dispatch_result(slot.call, dispatch.error());
            dispatch_result.payload.summary = summarize_tool_result(dispatch_result);
            if (slot.provider_index < callbacks_by_provider_index.size())
            {
              std::lock_guard lock(buffered_results_mutex);
              callbacks_by_provider_index[slot.provider_index] = std::move(callbacks);
              dispatch_results_by_provider_index[slot.provider_index] = dispatch_result;
            }
            return dispatch_result;
          },
          ToolParallelScheduleOptions{.max_workers = max_workers, .stop_token = schedule_stop_source.get_token()});
      if (!scheduled)
      {
        auto schedule_error = std::move(scheduled.error());
        if (auto committed = commit_recorded_prefix(); !committed)
        {
          return std::unexpected(std::move(committed.error()));
        }
        if (is_scheduler_canceled_error(schedule_error))
        {
          if (auto not_canceled = check_canceled_locked("after_parallel_tool_epoch"); !not_canceled)
          {
            return std::unexpected(std::move(not_canceled.error()));
          }
        }
        return std::unexpected(std::move(schedule_error));
      }
      for (auto& outcome : *scheduled)
      {
        auto const& callbacks = callbacks_by_provider_index[outcome.slot.provider_index];
        if (auto committed = commit_buffered_tool(outcome.slot.call, std::move(outcome.result), callbacks); !committed)
        {
          return std::unexpected(std::move(committed.error()));
        }
      }
      if (auto not_canceled = check_canceled_locked("after_parallel_tool_epoch"); !not_canceled)
      {
        return std::unexpected(std::move(not_canceled.error()));
      }
      return {};
    };

    auto const registered_tool_metadata = dispatcher.registered_tool_metadata();
    auto schedule = build_sequential_tool_schedule(turn->tool_calls, registered_tool_metadata);
    if (!options_.parallel_read_search_tools)
    {
      auto scheduled = run_sequential_tool_schedule(
          schedule, [&](ToolScheduleSlot const& slot) -> ava::core::Result<ToolDispatchResult> { return dispatch_and_commit_tool(slot.call); });
      if (!scheduled)
      {
        return std::unexpected(std::move(scheduled.error()));
      }
    }
    else
    {
      mark_parallel_ready_slots(schedule, tool_context);
      for (std::size_t index = 0; index < schedule.size();)
      {
        if (is_parallel_ready_slot(schedule[index]))
        {
          auto epoch_end = index + 1;
          while (epoch_end < schedule.size() && is_parallel_ready_slot(schedule[epoch_end])) ++epoch_end;
          if (auto committed = run_parallel_epoch_and_commit(std::span<ToolScheduleSlot const>(schedule).subspan(index, epoch_end - index)); !committed)
          {
            return std::unexpected(std::move(committed.error()));
          }
          index = epoch_end;
          continue;
        }

        auto dispatched = dispatch_and_commit_tool(schedule[index].call);
        if (!dispatched)
        {
          return std::unexpected(std::move(dispatched.error()));
        }
        ++index;
      }
    }

    ++tool_iterations;
    result.tool_iterations = tool_iterations;
    if (tool_iterations >= options_.max_tool_iterations)
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::Provider, "maximum tool iterations reached");
      error.with_context("max_tool_iterations", std::to_string(options_.max_tool_iterations));
      static_cast<void>(append_error_locked(error));
      return std::unexpected(std::move(error));
    }
  }
}

}  // namespace ava::agent
