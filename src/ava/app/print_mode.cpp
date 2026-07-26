#include "sys.h"
#include "ava/app/EventEnvelope.h"
#include "ava/app/print_mode.h"
#include "ava/app/runtime/Session.h"
#include "ava/app/runtime_sessions.h"
#include "ava/tui/composer.h"
#include "ava/config/auth.h"
#include "ava/config/openai_oauth.h"
#include "ava/permissions/permission_rules.h"
#include "ava/provider/curl_transport.h"
#include "ava/provider/registry.h"
#include "ava/core/error.h"

#include <iterator>
#include <ostream>
#include <string_view>
#include <utility>
#include <unistd.h>

namespace ava::app {
namespace {

bool has_value(std::optional<std::string> const& value)
{
  return value && !value->empty();
}

std::string read_all(std::istream& in)
{
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

ava::permissions::PermissionResolver deny_permission_resolver()
{
  return [](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
    return ava::permissions::PermissionResolutionDecision{
        ava::permissions::PermissionResolution::Deny,
        "print mode denied permission by default; use --allow read-only or --allow-tool <tool> for supported prompts"};
  };
}

std::string sanitize_terminal_output_text(std::string_view text)
{
  std::string output;
  output.reserve(text.size());
  std::size_t start = 0;
  for (std::size_t index = 0; index < text.size(); ++index)
  {
    if (text[index] != '\n')
      continue;
    output += ava::tui::sanitize_terminal_text(text.substr(start, index - start));
    output.push_back('\n');
    start = index + 1;
  }
  output += ava::tui::sanitize_terminal_text(text.substr(start));
  return output;
}

std::string terminal_output_text(std::string_view text, bool sanitize)
{
  return sanitize ? sanitize_terminal_output_text(text) : std::string(text);
}

void write_text_event_diagnostic(runtime::Event const& event, std::ostream& err, bool sanitize)
{
  if (event.type == runtime::EventType::ToolStart)
  {
    err << "tool_start";
    if (!event.tool_name.empty())
      err << " " << terminal_output_text(event.tool_name, sanitize);
    if (!event.text.empty())
      err << ": " << terminal_output_text(event.text, sanitize);
    err << '\n';
    return;
  }
  if (event.type == runtime::EventType::ToolResult)
  {
    err << "tool_result";
    if (!event.tool_name.empty())
      err << " " << terminal_output_text(event.tool_name, sanitize);
    if (!event.status.empty())
      err << " " << terminal_output_text(event.status, sanitize);
    if (!event.text.empty())
      err << ": " << terminal_output_text(event.text, sanitize);
    err << '\n';
    if ((event.error_code == "permission_denied" || event.error_category == "permission_denied") && !event.error_details.empty())
    {
      err << terminal_output_text(event.error_details, sanitize) << '\n';
    }
    return;
  }
  if (event.type == runtime::EventType::Error)
  {
    err << terminal_output_text(event.error_details.empty() ? event.error_message : event.error_details, sanitize) << '\n';
  }
}

runtime::RunOptions print_runtime_options(runtime::RunOptions options)
{
  if (!options.permission_resolver)
  {
    options.permission_resolver = deny_permission_resolver();
  }
  options.question_resolver = nullptr;
  return options;
}

runtime::Event runtime_error_event(runtime::Session const& session, ava::core::Error const& error)
{
  runtime::Event event;
  event.type = runtime::EventType::Error;
  event.timestamp = ava::session::now_timestamp();
  event.session_id = session.store.session_id();
  event.mode = session.mode();
  event.provider_id = session.model().provider_id;
  event.model_id = session.model().model_id;
  event.error_category = ava::core::to_string(error.category());
  event.error_message = error.message();
  event.error_details = error.format();
  return event;
}

}  // namespace

ava::core::Result<std::string> merge_print_prompt(PrintPromptInputs const& inputs)
{
  bool const has_explicit = has_value(inputs.explicit_prompt);
  bool const has_stdin = has_value(inputs.stdin_prompt);
  if (has_explicit && has_stdin)
    return *inputs.explicit_prompt + "\n\n" + *inputs.stdin_prompt;
  if (has_explicit)
    return *inputs.explicit_prompt;
  if (has_stdin)
    return *inputs.stdin_prompt;
  return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "print mode requires a prompt argument or stdin"));
}

ava::core::Result<ava::agent::AgentLoopResult> run_print_prompt(runtime::Session& session, std::string const& prompt, ava::provider::Provider const& provider,
                                                                ava::provider::Transport& transport, PrintModeRunOptions const& options, std::ostream& out,
                                                                std::ostream& err)
{
  bool emitted_error = false;
  auto runtime_options = print_runtime_options(options.runtime_options);
  runtime_options.permission_resolver = ava::permissions::build_persistent_permission_rule_resolver(session.permission_rule_store(),
                                                                                                    std::move(runtime_options.permission_resolver));
  EventBus event_bus;
  if (options.output_format == PrintOutputFormat::Json)
  {
    event_bus.subscribe([&out, &emitted_error](EventEnvelope const& envelope) {
      if (envelope.name == to_string(runtime::EventType::Error))
        emitted_error = true;
      out << serialize_event_envelope_jsonl(envelope);
      if (!out)
      {
        return ava::core::VoidResult{std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to write print JSON event"))};
      }
      return ava::core::VoidResult{};
    });
    runtime_options.event_sink = make_runtime_event_bus_adapter(event_bus);
  }
  else
  {
    runtime_options.event_sink = [&err, &emitted_error, sanitize = options.sanitize_terminal_diagnostics](runtime::Event const& event) {
      if (event.type == runtime::EventType::Error)
        emitted_error = true;
      write_text_event_diagnostic(event, err, sanitize);
      if (!err)
      {
        return ava::core::VoidResult{std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to write print diagnostic"))};
      }
      return ava::core::VoidResult{};
    };
  }

  auto result = run_prompt(session, prompt, provider, transport, runtime_options);
  if (!result)
  {
    if (!emitted_error)
    {
      if (options.output_format == PrintOutputFormat::Json)
      {
        // Best-effort fallback: preserve the runtime/provider error that caused the failed turn.
        static_cast<void>(event_bus.publish(to_event_envelope(runtime_error_event(session, result.error()))));
      }
      else
      {
        err << terminal_output_text(result.error().format(), options.sanitize_terminal_diagnostics) << '\n';
      }
    }
    return std::unexpected(result.error());
  }

  if (options.output_format == PrintOutputFormat::Text)
  {
    out << terminal_output_text(result->final_text, options.sanitize_terminal_output);
    if (!out)
    {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to write print output"));
    }
  }
  return result;
}

int run_print_mode(PrintModeOptions const& options, std::istream& in, std::ostream& out, std::ostream& err)
{
  bool const sanitize_stdout = ::isatty(STDOUT_FILENO) == 1;
  bool const sanitize_stderr = ::isatty(STDERR_FILENO) == 1;
  std::optional<std::string> stdin_prompt;
  if (options.read_stdin)
    stdin_prompt = read_all(in);

  auto prompt = merge_print_prompt(PrintPromptInputs{.explicit_prompt = options.explicit_prompt, .stdin_prompt = std::move(stdin_prompt)});
  if (!prompt)
  {
    err << terminal_output_text(prompt.error().format(), sanitize_stderr) << '\n';
    return 2;
  }

  auto session = open_runtime_session(options.open_options);
  if (!session)
  {
    err << terminal_output_text(session.error().format(), sanitize_stderr) << '\n';
    return 1;
  }

  ava::provider::CurlCliTransport default_transport;
  ava::provider::Transport& transport =
      options.transport_override ? options.transport_override->get() : static_cast<ava::provider::Transport&>(default_transport);
  ava::provider::Transport& auth_transport =
      options.transport_override ? options.transport_override->get() : static_cast<ava::provider::Transport&>(default_transport);
  auto registry = ava::provider::builtin_provider_registry();
  auto default_provider = registry.create(session->model().provider_id);
  if (!default_provider)
  {
    err << terminal_output_text(default_provider.error().format(), sanitize_stderr) << '\n';
    return 1;
  }
  ava::provider::Provider const& provider =
      options.provider_override ? options.provider_override->get() : static_cast<ava::provider::Provider const&>(**default_provider);
  runtime::RunOptions runtime_options;
  runtime_options.offline = session->is_offline() || options.open_options.offline;
  if (!runtime_options.offline)
  {
    auto request_credential = ava::config::provider_credential_for_request(session->paths(), session->model().provider_id, auth_transport);
    if (!request_credential)
    {
      err << terminal_output_text(request_credential.error().format(), sanitize_stderr) << '\n';
      return 1;
    }
    if (!*request_credential)
    {
      err << "print mode requires auth for provider `" << terminal_output_text(session->model().provider_id, sanitize_stderr) << "`. Configure a credential in "
          << terminal_output_text(session->paths().auth_file.string(), sanitize_stderr) << " or the provider API key environment variable\n";
      return 1;
    }
    runtime_options.access_token = (*request_credential)->access_token;
    runtime_options.credential_type = (*request_credential)->credential_type;
    runtime_options.openai_oauth = (*request_credential)->provider_id == "openai" && (*request_credential)->credential_type == "oauth";
    runtime_options.openai_account_id = (*request_credential)->account_id;
  }
  runtime_options.enable_transport_retries = !options.transport_override.has_value();
  runtime_options.permission_resolver = build_headless_permission_resolver(options.permission_policy);

  PrintModeRunOptions const run_options{.output_format = options.output_format,
                                        .runtime_options = std::move(runtime_options),
                                        .sanitize_terminal_output = sanitize_stdout,
                                        .sanitize_terminal_diagnostics = sanitize_stderr};
  auto result = run_print_prompt(*session, *prompt, provider, transport, run_options, out, err);
  return result ? 0 : 1;
}

}  // namespace ava::app
