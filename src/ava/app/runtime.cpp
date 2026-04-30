#include "ava/app/runtime.h"

#include <utility>

#include "ava/core/ids.h"
#include "ava/core/json.h"

namespace ava::app {
namespace {

ava::core::Result<std::filesystem::path> current_path_result() {
  std::error_code error;
  auto path = std::filesystem::current_path(error);
  if (!error) return path;
  auto result_error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to resolve current directory");
  result_error.with_context("cause", error.message());
  return std::unexpected(std::move(result_error));
}

ava::core::Result<std::string> resolve_session_id(const std::filesystem::path& workspace_dir,
                                                  const std::filesystem::path& root_dir,
                                                  std::string_view requested_id) {
  auto sessions = ava::session::SessionStore::list_sessions(workspace_dir, root_dir);
  if (!sessions) return std::unexpected(sessions.error());

  std::vector<std::string> matches;
  for (const auto& session : *sessions) {
    if (session.session_id == requested_id || session.session_id.starts_with(requested_id)) {
      matches.push_back(session.session_id);
    }
  }
  if (matches.empty()) {
    auto error = ava::core::Error(ava::core::ErrorCategory::NotFound, "session not found");
    error.with_context("session_id", std::string(requested_id));
    return std::unexpected(std::move(error));
  }
  if (matches.size() > 1) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "session id prefix is ambiguous");
    error.with_context("session_id", std::string(requested_id));
    error.with_context("matches", std::to_string(matches.size()));
    return std::unexpected(std::move(error));
  }
  return matches.front();
}

std::string session_start_data_json(ava::agent::Mode mode,
                                    const ava::config::ModelInfo& model,
                                    const ava::config::PromptSelection& prompt,
                                    std::size_t context_source_count) {
  return "{\"mode\":\"" + ava::agent::to_string(mode) + "\",\"provider\":\"" +
         ava::core::json::escape(model.provider_id) + "\",\"model\":\"" +
         ava::core::json::escape(model.model_id) + "\",\"prompt_override\":" +
         (prompt.from_override ? std::string("true") : std::string("false")) + ",\"context_sources\":" +
         std::to_string(context_source_count) + '}';
}

ava::core::VoidResult append_session_start(ava::session::SessionStore& store,
                                           ava::agent::Mode mode,
                                           const ava::config::ModelInfo& model,
                                           const ava::config::PromptSelection& prompt,
                                           std::size_t context_source_count) {
  return store.append(ava::session::SessionEntry{
      .id = ava::core::make_id("entry"),
      .parent_id = "",
      .type = ava::session::EntryType::SessionStart,
      .timestamp = ava::session::now_timestamp(),
      .data_json = session_start_data_json(mode, model, prompt, context_source_count),
  });
}

RuntimeEvent base_event(const RuntimeSession& session, RuntimeEventType type) {
  RuntimeEvent event;
  event.type = type;
  event.timestamp = ava::session::now_timestamp();
  event.session_id = session.store.session_id();
  event.mode = session.mode;
  event.provider_id = session.model.provider_id;
  event.model_id = session.model.model_id;
  return event;
}

ava::core::Result<RuntimePromptState> load_runtime_prompt_state(const ava::config::XdgPaths& paths,
                                                                const ava::config::ModelInfo& model,
                                                                ava::agent::Mode mode,
                                                                const std::filesystem::path& workspace_dir,
                                                                const std::filesystem::path& current_dir) {
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
  for (const auto& file : *loaded_context) {
    context_sources.push_back(ContextSourceMetadata{.path = file.path,
                                                    .source_type = file.source_type,
                                                    .byte_count = file.byte_count});
  }

  auto system_prompt = prompt->text + ava::context::format_context_for_prompt(*loaded_context);
  return RuntimePromptState{.mode = mode,
                            .prompt = std::move(*prompt),
                            .context_sources = std::move(context_sources),
                            .system_prompt = std::move(system_prompt)};
}

}  // namespace

ava::core::Result<RuntimeSession> open_runtime_session(const RuntimeOpenOptions& options) {
  if (options.requested_session_id && options.continue_last_session) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                           "use either requested session id or continue, not both"));
  }

  auto cwd = current_path_result();
  if (!cwd) return std::unexpected(std::move(cwd.error()));
  const auto workspace_dir = options.workspace_dir.empty() ? *cwd : options.workspace_dir;
  const auto current_dir = options.current_dir.empty() ? workspace_dir : options.current_dir;

  auto registry = ava::config::load_model_registry(options.paths);
  if (!registry) return std::unexpected(registry.error());
  auto model = ava::config::select_default_model(*registry);

  auto prompt_state = load_runtime_prompt_state(options.paths, model, options.mode, workspace_dir, current_dir);
  if (!prompt_state) return std::unexpected(prompt_state.error());

  bool created = true;
  ava::core::Result<ava::session::SessionStore> store = std::unexpected(
      ava::core::Error(ava::core::ErrorCategory::Unknown, "session was not initialized"));
  if (options.requested_session_id) {
    auto resolved = resolve_session_id(workspace_dir, options.paths.sessions_dir, *options.requested_session_id);
    if (!resolved) return std::unexpected(resolved.error());
    store = ava::session::SessionStore::open(workspace_dir, *resolved, options.paths.sessions_dir);
    created = false;
  } else if (options.continue_last_session) {
    auto sessions = ava::session::SessionStore::list_sessions(workspace_dir, options.paths.sessions_dir);
    if (!sessions) return std::unexpected(sessions.error());
    if (!sessions->empty()) {
      store = ava::session::SessionStore::open(workspace_dir, sessions->front().session_id, options.paths.sessions_dir);
      created = false;
    } else {
      store = ava::session::SessionStore::create(workspace_dir, options.paths.sessions_dir);
    }
  } else {
    store = ava::session::SessionStore::create(workspace_dir, options.paths.sessions_dir);
  }
  if (!store) return std::unexpected(store.error());

  if (created) {
    auto appended = append_session_start(*store, options.mode, model, prompt_state->prompt,
                                         prompt_state->context_sources.size());
    if (!appended) return std::unexpected(appended.error());
  }

  return RuntimeSession{.store = std::move(*store),
                        .mode = options.mode,
                        .model = std::move(model),
                        .prompt = std::move(prompt_state->prompt),
                        .paths = options.paths,
                        .workspace_dir = workspace_dir,
                        .current_dir = current_dir,
                        .context_sources = std::move(prompt_state->context_sources),
                        .system_prompt = std::move(prompt_state->system_prompt),
                        .created = created};
}

ava::core::Result<RuntimePromptState> select_runtime_prompt_state(const RuntimeSession& session,
                                                                  ava::agent::Mode mode) {
  return load_runtime_prompt_state(session.paths, session.model, mode, session.workspace_dir, session.current_dir);
}

void apply_runtime_prompt_state(RuntimeSession& session, RuntimePromptState prompt_state) {
  session.mode = prompt_state.mode;
  session.prompt = std::move(prompt_state.prompt);
  session.context_sources = std::move(prompt_state.context_sources);
  session.system_prompt = std::move(prompt_state.system_prompt);
}

ava::core::Result<ava::agent::AgentLoopResult> run_prompt(RuntimeSession& session,
                                                          const std::string& user_message,
                                                          const ava::provider::Provider& provider,
                                                          ava::provider::Transport& transport,
                                                          const RuntimeRunOptions& options) {
  auto session_event = base_event(session, RuntimeEventType::SessionStart);
  if (auto emitted = emit_event(options.event_sink, session_event); !emitted) {
    return std::unexpected(std::move(emitted.error()));
  }

  auto user_event = base_event(session, RuntimeEventType::UserMessage);
  user_event.text = user_message;
  if (auto emitted = emit_event(options.event_sink, user_event); !emitted) {
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
      .openai_oauth = options.openai_oauth,
      .openai_account_id = options.openai_account_id,
      .stream = options.stream,
      .on_tool_event = [&session, &options, &sink_error](const ava::agent::ToolTimelineEntry& entry) {
        if (sink_error) return;
        auto event = base_event(session, entry.status == ava::agent::ToolTimelineStatus::Running
                                            ? RuntimeEventType::ToolStart
                                            : RuntimeEventType::ToolResult);
        event.call_id = entry.call_id;
        event.tool_name = entry.name;
        event.text = entry.status == ava::agent::ToolTimelineStatus::Running ? entry.argument_summary
                                                                             : entry.result_summary;
        event.status = ava::agent::to_string(entry.status);
        if (auto emitted = emit_event(options.event_sink, event); !emitted) {
          sink_error = std::move(emitted.error());
        }
      },
      .permission_resolver = options.permission_resolver,
      .question_resolver = options.question_resolver,
      .cancel_requested = [&options, &sink_error] {
        return sink_error.has_value() || (options.cancel_requested && options.cancel_requested());
      }});

  auto result = loop.run_turn(user_message, session.store, provider, transport);
  if (sink_error) return std::unexpected(std::move(*sink_error));
  if (!result) {
    auto event = base_event(session, RuntimeEventType::Error);
    event.error_category = ava::core::to_string(result.error().category());
    event.error_message = result.error().message();
    event.error_details = result.error().format();
    static_cast<void>(emit_event(options.event_sink, event));
    return std::unexpected(result.error());
  }

  auto assistant_event = base_event(session, RuntimeEventType::AssistantMessage);
  assistant_event.text = result->final_text;
  if (auto emitted = emit_event(options.event_sink, assistant_event); !emitted) {
    return std::unexpected(std::move(emitted.error()));
  }

  auto done_event = base_event(session, RuntimeEventType::Done);
  done_event.stop_reason = result->stop_reason;
  done_event.provider_iterations = result->provider_iterations;
  done_event.tool_calls = result->tool_calls;
  if (auto emitted = emit_event(options.event_sink, done_event); !emitted) {
    return std::unexpected(std::move(emitted.error()));
  }

  return result;
}

}  // namespace ava::app
