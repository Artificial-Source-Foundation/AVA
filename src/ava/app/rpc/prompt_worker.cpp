#include "sys.h"
#include "output.h"
#include "prompt_worker.h"
#include "resolvers.h"
#include "run_state.h"
#include "serialization.h"
#include "session_operators.h"
#include "ava/app/events.h"
#include "ava/app/runtime.h"

#include <optional>
#include <utility>
#include <vector>

namespace ava::app::rpc {

std::jthread make_rpc_prompt_worker(RpcPromptWorkerOptions options)
{
  return std::jthread([options = std::move(options)](std::stop_token stop_token) mutable {
    std::optional<QueuedRpcMessage> next_follow_up;

    auto finish_with_queue_cleanup = [&](std::string_view reason) {
      auto cleared = deactivate_and_clear_queued_messages(options.run_state);
      if (next_follow_up)
      {
        cleared.follow_up_messages.push_back(std::move(*next_follow_up));
        next_follow_up.reset();
      }
      if (auto written = write_skipped_queue_events(options.output, options.session, options.session_mutex, cleared, reason); !written)
      {
        record_async_error(options.run_state, std::move(written.error()));
      }
      if (auto written = write_follow_up_errors(options.output, cleared.follow_up_messages, reason); !written)
      {
        record_async_error(options.run_state, std::move(written.error()));
      }
    };

    std::string prompt_provider_id;
    {
      std::lock_guard lock(options.session_mutex);
      prompt_provider_id = options.session.model.provider_id;
    }
    auto prompt_options =
        ensure_prompt_runtime_options(options.paths, prompt_provider_id, std::move(options.runtime_options), options.auth_transport, "prompt");
    if (!prompt_options)
    {
      if (auto written = write_error(options.output, options.request_id, prompt_options.error()); !written)
      {
        record_async_error(options.run_state, std::move(written.error()));
      }
      finish_with_queue_cleanup("prompt_start_failed");
      return;
    }
    auto policy_permission_resolver = prompt_options->permission_resolver;

    auto prepare_next_follow_up = [&]() -> std::optional<QueuedRpcMessage> {
      auto follow_up = take_next_follow_up_message(options.run_state);
      if (!follow_up)
      {
        set_active_run(options.run_state, false);
        return std::nullopt;
      }
      set_active_request_id(options.run_state, follow_up->request_id);
      return follow_up;
    };

    auto run_one_prompt = [&](std::string const& request_id, std::string const& message, std::vector<ava::session::ImageAttachmentRef> image_attachments,
                              auto&& before_terminal_response) -> ava::core::VoidResult {
      set_active_request_id(options.run_state, request_id);
      prompt_options->image_attachments = std::move(image_attachments);
      prompt_options->cancel_requested = [&run_state = options.run_state, stop_token] { return stop_token.stop_requested() || cancel_requested(run_state); };
      prompt_options->session_mutex = &options.session_mutex;
      prompt_options->permission_resolver = make_rpc_permission_resolver(options.pending_state, options.output, options.run_state, options.session,
                                                                         options.session_mutex, policy_permission_resolver, request_id);
      prompt_options->question_resolver =
          make_rpc_question_resolver(options.pending_state, options.output, options.run_state, options.session, options.session_mutex, request_id);
      prompt_options->take_steering_messages = [&options, request_id]() -> ava::core::Result<std::vector<std::string>> {
        auto queued = take_queued_steering_messages(options.run_state, request_id);
        std::vector<std::string> messages;
        messages.reserve(queued.size());
        for (auto const& item : queued)
        {
          if (auto written = write_queue_event(options.output, options.session, options.session_mutex, "steer_applied", item); !written)
          {
            return std::unexpected(std::move(written.error()));
          }
          messages.push_back(item.message);
        }
        return messages;
      };

      EventBus event_bus;
      subscribe_event_envelope_writer(event_bus, options.output);
      prompt_options->event_sink = make_runtime_event_bus_adapter(event_bus, rpc_event_context(request_id));

      ProviderHandle prompt_provider;
      {
        std::lock_guard lock(options.session_mutex);
        auto selected_provider = provider_for_session_model(options.session, options.injected_provider_id, options.injected_provider);
        if (!selected_provider)
          return std::unexpected(std::move(selected_provider.error()));
        prompt_provider = std::move(*selected_provider);
      }

      auto result = run_prompt(options.session, message, prompt_provider.get(), options.transport, *prompt_options);
      ClearedRpcQueues skipped_steering;
      skipped_steering.steering_messages = clear_queued_steering_messages(options.run_state);
      if (auto written =
              write_skipped_queue_events(options.output, options.session, options.session_mutex, skipped_steering, "run_completed_before_safe_point");
          !written)
      {
        return std::unexpected(std::move(written.error()));
      }
      if (!result)
      {
        before_terminal_response();
        if (auto written = write_error(options.output, request_id, result.error()); !written)
        {
          return std::unexpected(std::move(written.error()));
        }
        return {};
      }
      before_terminal_response();
      if (auto written = write_success(options.output, request_id, prompt_result_json(session_id_snapshot(options.session, options.session_mutex), *result));
          !written)
      {
        return std::unexpected(std::move(written.error()));
      }
      return {};
    };

    auto prompt_run =
        run_one_prompt(options.request_id, options.message, std::move(options.image_attachments), [&] { next_follow_up = prepare_next_follow_up(); });
    if (!prompt_run)
    {
      record_async_error(options.run_state, std::move(prompt_run.error()));
      finish_with_queue_cleanup("prompt_start_failed");
      return;
    }

    while (!cancel_requested(options.run_state))
    {
      if (!next_follow_up)
        break;
      auto follow_up = std::move(*next_follow_up);
      next_follow_up.reset();
      set_active_request_id(options.run_state, follow_up.request_id);
      auto started_event = follow_up;
      started_event.correlation_id = follow_up.request_id;
      if (auto written = write_queue_event(options.output, options.session, options.session_mutex, "follow_up_started", started_event); !written)
      {
        record_async_error(options.run_state, std::move(written.error()));
        finish_with_queue_cleanup("prompt_start_failed");
        return;
      }
      auto follow_up_run = run_one_prompt(follow_up.request_id, follow_up.message, {}, [&] { next_follow_up = prepare_next_follow_up(); });
      if (!follow_up_run)
      {
        record_async_error(options.run_state, std::move(follow_up_run.error()));
        finish_with_queue_cleanup("prompt_start_failed");
        return;
      }
    }

    bool const canceled = cancel_requested(options.run_state);
    auto cleared = deactivate_and_clear_queued_messages(options.run_state);
    if (next_follow_up)
    {
      cleared.follow_up_messages.push_back(std::move(*next_follow_up));
      next_follow_up.reset();
    }
    if (auto written = write_skipped_queue_events(options.output, options.session, options.session_mutex, cleared,
                                                  canceled ? "canceled" : "run_completed_before_safe_point");
        !written)
    {
      record_async_error(options.run_state, std::move(written.error()));
    }
    if (auto written = write_follow_up_errors(options.output, cleared.follow_up_messages, canceled ? "canceled" : "run_completed_before_safe_point"); !written)
    {
      record_async_error(options.run_state, std::move(written.error()));
    }
  });
}

}  // namespace ava::app::rpc
