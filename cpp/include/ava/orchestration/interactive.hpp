#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "ava/control_plane/interactive.hpp"
#include "ava/tools/permission_middleware.hpp"

namespace ava::orchestration {

struct ApprovalRequestPayload {
  ava::types::ToolCall call;
  ava::tools::PermissionInspection inspection;
};

struct QuestionRequestPayload {
  std::string question;
  std::vector<std::string> options;
};

struct PlanRequestPayload {
  nlohmann::json plan;
};

struct ApprovalResolution {
  ava::tools::ToolApproval approval;
  ava::control_plane::InteractiveRequestState state{ava::control_plane::InteractiveRequestState::Resolved};
};

struct QuestionResolution {
  std::optional<std::string> answer;
  ava::control_plane::InteractiveRequestState state{ava::control_plane::InteractiveRequestState::Resolved};
};

struct PlanResolution {
  bool accepted{false};
  ava::control_plane::InteractiveRequestState state{ava::control_plane::InteractiveRequestState::Resolved};
};

struct AdapterResolutionRecord {
  ava::control_plane::InteractiveRequestKind kind{ava::control_plane::InteractiveRequestKind::Approval};
  ava::control_plane::InteractiveRequestState state{ava::control_plane::InteractiveRequestState::Resolved};
  std::optional<ava::tools::ToolApproval> approval;
  std::optional<std::string> answer;
  std::optional<bool> plan_accepted;
};

using InteractiveApprovalResolver = std::function<ApprovalResolution(
    const ava::control_plane::InteractiveRequestHandle&,
    const ApprovalRequestPayload&
)>;
using InteractiveQuestionResolver = std::function<QuestionResolution(
    const ava::control_plane::InteractiveRequestHandle&,
    const QuestionRequestPayload&
)>;
using InteractivePlanResolver =
    std::function<PlanResolution(const ava::control_plane::InteractiveRequestHandle&, const PlanRequestPayload&)>;

class InteractiveBridge final : public ava::tools::ApprovalBridge {
 public:
  InteractiveBridge(
      std::optional<std::string> run_id,
      InteractiveApprovalResolver approval_resolver = nullptr,
      InteractiveQuestionResolver question_resolver = nullptr,
      InteractivePlanResolver plan_resolver = nullptr
  );

  [[nodiscard]] ava::tools::ToolApproval request_approval(
      const ava::types::ToolCall& call,
      const ava::tools::PermissionInspection& inspection
  ) const override;

  [[nodiscard]] std::optional<std::string> request_question(
      std::string question,
      std::vector<std::string> options = {}
  ) const;

  [[nodiscard]] bool request_plan(nlohmann::json plan) const;

  void set_run_id(std::optional<std::string> run_id);

  [[nodiscard]] ava::control_plane::InteractiveRequestHandle register_approval_for_adapter() const;
  [[nodiscard]] ava::control_plane::InteractiveRequestHandle register_question_for_adapter() const;
  [[nodiscard]] ava::control_plane::InteractiveRequestHandle register_plan_for_adapter() const;

  [[nodiscard]] std::optional<ava::control_plane::InteractiveRequestHandle> approve_from_adapter(
      const std::string& request_id,
      ava::tools::ToolApproval approval = ava::tools::ToolApproval::allowed()
  ) const;
  [[nodiscard]] std::optional<ava::control_plane::InteractiveRequestHandle> reject_from_adapter(
      const std::string& request_id,
      std::string reason
  ) const;
  [[nodiscard]] std::optional<ava::control_plane::InteractiveRequestHandle> answer_from_adapter(
      const std::string& request_id,
      std::string answer
  ) const;
  [[nodiscard]] std::optional<ava::control_plane::InteractiveRequestHandle> cancel_question_from_adapter(
      const std::string& request_id
  ) const;
  [[nodiscard]] std::optional<ava::control_plane::InteractiveRequestHandle> accept_plan_from_adapter(
      const std::string& request_id
  ) const;
  [[nodiscard]] std::optional<ava::control_plane::InteractiveRequestHandle> reject_plan_from_adapter(
      const std::string& request_id
  ) const;
  [[nodiscard]] std::optional<AdapterResolutionRecord> adapter_resolution_for(const std::string& request_id) const;

  // Test-only seam for validating settle idempotency behavior.
  void settle_request_for_testing(
      ava::control_plane::InteractiveRequestKind kind,
      const std::string& request_id,
      ava::control_plane::InteractiveRequestState state
  ) const;

  [[nodiscard]] const ava::control_plane::InteractiveRequestStore& approval_requests() const {
    return *approval_requests_;
  }
  [[nodiscard]] const ava::control_plane::InteractiveRequestStore& question_requests() const {
    return *question_requests_;
  }
  [[nodiscard]] const ava::control_plane::InteractiveRequestStore& plan_requests() const {
    return *plan_requests_;
  }

 private:
  void settle_request(
      ava::control_plane::InteractiveRequestStore& store,
      const ava::control_plane::InteractiveRequestHandle& handle,
      ava::control_plane::InteractiveRequestState state
  ) const;
  [[nodiscard]] std::optional<ava::control_plane::InteractiveRequestHandle> pending_request_for_adapter(
      const ava::control_plane::InteractiveRequestStore& store,
      const std::string& request_id
  ) const;
  [[nodiscard]] std::optional<std::string> current_run_id() const;

  std::optional<std::string> run_id_;
  mutable std::mutex run_id_mutex_;
  InteractiveApprovalResolver approval_resolver_;
  InteractiveQuestionResolver question_resolver_;
  InteractivePlanResolver plan_resolver_;

  std::shared_ptr<ava::control_plane::InteractiveRequestStore> approval_requests_;
  std::shared_ptr<ava::control_plane::InteractiveRequestStore> question_requests_;
  std::shared_ptr<ava::control_plane::InteractiveRequestStore> plan_requests_;
  mutable std::mutex adapter_resolutions_mutex_;
  mutable std::unordered_map<std::string, AdapterResolutionRecord> adapter_resolutions_;
};

}  // namespace ava::orchestration
