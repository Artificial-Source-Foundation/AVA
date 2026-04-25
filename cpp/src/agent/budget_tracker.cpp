#include "ava/agent/budget_tracker.hpp"

#include <algorithm>

namespace ava::agent {

BudgetTracker::BudgetTracker(double max_budget_usd) : max_budget_usd_(std::max(0.0, max_budget_usd)) {}

double BudgetTracker::max_budget_usd() const { return max_budget_usd_; }

double BudgetTracker::spent_usd() const { return spent_usd_; }

ava::types::TokenUsage BudgetTracker::usage() const { return usage_; }

bool BudgetTracker::budget_limit_configured() const { return max_budget_usd_ > 0.0; }

bool BudgetTracker::exhausted() const { return budget_limit_configured() && spent_usd_ >= max_budget_usd_; }

std::optional<double> BudgetTracker::remaining_usd() const {
  if(!budget_limit_configured()) {
    return std::nullopt;
  }
  return std::max(0.0, max_budget_usd_ - spent_usd_);
}

std::vector<BudgetWarning> BudgetTracker::observe(ava::types::TokenUsage usage, double cost_usd) {
  usage_.input_tokens += usage.input_tokens;
  usage_.output_tokens += usage.output_tokens;
  usage_.cache_read_tokens += usage.cache_read_tokens;
  usage_.cache_creation_tokens += usage.cache_creation_tokens;
  spent_usd_ += std::max(0.0, cost_usd);

  std::vector<BudgetWarning> warnings;
  if(!budget_limit_configured()) {
    return warnings;
  }

  const auto percent = static_cast<int>((spent_usd_ / max_budget_usd_) * 100.0);
  for(const auto threshold : {50, 75, 90, 100}) {
    if(percent >= threshold && highest_warning_percent_ < threshold) {
      highest_warning_percent_ = threshold;
      warnings.push_back(BudgetWarning{threshold, spent_usd_, max_budget_usd_});
    }
  }
  return warnings;
}

std::vector<BudgetWarning> BudgetTracker::exhaustion_warning() {
  if(!exhausted() || highest_warning_percent_ >= 100) {
    return {};
  }
  highest_warning_percent_ = 100;
  return {BudgetWarning{100, spent_usd_, max_budget_usd_}};
}

void BudgetTracker::record_skipped_steering(std::size_t count) { skipped_steering_ += count; }

void BudgetTracker::record_skipped_follow_ups(std::size_t count) { skipped_follow_ups_ += count; }

void BudgetTracker::record_skipped_post_complete(std::size_t count) { skipped_post_complete_ += count; }

nlohmann::json BudgetTracker::to_session_metadata() const {
  nlohmann::json metadata = {
      {"input_tokens", usage_.input_tokens},
      {"output_tokens", usage_.output_tokens},
      {"cache_read_tokens", usage_.cache_read_tokens},
      {"cache_creation_tokens", usage_.cache_creation_tokens},
      {"total_usd", spent_usd_},
      {"budget_usd", budget_limit_configured() ? nlohmann::json(max_budget_usd_) : nlohmann::json(nullptr)},
      {"last_alert_threshold_percent", highest_warning_percent_},
      {"skipped_steering", skipped_steering_},
      {"skipped_follow_ups", skipped_follow_ups_},
      {"skipped_post_complete", skipped_post_complete_},
      {"exhausted", exhausted()},
  };
  if(const auto remaining = remaining_usd(); remaining.has_value()) {
    metadata["remaining_usd"] = *remaining;
  }
  return metadata;
}

}  // namespace ava::agent
