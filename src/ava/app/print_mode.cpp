#include "ava/app/print_mode.h"

#include <iterator>
#include <ostream>
#include <string_view>
#include <utility>

#include "ava/config/auth.h"
#include "ava/config/openai_oauth.h"
#include "ava/core/error.h"
#include "ava/provider/curl_transport.h"
#include "ava/provider/openai_provider.h"

namespace ava::app {
namespace {

bool has_value(const std::optional<std::string>& value) { return value && !value->empty(); }

std::string read_all(std::istream& in) {
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

ava::permissions::PermissionResolver deny_permission_resolver() {
  return [](const ava::permissions::PermissionPrompt&) -> ava::core::Result<ava::permissions::PermissionResolution> {
    return ava::permissions::PermissionResolution::Deny;
  };
}

void write_text_event_diagnostic(const RuntimeEvent& event, std::ostream& err) {
  if (event.type == RuntimeEventType::ToolStart) {
    err << "tool_start";
    if (!event.tool_name.empty()) err << " " << event.tool_name;
    if (!event.text.empty()) err << ": " << event.text;
    err << '\n';
    return;
  }
  if (event.type == RuntimeEventType::ToolResult) {
    err << "tool_result";
    if (!event.tool_name.empty()) err << " " << event.tool_name;
    if (!event.status.empty()) err << " " << event.status;
    if (!event.text.empty()) err << ": " << event.text;
    err << '\n';
    return;
  }
  if (event.type == RuntimeEventType::Error) {
    err << (event.error_details.empty() ? event.error_message : event.error_details) << '\n';
  }
}

RuntimeRunOptions print_runtime_options(RuntimeRunOptions options) {
  if (!options.permission_resolver) {
    options.permission_resolver = deny_permission_resolver();
  }
  options.question_resolver = nullptr;
  return options;
}

RuntimeEvent runtime_error_event(const RuntimeSession& session, const ava::core::Error& error) {
  RuntimeEvent event;
  event.type = RuntimeEventType::Error;
  event.timestamp = ava::session::now_timestamp();
  event.session_id = session.store.session_id();
  event.mode = session.mode;
  event.provider_id = session.model.provider_id;
  event.model_id = session.model.model_id;
  event.error_category = ava::core::to_string(error.category());
  event.error_message = error.message();
  event.error_details = error.format();
  return event;
}

}  // namespace

ava::core::Result<std::string> merge_print_prompt(const PrintPromptInputs& inputs) {
  const bool has_explicit = has_value(inputs.explicit_prompt);
  const bool has_stdin = has_value(inputs.stdin_prompt);
  if (has_explicit && has_stdin) return *inputs.explicit_prompt + "\n\n" + *inputs.stdin_prompt;
  if (has_explicit) return *inputs.explicit_prompt;
  if (has_stdin) return *inputs.stdin_prompt;
  return std::unexpected(
      ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "print mode requires a prompt argument or stdin"));
}

ava::core::Result<ava::agent::AgentLoopResult> run_print_prompt(RuntimeSession& session, const std::string& prompt,
                                                                const ava::provider::Provider& provider,
                                                                ava::provider::Transport& transport,
                                                                const PrintModeRunOptions& options, std::ostream& out,
                                                                std::ostream& err) {
  bool emitted_error = false;
  auto runtime_options = print_runtime_options(options.runtime_options);
  EventBus event_bus;
  if (options.output_format == PrintOutputFormat::Json) {
    event_bus.subscribe([&out, &emitted_error](const EventEnvelope& envelope) {
      if (envelope.name == to_string(RuntimeEventType::Error)) emitted_error = true;
      out << serialize_event_envelope_jsonl(envelope);
      if (!out) {
        return ava::core::VoidResult{
            std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to write print JSON event"))};
      }
      return ava::core::VoidResult{};
    });
    runtime_options.event_sink = make_runtime_event_bus_adapter(event_bus);
  } else {
    runtime_options.event_sink = [&err, &emitted_error](const RuntimeEvent& event) {
      if (event.type == RuntimeEventType::Error) emitted_error = true;
      write_text_event_diagnostic(event, err);
      if (!err) {
        return ava::core::VoidResult{
            std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to write print diagnostic"))};
      }
      return ava::core::VoidResult{};
    };
  }

  auto result = run_prompt(session, prompt, provider, transport, runtime_options);
  if (!result) {
    if (!emitted_error) {
      if (options.output_format == PrintOutputFormat::Json) {
        // Best-effort fallback: preserve the runtime/provider error that caused the failed turn.
        static_cast<void>(event_bus.publish(to_event_envelope(runtime_error_event(session, result.error()))));
      } else {
        err << result.error().format() << '\n';
      }
    }
    return std::unexpected(result.error());
  }

  if (options.output_format == PrintOutputFormat::Text) {
    out << result->final_text;
    if (!out) {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to write print output"));
    }
  }
  return result;
}

int run_print_mode(const PrintModeOptions& options, std::istream& in, std::ostream& out, std::ostream& err) {
  std::optional<std::string> stdin_prompt;
  if (options.read_stdin) stdin_prompt = read_all(in);

  auto prompt = merge_print_prompt(
      PrintPromptInputs{.explicit_prompt = options.explicit_prompt, .stdin_prompt = std::move(stdin_prompt)});
  if (!prompt) {
    err << prompt.error().format() << '\n';
    return 2;
  }

  auto session = open_runtime_session(options.open_options);
  if (!session) {
    err << session.error().format() << '\n';
    return 1;
  }

  auto credential = ava::config::load_openai_credential(session->paths);
  if (!credential) {
    err << credential.error().format() << '\n';
    return 1;
  }
  if (!*credential) {
    err << "print mode requires OpenAI auth. Configure an OpenAI credential in " << session->paths.auth_file.string()
        << '\n';
    return 1;
  }
  ava::provider::CurlCliTransport default_transport;
  ava::provider::Transport& transport = options.transport_override
                                            ? options.transport_override->get()
                                            : static_cast<ava::provider::Transport&>(default_transport);
  auto request_credential = ava::config::openai_credential_for_request(session->paths, **credential, transport);
  if (!request_credential) {
    err << request_credential.error().format() << '\n';
    return 1;
  }
  auto token = ava::config::openai_access_token_for_request(*request_credential);
  if (!token) {
    err << token.error().format() << '\n';
    return 1;
  }

  std::string openai_account_id = request_credential->account_id;
  if (request_credential->type == ava::config::OpenAICredentialType::OAuth && openai_account_id.empty()) {
    openai_account_id = ava::config::openai_oauth_account_id_from_token(request_credential->access_token).value_or("");
  }

  const ava::provider::OpenAIProvider default_provider;
  const ava::provider::Provider& provider = options.provider_override
                                                ? options.provider_override->get()
                                                : static_cast<const ava::provider::Provider&>(default_provider);
  RuntimeRunOptions runtime_options;
  runtime_options.access_token = *token;
  runtime_options.openai_oauth = request_credential->type == ava::config::OpenAICredentialType::OAuth;
  runtime_options.openai_account_id = openai_account_id;
  runtime_options.permission_resolver = build_headless_permission_resolver(options.permission_policy);

  const PrintModeRunOptions run_options{.output_format = options.output_format,
                                        .runtime_options = std::move(runtime_options)};
  auto result = run_print_prompt(*session, *prompt, provider, transport, run_options, out, err);
  return result ? 0 : 1;
}

}  // namespace ava::app
