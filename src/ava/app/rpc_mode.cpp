#include "ava/app/rpc_mode.h"

#include <atomic>
#include <istream>
#include <mutex>
#include <optional>
#include <ostream>
#include <thread>
#include <utility>
#include <vector>

#include "ava/app/commands.h"
#include "ava/app/events.h"
#include "ava/app/rpc/control_handlers.h"
#include "ava/app/rpc/handlers.h"
#include "ava/app/rpc/output.h"
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

  auto reap_finished_prompt = [&] {
    if (prompt_worker && !rpc::active_run(run_state)) {
      prompt_worker.reset();
    }
  };

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
    reap_finished_prompt();
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

    if (command->type == "context" || command->type == "export" || command->type == "compact" ||
        rpc::is_plugin_rpc_command(command->type) || rpc::is_mcp_rpc_command(command->type)) {
      bool compact_active_run = false;
      {
        std::lock_guard lock(run_state.mutex);
        if (run_state.active_run) {
          if (auto written = rpc::write_error(output, command->id, rpc::active_run_reject_error(command->type));
              !written) {
            return written;
          }
          continue;
        }
        if (command->type == "compact") {
          run_state.active_run = true;
          run_state.active_request_id = command->id;
          run_state.cancel_requested.store(false, std::memory_order_relaxed);
          compact_active_run = true;
        }
      }
      auto clear_compact_active_run = [&] {
        if (compact_active_run) {
          rpc::set_active_run(run_state, false);
          compact_active_run = false;
        }
      };
      auto write_command_error = [&](ava::core::Error error) -> ava::core::VoidResult {
        clear_compact_active_run();
        return rpc::write_error(output, command->id, std::move(error));
      };
      auto write_command_success = [&](std::string json) -> ava::core::VoidResult {
        clear_compact_active_run();
        return rpc::write_success(output, command->id, std::move(json));
      };
      std::string slash_command;
      if (command->type == "context") {
        slash_command = "/context";
      } else if (command->type == "export") {
        slash_command = "/export";
      } else if (command->type == "compact") {
        slash_command = "/compact";
        if (command->instructions) slash_command += " " + *command->instructions;
      } else if (rpc::is_plugin_rpc_command(command->type)) {
        auto plugin_command = rpc::plugin_rpc_slash_command(*command);
        if (!plugin_command) {
          if (auto written = write_command_error(plugin_command.error()); !written) return written;
          continue;
        }
        slash_command = std::move(*plugin_command);
      } else {
        auto mcp_command = rpc::mcp_rpc_slash_command(*command);
        if (!mcp_command) {
          if (auto written = write_command_error(mcp_command.error()); !written) return written;
          continue;
        }
        slash_command = std::move(*mcp_command);
      }
      std::optional<RuntimeRunOptions> compact_runtime_options;
      std::optional<rpc::ProviderHandle> compact_provider;
      if (command->type == "compact") {
        ava::config::XdgPaths paths;
        std::string provider_id;
        {
          std::lock_guard lock(session_mutex);
          paths = session.paths;
          provider_id = session.model.provider_id;
          auto selected_provider = rpc::provider_for_session_model(session, injected_provider_id, provider);
          if (!selected_provider) {
            if (auto written = write_command_error(selected_provider.error()); !written) return written;
            continue;
          }
          compact_provider = std::move(*selected_provider);
        }
        auto ensured =
            rpc::ensure_prompt_runtime_options(paths, provider_id, runtime_options, auth_transport, "compact");
        if (!ensured) {
          if (auto written = write_command_error(ensured.error()); !written) return written;
          continue;
        }
        compact_runtime_options = std::move(*ensured);
      }
      EventBus event_bus;
      rpc::subscribe_event_envelope_writer(event_bus, output);
      std::unique_lock lock(session_mutex, std::defer_lock);
      if (command->type != "compact") lock.lock();
      auto summary_generator =
          command->type == "compact"
              ? CompactionSummaryGenerator([&](std::vector<ava::session::SessionEntry> const& entries,
                                               ava::session::CompactionConfig const& config,
                                               std::string_view instructions, std::size_t estimated_tokens) {
                  return generate_compaction_summary(session, entries, config, instructions, estimated_tokens,
                                                     compact_provider->get(), transport, *compact_runtime_options);
                })
              : CompactionSummaryGenerator{};
      auto result = run_command(session, CommandRequest{.command = std::move(slash_command),
                                                        .event_sink = make_runtime_event_bus_adapter(
                                                            event_bus, rpc::rpc_event_context(command->id)),
                                                        .permission_resolver = runtime_options.permission_resolver,
                                                        .compaction_summary_generator = std::move(summary_generator),
                                                        .session_mutex = &session_mutex,
                                                        .propagate_compaction_errors = command->type == "compact"});
      if (!result) {
        if (auto written = write_command_error(result.error()); !written) return written;
        continue;
      }
      if (auto written = write_command_success(rpc::command_result_json(*result)); !written) return written;
      continue;
    }

    if (command->type == "prompt") {
      if (!command->message) {
        if (auto written = rpc::write_error(output, command->id, rpc::invalid_rpc("prompt requires message"));
            !written) {
          return written;
        }
        continue;
      }

      if (rpc::active_run(run_state)) {
        if (auto written = rpc::write_error(output, command->id, rpc::active_run_reject_error(command->type));
            !written) {
          return written;
        }
        continue;
      }

      reap_finished_prompt();
      ava::config::XdgPaths paths;
      {
        std::lock_guard lock(session_mutex);
        paths = session.paths;
      }
      rpc::set_active_run(run_state, true, command->id);
      auto prompt_base_options = runtime_options;
      auto const prompt_id = command->id;
      auto const prompt_message = *command->message;
      prompt_worker.emplace([&, prompt_id, prompt_message, prompt_base_options = std::move(prompt_base_options),
                             paths = std::move(paths)](std::stop_token stop_token) mutable {
        auto finish_with_queue_cleanup = [&](std::string_view reason) {
          auto cleared = rpc::deactivate_and_clear_queued_messages(run_state);
          if (auto written = rpc::write_skipped_queue_events(output, session, session_mutex, cleared, reason);
              !written) {
            rpc::record_async_error(run_state, std::move(written.error()));
          }
          if (auto written = rpc::write_follow_up_errors(output, cleared.follow_up_messages, reason); !written) {
            rpc::record_async_error(run_state, std::move(written.error()));
          }
        };

        std::string prompt_provider_id;
        {
          std::lock_guard lock(session_mutex);
          prompt_provider_id = session.model.provider_id;
        }
        auto prompt_options = rpc::ensure_prompt_runtime_options(
            paths, prompt_provider_id, std::move(prompt_base_options), auth_transport, "prompt");
        if (!prompt_options) {
          if (auto written = rpc::write_error(output, prompt_id, prompt_options.error()); !written) {
            rpc::record_async_error(run_state, std::move(written.error()));
          }
          finish_with_queue_cleanup("prompt_start_failed");
          return;
        }
        auto policy_permission_resolver = prompt_options->permission_resolver;
        auto run_one_prompt = [&](std::string const& request_id, std::string const& message) -> ava::core::VoidResult {
          rpc::set_active_request_id(run_state, request_id);
          prompt_options->cancel_requested = [&run_state, stop_token] {
            return stop_token.stop_requested() || rpc::cancel_requested(run_state);
          };
          prompt_options->session_mutex = &session_mutex;
          prompt_options->permission_resolver = rpc::make_rpc_permission_resolver(
              pending_state, output, run_state, session, session_mutex, policy_permission_resolver, request_id);
          prompt_options->question_resolver =
              rpc::make_rpc_question_resolver(pending_state, output, run_state, session, session_mutex, request_id);
          prompt_options->take_steering_messages = [&, request_id]() -> ava::core::Result<std::vector<std::string>> {
            auto queued = rpc::take_queued_steering_messages(run_state, request_id);
            std::vector<std::string> messages;
            messages.reserve(queued.size());
            for (auto const& item : queued) {
              if (auto written = rpc::write_queue_event(output, session, session_mutex, "steer_applied", item);
                  !written) {
                return std::unexpected(std::move(written.error()));
              }
              messages.push_back(item.message);
            }
            return messages;
          };

          EventBus event_bus;
          rpc::subscribe_event_envelope_writer(event_bus, output);
          prompt_options->event_sink = make_runtime_event_bus_adapter(event_bus, rpc::rpc_event_context(request_id));

          rpc::ProviderHandle prompt_provider;
          {
            std::lock_guard lock(session_mutex);
            auto selected_provider = rpc::provider_for_session_model(session, injected_provider_id, provider);
            if (!selected_provider) return std::unexpected(std::move(selected_provider.error()));
            prompt_provider = std::move(*selected_provider);
          }

          auto result = run_prompt(session, message, prompt_provider.get(), transport, *prompt_options);
          rpc::ClearedRpcQueues skipped_steering;
          skipped_steering.steering_messages = rpc::clear_queued_steering_messages(run_state);
          if (auto written = rpc::write_skipped_queue_events(output, session, session_mutex, skipped_steering,
                                                             "run_completed_before_safe_point");
              !written) {
            return std::unexpected(std::move(written.error()));
          }
          if (!result) {
            if (auto written = rpc::write_error(output, request_id, result.error()); !written) {
              return std::unexpected(std::move(written.error()));
            }
            return {};
          }
          if (auto written = rpc::write_success(
                  output, request_id,
                  rpc::prompt_result_json(rpc::session_id_snapshot(session, session_mutex), *result));
              !written) {
            return std::unexpected(std::move(written.error()));
          }
          return {};
        };

        auto prompt_run = run_one_prompt(prompt_id, prompt_message);
        if (!prompt_run) {
          rpc::record_async_error(run_state, std::move(prompt_run.error()));
          finish_with_queue_cleanup("prompt_start_failed");
          return;
        }

        while (!rpc::cancel_requested(run_state)) {
          auto follow_up = rpc::take_next_follow_up_message(run_state);
          if (!follow_up) break;
          rpc::set_active_request_id(run_state, follow_up->request_id);
          auto started_event = *follow_up;
          started_event.correlation_id = follow_up->request_id;
          if (auto written = rpc::write_queue_event(output, session, session_mutex, "follow_up_started", started_event);
              !written) {
            rpc::record_async_error(run_state, std::move(written.error()));
            finish_with_queue_cleanup("prompt_start_failed");
            return;
          }
          auto follow_up_run = run_one_prompt(follow_up->request_id, follow_up->message);
          if (!follow_up_run) {
            rpc::record_async_error(run_state, std::move(follow_up_run.error()));
            finish_with_queue_cleanup("prompt_start_failed");
            return;
          }
        }

        bool const canceled = rpc::cancel_requested(run_state);
        auto cleared = rpc::deactivate_and_clear_queued_messages(run_state);
        if (auto written = rpc::write_skipped_queue_events(output, session, session_mutex, cleared,
                                                           canceled ? "canceled" : "run_completed_before_safe_point");
            !written) {
          rpc::record_async_error(run_state, std::move(written.error()));
        }
        if (auto written = rpc::write_follow_up_errors(output, cleared.follow_up_messages,
                                                       canceled ? "canceled" : "run_completed_before_safe_point");
            !written) {
          rpc::record_async_error(run_state, std::move(written.error()));
        }
      });
      continue;
    }

    auto error = rpc::invalid_rpc("unknown RPC command type");
    error.with_context("type", command->type);
    if (auto written = rpc::write_error(output, command->id, error); !written) return written;
  }

  if (prompt_worker) {
    auto cleared = rpc::close_input_and_cancel(run_state);
    static_cast<void>(rpc::cancel_pending_resolvers(pending_state));
    if (auto written = rpc::write_skipped_queue_events(output, session, session_mutex, cleared, "canceled"); !written) {
      return written;
    }
    if (auto written = rpc::write_follow_up_errors(output, cleared.follow_up_messages, "canceled"); !written)
      return written;
    prompt_worker.reset();
  }
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
