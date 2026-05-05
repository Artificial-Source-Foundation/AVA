#include "ava/app/rpc/resolvers.h"

#include <utility>

#include "ava/app/rpc/protocol.h"
#include "ava/app/rpc/resolver_support.h"
#include "ava/app/rpc/serialization.h"

namespace ava::app::rpc {

bool cancel_pending_resolvers(PendingResolverState& pending_state)
{
  std::lock_guard lock(pending_state.mutex);
  bool const had_pending = !pending_state.permission_requests.empty() || !pending_state.question_requests.empty();
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

std::string permission_session_grants_result_json(PendingResolverState& pending_state)
{
  std::vector<PermissionSessionGrant> grants;
  {
    std::lock_guard lock(pending_state.mutex);
    grants = pending_state.permission_session_grants;
  }

  std::string json = "{\"grants\":[";
  for (std::size_t index = 0; index < grants.size(); ++index) {
    if (index > 0) json += ',';
    json += detail::permission_session_grant_json(grants[index]);
  }
  json += "]}";
  return json;
}

ava::core::Result<std::string> permission_session_grant_revoke_result_json(PendingResolverState& pending_state,
                                                                           std::string_view grant_id)
{
  PermissionSessionGrant grant;
  {
    std::lock_guard lock(pending_state.mutex);
    auto found = pending_state.permission_session_grants.end();
    for (auto it = pending_state.permission_session_grants.begin(); it != pending_state.permission_session_grants.end();
         ++it) {
      if (it->grant_id == grant_id) {
        found = it;
        break;
      }
    }
    if (found == pending_state.permission_session_grants.end()) {
      auto error = invalid_rpc("permission_grant_revoke has no matching grant_id");
      error.with_context("grant_id", std::string(grant_id));
      return std::unexpected(std::move(error));
    }
    grant = *found;
    pending_state.permission_session_grants.erase(found);
  }

  std::string json = "{";
  json += bool_field_json("revoked", true);
  json += ",\"grant\":";
  json += detail::permission_session_grant_json(grant);
  json += '}';
  return json;
}

std::string permission_session_grants_clear_result_json(PendingResolverState& pending_state)
{
  std::size_t cleared = 0;
  {
    std::lock_guard lock(pending_state.mutex);
    cleared = pending_state.permission_session_grants.size();
    pending_state.permission_session_grants.clear();
  }

  std::string json = "{";
  json += number_field_json("cleared", cleared);
  json += '}';
  return json;
}

ava::permissions::PermissionResolver make_rpc_permission_resolver(
    PendingResolverState& pending_state, RpcOutput& output, RpcRunState& run_state, RuntimeSession const& session,
    std::mutex& session_mutex, ava::permissions::PermissionResolver policy_resolver, std::string prompt_request_id)
{
  return [&pending_state, &output, &run_state, &session, &session_mutex, policy_resolver = std::move(policy_resolver),
          prompt_request_id = std::move(prompt_request_id)](ava::permissions::PermissionPrompt const& prompt)
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
    {
      std::lock_guard lock(pending_state.mutex);
      for (auto const& grant : pending_state.permission_session_grants) {
        if (detail::grant_matches(grant, prompt)) return ava::permissions::PermissionResolution::AllowSessionGrant;
      }
    }

    auto pending = std::make_shared<PendingPermissionRequest>();
    pending->correlation_id = prompt_request_id;
    pending->permission_request_id = prompt.permission_request_id;
    pending->operation = prompt.operation;
    pending->mode = prompt.mode;
    pending->tool_name = prompt.tool_name;
    pending->target_path = prompt.target_path;
    pending->command = prompt.command;
    pending->reason = prompt.reason;
    pending->risk = prompt.risk;
    std::string request_id;
    {
      std::lock_guard lock(pending_state.mutex);
      request_id = detail::next_resolver_request_id("permission");
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
                                                        RpcRunState& run_state, RuntimeSession const& session,
                                                        std::mutex& session_mutex, std::string prompt_request_id)
{
  return
      [&pending_state, &output, &run_state, &session, &session_mutex, prompt_request_id = std::move(prompt_request_id)](
          ava::agent::QuestionPrompt const& prompt) -> ava::core::Result<ava::agent::QuestionAnswer> {
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
          request_id = detail::next_resolver_request_id("question");
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
                                               std::string_view correlation_id, std::string_view decision)
{
  ava::permissions::PermissionResolution resolution = ava::permissions::PermissionResolution::Deny;
  auto parsed_decision = detail::parse_permission_reply_decision(decision);
  if (!parsed_decision) return std::unexpected(std::move(parsed_decision.error()));
  resolution = parsed_decision->resolution;

  std::shared_ptr<PendingPermissionRequest> pending;
  {
    std::lock_guard lock(pending_state.mutex);
    auto found = pending_state.permission_requests.find(std::string(request_id));
    if (found == pending_state.permission_requests.end()) {
      return std::unexpected(detail::no_pending_request_error(request_id));
    }
    pending = found->second;
    if (pending->correlation_id != correlation_id) {
      auto error = invalid_rpc("permission_reply correlation_id does not match pending request");
      error.with_context("request_id", std::string(request_id));
      return std::unexpected(std::move(error));
    }
    pending_state.permission_requests.erase(found);
    if (parsed_decision->create_session_grant) {
      auto grant = detail::grant_from_request(*pending);
      bool duplicate = false;
      for (auto const& existing : pending_state.permission_session_grants) {
        duplicate = duplicate || detail::grant_matches(existing, *pending);
      }
      if (!duplicate) pending_state.permission_session_grants.push_back(std::move(grant));
    }
    pending->resolved = true;
    pending->resolution = resolution;
  }
  pending_state.cv.notify_all();
  return {};
}

ava::core::VoidResult resolve_question_reply(PendingResolverState& pending_state, std::string_view request_id,
                                             std::string_view correlation_id, std::optional<std::string> const& answer,
                                             std::optional<std::string> const& selected)
{
  if (answer && selected) return std::unexpected(invalid_rpc("question_reply requires answer or selected, not both"));

  std::shared_ptr<PendingQuestionRequest> pending;
  ava::core::Result<ava::agent::QuestionAnswer> parsed =
      std::unexpected(invalid_rpc("question_reply requires answer or selected"));
  {
    std::lock_guard lock(pending_state.mutex);
    auto found = pending_state.question_requests.find(std::string(request_id));
    if (found == pending_state.question_requests.end()) {
      return std::unexpected(detail::no_pending_request_error(request_id));
    }
    pending = found->second;
    if (pending->correlation_id != correlation_id) {
      auto error = invalid_rpc("question_reply correlation_id does not match pending request");
      error.with_context("request_id", std::string(request_id));
      return std::unexpected(std::move(error));
    }

    parsed = detail::parse_question_reply(*pending, answer, selected);
    if (!parsed) return std::unexpected(std::move(parsed.error()));

    pending_state.question_requests.erase(found);
    pending->resolved = true;
    pending->answer = std::move(*parsed);
  }
  pending_state.cv.notify_all();
  return {};
}

}  // namespace ava::app::rpc
