#include "ava/app/rpc_mode.h"

#include "ava/app/commands.h"
#include "ava/app/events.h"
#include "ava/app/rpc/handlers.h"
#include "ava/app/rpc/output.h"
#include "ava/app/rpc/protocol.h"
#include "ava/app/rpc/resolvers.h"
#include "ava/app/rpc/run_state.h"
#include "ava/app/rpc/serialization.h"

#include "ava/provider/curl_transport.h"
#include "ava/provider/registry.h"

#include <atomic>
#include <istream>
#include <mutex>
#include <optional>
#include <ostream>
#include <thread>
#include <utility>
#include <vector>

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

  auto write_state_response = [&](std::string_view id) -> ava::core::VoidResult {
    bool const canceled = rpc::cancel_requested(run_state);
    std::lock_guard lock(session_mutex);
    return rpc::write_success(output, id, rpc::state_result_json(session, canceled));
  };

  auto write_follow_up_errors = [&](std::vector<rpc::QueuedRpcMessage> const& follow_ups,
                                    std::string_view reason) -> ava::core::VoidResult {
    for (auto const& queued : follow_ups) {
      auto const error = reason == "canceled" ? rpc::canceled_error() : rpc::skipped_follow_up_error(reason);
      if (auto written = rpc::write_error(output, queued.request_id, error); !written) return written;
    }
    return {};
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
      if (auto written = write_state_response(command->id); !written) return written;
      continue;
    }

    if (command->type == "permission_grants") {
      if (auto written =
              rpc::write_success(output, command->id, rpc::permission_session_grants_result_json(pending_state));
          !written) {
        return written;
      }
      continue;
    }

    if (command->type == "permission_grant_revoke") {
      if (!command->grant_id || command->grant_id->empty()) {
        if (auto written =
                rpc::write_error(output, command->id, rpc::invalid_rpc("permission_grant_revoke requires grant_id"));
            !written) {
          return written;
        }
        continue;
      }
      auto revoked = rpc::permission_session_grant_revoke_result_json(pending_state, *command->grant_id);
      if (!revoked) {
        if (auto written = rpc::write_error(output, command->id, revoked.error()); !written) return written;
        continue;
      }
      auto envelope = rpc::resolver_event_envelope("permission_grant_revoked", command->id, command->id,
                                                   rpc::session_id_snapshot(session, session_mutex), *revoked);
      if (auto written = rpc::write_record(output, serialize_event_envelope_jsonl(envelope)); !written) return written;
      if (auto written = rpc::write_success(output, command->id, *revoked); !written) return written;
      continue;
    }

    if (command->type == "permission_grants_clear") {
      auto const cleared = rpc::permission_session_grants_clear_result_json(pending_state);
      auto envelope = rpc::resolver_event_envelope("permission_grants_cleared", command->id, command->id,
                                                   rpc::session_id_snapshot(session, session_mutex), cleared);
      if (auto written = rpc::write_record(output, serialize_event_envelope_jsonl(envelope)); !written) return written;
      if (auto written = rpc::write_success(output, command->id, cleared); !written) return written;
      continue;
    }

    if (command->type == "list_sessions") {
      std::lock_guard lock(session_mutex);
      auto sessions_json = rpc::list_sessions_result_json(session);
      if (!sessions_json) {
        if (auto written = rpc::write_error(output, command->id, sessions_json.error()); !written) return written;
        continue;
      }
      if (auto written = rpc::write_success(output, command->id, *sessions_json); !written) return written;
      continue;
    }

    if (command->type == "list_models") {
      std::lock_guard lock(session_mutex);
      auto models_json = rpc::list_models_result_json(session);
      if (!models_json) {
        if (auto written = rpc::write_error(output, command->id, models_json.error()); !written) return written;
        continue;
      }
      if (auto written = rpc::write_success(output, command->id, *models_json); !written) return written;
      continue;
    }

    if (command->type == "get_messages") {
      if (rpc::active_run(run_state)) {
        if (auto written = rpc::write_error(output, command->id, rpc::active_run_reject_error(command->type));
            !written) {
          return written;
        }
        continue;
      }
      std::lock_guard lock(session_mutex);
      auto messages_json = rpc::messages_result_json(session);
      if (!messages_json) {
        if (auto written = rpc::write_error(output, command->id, messages_json.error()); !written) return written;
        continue;
      }
      if (auto written = rpc::write_success(output, command->id, *messages_json); !written) return written;
      continue;
    }

    if (command->type == "get_session_stats") {
      if (rpc::active_run(run_state)) {
        if (auto written = rpc::write_error(output, command->id, rpc::active_run_reject_error(command->type));
            !written) {
          return written;
        }
        continue;
      }
      std::lock_guard lock(session_mutex);
      auto stats_json = rpc::session_stats_result_json(session);
      if (!stats_json) {
        if (auto written = rpc::write_error(output, command->id, stats_json.error()); !written) return written;
        continue;
      }
      if (auto written = rpc::write_success(output, command->id, *stats_json); !written) return written;
      continue;
    }

    if (command->type == "validate_session") {
      if (rpc::active_run(run_state)) {
        if (auto written = rpc::write_error(output, command->id, rpc::active_run_reject_error(command->type));
            !written) {
          return written;
        }
        continue;
      }
      std::lock_guard lock(session_mutex);
      auto validation_json = rpc::session_validation_result_json(session);
      if (!validation_json) {
        if (auto written = rpc::write_error(output, command->id, validation_json.error()); !written) return written;
        continue;
      }
      if (auto written = rpc::write_success(output, command->id, *validation_json); !written) return written;
      continue;
    }

    if (command->type == "set_model" || command->type == "cycle_model") {
      if (rpc::active_run(run_state)) {
        if (auto written = rpc::write_error(output, command->id, rpc::active_run_reject_error(command->type));
            !written) {
          return written;
        }
        continue;
      }
      std::lock_guard lock(session_mutex);
      ava::core::Result<ava::config::ModelInfo> selected = command->type == "set_model"
                                                               ? rpc::resolve_requested_model(session, *command)
                                                               : rpc::next_runtime_model(session);
      if (!selected) {
        if (auto written = rpc::write_error(output, command->id, selected.error()); !written) return written;
        continue;
      }
      auto switched = switch_runtime_model(session, std::move(*selected));
      if (!switched) {
        if (auto written = rpc::write_error(output, command->id, switched.error()); !written) return written;
        continue;
      }
      if (auto written = rpc::write_success(output, command->id,
                                            rpc::state_result_json(session, rpc::cancel_requested(run_state)));
          !written) {
        return written;
      }
      continue;
    }

    if (command->type == "set_reasoning" || command->type == "clear_reasoning") {
      if (rpc::active_run(run_state)) {
        if (auto written = rpc::write_error(output, command->id, rpc::active_run_reject_error(command->type));
            !written) {
          return written;
        }
        continue;
      }
      std::optional<RuntimeReasoningSelection> selection = std::nullopt;
      if (command->type == "set_reasoning") {
        if (!command->reasoning_level || command->reasoning_level->empty()) {
          if (auto written =
                  rpc::write_error(output, command->id, rpc::invalid_rpc("set_reasoning requires reasoning_level"));
              !written) {
            return written;
          }
          continue;
        }
        selection = RuntimeReasoningSelection{.level = *command->reasoning_level,
                                              .budget_tokens = command->reasoning_budget_tokens,
                                              .display = command->reasoning_display.value_or("")};
      }

      std::lock_guard lock(session_mutex);
      auto changed = set_runtime_reasoning(session, std::move(selection));
      if (!changed) {
        if (auto written = rpc::write_error(output, command->id, changed.error()); !written) return written;
        continue;
      }
      if (auto written = rpc::write_success(output, command->id,
                                            rpc::state_result_json(session, rpc::cancel_requested(run_state)));
          !written) {
        return written;
      }
      continue;
    }

    if (command->type == "new_session") {
      if (rpc::active_run(run_state)) {
        if (auto written = rpc::write_error(output, command->id, rpc::active_run_reject_error(command->type));
            !written) {
          return written;
        }
        continue;
      }
      std::lock_guard lock(session_mutex);
      auto created = rpc::create_new_session(session, open_options);
      if (!created) {
        if (auto written = rpc::write_error(output, command->id, created.error()); !written) return written;
        continue;
      }
      session = std::move(*created);
      {
        std::lock_guard state_lock(run_state.mutex);
        run_state.cancel_requested.store(false, std::memory_order_relaxed);
      }
      if (auto written = rpc::write_success(output, command->id, rpc::state_result_json(session, false)); !written) {
        return written;
      }
      continue;
    }

    if (command->type == "open_session" || command->type == "switch_session") {
      if (!command->session_id || command->session_id->empty()) {
        if (auto written =
                rpc::write_error(output, command->id, rpc::invalid_rpc(command->type + " requires session_id"));
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
      std::lock_guard lock(session_mutex);
      auto opened = rpc::open_requested_session(session, open_options, *command->session_id);
      if (!opened) {
        if (auto written = rpc::write_error(output, command->id, opened.error()); !written) return written;
        continue;
      }
      session = std::move(*opened);
      {
        std::lock_guard state_lock(run_state.mutex);
        run_state.cancel_requested.store(false, std::memory_order_relaxed);
      }
      if (auto written = rpc::write_success(output, command->id, rpc::state_result_json(session, false)); !written) {
        return written;
      }
      continue;
    }

    if (command->type == "permission_reply") {
      if (!command->request_id || command->request_id->empty()) {
        if (auto written =
                rpc::write_error(output, command->id, rpc::invalid_rpc(command->type + " requires request_id"));
            !written) {
          return written;
        }
        continue;
      }
      if (!command->decision) {
        if (auto written =
                rpc::write_error(output, command->id, rpc::invalid_rpc("permission_reply requires decision"));
            !written) {
          return written;
        }
        continue;
      }
      if (!command->correlation_id || command->correlation_id->empty()) {
        if (auto written =
                rpc::write_error(output, command->id, rpc::invalid_rpc("permission_reply requires correlation_id"));
            !written) {
          return written;
        }
        continue;
      }
      auto resolved = rpc::resolve_permission_reply(pending_state, *command->request_id, *command->correlation_id,
                                                    *command->decision);
      if (!resolved) {
        if (auto written = rpc::write_error(output, command->id, resolved.error()); !written) return written;
        continue;
      }
      auto envelope = rpc::resolver_event_envelope(
          "permission_replied", *command->correlation_id, *command->correlation_id,
          rpc::session_id_snapshot(session, session_mutex),
          rpc::permission_reply_payload_json(*command->request_id, *command->decision, command->reason));
      if (auto written = rpc::write_record(output, serialize_event_envelope_jsonl(envelope)); !written) return written;
      if (auto written = rpc::write_success(output, command->id, "{}"); !written) return written;
      continue;
    }

    if (command->type == "question_reply") {
      if (!command->request_id || command->request_id->empty()) {
        if (auto written =
                rpc::write_error(output, command->id, rpc::invalid_rpc(command->type + " requires request_id"));
            !written) {
          return written;
        }
        continue;
      }
      if (!command->correlation_id || command->correlation_id->empty()) {
        if (auto written =
                rpc::write_error(output, command->id, rpc::invalid_rpc("question_reply requires correlation_id"));
            !written) {
          return written;
        }
        continue;
      }
      auto resolved = rpc::resolve_question_reply(pending_state, *command->request_id, *command->correlation_id,
                                                  command->answer, command->selected);
      if (!resolved) {
        if (auto written = rpc::write_error(output, command->id, resolved.error()); !written) return written;
        continue;
      }
      auto envelope = rpc::resolver_event_envelope(
          "question_replied", *command->correlation_id, *command->correlation_id,
          rpc::session_id_snapshot(session, session_mutex),
          rpc::question_reply_payload_json(*command->request_id, command->answer, command->selected));
      if (auto written = rpc::write_record(output, serialize_event_envelope_jsonl(envelope)); !written) return written;
      if (auto written = rpc::write_success(output, command->id, "{}"); !written) return written;
      continue;
    }

    if (command->type == "steer") {
      if (!command->message) {
        if (auto written = rpc::write_error(output, command->id, rpc::invalid_rpc("steer requires message"));
            !written) {
          return written;
        }
        continue;
      }
      auto queued =
          rpc::queue_rpc_message(run_state.steering_messages, run_state, command->type, command->id, *command->message);
      if (!queued) {
        if (auto written = rpc::write_error(output, command->id, queued.error()); !written) return written;
        continue;
      }
      if (auto written = rpc::write_queue_event(output, session, session_mutex, "steer_queued", *queued); !written) {
        return written;
      }
      std::string json = "{";
      json += rpc::bool_field_json("queued", true);
      json += ',';
      json += rpc::string_field_json("correlation_id", queued->correlation_id);
      json += '}';
      if (auto written = rpc::write_success(output, command->id, json); !written) return written;
      continue;
    }

    if (command->type == "follow_up") {
      if (!command->message) {
        if (auto written = rpc::write_error(output, command->id, rpc::invalid_rpc("follow_up requires message"));
            !written) {
          return written;
        }
        continue;
      }
      auto queued = rpc::queue_rpc_message(run_state.follow_up_messages, run_state, command->type, command->id,
                                           *command->message);
      if (!queued) {
        if (auto written = rpc::write_error(output, command->id, queued.error()); !written) return written;
        continue;
      }
      if (auto written = rpc::write_queue_event(output, session, session_mutex, "follow_up_queued", *queued);
          !written) {
        return written;
      }
      continue;
    }

    if (command->type == "cancel") {
      bool was_active = false;
      std::string active_request_id;
      rpc::ClearedRpcQueues cleared;
      {
        std::lock_guard lock(run_state.mutex);
        run_state.cancel_requested.store(true, std::memory_order_relaxed);
        was_active = run_state.active_run;
        active_request_id = run_state.active_request_id;
        cleared.steering_messages.reserve(run_state.steering_messages.size());
        while (!run_state.steering_messages.empty()) {
          cleared.steering_messages.push_back(std::move(run_state.steering_messages.front()));
          run_state.steering_messages.pop_front();
        }
        cleared.follow_up_messages.reserve(run_state.follow_up_messages.size());
        while (!run_state.follow_up_messages.empty()) {
          cleared.follow_up_messages.push_back(std::move(run_state.follow_up_messages.front()));
          run_state.follow_up_messages.pop_front();
        }
      }
      static_cast<void>(rpc::cancel_pending_resolvers(pending_state));
      if (auto written = rpc::write_skipped_queue_events(output, session, session_mutex, cleared, "canceled");
          !written) {
        return written;
      }
      auto cancel_event = rpc::resolver_event_envelope(
          "cancel_requested", command->id, active_request_id.empty() ? command->id : active_request_id,
          rpc::session_id_snapshot(session, session_mutex),
          rpc::cancel_requested_payload_json(was_active, cleared.steering_messages.size(),
                                             cleared.follow_up_messages.size(), active_request_id));
      if (auto written = rpc::write_record(output, serialize_event_envelope_jsonl(cancel_event)); !written) {
        return written;
      }
      std::string json = "{";
      json += rpc::bool_field_json("cancel_requested", true);
      json += ',';
      json += rpc::bool_field_json("active_run", was_active);
      json += ',';
      json += rpc::number_field_json("cleared_steer", cleared.steering_messages.size());
      json += ',';
      json += rpc::number_field_json("cleared_follow_up", cleared.follow_up_messages.size());
      json += '}';
      if (auto written = rpc::write_success(output, command->id, json); !written) return written;
      if (auto written = write_follow_up_errors(cleared.follow_up_messages, "canceled"); !written) return written;
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
          if (auto written = write_follow_up_errors(cleared.follow_up_messages, reason); !written) {
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
        if (auto written = write_follow_up_errors(cleared.follow_up_messages,
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
    if (auto written = write_follow_up_errors(cleared.follow_up_messages, "canceled"); !written) return written;
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
