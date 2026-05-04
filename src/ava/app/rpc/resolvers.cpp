#include "ava/app/rpc/resolvers.h"

#include <utility>

#include "ava/app/rpc/protocol.h"
#include "ava/app/rpc/serialization.h"
#include "ava/core/ids.h"

namespace ava::app::rpc {
namespace {

ava::core::Error no_pending_request_error(std::string_view request_id) {
  auto error = invalid_rpc("RPC resolver reply has no matching pending request");
  error.with_context("request_id", std::string(request_id));
  return error;
}

std::string next_resolver_request_id(std::string_view prefix) { return ava::core::make_id(prefix); }

}  // namespace

bool cancel_pending_resolvers(PendingResolverState& pending_state) {
  std::lock_guard lock(pending_state.mutex);
  const bool had_pending = !pending_state.permission_requests.empty() || !pending_state.question_requests.empty();
  for (auto& [request_id, request] : pending_state.permission_requests) {
    static_cast<void>(request_id);
    request->resolved = true;
    request->resolution = ava::permissions::PermissionResolution::Deny;
    request->error = canceled_error();
  }
  for (auto& [request_id, request] : pending_state.question_requests) {
    static_cast<void>(request_id);
    request->resolved = true;
    request->error = canceled_error();
  }
  pending_state.permission_requests.clear();
  pending_state.question_requests.clear();
  pending_state.cv.notify_all();
  return had_pending;
}

ava::permissions::PermissionResolver make_rpc_permission_resolver(
    PendingResolverState& pending_state, RpcOutput& output, RpcRunState& run_state, const RuntimeSession& session,
    std::mutex& session_mutex, ava::permissions::PermissionResolver policy_resolver, std::string prompt_request_id) {
  return [&pending_state, &output, &run_state, &session, &session_mutex, policy_resolver = std::move(policy_resolver),
          prompt_request_id = std::move(prompt_request_id)](const ava::permissions::PermissionPrompt& prompt)
             -> ava::core::Result<ava::permissions::PermissionResolution> {
    if (cancel_requested(run_state)) return std::unexpected(canceled_error());
    if (input_closed(run_state)) return std::unexpected(canceled_error());

    if (policy_resolver) {
      auto policy_result = policy_resolver(prompt);
      if (!policy_result) return std::unexpected(std::move(policy_result.error()));
      if (*policy_result == ava::permissions::PermissionResolution::Allow) {
        return ava::permissions::PermissionResolution::Allow;
      }
    }

    auto pending = std::make_shared<PendingPermissionRequest>();
    pending->correlation_id = prompt_request_id;
    std::string request_id;
    {
      std::lock_guard lock(pending_state.mutex);
      request_id = next_resolver_request_id("permission");
      pending_state.permission_requests[request_id] = pending;
    }

    if (cancel_requested(run_state) || input_closed(run_state)) {
      std::lock_guard lock(pending_state.mutex);
      pending_state.permission_requests.erase(request_id);
      return std::unexpected(canceled_error());
    }

    auto envelope = resolver_event_envelope("permission_requested", prompt_request_id, prompt_request_id,
                                            session_id_snapshot(session, session_mutex),
                                            permission_request_payload_json(request_id, prompt));
    if (auto written = write_record(output, serialize_event_envelope_jsonl(envelope)); !written) {
      std::lock_guard lock(pending_state.mutex);
      pending_state.permission_requests.erase(request_id);
      return std::unexpected(std::move(written.error()));
    }

    std::unique_lock lock(pending_state.mutex);
    pending_state.cv.wait(lock, [&pending] { return pending->resolved; });
    if (pending->error) return std::unexpected(*pending->error);
    if (!pending->resolution) return std::unexpected(canceled_error());
    return *pending->resolution;
  };
}

ava::agent::QuestionResolver make_rpc_question_resolver(PendingResolverState& pending_state, RpcOutput& output,
                                                        RpcRunState& run_state, const RuntimeSession& session,
                                                        std::mutex& session_mutex, std::string prompt_request_id) {
  return
      [&pending_state, &output, &run_state, &session, &session_mutex, prompt_request_id = std::move(prompt_request_id)](
          const ava::agent::QuestionPrompt& prompt) -> ava::core::Result<ava::agent::QuestionAnswer> {
        if (cancel_requested(run_state)) return std::unexpected(canceled_error());
        if (input_closed(run_state)) return std::unexpected(canceled_error());
        if (prompt.multiple) {
          return std::unexpected(invalid_rpc("RPC question resolver does not support multiple selections yet"));
        }

        auto pending = std::make_shared<PendingQuestionRequest>();
        pending->correlation_id = prompt_request_id;
        pending->allow_custom = prompt.allow_custom;
        pending->options = prompt.options;
        std::string request_id;
        {
          std::lock_guard lock(pending_state.mutex);
          request_id = next_resolver_request_id("question");
          pending_state.question_requests[request_id] = pending;
        }

        if (cancel_requested(run_state) || input_closed(run_state)) {
          std::lock_guard lock(pending_state.mutex);
          pending_state.question_requests.erase(request_id);
          return std::unexpected(canceled_error());
        }

        auto envelope = resolver_event_envelope("question_requested", prompt_request_id, prompt_request_id,
                                                session_id_snapshot(session, session_mutex),
                                                question_request_payload_json(request_id, prompt));
        if (auto written = write_record(output, serialize_event_envelope_jsonl(envelope)); !written) {
          std::lock_guard lock(pending_state.mutex);
          pending_state.question_requests.erase(request_id);
          return std::unexpected(std::move(written.error()));
        }

        std::unique_lock lock(pending_state.mutex);
        pending_state.cv.wait(lock, [&pending] { return pending->resolved; });
        if (pending->error) return std::unexpected(*pending->error);
        if (!pending->answer) return std::unexpected(canceled_error());
        return *pending->answer;
      };
}

ava::core::VoidResult resolve_permission_reply(PendingResolverState& pending_state, std::string_view request_id,
                                               std::string_view correlation_id, std::string_view decision) {
  ava::permissions::PermissionResolution resolution = ava::permissions::PermissionResolution::Deny;
  if (decision == "allow") {
    resolution = ava::permissions::PermissionResolution::Allow;
  } else if (decision == "deny") {
    resolution = ava::permissions::PermissionResolution::Deny;
  } else {
    auto error = invalid_rpc("permission_reply decision must be allow or deny");
    error.with_context("decision", std::string(decision));
    return std::unexpected(std::move(error));
  }

  std::shared_ptr<PendingPermissionRequest> pending;
  {
    std::lock_guard lock(pending_state.mutex);
    auto found = pending_state.permission_requests.find(std::string(request_id));
    if (found == pending_state.permission_requests.end()) {
      return std::unexpected(no_pending_request_error(request_id));
    }
    pending = found->second;
    if (pending->correlation_id != correlation_id) {
      auto error = invalid_rpc("permission_reply correlation_id does not match pending request");
      error.with_context("request_id", std::string(request_id));
      return std::unexpected(std::move(error));
    }
    pending_state.permission_requests.erase(found);
    pending->resolved = true;
    pending->resolution = resolution;
  }
  pending_state.cv.notify_all();
  return {};
}

ava::core::VoidResult resolve_question_reply(PendingResolverState& pending_state, std::string_view request_id,
                                             std::string_view correlation_id, const std::optional<std::string>& answer,
                                             const std::optional<std::string>& selected) {
  if (answer && selected) return std::unexpected(invalid_rpc("question_reply requires answer or selected, not both"));

  std::shared_ptr<PendingQuestionRequest> pending;
  ava::agent::QuestionAnswer parsed;
  {
    std::lock_guard lock(pending_state.mutex);
    auto found = pending_state.question_requests.find(std::string(request_id));
    if (found == pending_state.question_requests.end()) {
      return std::unexpected(no_pending_request_error(request_id));
    }
    pending = found->second;
    if (pending->correlation_id != correlation_id) {
      auto error = invalid_rpc("question_reply correlation_id does not match pending request");
      error.with_context("request_id", std::string(request_id));
      return std::unexpected(std::move(error));
    }

    if (answer) {
      if (!pending->allow_custom) {
        return std::unexpected(invalid_rpc("question_reply answer is not allowed for this request"));
      }
      parsed.custom_text = *answer;
    } else if (selected) {
      bool valid_option = false;
      for (const auto& option : pending->options) valid_option = valid_option || option.value == *selected;
      if (!valid_option) {
        return std::unexpected(invalid_rpc("question_reply selected option is not valid for this request"));
      }
      parsed.selected_options.push_back(*selected);
    } else {
      return std::unexpected(invalid_rpc("question_reply requires answer or selected"));
    }

    pending_state.question_requests.erase(found);
    pending->resolved = true;
    pending->answer = std::move(parsed);
  }
  pending_state.cv.notify_all();
  return {};
}

}  // namespace ava::app::rpc
