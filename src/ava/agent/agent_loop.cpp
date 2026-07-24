#include "sys.h"
#include "ava/agent/agent_loop.h"
#include "ava/agent/agent_loop_session.h"
#include "ava/agent/agent_turn_executor_internal.h"
#include "ava/agent/assistant_turn.h"
#include "ava/agent/provider_output_validation.h"
#include "ava/agent/stream_bridge.h"
#include "ava/agent/subagent_config.h"
#include "ava/agent/tool_dispatch_common.h"
#include "ava/agent/tool_dispatcher.h"
#include "ava/agent/tool_result.h"
#include "ava/agent/tool_scheduler.h"
#include "ava/agent/tool_summaries.h"
#include "ava/agent/tool_timeline.h"
#include "ava/agent/usage_accounting.h"
#include "ava/provider/provider_utils.h"
#include "ava/core/json.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
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

constexpr std::string_view kRedactedRunCommand = "<redacted one-shot command>";

bool is_run_command_call(ProviderToolCall const& call)
{
  return call.name == "bash";
}

std::string serialized_tool_arguments(ProviderToolCall const& call)
{
  if (is_run_command_call(call))
    return "{\"command\":\"" + ava::core::json::escape(kRedactedRunCommand) + "\"}";
  return call.arguments_json;
}

ParsedAssistantTurn persistable_turn(ParsedAssistantTurn const& turn)
{
  auto persisted = turn;
  for (auto& call : persisted.tool_calls) call.arguments_json = serialized_tool_arguments(call);
  for (auto& ordered : persisted.ordered_items)
  {
    if (auto* function = std::get_if<AssistantFunctionCallItem>(&ordered.item))
      function->tool_call.arguments_json = serialized_tool_arguments(function->tool_call);
  }
  return persisted;
}

template <typename Cleanup>
class BestEffortScopeExit
{
 public:
  explicit BestEffortScopeExit(Cleanup cleanup) : cleanup_(std::move(cleanup)) { }
  BestEffortScopeExit(BestEffortScopeExit const&) = delete;
  BestEffortScopeExit& operator=(BestEffortScopeExit const&) = delete;
  ~BestEffortScopeExit() noexcept
  {
    try
    {
      cleanup_();
    }
    catch (...)
    {
      // Cleanup persists only a terminal audit record. The original error or
      // cancellation remains authoritative if the append route itself fails.
    }
  }

 private:
  Cleanup cleanup_;
};

struct PendingCommittedToolResult
{
  ProviderToolCall call;
  std::string assistant_output_entry_id;
  bool durably_recorded = false;
};

bool error_has_context(ava::core::Error const& error, std::string_view key, std::string_view value)
{
  return std::ranges::any_of(error.context(), [&](ava::core::ErrorContext const& item) { return item.key == key && item.value == value; });
}

bool is_scheduler_canceled_error(ava::core::Error const& error)
{
  return error_has_context(error, "canceled", "true");
}

bool permission_decision_cannot_ask(ava::tools::ToolContext const& context, ava::permissions::Operation operation, std::filesystem::path const& target_path)
{
  if (context.require_explicit_file_permissions && (operation == ava::permissions::Operation::ReadFile || operation == ava::permissions::Operation::EditFile))
    return false;
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

}  // namespace

AgentLoop::AgentLoop(AgentLoopOptions options) : options_(std::move(options))
{
  auto [roots, over_limit] = detail::bounded_deduplicated_authority_roots(std::move(options_.ava_authority_roots));
  options_.ava_authority_roots = std::move(roots);
  ava_authority_roots_over_limit_ = over_limit;
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
  if (ava_authority_roots_over_limit_)
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "AgentLoop received more than 64 distinct AVA authority roots"));
  }
  if (!store.is_ephemeral() && (!options_.append_entry || !options_.append_batch))
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                            "persistent AgentLoop requires append and batch authority routes before producing records"));
  }
  if (!options_.session_read_authority)
  {
    return std::unexpected(
        ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "AgentLoop requires a lifetime-safe session read authority before reading history"));
  }
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
  detail::AgentTurnExecutor executor(options_, user_message, image_attachments, store, provider, transport, trace_context);
  return executor.run();
}

ava::core::Result<AgentLoopResult> detail::AgentTurnExecutor::run()
{
  auto finalized_ids_result = session_.persisted_provider_tool_call_ids();
  if (!finalized_ids_result)
    return std::unexpected(std::move(finalized_ids_result.error()));
  finalized_provider_tool_call_ids_ = std::move(*finalized_ids_result);

  if (auto not_canceled = session_.check_canceled("before_turn_start"); !not_canceled)
  {
    return std::unexpected(std::move(not_canceled.error()));
  }
  pre_turn_compacted_ = false;
  if (options_.compact_context)
  {
    auto compacted = compact_context("auto");
    if (!compacted)
      return std::unexpected(std::move(compacted.error()));
    pre_turn_compacted_ = *compacted;
    if (auto not_canceled = session_.check_canceled("after_pre_turn_auto_compaction"); !not_canceled)
    {
      return std::unexpected(std::move(not_canceled.error()));
    }
  }
  if (auto phase = publish_phase(RunPhase::BuildingContext); !phase)
    return std::unexpected(std::move(phase.error()));
  if (auto appended = append_active_turn_user_message(user_message_, image_attachments_); !appended)
    return std::unexpected(appended.error());

  subagents_ = options_.subagents.empty() ? builtin_subagents() : options_.subagents;
  if (auto initialized = initialize_tools(); !initialized)
    return std::unexpected(std::move(initialized.error()));
  auto& tool_context = *tool_context_storage_;
  ToolDispatcher const& dispatcher = *dispatcher_storage_;

  while (true)
  {
    if (auto not_canceled = session_.check_canceled("before_provider_call"); !not_canceled)
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
        if (auto appended = append_active_turn_user_message(steering_message, {}); !appended)
        {
          return std::unexpected(appended.error());
        }
      }
    }

    if (skip_auto_compaction_after_overflow_retry_)
    {
      skip_auto_compaction_after_overflow_retry_ = false;
    }
    else if (options_.compact_context && result_.provider_iterations == 0 && !pre_turn_compacted_)
    {
      auto compacted = compact_context("auto");
      if (!compacted)
        return std::unexpected(std::move(compacted.error()));
      if (*compacted)
      {
        if (auto replayed = replay_active_turn_user_messages(); !replayed)
        {
          return std::unexpected(std::move(replayed.error()));
        }
      }
      if (auto not_canceled = session_.check_canceled("after_auto_compaction"); !not_canceled)
      {
        return std::unexpected(std::move(not_canceled.error()));
      }
    }

    auto messages = build_messages();
    if (!messages)
      return std::unexpected(messages.error());
    auto const tool_schemas = options_.model_supports_tools ? dispatcher.registered_tool_schemas_json() : std::vector<std::string>{};
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
      static_cast<void>(session_.append_error(valid_images.error()));
      return std::unexpected(std::move(valid_images.error()));
    }
    if (auto attached_images = session_.attach_verified_image_payloads(provider_request); !attached_images)
    {
      static_cast<void>(session_.append_error(attached_images.error()));
      return std::unexpected(std::move(attached_images.error()));
    }
    ava::provider::ProviderAuthContext const auth_context{
        .access_token = options_.access_token,
        .credential_type = options_.openai_oauth && options_.credential_type == "bearer" ? "oauth" : options_.credential_type,
        .account_id = options_.openai_account_id};
    auto request = provider_.build_request(provider_request, auth_context);
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
      static_cast<void>(session_.append_error(request.error()));
      return std::unexpected(request.error());
    }
    result_.used_compacted_context = result_.used_compacted_context || messages->used_compacted_context;
    if (result_.provider_iterations == 0)
    {
      result_.initial_context_messages = provider_request.messages.size();
    }
    std::vector<ava::provider::StreamEvent> provider_events;
    std::size_t streamed_assistant_text_bytes = 0;
    std::map<std::string, std::size_t> streamed_tool_argument_bytes;
    std::unordered_map<std::string, std::string> streamed_tool_names;
    std::unordered_set<std::string> current_provider_tool_call_ids;
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
          auto const event_bytes =
              event.type == ava::provider::StreamEventType::ReasoningEnd
                  ? event.reasoning_signature.size() + event.reasoning_redacted_data.size() + event.reasoning_native_item_json.size()
                  : event.text.size() + event.reasoning_signature.size() + event.reasoning_redacted_data.size() + event.reasoning_native_item_json.size();
          if (would_exceed(streamed_assistant_text_bytes, event_bytes, options_.max_assistant_text_bytes))
          {
            return std::unexpected(output_limit_error("reasoning byte limit exceeded", "max_assistant_text_bytes", options_.max_assistant_text_bytes));
          }
          streamed_assistant_text_bytes += event_bytes;
        }
        else if (event.type == ava::provider::StreamEventType::ToolCallStart || event.type == ava::provider::StreamEventType::ToolCallDelta ||
                 event.type == ava::provider::StreamEventType::ToolCallEnd)
        {
          if (auto valid_id = validate_provider_tool_call_id(event.tool_call_id); !valid_id)
          {
            return std::unexpected(std::move(valid_id.error()));
          }
          if (finalized_provider_tool_call_ids_.contains(event.tool_call_id))
          {
            auto error = ava::core::Error(ava::core::ErrorCategory::Provider, "provider reused a finalized tool call id in the persistent session");
            error.with_context("tool_call_id", event.tool_call_id);
            error.with_context("provider_iteration", std::to_string(result_.provider_iterations + 1));
            error.with_context("hint", "provider tool call ids must remain unique for the complete persistent session");
            return std::unexpected(std::move(error));
          }
          current_provider_tool_call_ids.insert(event.tool_call_id);
          if (event.type == ava::provider::StreamEventType::ToolCallDelta)
          {
            auto& bytes = streamed_tool_argument_bytes[event.tool_call_id];
            if (would_exceed(bytes, event.text.size(), options_.max_tool_argument_bytes))
            {
              return std::unexpected(output_limit_error("tool argument byte limit exceeded", "max_tool_argument_bytes", options_.max_tool_argument_bytes));
            }
            bytes += event.text.size();
          }
        }
        // Trace parser output before publishing the product event: an observer
        // failure is isolated and cannot suppress or reorder product output.
        if (options_.observation)
        {
          options_.observation->emit(ava::observability::TraceEventType::ProviderStreamEvent, trace_context_, [&event](auto& trace) {
            trace.phase = ava::observability::TracePhase::Provider;
            switch (event.type)
            {
              case ava::provider::StreamEventType::TextStart:
              case ava::provider::StreamEventType::TextDelta:
              case ava::provider::StreamEventType::TextEnd:
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
        if (event.type == ava::provider::StreamEventType::ToolCallStart && !event.tool_call_id.empty() && !event.tool_name.empty())
          streamed_tool_names[event.tool_call_id] = event.tool_name;

        bool const should_publish = publish_all_events || event.type == ava::provider::StreamEventType::ReasoningStart ||
                                    event.type == ava::provider::StreamEventType::ReasoningDelta || event.type == ava::provider::StreamEventType::ReasoningEnd;
        if (should_publish)
        {
          auto public_event = event;
          if (event.type == ava::provider::StreamEventType::ToolCallStart || event.type == ava::provider::StreamEventType::ToolCallEnd)
          {
            // Start/end carry only lifecycle identity. Never surface an
            // unexpected provider payload from those lifecycle records.
            public_event.text.clear();
          }
          else if (event.type == ava::provider::StreamEventType::ToolCallDelta)
          {
            auto const found = streamed_tool_names.find(event.tool_call_id);
            if (found == streamed_tool_names.end())
            {
              // A malformed stream without a start cannot prove that this is
              // not a shell request. Suppress its arguments rather than
              // exposing a payload before permission mediation.
              public_event.text = "<redacted tool arguments>";
            }
            else if (found->second == "bash")
            {
              public_event.tool_name = found->second;
              public_event.text = std::string(kRedactedRunCommand);
            }
          }
          if (auto published = publish_stream_event(options_, public_event); !published)
          {
            return std::unexpected(std::move(published.error()));
          }
        }
        if (event.type == ava::provider::StreamEventType::ToolCallEnd)
          streamed_tool_names.erase(event.tool_call_id);
        provider_events.push_back(std::move(event));
      }
      return {};
    };

    if (auto phase = publish_phase(RunPhase::AwaitingProvider); !phase)
      return std::unexpected(std::move(phase.error()));
    if (auto not_canceled = session_.check_canceled("before_provider_transport"); !not_canceled)
      return std::unexpected(std::move(not_canceled.error()));
    if (provider_request.stream && effective_transport_->supports_streaming())
    {
      auto stream_parser = provider_.create_stream_parser();
      auto response = effective_transport_->send_streaming(
          *request,
          [&](std::string_view chunk) -> ava::core::VoidResult {
            processed_stream_chunks = true;
            if (session_.is_canceled())
            {
              return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "agent loop canceled"));
            }
            auto parsed = stream_parser->append(chunk);
            if (!parsed)
              return std::unexpected(std::move(parsed.error()));
            return append_stream_events(std::move(*parsed));
          },
          [cancel_requested = options_.cancel_requested]() { return cancel_requested && cancel_requested(); });
      if (!response)
      {
        if (session_.is_canceled())
        {
          if (auto not_canceled = session_.check_canceled("during_provider_stream"); !not_canceled)
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
        static_cast<void>(session_.append_error(response.error()));
        return std::unexpected(response.error());
      }
      if (auto not_canceled = session_.check_canceled("after_provider_call"); !not_canceled)
      {
        return std::unexpected(std::move(not_canceled.error()));
      }
      if (response->status_code < 200 || response->status_code >= 300)
      {
        auto events = provider_.parse_response(*response, provider_request.stream);
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
          static_cast<void>(session_.append_error(events.error()));
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
          static_cast<void>(session_.append_error(appended.error()));
          return std::unexpected(std::move(appended.error()));
        }
      }
      else if (!processed_stream_chunks && provider_events.empty() && !response->body.empty())
      {
        auto events = provider_.parse_response(*response, provider_request.stream);
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
          static_cast<void>(session_.append_error(events.error()));
          return std::unexpected(events.error());
        }
        if (auto appended = append_stream_events(std::move(*events)); !appended)
        {
          if (session_.is_canceled())
          {
            if (auto not_canceled = session_.check_canceled("during_provider_stream"); !not_canceled)
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
          static_cast<void>(session_.append_error(appended.error()));
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
          static_cast<void>(session_.append_error(parsed.error()));
          return std::unexpected(parsed.error());
        }
        if (auto appended = append_stream_events(std::move(*parsed)); !appended)
        {
          if (session_.is_canceled())
          {
            if (auto not_canceled = session_.check_canceled("during_provider_stream"); !not_canceled)
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
          static_cast<void>(session_.append_error(appended.error()));
          return std::unexpected(std::move(appended.error()));
        }
      }
    }
    else
    {
      auto response = effective_transport_->send(*request, [cancel_requested = options_.cancel_requested]() { return cancel_requested && cancel_requested(); });
      if (!response)
      {
        if (session_.is_canceled())
        {
          if (auto not_canceled = session_.check_canceled("during_provider_request"); !not_canceled)
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
        static_cast<void>(session_.append_error(response.error()));
        return std::unexpected(response.error());
      }
      if (auto not_canceled = session_.check_canceled("after_provider_call"); !not_canceled)
      {
        return std::unexpected(std::move(not_canceled.error()));
      }
      auto events = provider_.parse_response(*response, provider_request.stream);
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
        static_cast<void>(session_.append_error(events.error()));
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
        static_cast<void>(session_.append_error(appended.error()));
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
      static_cast<void>(session_.append_error(turn.error()));
      return std::unexpected(turn.error());
    }
    std::unordered_set<std::string> iteration_tool_call_ids;
    for (auto const& call : turn->tool_calls)
    {
      if (!iteration_tool_call_ids.insert(call.id).second)
        continue;  // One provider turn may merge multiple deltas for one finalized call.
      if (!current_provider_tool_call_ids.contains(call.id) || finalized_provider_tool_call_ids_.contains(call.id))
      {
        auto error =
            ava::core::Error(ava::core::ErrorCategory::Provider, "provider reused or inconsistently finalized a tool call id in the persistent session");
        error.with_context("tool_call_id", call.id);
        error.with_context("provider_iteration", std::to_string(result_.provider_iterations + 1));
        error.with_context("hint", "provider tool call ids must remain unique for the complete persistent session");
        static_cast<void>(session_.append_error(error));
        return std::unexpected(std::move(error));
      }
    }
    if (auto not_canceled = session_.check_canceled("before_assistant_append"); !not_canceled)
    {
      return std::unexpected(std::move(not_canceled.error()));
    }

    ++result_.provider_iterations;
    auto usage = turn->usage ? with_total_tokens(*turn->usage) : estimate_usage_from_turn(request->body, *turn);
    auto const cost_usd =
        options_.model_pricing && !usage.estimated ? ava::config::usage_cost_usd(*options_.model_pricing, usage) : std::optional<long double>{};
    accumulate_usage(result_.usage, usage);
    if (cost_usd && accumulated_cost_known_)
    {
      result_.cost_usd = result_.cost_usd.value_or(0.0L) + *cost_usd;
    }
    else
    {
      accumulated_cost_known_ = false;
      result_.cost_usd = std::nullopt;
    }
    if (*turn->finish_reason == ava::provider::ProviderFinishReason::Cancelled)
    {
      result_.final_text = turn->text;
      result_.tool_iterations = tool_iterations_;
      result_.outcome = ava::core::RuntimeTerminalOutcome::Cancelled;
      return result_;
    }
    // Completing is the terminal arbitration boundary. Establish it before
    // persisting a terminal assistant so an accepted stop cannot label durable
    // success as canceled, while a later stop is a bounded no-op.
    if (turn->tool_calls.empty())
    {
      if (auto phase = publish_phase(RunPhase::Completing); !phase)
        return std::unexpected(std::move(phase.error()));
    }
    if (!turn->tool_calls.empty())
    {
      if (auto phase = publish_phase(RunPhase::PersistingAssistant); !phase)
        return std::unexpected(std::move(phase.error()));
    }
    // Permission prompts receive the transient provider call below. Durable
    // assistant-output/session state never retains raw RunCommand argv or
    // shell payloads; it retains only the call identity and a stable marker.
    auto persisted_turn = session_.append_assistant_turn(persistable_turn(*turn), usage, cost_usd);
    if (!persisted_turn)
    {
      return std::unexpected(std::move(persisted_turn.error()));
    }
    result_.committed_turn_id = persisted_turn->committed_turn_id;

    std::vector<PendingCommittedToolResult> pending_tool_results;
    try
    {
      pending_tool_results.reserve(turn->tool_calls.size());
      for (auto const& call : turn->tool_calls)
      {
        auto const binding = persisted_turn->function_output_entry_ids_by_call_id.find(call.id);
        if (binding == persisted_turn->function_output_entry_ids_by_call_id.end())
        {
          auto error = ava::core::Error(ava::core::ErrorCategory::Session, "committed assistant turn is missing a function output binding");
          error.with_context("call_id", call.id).with_context("tool_name", call.name);
          return std::unexpected(std::move(error));
        }
        pending_tool_results.push_back(PendingCommittedToolResult{.call = call, .assistant_output_entry_id = binding->second, .durably_recorded = false});
      }
    }
    catch (...)
    {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "failed to track committed function calls for terminal-result recovery"));
    }
    // Every exit after this point closes only the committed bindings that did
    // not durably receive their normal result. This never dispatches a tool.
    [[maybe_unused]] auto close_unfinished_tool_results = BestEffortScopeExit([&] {
      auto const terminal_status = session_.is_canceled() ? ToolResultStatus::Canceled : ToolResultStatus::Error;
      for (auto& pending : pending_tool_results)
      {
        if (pending.durably_recorded)
          continue;
        auto synthetic = synthetic_terminal_tool_result(pending.call, terminal_status);
        auto appended = session_.append_tool_result(synthetic, pending.assistant_output_entry_id);
        if (appended || error_has_context(appended.error(), "append_commit_state", "committed_to_leased_inode"))
          pending.durably_recorded = true;
      }
    });
    // A tool call becomes finalized only after the committed v4 turn is
    // durable. Incomplete staging is intentionally absent from this set.
    finalized_provider_tool_call_ids_.insert(iteration_tool_call_ids.begin(), iteration_tool_call_ids.end());

    if (turn->tool_calls.empty())
    {
      result_.final_text = turn->text;
      result_.tool_iterations = tool_iterations_;
      switch (*turn->finish_reason)
      {
        case ava::provider::ProviderFinishReason::Completed:
          result_.outcome = ava::core::RuntimeTerminalOutcome::Completed;
          break;
        case ava::provider::ProviderFinishReason::MaxTokens:
          result_.outcome = ava::core::RuntimeTerminalOutcome::MaxTokens;
          break;
        case ava::provider::ProviderFinishReason::Refusal:
          result_.outcome = ava::core::RuntimeTerminalOutcome::Refusal;
          break;
        case ava::provider::ProviderFinishReason::Cancelled:
          result_.outcome = ava::core::RuntimeTerminalOutcome::Cancelled;
          break;
        case ava::provider::ProviderFinishReason::ToolCalls:
        case ava::provider::ProviderFinishReason::Error:
          result_.outcome = ava::core::RuntimeTerminalOutcome::Error;
          break;
      }
      return result_;
    }

    if (auto phase = publish_phase(RunPhase::PreparingTools); !phase)
      return std::unexpected(std::move(phase.error()));
    auto output_binding_for = [&](ProviderToolCall const& call) -> ava::core::Result<std::string_view> {
      auto const found = std::ranges::find_if(
          pending_tool_results, [&](PendingCommittedToolResult const& pending) { return pending.call.id == call.id && pending.call.name == call.name; });
      if (found == pending_tool_results.end())
      {
        auto error = ava::core::Error(ava::core::ErrorCategory::Session, "committed assistant turn is missing a tracked function output binding");
        error.with_context("call_id", call.id).with_context("tool_name", call.name);
        return std::unexpected(std::move(error));
      }
      return found->assistant_output_entry_id;
    };
    auto mark_tool_result_durable = [&](ProviderToolCall const& call, ava::core::VoidResult const& appended) {
      if (!appended && !error_has_context(appended.error(), "append_commit_state", "committed_to_leased_inode"))
        return;
      auto const found = std::ranges::find_if(
          pending_tool_results, [&](PendingCommittedToolResult const& pending) { return pending.call.id == call.id && pending.call.name == call.name; });
      if (found != pending_tool_results.end())
        found->durably_recorded = true;
    };
    auto dispatch_and_commit_tool = [&](ProviderToolCall const& call) -> ava::core::Result<ToolDispatchResult> {
      if (auto phase = publish_phase(RunPhase::ExecutingTools); !phase)
        return std::unexpected(std::move(phase.error()));
      if (auto not_canceled = session_.check_canceled("before_tool_dispatch"); !not_canceled)
      {
        return std::unexpected(std::move(not_canceled.error()));
      }
      if (auto not_canceled = session_.check_canceled("before_tool_call_append"); !not_canceled)
      {
        return std::unexpected(std::move(not_canceled.error()));
      }
      auto assistant_output_entry_id = output_binding_for(call);
      if (!assistant_output_entry_id)
        return std::unexpected(std::move(assistant_output_entry_id.error()));
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
      auto appended = session_.append_tool_result(dispatch_result, *assistant_output_entry_id);
      mark_tool_result_durable(call, appended);
      if (!appended)
      {
        return std::unexpected(std::move(appended.error()));
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
      result_.tool_timeline.push_back(timeline_entry);
      publish_tool_event(options_, timeline_entry);
      ++result_.tool_calls;
      if (auto not_canceled = session_.check_canceled("after_tool_dispatch"); !not_canceled)
      {
        return std::unexpected(std::move(not_canceled.error()));
      }
      return dispatch_result;
    };

    auto commit_buffered_tool = [&](ProviderToolCall const& call, ToolDispatchResult dispatch_result,
                                    BufferedToolCallbacks const& callbacks) -> ava::core::VoidResult {
      dispatch_result.payload.summary = summarize_tool_result(dispatch_result);
      auto assistant_output_entry_id = output_binding_for(call);
      if (!assistant_output_entry_id)
        return std::unexpected(std::move(assistant_output_entry_id.error()));
      ToolTimelineEntry timeline_entry{.status = ToolTimelineStatus::Running,
                                       .call_id = call.id,
                                       .name = call.name,
                                       .argument_summary = summarize_tool_arguments(call),
                                       .result_summary = "",
                                       .arguments_json = call.arguments_json};
      publish_tool_event(options_, timeline_entry);
      for (auto const& event : callbacks.permission_audits)
      {
        if (auto appended = session_.append_permission_decision(event); !appended)
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
      auto appended = session_.append_tool_result(dispatch_result, *assistant_output_entry_id);
      mark_tool_result_durable(call, appended);
      if (!appended)
      {
        return std::unexpected(std::move(appended.error()));
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
      result_.tool_timeline.push_back(timeline_entry);
      publish_tool_event(options_, timeline_entry);
      ++result_.tool_calls;
      return {};
    };

    auto run_parallel_epoch_and_commit = [&](std::span<ToolScheduleSlot const> epoch) -> ava::core::VoidResult {
      // A parallel read/search epoch has one visible execution boundary; worker
      // scheduling itself must not publish duplicate phase changes.
      if (auto phase = publish_phase(RunPhase::ExecutingTools); !phase)
        return std::unexpected(std::move(phase.error()));
      if (auto not_canceled = session_.check_canceled("before_parallel_tool_epoch"); !not_canceled)
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
          if (auto not_canceled = session_.check_canceled("after_parallel_tool_epoch"); !not_canceled)
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
      if (auto not_canceled = session_.check_canceled("after_parallel_tool_epoch"); !not_canceled)
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

    if (auto phase = publish_phase(RunPhase::SettlingTools); !phase)
      return std::unexpected(std::move(phase.error()));
    ++tool_iterations_;
    result_.tool_iterations = tool_iterations_;
    if (tool_iterations_ >= options_.max_tool_iterations)
    {
      if (auto phase = publish_phase(RunPhase::Completing); !phase)
        return std::unexpected(std::move(phase.error()));
      result_.outcome = ava::core::RuntimeTerminalOutcome::MaxTurnRequests;
      return result_;
    }
    if (auto phase = publish_phase(RunPhase::AwaitingProvider); !phase)
      return std::unexpected(std::move(phase.error()));
  }
}

}  // namespace ava::agent
