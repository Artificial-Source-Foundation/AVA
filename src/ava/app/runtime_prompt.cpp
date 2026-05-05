#include "ava/app/runtime_prompt.h"

#include "ava/app/plugin_event_hooks.h"
#include "ava/app/runtime_compaction.h"
#include "ava/app/runtime_reasoning.h"
#include "ava/app/runtime_retry.h"

#include <optional>
#include <utility>

namespace ava::app::runtime {

ava::core::Result<RuntimePromptState> load_runtime_prompt_state(ava::config::XdgPaths const& paths,
                                                                ava::config::ModelInfo const& model,
                                                                ava::agent::Mode mode,
                                                                std::filesystem::path const& workspace_dir,
                                                                std::filesystem::path const& current_dir)
{
  auto prompt = ava::config::select_prompt(paths, model, mode);
  if (!prompt) return std::unexpected(prompt.error());

  auto loaded_context = ava::context::load_context_files(ava::context::ContextLoadOptions{
      .workspace_root = workspace_dir,
      .current_dir = current_dir,
      .global_agents_file = paths.global_agents_file,
  });
  if (!loaded_context) return std::unexpected(loaded_context.error());

  std::vector<ContextSourceMetadata> context_sources;
  context_sources.reserve(loaded_context->size());
  for (auto const& file : *loaded_context) {
    context_sources.push_back(
        ContextSourceMetadata{.path = file.path, .source_type = file.source_type, .byte_count = file.byte_count});
  }

  auto system_prompt = prompt->text + ava::context::format_context_for_prompt(*loaded_context);
  return RuntimePromptState{.mode = mode,
                            .prompt = std::move(*prompt),
                            .context_sources = std::move(context_sources),
                            .system_prompt = std::move(system_prompt)};
}

}  // namespace ava::app::runtime

namespace ava::app {
namespace {

RuntimeEvent base_event(RuntimeSession const& session, RuntimeEventType type)
{
  RuntimeEvent event;
  event.type = type;
  event.timestamp = ava::session::now_timestamp();
  event.session_id = session.store.session_id();
  event.mode = session.mode;
  event.provider_id = session.model.provider_id;
  event.model_id = session.model.model_id;
  return event;
}

RuntimeEvent base_event_locked(RuntimeSession const& session, RuntimeEventType type, std::mutex* mutex)
{
  if (!mutex) return base_event(session, type);
  std::lock_guard lock(*mutex);
  return base_event(session, type);
}

bool is_agent_loop_canceled_error(ava::core::Error const& error)
{
  return error.message() == "agent loop canceled" || error.format().find("agent loop canceled") != std::string::npos;
}

}  // namespace

ava::core::Result<RuntimePromptState> select_runtime_prompt_state(RuntimeSession const& session, ava::agent::Mode mode)
{
  return runtime::load_runtime_prompt_state(session.paths, session.model, mode, session.workspace_dir,
                                            session.current_dir);
}

void apply_runtime_prompt_state(RuntimeSession& session, RuntimePromptState prompt_state)
{
  session.mode = prompt_state.mode;
  session.prompt = std::move(prompt_state.prompt);
  session.context_sources = std::move(prompt_state.context_sources);
  session.system_prompt = std::move(prompt_state.system_prompt);
}

ava::core::Result<ava::agent::AgentLoopResult> run_prompt(RuntimeSession& session, std::string const& user_message,
                                                          ava::provider::Provider const& provider,
                                                          ava::provider::Transport& transport,
                                                          RuntimeRunOptions const& options)
{
  auto event_sink = make_plugin_event_observer_sink(
      plugin_event_observer_options(session, options.permission_resolver, options.session_mutex), options.event_sink);
  auto runtime_options = options;
  runtime_options.event_sink = event_sink;
  std::optional<ava::provider::RetryTransport> retry_transport;
  ava::provider::Transport* runtime_transport = &transport;
  if (runtime_options.enable_transport_retries) {
    retry_transport.emplace(transport, runtime::runtime_retry_options(session, runtime_options));
    runtime_transport = &*retry_transport;
    runtime_options.enable_transport_retries = false;
  }
  auto session_event = base_event_locked(session, RuntimeEventType::SessionStart, options.session_mutex);
  if (auto emitted = emit_event(event_sink, session_event); !emitted) {
    return std::unexpected(std::move(emitted.error()));
  }

  auto user_event = base_event_locked(session, RuntimeEventType::UserMessage, options.session_mutex);
  user_event.text = user_message;
  if (auto emitted = emit_event(event_sink, user_event); !emitted) {
    return std::unexpected(std::move(emitted.error()));
  }

  std::optional<ava::core::Error> sink_error;
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
      .workspace_dir = session.workspace_dir,
      .mode = session.mode,
      .provider_id = session.model.provider_id,
      .model_id = session.model.model_id,
      .system_prompt = session.system_prompt,
      .access_token = options.access_token,
      .credential_type =
          options.openai_oauth && options.credential_type == "bearer" ? "oauth" : options.credential_type,
      .openai_oauth = options.openai_oauth,
      .openai_account_id = options.openai_account_id,
      .stream = runtime_options.stream,
      .model_supports_tools = session.model.supports_tools.value_or(true),
      .model_supports_streaming = session.model.supports_streaming.value_or(true),
      .model_max_output_tokens = session.model.max_output_tokens,
      .reasoning =
          session.reasoning ? std::optional(runtime::provider_reasoning_options(*session.reasoning)) : std::nullopt,
      .on_tool_event =
          [&session, &options, &event_sink, &sink_error](ava::agent::ToolTimelineEntry const& entry) {
            if (sink_error) return;
            auto event = base_event_locked(session,
                                           entry.status == ava::agent::ToolTimelineStatus::Running
                                               ? RuntimeEventType::ToolStart
                                               : RuntimeEventType::ToolResult,
                                           options.session_mutex);
            event.call_id = entry.call_id;
            event.tool_name = entry.name;
            event.text =
                entry.status == ava::agent::ToolTimelineStatus::Running ? entry.argument_summary : entry.result_summary;
            event.tool_arguments_json = entry.arguments_json;
            event.tool_result_json = entry.result_json;
            event.tool_structured_result_json = entry.structured_result_json;
            event.content_type = entry.content_type;
            event.error_category = entry.error_category;
            event.error_code = entry.error_code;
            event.error_message = entry.error_message;
            event.error_details = entry.error_details;
            event.diff = entry.diff;
            event.diff_truncated = entry.diff_truncated;
            event.changed_paths = entry.changed_paths;
            event.permission_request_ids = entry.permission_request_ids;
            event.truncated = entry.truncated;
            event.spill_path = entry.spill_path;
            event.spill_truncated = entry.spill_truncated;
            if (entry.output_bytes) event.output_bytes = *entry.output_bytes;
            if (entry.total_bytes) event.total_bytes = *entry.total_bytes;
            if (entry.omitted_bytes) event.omitted_bytes = *entry.omitted_bytes;
            if (entry.omitted_lines) event.omitted_lines = *entry.omitted_lines;
            if (entry.visible_matches) event.visible_matches = *entry.visible_matches;
            if (entry.total_matches) event.total_matches = *entry.total_matches;
            event.status = ava::agent::to_string(entry.status);
            if (auto emitted = emit_event(event_sink, event); !emitted) {
              sink_error = std::move(emitted.error());
            }
          },
      .on_tool_progress = [&session, &options, &event_sink,
                           &sink_error](ava::agent::ToolProgressEntry const& entry) -> ava::core::VoidResult {
        if (sink_error) return std::unexpected(*sink_error);
        auto event = base_event_locked(session, RuntimeEventType::ToolProgress, options.session_mutex);
        event.call_id = entry.call_id;
        event.tool_name = entry.name;
        event.text = entry.text;
        event.status = entry.status;
        if (auto emitted = emit_event(event_sink, event); !emitted) {
          sink_error = std::move(emitted.error());
          return std::unexpected(*sink_error);
        }
        return {};
      },
      .on_stream_event = [&session, &options, &event_sink,
                          &sink_error](ava::provider::StreamEvent const& stream_event) -> ava::core::VoidResult {
        if (sink_error) return std::unexpected(*sink_error);
        auto event = base_event_locked(
            session,
            stream_event.type == ava::provider::StreamEventType::TextDelta        ? RuntimeEventType::MessageUpdate
            : stream_event.type == ava::provider::StreamEventType::ReasoningStart ? RuntimeEventType::ReasoningStart
            : stream_event.type == ava::provider::StreamEventType::ReasoningDelta ? RuntimeEventType::ReasoningDelta
            : stream_event.type == ava::provider::StreamEventType::ReasoningEnd   ? RuntimeEventType::ReasoningEnd
            : stream_event.type == ava::provider::StreamEventType::Done           ? RuntimeEventType::MessageEnd
                                                                                  : RuntimeEventType::ProviderEvent,
            options.session_mutex);
        event.text = stream_event.text;
        event.call_id = stream_event.tool_call_id;
        event.tool_name = stream_event.tool_name;
        event.status = ava::provider::to_string(stream_event.type);
        event.error_message = stream_event.error_message;
        event.stop_reason = stream_event.stop_reason;
        event.reasoning_format = stream_event.reasoning_format;
        event.reasoning_redacted = stream_event.redacted;
        event.reasoning_signature_present =
            stream_event.reasoning_signature_present || !stream_event.reasoning_signature.empty();
        if (auto emitted = emit_event(event_sink, event); !emitted) {
          sink_error = std::move(emitted.error());
          return std::unexpected(*sink_error);
        }
        return {};
      },
      .permission_resolver = runtime_options.permission_resolver,
      .question_resolver = runtime_options.question_resolver,
      .cancel_requested =
          [&runtime_options, &sink_error] {
            return sink_error.has_value() || (runtime_options.cancel_requested && runtime_options.cancel_requested());
          },
      .take_steering_messages = runtime_options.take_steering_messages,
      .compact_context = runtime_options.access_token.empty()
                             ? decltype(ava::agent::AgentLoopOptions{}.compact_context){}
                             : [&](ava::session::SessionStore& store, std::string_view trigger,
                                   std::vector<std::string> const& replayed_user_messages) -> ava::core::Result<bool> {
        return runtime::compact_runtime_context(session, store, trigger, provider, *runtime_transport, runtime_options,
                                                replayed_user_messages);
      },
      .session_mutex = runtime_options.session_mutex,
      .model_pricing = session.model.pricing});

  auto result = loop.run_turn(user_message, session.store, provider, *runtime_transport);
  if (sink_error) return std::unexpected(std::move(*sink_error));
  if (!result) {
    auto event = base_event_locked(
        session, is_agent_loop_canceled_error(result.error()) ? RuntimeEventType::Canceled : RuntimeEventType::Error,
        options.session_mutex);
    event.error_category = ava::core::to_string(result.error().category());
    event.error_message = result.error().message();
    event.error_details = result.error().format();
    if (event.type == RuntimeEventType::Canceled) {
      event.text = "stopped by user";
      event.reason = result.error().message();
    }
    static_cast<void>(emit_event(event_sink, event));
    return std::unexpected(result.error());
  }

  auto assistant_event = base_event_locked(session, RuntimeEventType::AssistantMessage, options.session_mutex);
  assistant_event.text = result->final_text;
  if (auto emitted = emit_event(event_sink, assistant_event); !emitted) {
    return std::unexpected(std::move(emitted.error()));
  }

  auto done_event = base_event_locked(session, RuntimeEventType::Done, options.session_mutex);
  done_event.stop_reason = result->stop_reason;
  done_event.provider_iterations = result->provider_iterations;
  done_event.tool_calls = result->tool_calls;
  if (auto emitted = emit_event(event_sink, done_event); !emitted) {
    return std::unexpected(std::move(emitted.error()));
  }

  return result;
}

}  // namespace ava::app
