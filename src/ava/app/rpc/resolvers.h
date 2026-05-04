#pragma once

#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ava/agent/question.h"
#include "ava/app/rpc/output.h"
#include "ava/app/rpc/run_state.h"
#include "ava/app/runtime.h"
#include "ava/core/result.h"
#include "ava/permissions/permission.h"

namespace ava::app::rpc {

struct PendingPermissionRequest {
  bool resolved = false;
  std::string correlation_id;
  std::optional<ava::permissions::PermissionResolution> resolution;
  std::optional<ava::core::Error> error;
};

struct PendingQuestionRequest {
  bool resolved = false;
  std::string correlation_id;
  bool allow_custom = false;
  std::vector<ava::agent::QuestionOption> options;
  std::optional<ava::agent::QuestionAnswer> answer;
  std::optional<ava::core::Error> error;
};

struct PendingResolverState {
  std::mutex mutex;
  std::condition_variable cv;
  std::map<std::string, std::shared_ptr<PendingPermissionRequest>> permission_requests;
  std::map<std::string, std::shared_ptr<PendingQuestionRequest>> question_requests;
};

[[nodiscard]] bool cancel_pending_resolvers(PendingResolverState& pending_state);

[[nodiscard]] ava::permissions::PermissionResolver make_rpc_permission_resolver(
    PendingResolverState& pending_state, RpcOutput& output, RpcRunState& run_state, const RuntimeSession& session,
    std::mutex& session_mutex, ava::permissions::PermissionResolver policy_resolver, std::string prompt_request_id);
[[nodiscard]] ava::agent::QuestionResolver make_rpc_question_resolver(PendingResolverState& pending_state,
                                                                      RpcOutput& output, RpcRunState& run_state,
                                                                      const RuntimeSession& session,
                                                                      std::mutex& session_mutex,
                                                                      std::string prompt_request_id);

[[nodiscard]] ava::core::VoidResult resolve_permission_reply(PendingResolverState& pending_state,
                                                             std::string_view request_id,
                                                             std::string_view correlation_id,
                                                             std::string_view decision);
[[nodiscard]] ava::core::VoidResult resolve_question_reply(PendingResolverState& pending_state,
                                                           std::string_view request_id,
                                                           std::string_view correlation_id,
                                                           const std::optional<std::string>& answer,
                                                           const std::optional<std::string>& selected);

}  // namespace ava::app::rpc
