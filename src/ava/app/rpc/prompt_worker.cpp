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
    auto publish_terminal = [&](std::string const& request_id, ava::core::Result<std::string> result, bool allow_follow_up,
                                std::string_view terminal_reason) -> ava::core::Result<RpcFollowUpTransition> {
      RpcFollowUpTransition transition;
      if (allow_follow_up)
      {
        begin_prompt_terminal_publication(options.run_state);
      }
      else
      {
        transition.kind = RpcFollowUpTransitionKind::Deactivated;
        transition.cleared = begin_terminal_publication(options.run_state);
      }

      auto fail_publication = [&](ava::core::Error error) -> ava::core::Result<RpcFollowUpTransition> {
        complete_terminal_publication(options.run_state, request_id);
        return std::unexpected(std::move(error));
      };

      ava::core::VoidResult written;
      if (result)
        written = write_success(options.output, request_id, *result);
      else
        written = write_error(options.output, request_id, result.error());
      if (!written)
        return fail_publication(std::move(written.error()));

      if (allow_follow_up)
        transition = transition_after_prompt_terminal_response(options.run_state);

      if (transition.kind == RpcFollowUpTransitionKind::Activated)
      {
        auto started_event = *transition.follow_up;
        started_event.correlation_id = started_event.request_id;
        if (auto started = write_queue_event(options.output, options.session, options.session_mutex, "follow_up_started", started_event); !started)
          return fail_publication(std::move(started.error()));
      }
      else
      {
        auto const reason = transition.kind == RpcFollowUpTransitionKind::Skipped ? std::string_view("canceled") : terminal_reason;
        if (auto skipped = write_skipped_queue_events(options.output, options.session, options.session_mutex, transition.cleared, reason); !skipped)
          return fail_publication(std::move(skipped.error()));
        if (auto follow_up_errors = write_follow_up_errors(options.output, options.run_state, transition.cleared.follow_up_messages, reason); !follow_up_errors)
          return fail_publication(std::move(follow_up_errors.error()));
      }

      complete_terminal_publication(options.run_state, request_id);
      return transition;
    };

    auto abandon_after_output_failure = [&](ava::core::Error error) {
      record_async_error(options.run_state, std::move(error));
      static_cast<void>(deactivate_and_clear_queued_messages(options.run_state));
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
      auto published = publish_terminal(options.request_id, std::unexpected(std::move(prompt_options.error())), false, "prompt_start_failed");
      if (!published)
        abandon_after_output_failure(std::move(published.error()));
      return;
    }
    auto policy_permission_resolver = prompt_options->permission_resolver;

    auto run_one_prompt = [&](std::string const& request_id, std::string const& message,
                              std::vector<ava::session::ImageAttachmentRef> image_attachments) -> ava::core::Result<std::string> {
      set_active_request_id(options.run_state, request_id);
      prompt_options->request_id = request_id;
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
            return std::unexpected(std::move(written.error()));
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
        return std::unexpected(std::move(result.error()));
      return prompt_result_json(session_id_snapshot(options.session, options.session_mutex), *result);
    };

    std::string request_id = options.request_id;
    auto prompt_run = run_one_prompt(request_id, options.message, std::move(options.image_attachments));
    while (true)
    {
      auto published = publish_terminal(request_id, std::move(prompt_run), true, "run_completed_before_safe_point");
      if (!published)
      {
        abandon_after_output_failure(std::move(published.error()));
        return;
      }
      if (published->kind != RpcFollowUpTransitionKind::Activated)
        return;

      auto follow_up = std::move(*published->follow_up);
      request_id = follow_up.request_id;
      prompt_run = run_one_prompt(request_id, follow_up.message, {});
    }
  });
}

}  // namespace ava::app::rpc
