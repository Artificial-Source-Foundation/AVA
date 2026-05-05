#include "ava/app/rpc_mode.h"

#include <istream>
#include <mutex>
#include <optional>
#include <ostream>
#include <thread>
#include <utility>

#include "ava/app/rpc/command_handlers.h"
#include "ava/app/rpc/control_handlers.h"
#include "ava/app/rpc/handlers.h"
#include "ava/app/rpc/output.h"
#include "ava/app/rpc/prompt_handlers.h"
#include "ava/app/rpc/protocol.h"
#include "ava/app/rpc/query_handlers.h"
#include "ava/app/rpc/resolvers.h"
#include "ava/app/rpc/run_state.h"
#include "ava/app/rpc/runtime_handlers.h"
#include "ava/app/rpc/serialization.h"
#include "ava/app/rpc/session_handlers.h"
#include "ava/provider/curl_transport.h"
#include "ava/provider/registry.h"

namespace ava::app {

ava::core::VoidResult run_rpc_loop(RuntimeSession& session, RuntimeOpenOptions const& open_options,
                                   ava::provider::Provider const& provider, ava::provider::Transport& transport,
                                   ava::provider::Transport& auth_transport, RuntimeRunOptions runtime_options,
                                   std::istream& in, std::ostream& out)
{
  rpc::RpcOutput output(out);
  rpc::RpcRunState run_state;
  rpc::PendingResolverState pending_state;
  std::mutex session_mutex;
  std::optional<std::jthread> prompt_worker;
  if (!runtime_options.permission_resolver) {
    runtime_options.permission_resolver = build_headless_permission_resolver(HeadlessPermissionPolicyOptions{});
  }
  std::string const injected_provider_id = session.model.provider_id;

  output.on_write_failure = [&] {
    static_cast<void>(rpc::close_input_and_cancel(run_state));
    static_cast<void>(rpc::cancel_pending_resolvers(pending_state));
  };

  std::string line;
  std::optional<ava::core::Error> input_read_error;
  while (true) {
    auto read_line = rpc::read_rpc_line_bounded(in, line);
    if (!read_line) {
      if (read_line.error().category() == ava::core::ErrorCategory::InvalidArgument) {
        if (auto written = rpc::write_error(output, "", read_line.error()); !written) return written;
        continue;
      }
      input_read_error = std::move(read_line.error());
      break;
    }
    if (!*read_line) break;
    rpc::reap_prompt_worker(prompt_worker, run_state);
    if (auto async_error = rpc::take_async_error(run_state)) return std::unexpected(std::move(*async_error));
    auto command = parse_rpc_command_line(line);
    if (!command) {
      if (auto written = rpc::write_error(output, rpc::parse_error_response_id(line), command.error()); !written) {
        return written;
      }
      continue;
    }
    if (auto valid_version = rpc::validate_protocol_version(*command); !valid_version) {
      if (auto written = rpc::write_error(output, command->id, valid_version.error()); !written) return written;
      continue;
    }

    if (command->type == "get_protocol") {
      if (auto written = rpc::write_success(output, command->id, rpc::rpc_protocol_result_json()); !written) {
        return written;
      }
      continue;
    }

    if (command->type == "get_state") {
      if (auto written = rpc::handle_get_state_command(output, session, session_mutex, run_state, *command); !written)
        return written;
      continue;
    }

    if (command->type == "permission_grants") {
      if (auto written = rpc::handle_permission_grants_command(output, pending_state, *command); !written) {
        return written;
      }
      continue;
    }

    if (command->type == "permission_grant_revoke") {
      if (auto written =
              rpc::handle_permission_grant_revoke_command(output, session, session_mutex, pending_state, *command);
          !written) {
        return written;
      }
      continue;
    }

    if (command->type == "permission_grants_clear") {
      if (auto written =
              rpc::handle_permission_grants_clear_command(output, session, session_mutex, pending_state, *command);
          !written) {
        return written;
      }
      continue;
    }

    if (command->type == "list_sessions") {
      if (auto written = rpc::handle_list_sessions_command(output, session, session_mutex, *command); !written)
        return written;
      continue;
    }

    if (command->type == "list_models") {
      if (auto written = rpc::handle_list_models_command(output, session, session_mutex, *command); !written)
        return written;
      continue;
    }

    if (command->type == "get_messages") {
      if (auto written = rpc::handle_get_messages_command(output, session, session_mutex, run_state, *command);
          !written)
        return written;
      continue;
    }

    if (command->type == "get_session_stats") {
      if (auto written = rpc::handle_get_session_stats_command(output, session, session_mutex, run_state, *command);
          !written)
        return written;
      continue;
    }

    if (command->type == "validate_session") {
      if (auto written = rpc::handle_validate_session_command(output, session, session_mutex, run_state, *command);
          !written)
        return written;
      continue;
    }

    if (command->type == "set_model" || command->type == "cycle_model") {
      if (auto written = rpc::handle_model_command(output, session, session_mutex, run_state, *command); !written)
        return written;
      continue;
    }

    if (command->type == "set_reasoning" || command->type == "clear_reasoning") {
      if (auto written = rpc::handle_reasoning_command(output, session, session_mutex, run_state, *command); !written)
        return written;
      continue;
    }

    if (command->type == "new_session") {
      if (auto written =
              rpc::handle_new_session_command(output, session, open_options, session_mutex, run_state, *command);
          !written)
        return written;
      continue;
    }

    if (command->type == "open_session" || command->type == "switch_session") {
      if (auto written =
              rpc::handle_open_session_command(output, session, open_options, session_mutex, run_state, *command);
          !written)
        return written;
      continue;
    }

    if (command->type == "permission_reply") {
      if (auto written = rpc::handle_permission_reply_command(output, session, session_mutex, pending_state, *command);
          !written) {
        return written;
      }
      continue;
    }

    if (command->type == "question_reply") {
      if (auto written = rpc::handle_question_reply_command(output, session, session_mutex, pending_state, *command);
          !written) {
        return written;
      }
      continue;
    }

    if (command->type == "steer") {
      if (auto written = rpc::handle_steer_command(output, session, session_mutex, run_state, *command); !written) {
        return written;
      }
      continue;
    }

    if (command->type == "follow_up") {
      if (auto written = rpc::handle_follow_up_command(output, session, session_mutex, run_state, *command); !written) {
        return written;
      }
      continue;
    }

    if (command->type == "cancel") {
      if (auto written = rpc::handle_cancel_command(output, session, session_mutex, run_state, pending_state, *command);
          !written) {
        return written;
      }
      continue;
    }

    if (rpc::is_command_bridge_command(command->type)) {
      if (auto written =
              rpc::handle_command_bridge_command(output, session, session_mutex, run_state, *command, runtime_options,
                                                 provider, transport, auth_transport, injected_provider_id);
          !written)
        return written;
      continue;
    }

    if (command->type == "prompt") {
      if (auto written = rpc::handle_prompt_command(output, session, session_mutex, run_state, pending_state,
                                                    prompt_worker, *command, runtime_options, provider, transport,
                                                    auth_transport, injected_provider_id);
          !written)
        return written;
      continue;
    }

    auto error = rpc::invalid_rpc("unknown RPC command type");
    error.with_context("type", command->type);
    if (auto written = rpc::write_error(output, command->id, error); !written) return written;
  }

  if (auto closed =
          rpc::close_prompt_worker(output, session, session_mutex, run_state, pending_state, prompt_worker, "canceled");
      !closed)
    return closed;
  if (auto async_error = rpc::take_async_error(run_state)) return std::unexpected(std::move(*async_error));
  if (input_read_error) return std::unexpected(std::move(*input_read_error));

  return {};
}

ava::core::VoidResult run_rpc_loop(RuntimeSession& session, RuntimeOpenOptions const& open_options,
                                   ava::provider::Provider const& provider, ava::provider::Transport& transport,
                                   RuntimeRunOptions runtime_options, std::istream& in, std::ostream& out)
{
  return run_rpc_loop(session, open_options, provider, transport, transport, std::move(runtime_options), in, out);
}

int run_rpc_mode(RpcModeOptions const& options, std::istream& in, std::ostream& out, std::ostream& err)
{
  auto session = open_runtime_session(options.open_options);
  if (!session) {
    err << session.error().format() << '\n';
    return 1;
  }

  RuntimeRunOptions runtime_options;
  runtime_options.permission_resolver = build_headless_permission_resolver(options.permission_policy);
  runtime_options.question_resolver = nullptr;
  runtime_options.enable_transport_retries = true;

  auto registry = ava::provider::builtin_provider_registry();
  auto provider = registry.create(session->model.provider_id);
  if (!provider) {
    err << provider.error().format() << '\n';
    return 1;
  }
  ava::provider::CurlCliTransport transport;
  auto result = run_rpc_loop(*session, options.open_options, **provider, transport, transport,
                             std::move(runtime_options), in, out);
  if (!result) {
    err << result.error().format() << '\n';
    return 1;
  }
  return 0;
}

}  // namespace ava::app
