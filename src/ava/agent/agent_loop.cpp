#include "sys.h"
#include "ava/agent/agent_loop.h"
#include "ava/agent/agent_loop_session.h"
#include "ava/agent/assistant_turn.h"
#include "ava/agent/job_control.h"
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
#include "ava/session/assistant_output.h"
#include "ava/session/attachments.h"
#include "ava/session/session_branch.h"
#include "ava/session/session_metadata.h"
#include "ava/provider/provider_utils.h"
#include "ava/core/AnchorSet.h"
#include "ava/core/json.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
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

constexpr std::size_t kMaxAvaAuthorityRoots = 64;

struct BoundedAuthorityRoots
{
  std::vector<std::filesystem::path> roots;
  bool over_limit = false;
};

BoundedAuthorityRoots bounded_deduplicated_authority_roots(std::vector<std::filesystem::path> roots)
{
  BoundedAuthorityRoots result;
  result.roots.reserve(std::min(roots.size(), kMaxAvaAuthorityRoots));
  for (auto& root : roots)
  {
    if (root.empty())
      continue;
    root = root.lexically_normal();
    if (std::ranges::find(result.roots, root) != result.roots.end())
      continue;
    if (result.roots.size() == kMaxAvaAuthorityRoots)
    {
      result.over_limit = true;
      continue;
    }
    result.roots.push_back(std::move(root));
  }
  return result;
}

void append_authority_root(std::vector<std::filesystem::path>& roots, std::filesystem::path root)
{
  if (root.empty())
    return;
  root = root.lexically_normal();
  if (std::ranges::find(roots, root) == roots.end())
    roots.push_back(std::move(root));
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

ava::core::Result<std::unordered_set<std::string>> persisted_provider_tool_call_ids(ava::session::SessionReadAuthority read_authority,
                                                                                    ava::session::SessionReadLimits const& read_limits)
{
  auto entries = read_authority.load_bounded(read_limits);
  if (!entries)
  {
    auto error = std::move(entries.error());
    error.with_context("operation", "seed persistent provider tool-call ids");
    return std::unexpected(std::move(error));
  }

  std::unordered_set<std::string> ids;
  auto const assistant_output = ava::session::classify_assistant_output(*entries);
  for (auto const& diagnostic : assistant_output.diagnostics)
  {
    if (diagnostic.severity == ava::session::AssistantOutputDiagnosticSeverity::Warning &&
        diagnostic.kind == ava::session::AssistantOutputDiagnosticKind::IncompleteAssistantTurn)
    {
      continue;
    }
    auto error = ava::core::Error(ava::core::ErrorCategory::Session, "persisted assistant-output records are malformed");
    error.with_context("diagnostic", diagnostic.message).with_context("entry_id", diagnostic.entry_id);
    return std::unexpected(std::move(error));
  }
  auto add_id = [&](std::string id, std::string_view source) -> ava::core::VoidResult {
    if (auto valid = validate_provider_tool_call_id(id); !valid)
    {
      auto error = std::move(valid.error());
      error.with_context("source", std::string(source));
      return std::unexpected(std::move(error));
    }
    if (!ids.insert(std::move(id)).second)
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::Session, "provider tool call id is duplicated in persisted session history");
      error.with_context("source", std::string(source));
      return std::unexpected(std::move(error));
    }
    return {};
  };
  for (auto const& entry : *entries)
  {
    if (entry.type == ava::session::EntryType::ToolCall)
    {
      if (auto added = add_id(ava::core::json::string_field(entry.data_json, "call_id").value_or(""), "persisted_session_tool_call"); !added)
        return std::unexpected(std::move(added.error()));
    }
  }
  for (auto const& turn : assistant_output.turns)
  {
    for (auto const& item : turn.items)
    {
      auto const* function = std::get_if<ava::session::AssistantOutputFunctionCall>(&item.item.payload);
      if (!function)
        continue;
      if (auto added = add_id(function->call_id, "committed_assistant_output_function"); !added)
        return std::unexpected(std::move(added.error()));
    }
  }
  return ids;
}

bool is_canceled(AgentLoopOptions const& options)
{
  return options.cancel_requested && options.cancel_requested();
}

ava::core::VoidResult publish_phase(AgentLoopOptions const& options, RunPhase phase)
{
  if (!options.on_phase)
    return {};
  try
  {
    return options.on_phase(phase);
  }
  catch (...)
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "run phase callback threw"));
  }
}

ava::core::VoidResult check_canceled(AgentLoopOptions const& options, ava::session::SessionStore& store, std::string_view boundary)
{
  if (!is_canceled(options))
    return {};
  if (options.append_entry)
    static_cast<void>(append_cancel(options.append_entry, boundary));
  else
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
  add_excluded_tool(parent, "job");
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

void append_subagent_error_best_effort(SessionAppendSink const& append_sink, ava::core::Error const& error)
{
  if (!append_sink)
    return;
  auto safe = ava::core::Error(error.category(), safe_subagent_error_message(error));
  static_cast<void>(append_error(append_sink, safe));
}

bool subagent_terminal(SubagentExecutionState state) noexcept
{
  return state == SubagentExecutionState::Completed || state == SubagentExecutionState::Failed || state == SubagentExecutionState::Canceled ||
         state == SubagentExecutionState::Interrupted;
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
  auto roots = bounded_deduplicated_authority_roots(std::move(options_.ava_authority_roots));
  options_.ava_authority_roots = std::move(roots.roots);
  ava_authority_roots_over_limit_ = roots.over_limit;
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
      return options_.append_entry ? append_user_message(options_.append_entry, text, attachments, options_.synthetic_user_message_provenance)
                                   : append_user_message(store, text, attachments, options_.synthetic_user_message_provenance);
    }
    return options_.append_entry ? append_user_message(options_.append_entry, text, attachments, options_.synthetic_user_message_provenance)
                                 : append_user_message(store, text, attachments, options_.synthetic_user_message_provenance);
  };
  struct ActiveTurnUserMessage
  {
    std::string id;
    std::string text;
    std::vector<ava::session::ImageAttachmentRef> image_attachments;
  };
  std::vector<ActiveTurnUserMessage> active_turn_user_messages;
  auto message_build_options = [&]() {
    auto api_family = options_.api_family;
    auto reasoning_format = options_.reasoning_format;
    if (api_family.empty())
    {
      if (options_.provider_id == "openai")
        api_family = "openai_responses";
      else if (options_.provider_id == "anthropic")
        api_family = "anthropic_messages";
      else if (options_.provider_id == "gemini")
        api_family = "gemini_generate_content";
      else
        api_family = "openai_chat_completions";
    }
    if (reasoning_format.empty())
    {
      if (api_family == "openai_responses")
        reasoning_format = "openai_responses";
      else if (api_family == "anthropic_messages")
        reasoning_format = "anthropic_thinking";
    }
    std::vector<std::string> active_entry_ids;
    active_entry_ids.reserve(active_turn_user_messages.size());
    for (auto const& message : active_turn_user_messages) active_entry_ids.push_back(message.id);
    bool const supports_images =
        std::find(options_.model_input_modalities.begin(), options_.model_input_modalities.end(), "image") != options_.model_input_modalities.end();
    return MessageBuildOptions{.max_tool_result_context_bytes = options_.max_tool_result_context_bytes,
                               .target = HistoryReplayTarget{.provider_id = options_.provider_id,
                                                             .model_id = options_.model_id,
                                                             .api_family = std::move(api_family),
                                                             .reasoning_format = std::move(reasoning_format),
                                                             .supports_tools = options_.model_supports_tools,
                                                             .supports_images = supports_images},
                               .active_turn_user_entry_ids = std::move(active_entry_ids)};
  };
  auto build_messages_locked = [&]() -> ava::core::Result<BuiltProviderMessages> {
    auto build_options = message_build_options();
    if (options_.session_mutex)
    {
      std::lock_guard lock(*options_.session_mutex);
      return build_messages(*options_.session_read_authority, std::move(build_options));
    }
    return build_messages(*options_.session_read_authority, std::move(build_options));
  };
  auto append_assistant_turn_locked = [&](ParsedAssistantTurn const& turn, ava::provider::TokenUsage const& usage,
                                          std::optional<long double> const& cost_usd) -> ava::core::Result<PersistedAssistantTurn> {
    auto append = [&]() -> ava::core::Result<PersistedAssistantTurn> {
      auto const source_api_family = options_.api_family.empty() ? std::optional<std::string_view>{} : std::optional<std::string_view>{options_.api_family};
      auto const source_reasoning_format =
          options_.reasoning_format.empty() ? std::optional<std::string_view>{} : std::optional<std::string_view>{options_.reasoning_format};
      return options_.append_batch
                 ? append_assistant_turn(options_.append_batch, turn, options_.provider_id, options_.model_id, usage, cost_usd, source_api_family,
                                         source_reasoning_format)
                 : append_assistant_turn(store, turn, options_.provider_id, options_.model_id, usage, cost_usd, source_api_family, source_reasoning_format);
    };
    if (options_.session_mutex)
    {
      std::lock_guard lock(*options_.session_mutex);
      return append();
    }
    return append();
  };
  auto append_tool_result_locked = [&](ToolDispatchResult const& dispatch_result,
                                       std::optional<std::string_view> assistant_output_entry_id = std::nullopt) -> ava::core::VoidResult {
    if (options_.session_mutex)
    {
      std::lock_guard lock(*options_.session_mutex);
      return options_.append_entry ? append_tool_result(options_.append_entry, dispatch_result, assistant_output_entry_id)
                                   : append_tool_result(store, dispatch_result, assistant_output_entry_id);
    }
    return options_.append_entry ? append_tool_result(options_.append_entry, dispatch_result, assistant_output_entry_id)
                                 : append_tool_result(store, dispatch_result, assistant_output_entry_id);
  };
  auto append_permission_decision_locked = [&](ava::tools::PermissionAuditEvent const& event) -> ava::core::VoidResult {
    if (options_.session_mutex)
    {
      std::lock_guard lock(*options_.session_mutex);
      return options_.append_entry ? append_permission_decision(options_.append_entry, event) : append_permission_decision(store, event);
    }
    return options_.append_entry ? append_permission_decision(options_.append_entry, event) : append_permission_decision(store, event);
  };
  auto append_error_locked = [&](ava::core::Error const& error) -> ava::core::VoidResult {
    if (options_.session_mutex)
    {
      std::lock_guard lock(*options_.session_mutex);
      return options_.append_entry ? append_error(options_.append_entry, error) : append_error(store, error);
    }
    return options_.append_entry ? append_error(options_.append_entry, error) : append_error(store, error);
  };
  auto replayable_active_turn_texts = [&]() {
    std::vector<std::string> messages;
    messages.reserve(active_turn_user_messages.size());
    for (auto const& message : active_turn_user_messages) messages.push_back(message.text);
    return messages;
  };
  auto compact_context = [&](std::string_view trigger) -> ava::core::Result<bool> {
    if (auto phase = publish_phase(options_, RunPhase::Compacting); !phase)
      return std::unexpected(std::move(phase.error()));
    if (!options_.compact_context)
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::Provider, "context compaction is unavailable");
      error.with_context("trigger", std::string(trigger));
      return std::unexpected(std::move(error));
    }
    auto const replayed_messages = replayable_active_turn_texts();
    return options_.compact_context(*options_.session_read_authority, trigger, replayed_messages);
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
          return options_.append_entry ? append_replay_user_message(options_.append_entry, message.text, message.image_attachments, message.id)
                                       : append_replay_user_message(store, message.text, message.image_attachments, message.id);
        }
        return options_.append_entry ? append_replay_user_message(options_.append_entry, message.text, message.image_attachments, message.id)
                                     : append_replay_user_message(store, message.text, message.image_attachments, message.id);
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
      // Both errors can originate in provider callbacks. Do not carry their
      // diagnostics into the session or public runtime error path.
      auto compact_error = ava::core::Error(ava::core::ErrorCategory::Provider, "context overflow compaction failed");
      compact_error.with_context("provider_error_kind", "context_overflow");
      compact_error.with_context("compaction_status", "failed");
      for (auto const& context : compacted.error().context())
      {
        bool const decimal_status =
            context.key == "status" && context.value.size() == 3 && std::ranges::all_of(context.value, [](unsigned char ch) { return std::isdigit(ch) != 0; });
        if (decimal_status)
        {
          compact_error.with_context("compaction_provider_status", context.value);
          break;
        }
      }
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

  auto finalized_ids_result = [&]() {
    if (options_.session_mutex)
    {
      std::lock_guard lock(*options_.session_mutex);
      return persisted_provider_tool_call_ids(*options_.session_read_authority, options_.session_read_limits);
    }
    return persisted_provider_tool_call_ids(*options_.session_read_authority, options_.session_read_limits);
  }();
  if (!finalized_ids_result)
    return std::unexpected(std::move(finalized_ids_result.error()));
  auto finalized_provider_tool_call_ids = std::move(*finalized_ids_result);

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
  if (auto phase = publish_phase(options_, RunPhase::BuildingContext); !phase)
    return std::unexpected(std::move(phase.error()));
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
    bool const has_provider_factory = static_cast<bool>(options_.background_provider_factory);
    bool const has_transport_factory = static_cast<bool>(options_.background_transport_factory);
    bool const has_coordinator = static_cast<bool>(options_.subagent_coordinator);
    if (request.background && (!has_provider_factory || !has_transport_factory || (!has_coordinator && !options_.background_jobs)))
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::Tool, "background task subagents are unavailable");
      error.with_context("subagent_type", request.subagent_type);
      return std::unexpected(std::move(error));
    }
    bool const use_coordinator = has_coordinator && has_provider_factory && has_transport_factory;
    if (!request.background && (has_coordinator || has_provider_factory || has_transport_factory) && !use_coordinator)
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::Tool, "coordinated foreground task subagents are unavailable");
      error.with_context("subagent_type", request.subagent_type);
      return std::unexpected(std::move(error));
    }

    auto child_store_result = request.task_id ? ava::session::SessionStore::open(options_.workspace_dir, *request.task_id, session_root)
                                              : ava::session::SessionStore::create(options_.workspace_dir, session_root);
    if (!child_store_result)
      return std::unexpected(std::move(child_store_result.error()));
    auto child_store = std::move(*child_store_result);

    auto child_lease_result = request.task_id ? ava::session::SessionLease::acquire(child_store.session_path())
                                              : ava::session::SessionLease::create_and_acquire(child_store.session_path());
    if (!child_lease_result)
      return std::unexpected(std::move(child_lease_result.error()));
    auto child_lease = std::move(*child_lease_result);
    if (request.task_id)
    {
      auto recovered = child_store.recover_torn_tail(child_lease, options_.session_read_limits, options_.cancel_requested);
      if (!recovered)
        return std::unexpected(std::move(recovered.error()));
      auto staged_recovery = child_store.recover_incomplete_assistant_output_suffix(child_lease, options_.session_read_limits, options_.cancel_requested);
      if (!staged_recovery)
        return std::unexpected(std::move(staged_recovery.error()));
    }
    else
    {
      auto name = request.description + " (@" + request.subagent_type + " subagent)";
      if (name.size() > ava::session::kMaxSessionNameBytes)
      {
        name.resize(ava::session::kMaxSessionNameBytes);
      }
      auto metadata = ava::session::append_session_metadata(
          child_store, child_lease, ava::session::SessionMetadataUpdate{.name = std::move(name), .parent_session_id = store.session_id(), .actor = "subagent"});
      if (!metadata)
        return std::unexpected(std::move(metadata.error()));
    }

    auto child_read_authority = ava::session::SessionReadAuthority::create_persistent(child_store, child_lease, options_.session_read_limits);
    if (!child_read_authority)
      return std::unexpected(std::move(child_read_authority.error()));

    auto child_options = options_;
    // A child owns a distinct exact session namespace. Preserve the parent
    // roots and add the child directory before its AgentLoop constructs any
    // model ToolContext; duplicates remain bounded and harmless.
    append_authority_root(child_options.ava_authority_roots, child_store.session_path().parent_path());
    child_options.session_read_authority = std::move(*child_read_authority);
    child_options.system_prompt = subagent_system_prompt(options_.system_prompt, request.subagent_system_prompt);
    child_options.tool_visibility = subagent_tool_visibility(options_.tool_visibility, request.tool_preset);
    child_options.max_tool_iterations = std::min<std::size_t>(child_options.max_tool_iterations, 6);
    // Child history is independent. Never inherit a parent append callback:
    // it may capture parent run/session ownership and would both mix histories
    // and outlive the parent.
    child_options.append_entry = nullptr;
    child_options.append_batch = nullptr;
    child_options.on_phase = nullptr;
    child_options.on_tool_event = nullptr;
    child_options.on_tool_progress = nullptr;
    child_options.on_stream_event = nullptr;
    child_options.take_steering_messages = nullptr;
    child_options.compact_context = nullptr;
    child_options.background_provider_factory = nullptr;
    child_options.background_transport_factory = nullptr;
    child_options.subagent_coordinator = nullptr;
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

    if (request.background || use_coordinator)
    {
      auto const task_id = child_store.session_id();
      auto const session_path = child_store.session_path();
      auto child_provider = options_.background_provider_factory();
      if (!child_provider)
      {
        auto error = std::move(child_provider.error());
        if (!request.task_id)
          ava::session::rollback_created_session_with_context(child_store, child_lease, error);
        return std::unexpected(std::move(error));
      }
      auto child_transport = options_.background_transport_factory();
      if (!child_transport)
      {
        auto error = std::move(child_transport.error());
        if (!request.task_id)
          ava::session::rollback_created_session_with_context(child_store, child_lease, error);
        return std::unexpected(std::move(error));
      }
      auto interaction_gate = SubagentInteractionGate::create(request.background ? SubagentJobMode::Background : SubagentJobMode::Foreground,
                                                              options_.permission_resolver, options_.question_resolver);
      child_options.permission_resolver = interaction_gate->permission_resolver();
      child_options.question_resolver = interaction_gate->question_resolver();
      child_options.session_mutex = nullptr;
      child_options.lsp_diagnostics_provider = nullptr;
      struct CoordinatedTaskResultState
      {
        std::mutex mutex;
        std::optional<AgentLoopResult> terminal_result = std::nullopt;
      };
      struct CoordinatedTaskRunState
      {
        ava::session::SessionStore child_store;
        ava::session::SessionLease child_lease;
        AgentLoopOptions child_options;
        SessionAppendSink child_append;
        std::string prompt;
        std::shared_ptr<CoordinatedTaskResultState> result_state;
        std::unique_ptr<ava::provider::Provider> provider_instance;
        std::unique_ptr<ava::provider::Transport> transport_instance;
        std::shared_ptr<SubagentInteractionGate> interaction_gate;
      };
      auto run_state = std::make_shared<CoordinatedTaskRunState>(CoordinatedTaskRunState{.child_store = std::move(child_store),
                                                                                         .child_lease = std::move(child_lease),
                                                                                         .child_options = std::move(child_options),
                                                                                         .child_append = {},
                                                                                         .prompt = request.prompt,
                                                                                         .result_state = std::make_shared<CoordinatedTaskResultState>(),
                                                                                         .provider_instance = std::move(*child_provider),
                                                                                         .transport_instance = std::move(*child_transport),
                                                                                         .interaction_gate = interaction_gate});
      auto child_target =
          ava::session::SessionAppendTarget::create_persistent(run_state->child_store, run_state->child_lease, run_state->child_options.session_read_limits);
      if (!child_target)
      {
        auto error = std::move(child_target.error());
        if (!request.task_id)
          ava::session::rollback_created_session_with_context(run_state->child_store, run_state->child_lease, error);
        return std::unexpected(std::move(error));
      }
      if (auto reconciled = reconcile_unresolved_committed_function_calls(
              *run_state->child_options.session_read_authority, [target = *child_target](ava::session::SessionEntry entry) { return target->append(entry); },
              run_state->child_options.session_read_limits);
          !reconciled)
      {
        auto error = std::move(reconciled.error());
        if (!request.task_id)
          ava::session::rollback_created_session_with_context(run_state->child_store, run_state->child_lease, error);
        return std::unexpected(std::move(error));
      }
      auto child_append_target = *child_target;
      run_state->child_options.append_entry = [target = child_append_target](ava::session::SessionEntry entry) { return target->append(entry); };
      run_state->child_options.append_batch = [target = std::move(child_append_target)](std::vector<ava::session::SessionEntry> entries) {
        return target->append_batch(std::move(entries));
      };
      run_state->child_append = run_state->child_options.append_entry;
      BackgroundJobStartOptions start_options{.title = request.description,
                                              .description = request.prompt,
                                              .subagent_type = request.subagent_type,
                                              .child_session_id = task_id,
                                              .child_session_path = session_path};
      BackgroundJobWorker worker = [run_state](BackgroundJobContext const& context) mutable {
        struct FinishInteractionGate final
        {
          std::shared_ptr<SubagentInteractionGate> gate;
          ~FinishInteractionGate() { gate->finish(); }
        } finish_gate{run_state->interaction_gate};
        run_state->child_options.cancel_requested = [stop_token = context.stop_token] { return stop_token.stop_requested(); };
        AgentLoop child_loop(std::move(run_state->child_options));
        auto child_result = child_loop.run_turn(run_state->prompt, run_state->child_store, *run_state->provider_instance, *run_state->transport_instance);
        if (!child_result)
        {
          auto error = child_result.error();
          if (!context.stop_token.stop_requested())
            append_subagent_error_best_effort(run_state->child_append, error);
          return background_failure_completion(context, error);
        }
        auto completion = BackgroundJobCompletion{.state = BackgroundJobState::Completed,
                                                  .final_text = child_result->final_text,
                                                  .stop_reason = std::string(ava::core::to_string(child_result->outcome)),
                                                  .provider_iterations = child_result->provider_iterations,
                                                  .tool_calls = child_result->tool_calls,
                                                  .tool_iterations = child_result->tool_iterations};
        {
          std::lock_guard lock(run_state->result_state->mutex);
          run_state->result_state->terminal_result = *child_result;
        }
        return completion;
      };

      ava::core::Result<SubagentCoordinatorJobSnapshot> coordinated =
          std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "coordinator was not selected"));
      std::string job_id;
      std::string job_state;
      if (use_coordinator)
      {
        coordinated = options_.subagent_coordinator->start(store.session_id(), request.background ? SubagentJobMode::Background : SubagentJobMode::Foreground,
                                                           std::move(start_options), std::move(worker), interaction_gate);
        if (!coordinated)
        {
          auto error = std::move(coordinated.error());
          if (!request.task_id && subagent_publication_commit_state(error) == SubagentPublicationCommitState::ProvenUnpublished)
            ava::session::rollback_created_session_with_context(run_state->child_store, run_state->child_lease, error);
          return std::unexpected(std::move(error));
        }
        job_id = coordinated->job.identity.job_id;
        job_state = std::string(to_string(coordinated->job.execution));
      }
      else
      {
        auto job = options_.background_jobs->start(std::move(start_options), std::move(worker));
        if (!job)
        {
          auto error = std::move(job.error());
          if (!request.task_id)
            ava::session::rollback_created_session_with_context(run_state->child_store, run_state->child_lease, error);
          return std::unexpected(std::move(error));
        }
        job_id = job->job_id;
        job_state = to_string(job->state);
      }

      if (request.background)
      {
        return ava::tools::TaskSubagentResult{.task_id = task_id,
                                              .job_id = std::move(job_id),
                                              .session_path = session_path,
                                              .subagent_type = request.subagent_type,
                                              .state = std::move(job_state),
                                              .final_text = "",
                                              .stop_reason = "background",
                                              .provider_iterations = 0,
                                              .tool_calls = 0,
                                              .tool_iterations = 0};
      }

      for (;;)
      {
        auto waited = options_.subagent_coordinator->wait(store.session_id(), job_id, std::chrono::milliseconds(50), SubagentWaitMode::TerminalOrPromotion);
        if (!waited)
          return std::unexpected(std::move(waited.error()));
        if (waited->job.was_promoted && !subagent_terminal(waited->job.execution))
        {
          return ava::tools::TaskSubagentResult{.task_id = task_id,
                                                .job_id = job_id,
                                                .session_path = session_path,
                                                .subagent_type = request.subagent_type,
                                                .state = std::string(to_string(waited->job.execution)),
                                                .final_text = "",
                                                .stop_reason = "promoted",
                                                .provider_iterations = 0,
                                                .tool_calls = 0,
                                                .tool_iterations = 0};
        }
        if (subagent_terminal(waited->job.execution))
        {
          if (waited->timed_out)
            continue;
          if (waited->job.execution != SubagentExecutionState::Completed)
          {
            auto error =
                ava::core::Error(ava::core::ErrorCategory::Tool,
                                 waited->job.execution == SubagentExecutionState::Canceled ? "foreground subagent was canceled" : "foreground subagent failed");
            error.with_context("job_id", job_id);
            if (waited->job.error)
              error.with_context("cause", *waited->job.error);
            if (waited->job.stop_reason)
              error.with_context("stop_reason", *waited->job.stop_reason);
            return std::unexpected(std::move(error));
          }
          std::lock_guard result_lock(run_state->result_state->mutex);
          if (!run_state->result_state->terminal_result)
            return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "foreground subagent terminal result is unavailable"));
          return ava::tools::TaskSubagentResult{.task_id = task_id,
                                                .job_id = job_id,
                                                .session_path = session_path,
                                                .subagent_type = request.subagent_type,
                                                .state = std::string(to_string(waited->job.execution)),
                                                .final_text = run_state->result_state->terminal_result->final_text,
                                                .stop_reason = std::string(ava::core::to_string(run_state->result_state->terminal_result->outcome)),
                                                .provider_iterations = run_state->result_state->terminal_result->provider_iterations,
                                                .tool_calls = run_state->result_state->terminal_result->tool_calls,
                                                .tool_iterations = run_state->result_state->terminal_result->tool_iterations};
        }
        if (options_.cancel_requested && options_.cancel_requested())
          static_cast<void>(options_.subagent_coordinator->cancel(store.session_id(), job_id));
      }
    }

    auto child_target = ava::session::SessionAppendTarget::create_persistent(child_store, child_lease, child_options.session_read_limits);
    if (!child_target)
      return std::unexpected(std::move(child_target.error()));
    if (auto reconciled = reconcile_unresolved_committed_function_calls(
            *child_options.session_read_authority, [target = *child_target](ava::session::SessionEntry entry) { return target->append(entry); },
            child_options.session_read_limits);
        !reconciled)
    {
      return std::unexpected(std::move(reconciled.error()));
    }
    auto child_append_target = *child_target;
    child_options.append_entry = [target = child_append_target](ava::session::SessionEntry entry) { return target->append(entry); };
    child_options.append_batch = [target = std::move(child_append_target)](std::vector<ava::session::SessionEntry> entries) {
      return target->append_batch(std::move(entries));
    };
    AgentLoop child_loop(std::move(child_options));
    auto child_result = child_loop.run_turn(request.prompt, child_store, provider, transport);
    if (!child_result)
      return std::unexpected(std::move(child_result.error()));
    return ava::tools::TaskSubagentResult{.task_id = child_store.session_id(),
                                          .job_id = "",
                                          .session_path = child_store.session_path(),
                                          .subagent_type = request.subagent_type,
                                          .final_text = child_result->final_text,
                                          .stop_reason = std::string(ava::core::to_string(child_result->outcome)),
                                          .provider_iterations = child_result->provider_iterations,
                                          .tool_calls = child_result->tool_calls,
                                          .tool_iterations = child_result->tool_iterations};
  };
  ava::tools::ToolContext tool_context{
      .workspace_dir = options_.workspace_dir,
      .spill_dir = store.session_path().parent_path() / "spill",
      .mode = options_.mode,
      .permission_resolver = options_.permission_resolver,
      .command_deny_preflight = options_.command_deny_preflight,
      .permission_audit_sink = append_permission_decision_locked,
      .progress_sink = [this](ava::tools::ToolProgressEvent const& event) -> ava::core::VoidResult {
        return publish_tool_progress(options_,
                                     ToolProgressEntry{.call_id = event.call_id, .name = event.tool_name, .text = event.text, .status = event.status});
      },
      .announce_execution_after_permission = options_.announce_execution_after_permission,
      .cancel_requested = options_.cancel_requested,
      .question_resolver = options_.question_resolver,
      .task_subagent_runner =
          options_.trace_context.parent_session_id.empty() ? ava::tools::TaskSubagentRunner(run_task_subagent) : ava::tools::TaskSubagentRunner{},
      .subagent_coordinator = options_.subagent_coordinator,
      .subagents = subagents,
      .redact_permission_audit_arguments = options_.redact_permission_audit_arguments,
      .require_explicit_file_permissions = options_.require_explicit_file_permissions,
      .anchor_set = options_.anchor_set,
      .ava_authority_roots = options_.ava_authority_roots,
      .exact_file_access = options_.exact_file_access,
      .command_executor = options_.command_executor,
      .lsp_diagnostics_provider = options_.lsp_diagnostics_provider,
      .plugin_global_plugins_dir = options_.plugin_global_plugins_dir,
      .plugin_project_plugins_dir = options_.plugin_project_plugins_dir,
      .plugin_enablement_file = options_.plugin_enablement_file,
      .include_project_plugins = options_.include_project_resources,
      .include_project_mcp_config = options_.include_project_resources,
      .session_mcp_config = options_.session_mcp_config,
      .exact_builtin_tool_names = options_.exact_builtin_tool_names,
      .require_descriptor_secure_workspace = options_.require_descriptor_secure_workspace,
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
  if (options_.exact_builtin_tool_names || options_.require_descriptor_secure_workspace)
  {
    auto strict_dispatcher = ToolDispatcher::create_strict(tool_context);
    if (!strict_dispatcher)
      return std::unexpected(std::move(strict_dispatcher.error()));
    dispatcher_storage.emplace(std::move(*strict_dispatcher));
  }
  else
  {
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
          if (finalized_provider_tool_call_ids.contains(event.tool_call_id))
          {
            auto error = ava::core::Error(ava::core::ErrorCategory::Provider, "provider reused a finalized tool call id in the persistent session");
            error.with_context("tool_call_id", event.tool_call_id);
            error.with_context("provider_iteration", std::to_string(result.provider_iterations + 1));
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
          options_.observation->emit(ava::observability::TraceEventType::ProviderStreamEvent, trace_context, [&event](auto& trace) {
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

    if (auto phase = publish_phase(options_, RunPhase::AwaitingProvider); !phase)
      return std::unexpected(std::move(phase.error()));
    if (auto not_canceled = check_canceled_locked("before_provider_transport"); !not_canceled)
      return std::unexpected(std::move(not_canceled.error()));
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
    std::unordered_set<std::string> iteration_tool_call_ids;
    for (auto const& call : turn->tool_calls)
    {
      if (!iteration_tool_call_ids.insert(call.id).second)
        continue;  // One provider turn may merge multiple deltas for one finalized call.
      if (!current_provider_tool_call_ids.contains(call.id) || finalized_provider_tool_call_ids.contains(call.id))
      {
        auto error =
            ava::core::Error(ava::core::ErrorCategory::Provider, "provider reused or inconsistently finalized a tool call id in the persistent session");
        error.with_context("tool_call_id", call.id);
        error.with_context("provider_iteration", std::to_string(result.provider_iterations + 1));
        error.with_context("hint", "provider tool call ids must remain unique for the complete persistent session");
        static_cast<void>(append_error_locked(error));
        return std::unexpected(std::move(error));
      }
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
    if (*turn->finish_reason == ava::provider::ProviderFinishReason::Cancelled)
    {
      result.final_text = turn->text;
      result.tool_iterations = tool_iterations;
      result.outcome = ava::core::RuntimeTerminalOutcome::Cancelled;
      return result;
    }
    // Completing is the terminal arbitration boundary. Establish it before
    // persisting a terminal assistant so an accepted stop cannot label durable
    // success as canceled, while a later stop is a bounded no-op.
    if (turn->tool_calls.empty())
    {
      if (auto phase = publish_phase(options_, RunPhase::Completing); !phase)
        return std::unexpected(std::move(phase.error()));
    }
    if (!turn->tool_calls.empty())
    {
      if (auto phase = publish_phase(options_, RunPhase::PersistingAssistant); !phase)
        return std::unexpected(std::move(phase.error()));
    }
    // Permission prompts receive the transient provider call below. Durable
    // assistant-output/session state never retains raw RunCommand argv or
    // shell payloads; it retains only the call identity and a stable marker.
    auto persisted_turn = append_assistant_turn_locked(persistable_turn(*turn), usage, cost_usd);
    if (!persisted_turn)
    {
      return std::unexpected(std::move(persisted_turn.error()));
    }
    result.committed_turn_id = persisted_turn->committed_turn_id;

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
      auto const terminal_status = is_canceled(options_) ? ToolResultStatus::Canceled : ToolResultStatus::Error;
      for (auto& pending : pending_tool_results)
      {
        if (pending.durably_recorded)
          continue;
        auto synthetic = synthetic_terminal_tool_result(pending.call, terminal_status);
        auto appended = append_tool_result_locked(synthetic, pending.assistant_output_entry_id);
        if (appended || error_has_context(appended.error(), "append_commit_state", "committed_to_leased_inode"))
          pending.durably_recorded = true;
      }
    });
    // A tool call becomes finalized only after the committed v4 turn is
    // durable. Incomplete staging is intentionally absent from this set.
    finalized_provider_tool_call_ids.insert(iteration_tool_call_ids.begin(), iteration_tool_call_ids.end());

    if (turn->tool_calls.empty())
    {
      result.final_text = turn->text;
      result.tool_iterations = tool_iterations;
      switch (*turn->finish_reason)
      {
        case ava::provider::ProviderFinishReason::Completed:
          result.outcome = ava::core::RuntimeTerminalOutcome::Completed;
          break;
        case ava::provider::ProviderFinishReason::MaxTokens:
          result.outcome = ava::core::RuntimeTerminalOutcome::MaxTokens;
          break;
        case ava::provider::ProviderFinishReason::Refusal:
          result.outcome = ava::core::RuntimeTerminalOutcome::Refusal;
          break;
        case ava::provider::ProviderFinishReason::Cancelled:
          result.outcome = ava::core::RuntimeTerminalOutcome::Cancelled;
          break;
        case ava::provider::ProviderFinishReason::ToolCalls:
        case ava::provider::ProviderFinishReason::Error:
          result.outcome = ava::core::RuntimeTerminalOutcome::Error;
          break;
      }
      return result;
    }

    if (auto phase = publish_phase(options_, RunPhase::PreparingTools); !phase)
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
      if (auto phase = publish_phase(options_, RunPhase::ExecutingTools); !phase)
        return std::unexpected(std::move(phase.error()));
      if (auto not_canceled = check_canceled_locked("before_tool_dispatch"); !not_canceled)
      {
        return std::unexpected(std::move(not_canceled.error()));
      }
      if (auto not_canceled = check_canceled_locked("before_tool_call_append"); !not_canceled)
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
      auto appended = append_tool_result_locked(dispatch_result, *assistant_output_entry_id);
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
      auto appended = append_tool_result_locked(dispatch_result, *assistant_output_entry_id);
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
      result.tool_timeline.push_back(timeline_entry);
      publish_tool_event(options_, timeline_entry);
      ++result.tool_calls;
      return {};
    };

    auto run_parallel_epoch_and_commit = [&](std::span<ToolScheduleSlot const> epoch) -> ava::core::VoidResult {
      // A parallel read/search epoch has one visible execution boundary; worker
      // scheduling itself must not publish duplicate phase changes.
      if (auto phase = publish_phase(options_, RunPhase::ExecutingTools); !phase)
        return std::unexpected(std::move(phase.error()));
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

    if (auto phase = publish_phase(options_, RunPhase::SettlingTools); !phase)
      return std::unexpected(std::move(phase.error()));
    ++tool_iterations;
    result.tool_iterations = tool_iterations;
    if (tool_iterations >= options_.max_tool_iterations)
    {
      if (auto phase = publish_phase(options_, RunPhase::Completing); !phase)
        return std::unexpected(std::move(phase.error()));
      result.outcome = ava::core::RuntimeTerminalOutcome::MaxTurnRequests;
      return result;
    }
    if (auto phase = publish_phase(options_, RunPhase::AwaitingProvider); !phase)
      return std::unexpected(std::move(phase.error()));
  }
}

}  // namespace ava::agent
