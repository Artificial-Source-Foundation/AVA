#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "ava/types/streaming.hpp"

namespace ava::agent {

struct BudgetWarning {
  int threshold_percent{0};
  double spent_usd{0.0};
  double budget_usd{0.0};
};

class BudgetTracker {
public:
  explicit BudgetTracker(double max_budget_usd = 0.0);

  [[nodiscard]] double max_budget_usd() const;
  [[nodiscard]] double spent_usd() const;
  [[nodiscard]] ava::types::TokenUsage usage() const;
  [[nodiscard]] bool budget_limit_configured() const;
  [[nodiscard]] bool exhausted() const;
  [[nodiscard]] std::optional<double> remaining_usd() const;

  [[nodiscard]] std::vector<BudgetWarning> observe(ava::types::TokenUsage usage, double cost_usd);
  [[nodiscard]] std::vector<BudgetWarning> exhaustion_warning();
  void record_skipped_steering(std::size_t count);
  void record_skipped_follow_ups(std::size_t count);
  void record_skipped_post_complete(std::size_t count);
  [[nodiscard]] nlohmann::json to_session_metadata() const;

private:
  double max_budget_usd_{0.0};
  double spent_usd_{0.0};
  ava::types::TokenUsage usage_{};
  int highest_warning_percent_{0};
  std::size_t skipped_steering_{0};
  std::size_t skipped_follow_ups_{0};
  std::size_t skipped_post_complete_{0};
};

}  // namespace ava::agent
