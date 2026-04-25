#pragma once

#include <memory>
#include <optional>
#include <string>

#include "ava/control_plane/interactive.hpp"
#include "ava/orchestration/interactive.hpp"

namespace ava::tui {

enum class InteractiveAdapterActionKind {
  Approve,
  Reject,
  Answer,
  AcceptPlan,
  RejectPlan,
  CancelQuestion,
};

struct InteractiveAdapterAction {
  InteractiveAdapterActionKind kind{InteractiveAdapterActionKind::Approve};
  std::string request_id;
  std::optional<std::string> value;
};

struct InteractiveAdapterActionResult {
  bool accepted{false};
  std::optional<ava::control_plane::InteractiveRequestHandle> terminal_request;
  std::string error;
};

class InteractiveActionAdapter final {
 public:
  explicit InteractiveActionAdapter(std::shared_ptr<ava::orchestration::InteractiveBridge> bridge);

  [[nodiscard]] InteractiveAdapterActionResult apply(const InteractiveAdapterAction& action) const;

 private:
  [[nodiscard]] InteractiveAdapterActionResult success(ava::control_plane::InteractiveRequestHandle request) const;
  [[nodiscard]] InteractiveAdapterActionResult failure(std::string message) const;

  std::shared_ptr<ava::orchestration::InteractiveBridge> bridge_;
};

}  // namespace ava::tui
