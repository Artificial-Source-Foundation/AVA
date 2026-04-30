#include "ava/app/rpc_mode.h"

#include <cctype>
#include <istream>
#include <ostream>
#include <string_view>
#include <utility>

#include "ava/app/commands.h"
#include "ava/config/auth.h"
#include "ava/config/openai_oauth.h"
#include "ava/core/json.h"
#include "ava/provider/curl_transport.h"
#include "ava/provider/openai_provider.h"
#include "ava/session/session_store.h"

namespace ava::app {
namespace {

constexpr std::size_t kMaxRpcLineBytes = 1024 * 1024;

std::string_view trim(std::string_view value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) value.remove_prefix(1);
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) value.remove_suffix(1);
  return value;
}

bool is_json_object_line(std::string_view line) {
  line = trim(line);
  if (line.size() < 2 || line.front() != '{') return false;
  bool in_string = false;
  bool escaped = false;
  int depth = 0;
  std::size_t object_end = std::string_view::npos;
  for (std::size_t index = 0; index < line.size(); ++index) {
    const char ch = line[index];
    if (escaped) {
      escaped = false;
      continue;
    }
    if (ch == '\\' && in_string) {
      escaped = true;
      continue;
    }
    if (ch == '"') {
      in_string = !in_string;
      continue;
    }
    if (in_string) continue;
    if (ch == '{') {
      ++depth;
    } else if (ch == '}') {
      --depth;
      if (depth == 0) {
        object_end = index;
        break;
      }
      if (depth < 0) return false;
    }
  }
  if (in_string || depth != 0 || object_end == std::string_view::npos) return false;
  return trim(line.substr(object_end + 1)).empty();
}

std::string string_field_json(std::string_view key, std::string_view value) {
  return "\"" + std::string(key) + "\":\"" + ava::core::json::escape(value) + "\"";
}

std::string bool_field_json(std::string_view key, bool value) {
  return "\"" + std::string(key) + "\":" + (value ? "true" : "false");
}

std::string number_field_json(std::string_view key, std::size_t value) {
  return "\"" + std::string(key) + "\":" + std::to_string(value);
}

std::string output_array_json(const std::vector<std::string>& output) {
  std::string json = "[";
  for (std::size_t index = 0; index < output.size(); ++index) {
    if (index > 0) json += ',';
    json += '"';
    json += ava::core::json::escape(output[index]);
    json += '"';
  }
  json += ']';
  return json;
}

std::string joined_output(const std::vector<std::string>& output) {
  std::string text;
  for (std::size_t index = 0; index < output.size(); ++index) {
    if (index > 0) text += '\n';
    text += output[index];
  }
  return text;
}

std::string context_sources_json(const RuntimeSession& session) {
  std::string json = "[";
  for (std::size_t index = 0; index < session.context_sources.size(); ++index) {
    const auto& source = session.context_sources[index];
    if (index > 0) json += ',';
    json += '{';
    json += string_field_json("path", source.path.string());
    json += ',';
    json += string_field_json("source_type", ava::context::to_string(source.source_type));
    json += ',';
    json += number_field_json("byte_count", source.byte_count);
    json += '}';
  }
  json += ']';
  return json;
}

std::string state_result_json(const RuntimeSession& session, bool cancel_requested) {
  std::string json = "{";
  json += string_field_json("session_id", session.store.session_id());
  json += ',';
  json += string_field_json("session_path", session.store.session_path().string());
  json += ',';
  json += string_field_json("mode", ava::agent::to_string(session.mode));
  json += ',';
  json += string_field_json("provider", session.model.provider_id);
  json += ',';
  json += string_field_json("model", session.model.model_id);
  json += ',';
  json += string_field_json("workspace_dir", session.workspace_dir.string());
  json += ',';
  json += string_field_json("current_dir", session.current_dir.string());
  json += ',';
  json += bool_field_json("created", session.created);
  json += ',';
  json += bool_field_json("cancel_requested", cancel_requested);
  json += ',';
  json += number_field_json("context_source_count", session.context_sources.size());
  json += ",\"context_sources\":";
  json += context_sources_json(session);
  json += '}';
  return json;
}

ava::core::Result<std::string> list_sessions_result_json(const RuntimeSession& session) {
  auto sessions = ava::session::SessionStore::list_sessions(session.workspace_dir, session.paths.sessions_dir);
  if (!sessions) return std::unexpected(sessions.error());
  std::string json = "{\"sessions\":[";
  for (std::size_t index = 0; index < sessions->size(); ++index) {
    const auto& summary = (*sessions)[index];
    if (index > 0) json += ',';
    json += '{';
    json += string_field_json("session_id", summary.session_id);
    json += ',';
    json += string_field_json("path", summary.path.string());
    json += ',';
    json += string_field_json("last_updated", summary.last_updated);
    json += ',';
    json += number_field_json("entry_count", summary.entry_count);
    json += '}';
  }
  json += "]}";
  return json;
}

std::string command_result_json(const CommandResult& result) {
  std::string json = "{";
  json += bool_field_json("handled", result.handled);
  json += ',';
  json += bool_field_json("quit", result.quit);
  json += ",\"output\":";
  json += output_array_json(result.output);
  json += ',';
  json += string_field_json("text", joined_output(result.output));
  json += '}';
  return json;
}

ava::core::Error invalid_rpc(std::string message) {
  return ava::core::Error(ava::core::ErrorCategory::InvalidArgument, std::move(message));
}

ava::core::VoidResult write_record(std::ostream& out, std::string_view record) {
  out << record;
  out.flush();
  if (!out) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to write RPC JSONL record"));
  }
  return {};
}

ava::core::VoidResult write_success(std::ostream& out, std::string_view id, std::string_view result_json) {
  return write_record(out, serialize_rpc_success_jsonl(id, result_json));
}

ava::core::VoidResult write_error(std::ostream& out, std::string_view id, const ava::core::Error& error) {
  return write_record(out, serialize_rpc_error_jsonl(id, error));
}

ava::core::Result<RuntimeRunOptions> ensure_prompt_runtime_options(const RuntimeSession& session,
                                                                   RuntimeRunOptions options,
                                                                   ava::provider::Transport& transport) {
  if (!options.permission_resolver) {
    options.permission_resolver = build_headless_permission_resolver(HeadlessPermissionPolicyOptions{});
  }
  options.question_resolver = nullptr;

  if (!options.access_token.empty()) return options;

  auto credential = ava::config::load_openai_credential(session.paths);
  if (!credential) return std::unexpected(credential.error());
  if (!*credential) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "RPC prompt requires OpenAI auth");
    error.with_context("auth_file", session.paths.auth_file.string());
    return std::unexpected(std::move(error));
  }
  auto request_credential = ava::config::openai_credential_for_request(session.paths, **credential, transport);
  if (!request_credential) return std::unexpected(request_credential.error());
  auto token = ava::config::openai_access_token_for_request(*request_credential);
  if (!token) return std::unexpected(token.error());

  options.access_token = *token;
  options.openai_oauth = request_credential->type == ava::config::OpenAICredentialType::OAuth;
  options.openai_account_id = request_credential->account_id;
  if (options.openai_oauth && options.openai_account_id.empty()) {
    options.openai_account_id =
        ava::config::openai_oauth_account_id_from_token(request_credential->access_token).value_or("");
  }
  return options;
}

ava::core::Result<RuntimeSession> open_requested_session(const RuntimeSession& current,
                                                         const RuntimeOpenOptions& base_options,
                                                         std::string_view requested_session_id) {
  RuntimeOpenOptions options = base_options;
  options.workspace_dir = current.workspace_dir;
  options.current_dir = current.current_dir;
  options.mode = current.mode;
  options.paths = current.paths;
  options.requested_session_id = std::string(requested_session_id);
  options.continue_last_session = false;
  return open_runtime_session(options);
}

}  // namespace

ava::core::Result<RpcCommand> parse_rpc_command_line(std::string_view line) {
  if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
  if (line.size() > kMaxRpcLineBytes) return std::unexpected(invalid_rpc("RPC request line is too large"));
  if (!is_json_object_line(line)) return std::unexpected(invalid_rpc("malformed RPC JSON object"));

  auto id = ava::core::json::string_field(line, "id");
  if (!id || id->empty()) return std::unexpected(invalid_rpc("RPC request requires a non-empty string id"));
  auto type = ava::core::json::string_field(line, "type");
  if (!type || type->empty()) return std::unexpected(invalid_rpc("RPC request requires a non-empty string type"));

  return RpcCommand{.id = std::move(*id),
                    .type = std::move(*type),
                    .message = ava::core::json::string_field(line, "message"),
                    .session_id = ava::core::json::string_field(line, "session_id"),
                    .instructions = ava::core::json::string_field(line, "instructions")};
}

std::string serialize_rpc_success_jsonl(std::string_view id, std::string_view result_json) {
  std::string json = "{\"id\":\"" + ava::core::json::escape(id) + "\",\"type\":\"response\",\"success\":true";
  if (!result_json.empty()) {
    json += ",\"result\":";
    json += result_json;
  }
  json += "}\n";
  return json;
}

std::string serialize_rpc_error_jsonl(std::string_view id, const ava::core::Error& error) {
  std::string json =
      "{\"id\":\"" + ava::core::json::escape(id) + "\",\"type\":\"response\",\"success\":false,\"error\":{";
  json += string_field_json("category", ava::core::to_string(error.category()));
  json += ',';
  json += string_field_json("message", error.message());
  json += ',';
  json += string_field_json("details", error.format());
  json += "}}\n";
  return json;
}

ava::core::VoidResult run_rpc_loop(RuntimeSession& session, const RuntimeOpenOptions& open_options,
                                   const ava::provider::Provider& provider, ava::provider::Transport& transport,
                                   RuntimeRunOptions runtime_options, std::istream& in, std::ostream& out) {
  bool cancel_requested = false;
  bool active_run = false;
  if (!runtime_options.permission_resolver) {
    runtime_options.permission_resolver = build_headless_permission_resolver(HeadlessPermissionPolicyOptions{});
  }
  runtime_options.question_resolver = nullptr;

  std::string line;
  while (std::getline(in, line)) {
    auto command = parse_rpc_command_line(line);
    if (!command) {
      if (auto written = write_error(out, "", command.error()); !written) return written;
      continue;
    }

    if (command->type == "get_state") {
      if (auto written = write_success(out, command->id, state_result_json(session, cancel_requested)); !written) {
        return written;
      }
      continue;
    }

    if (command->type == "list_sessions") {
      auto sessions_json = list_sessions_result_json(session);
      if (!sessions_json) {
        if (auto written = write_error(out, command->id, sessions_json.error()); !written) return written;
        continue;
      }
      if (auto written = write_success(out, command->id, *sessions_json); !written) return written;
      continue;
    }

    if (command->type == "open_session") {
      if (!command->session_id || command->session_id->empty()) {
        if (auto written = write_error(out, command->id, invalid_rpc("open_session requires session_id")); !written) {
          return written;
        }
        continue;
      }
      auto opened = open_requested_session(session, open_options, *command->session_id);
      if (!opened) {
        if (auto written = write_error(out, command->id, opened.error()); !written) return written;
        continue;
      }
      session = std::move(*opened);
      if (auto written = write_success(out, command->id, state_result_json(session, cancel_requested)); !written) {
        return written;
      }
      continue;
    }

    if (command->type == "cancel") {
      cancel_requested = true;
      std::string json = "{";
      json += bool_field_json("cancel_requested", cancel_requested);
      json += ',';
      json += bool_field_json("active_run", active_run);
      json += '}';
      if (auto written = write_success(out, command->id, json); !written) return written;
      continue;
    }

    if (command->type == "context" || command->type == "export" || command->type == "compact") {
      std::string slash_command;
      if (command->type == "context") {
        slash_command = "/context";
      } else if (command->type == "export") {
        slash_command = "/export";
      } else {
        slash_command = "/compact";
        if (command->instructions) slash_command += " " + *command->instructions;
      }
      auto result = run_command(
          session,
          CommandRequest{
              .command = std::move(slash_command),
              .event_sink = [&](const RuntimeEvent& event) { return write_record(out, serialize_event_jsonl(event)); },
              .permission_resolver = runtime_options.permission_resolver});
      if (!result) {
        if (auto written = write_error(out, command->id, result.error()); !written) return written;
        continue;
      }
      if (auto written = write_success(out, command->id, command_result_json(*result)); !written) return written;
      continue;
    }

    if (command->type == "prompt") {
      if (!command->message) {
        if (auto written = write_error(out, command->id, invalid_rpc("prompt requires message")); !written) {
          return written;
        }
        continue;
      }

      auto prompt_options = ensure_prompt_runtime_options(session, runtime_options, transport);
      if (!prompt_options) {
        if (auto written = write_error(out, command->id, prompt_options.error()); !written) return written;
        continue;
      }
      prompt_options->cancel_requested = [&cancel_requested] { return cancel_requested; };
      prompt_options->event_sink = [&out](const RuntimeEvent& event) {
        return write_record(out, serialize_event_jsonl(event));
      };

      active_run = true;
      auto result = run_prompt(session, *command->message, provider, transport, *prompt_options);
      active_run = false;
      if (!result) {
        if (auto written = write_error(out, command->id, result.error()); !written) return written;
        continue;
      }
      std::string json = "{";
      json += string_field_json("session_id", session.store.session_id());
      json += ',';
      json += string_field_json("final_text", result->final_text);
      json += ',';
      json += string_field_json("stop_reason", result->stop_reason);
      json += ',';
      json += number_field_json("provider_iterations", result->provider_iterations);
      json += ',';
      json += number_field_json("tool_calls", result->tool_calls);
      json += '}';
      if (auto written = write_success(out, command->id, json); !written) return written;
      continue;
    }

    auto error = invalid_rpc("unknown RPC command type");
    error.with_context("type", command->type);
    if (auto written = write_error(out, command->id, error); !written) return written;
  }

  if (!in.eof() && in.fail()) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to read RPC stdin"));
  }
  return {};
}

int run_rpc_mode(const RpcModeOptions& options, std::istream& in, std::ostream& out, std::ostream& err) {
  auto session = open_runtime_session(options.open_options);
  if (!session) {
    err << session.error().format() << '\n';
    return 1;
  }

  RuntimeRunOptions runtime_options;
  runtime_options.permission_resolver = build_headless_permission_resolver(options.permission_policy);
  runtime_options.question_resolver = nullptr;

  ava::provider::OpenAIProvider provider;
  ava::provider::CurlCliTransport transport;
  auto result = run_rpc_loop(*session, options.open_options, provider, transport, std::move(runtime_options), in, out);
  if (!result) {
    err << result.error().format() << '\n';
    return 1;
  }
  return 0;
}

}  // namespace ava::app
