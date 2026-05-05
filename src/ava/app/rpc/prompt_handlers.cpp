#include "ava/app/rpc/prompt_handlers.h"

#include <string>
#include <utility>
#include <vector>

#include "ava/app/events.h"
#include "ava/app/rpc/control_handlers.h"
#include "ava/app/rpc/handlers.h"
#include "ava/app/rpc/serialization.h"

namespace ava::app::rpc {

void reap_prompt_worker(std::optional<std::jthread>& prompt_worker, RpcRunState& run_state)
{
  if (prompt_worker && !active_run(run_state)) prompt_worker.reset();
}

ava::core::VoidResult handle_prompt_command(RpcOutput& output, RuntimeSession& session, std::mutex& session_mutex,
                                            RpcRunState& run_state, PendingResolverState& pending_state,
                                            std::optional<std::jthread>& prompt_worker, RpcCommand const& command,
                                            RuntimeRunOptions const& runtime_options,
                                            ava::provider::Provider const& provider,
                                            ava::provider::Transport& transport,
                                            ava::provider::Transport& auth_transport,
                                            std::string_view injected_provider_id)
{
  if (!command.message) return write_error(output, command.id, invalid_rpc("prompt requires message"));

  if (active_run(run_state)) return write_error(output, command.id, active_run_reject_error(command.type));

  reap_prompt_worker(prompt_worker, run_state);
  ava::config::XdgPaths paths;
  {
    std::lock_guard lock(session_mutex);
    paths = session.paths;
  }
  set_active_run(run_state, true, command.id);
  auto prompt_base_options = runtime_options;
  auto const prompt_id = command.id;
  auto const prompt_message = *command.message;
  prompt_worker.emplace([&, prompt_id, prompt_message, prompt_base_options = std::move(prompt_base_options),
                         paths = std::move(paths)](std::stop_token stop_token) mutable {
    auto finish_with_queue_cleanup = [&](std::string_view reason) {
      auto cleared = deactivate_and_clear_queued_messages(run_state);
      if (auto written = write_skipped_queue_events(output, session, session_mutex, cleared, reason); !written) {
        record_async_error(run_state, std::move(written.error()));
      }
      if (auto written = write_follow_up_errors(output, cleared.follow_up_messages, reason); !written) {
        record_async_error(run_state, std::move(written.error()));
      }
    };

    std::string prompt_provider_id;
    {
      std::lock_guard lock(session_mutex);
      prompt_provider_id = session.model.provider_id;
    }
    auto prompt_options = ensure_prompt_runtime_options(paths, prompt_provider_id, std::move(prompt_base_options),
                                                        auth_transport, "prompt");
    if (!prompt_options) {
      if (auto written = write_error(output, prompt_id, prompt_options.error()); !written) {
        record_async_error(run_state, std::move(written.error()));
      }
      finish_with_queue_cleanup("prompt_start_failed");
      return;
    }
    auto policy_permission_resolver = prompt_options->permission_resolver;
    auto run_one_prompt = [&](std::string const& request_id, std::string const& message) -> ava::core::VoidResult {
      set_active_request_id(run_state, request_id);
      prompt_options->cancel_requested = [&run_state, stop_token] {
        return stop_token.stop_requested() || cancel_requested(run_state);
      };
      prompt_options->session_mutex = &session_mutex;
      prompt_options->permission_resolver = make_rpc_permission_resolver(
          pending_state, output, run_state, session, session_mutex, policy_permission_resolver, request_id);
      prompt_options->question_resolver =
          make_rpc_question_resolver(pending_state, output, run_state, session, session_mutex, request_id);
      prompt_options->take_steering_messages = [&, request_id]() -> ava::core::Result<std::vector<std::string>> {
        auto queued = take_queued_steering_messages(run_state, request_id);
        std::vector<std::string> messages;
        messages.reserve(queued.size());
        for (auto const& item : queued) {
          if (auto written = write_queue_event(output, session, session_mutex, "steer_applied", item); !written) {
            return std::unexpected(std::move(written.error()));
          }
          messages.push_back(item.message);
        }
        return messages;
      };

      EventBus event_bus;
      subscribe_event_envelope_writer(event_bus, output);
      prompt_options->event_sink = make_runtime_event_bus_adapter(event_bus, rpc_event_context(request_id));

      ProviderHandle prompt_provider;
      {
        std::lock_guard lock(session_mutex);
        auto selected_provider = provider_for_session_model(session, injected_provider_id, provider);
        if (!selected_provider) return std::unexpected(std::move(selected_provider.error()));
        prompt_provider = std::move(*selected_provider);
      }

      auto result = run_prompt(session, message, prompt_provider.get(), transport, *prompt_options);
      ClearedRpcQueues skipped_steering;
      skipped_steering.steering_messages = clear_queued_steering_messages(run_state);
      if (auto written = write_skipped_queue_events(output, session, session_mutex, skipped_steering,
                                                    "run_completed_before_safe_point");
          !written) {
        return std::unexpected(std::move(written.error()));
      }
      if (!result) {
        if (auto written = write_error(output, request_id, result.error()); !written) {
          return std::unexpected(std::move(written.error()));
        }
        return {};
      }
      if (auto written = write_success(output, request_id,
                                       prompt_result_json(session_id_snapshot(session, session_mutex), *result));
          !written) {
        return std::unexpected(std::move(written.error()));
      }
      return {};
    };

    auto prompt_run = run_one_prompt(prompt_id, prompt_message);
    if (!prompt_run) {
      record_async_error(run_state, std::move(prompt_run.error()));
      finish_with_queue_cleanup("prompt_start_failed");
      return;
    }

    while (!cancel_requested(run_state)) {
      auto follow_up = take_next_follow_up_message(run_state);
      if (!follow_up) break;
      set_active_request_id(run_state, follow_up->request_id);
      auto started_event = *follow_up;
      started_event.correlation_id = follow_up->request_id;
      if (auto written = write_queue_event(output, session, session_mutex, "follow_up_started", started_event);
          !written) {
        record_async_error(run_state, std::move(written.error()));
        finish_with_queue_cleanup("prompt_start_failed");
        return;
      }
      auto follow_up_run = run_one_prompt(follow_up->request_id, follow_up->message);
      if (!follow_up_run) {
        record_async_error(run_state, std::move(follow_up_run.error()));
        finish_with_queue_cleanup("prompt_start_failed");
        return;
      }
    }

    bool const canceled = cancel_requested(run_state);
    auto cleared = deactivate_and_clear_queued_messages(run_state);
    if (auto written = write_skipped_queue_events(output, session, session_mutex, cleared,
                                                  canceled ? "canceled" : "run_completed_before_safe_point");
        !written) {
      record_async_error(run_state, std::move(written.error()));
    }
    if (auto written = write_follow_up_errors(output, cleared.follow_up_messages,
                                              canceled ? "canceled" : "run_completed_before_safe_point");
        !written) {
      record_async_error(run_state, std::move(written.error()));
    }
  });
  return {};
}

ava::core::VoidResult close_prompt_worker(RpcOutput& output, RuntimeSession& session, std::mutex& session_mutex,
                                          RpcRunState& run_state, PendingResolverState& pending_state,
                                          std::optional<std::jthread>& prompt_worker, std::string_view reason)
{
  if (!prompt_worker) return {};

  auto cleared = close_input_and_cancel(run_state);
  static_cast<void>(cancel_pending_resolvers(pending_state));
  if (auto written = write_skipped_queue_events(output, session, session_mutex, cleared, reason); !written) {
    return written;
  }
  if (auto written = write_follow_up_errors(output, cleared.follow_up_messages, reason); !written) return written;
  set_active_run(run_state, false);
  prompt_worker.reset();
  return {};
}

}  // namespace ava::app::rpc
