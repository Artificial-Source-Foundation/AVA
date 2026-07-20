#include "sys.h"
#include "ava/command/private_group.h"
#include "ava/agent/job_journal.h"
#include "ava/session/session_store.h"
#include "ava/core/json.h"
#include "ava/core/strict_json.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <utility>
#include <dirent.h>
#include <fcntl.h>
#include <nlohmann/json.hpp>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ava::agent {
namespace {

using Json = nlohmann::json;

class ScopedFd
{
 public:
  explicit ScopedFd(int fd = -1) noexcept : fd_(fd) { }
  ScopedFd(ScopedFd const&) = delete;
  ScopedFd& operator=(ScopedFd const&) = delete;
  ScopedFd(ScopedFd&& other) noexcept : fd_(std::exchange(other.fd_, -1)) { }
  ScopedFd& operator=(ScopedFd&& other) noexcept
  {
    if (this != &other)
    {
      reset();
      fd_ = std::exchange(other.fd_, -1);
    }
    return *this;
  }
  ~ScopedFd() { reset(); }
  [[nodiscard]] int get() const noexcept { return fd_; }
  [[nodiscard]] int release() noexcept { return std::exchange(fd_, -1); }
  void reset(int fd = -1) noexcept
  {
    if (fd_ >= 0)
      static_cast<void>(::close(fd_));
    fd_ = fd;
  }

 private:
  int fd_ = -1;
};

class FlockGuard
{
 public:
  explicit FlockGuard(int fd) noexcept : fd_(fd) { }
  FlockGuard(FlockGuard const&) = delete;
  FlockGuard& operator=(FlockGuard const&) = delete;
  ~FlockGuard()
  {
    if (fd_ >= 0)
      static_cast<void>(::flock(fd_, LOCK_UN));
  }

 private:
  int fd_;
};

ava::core::Error journal_error(ava::core::ErrorCategory category, std::string message, std::string_view parent_session_id, int error_number = 0)
{
  auto error = ava::core::Error(category, std::move(message));
  error.with_context("parent_session_id", std::string(parent_session_id));
  if (error_number != 0)
    error.with_context("cause", std::strerror(error_number));
  return error;
}

ava::core::Error transition_error(std::string message, JobJournalRecord const& record)
{
  auto error = journal_error(ava::core::ErrorCategory::Session, std::move(message), record.identity.parent_session_id);
  error.with_context("job_id", record.identity.job_id).with_context("transition", std::string(to_string(record.kind)));
  return error;
}

bool same_identity(struct stat const& left, struct stat const& right) noexcept
{
  return S_ISREG(left.st_mode) && S_ISREG(right.st_mode) && left.st_dev == right.st_dev && left.st_ino == right.st_ino;
}

ava::core::VoidResult validate_limits(JobJournalLimits const& limits)
{
  if (limits.max_file_bytes == 0 || limits.max_line_bytes == 0 || limits.max_records == 0 || limits.max_jobs == 0 || limits.max_id_bytes == 0 ||
      limits.max_timestamp_bytes == 0 || limits.max_summary_bytes == 0 || limits.max_error_bytes == 0 || limits.max_stop_reason_bytes == 0 ||
      limits.max_delivery_attempts_per_job == 0 || limits.max_retained_terminal_jobs == 0 || limits.max_line_bytes > limits.max_file_bytes ||
      limits.compact_after_file_bytes == 0 || limits.compact_after_file_bytes > limits.max_file_bytes || limits.compact_after_records == 0 ||
      limits.compact_after_records > limits.max_records || limits.max_retained_terminal_jobs > limits.max_jobs)
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "subagent job journal limits are invalid"));
  }
  return {};
}

bool has_forbidden_control(std::string_view value) noexcept
{
  return std::ranges::any_of(value, [](char ch) {
    auto const byte = static_cast<unsigned char>(ch);
    return byte < 0x20U || byte == 0x7FU;
  });
}

bool has_forbidden_text_control(std::string_view value) noexcept
{
  return std::ranges::any_of(value, [](char ch) {
    auto const byte = static_cast<unsigned char>(ch);
    return (byte < 0x20U && ch != '\n' && ch != '\r' && ch != '\t') || byte == 0x7FU;
  });
}

std::string utf8_prefix(std::string_view value, std::size_t max_bytes)
{
  if (value.size() <= max_bytes)
    return std::string(value);
  std::size_t end = max_bytes;
  while (end > 0 && (static_cast<unsigned char>(value[end]) & 0xC0U) == 0x80U) --end;
  return std::string(value.substr(0, end));
}

ava::core::VoidResult validate_id(std::string_view name, std::string_view value, JobJournalLimits const& limits, std::string_view parent_session_id)
{
  if (value.empty() || value.size() > limits.max_id_bytes)
  {
    auto error = journal_error(ava::core::ErrorCategory::InvalidArgument, "subagent job journal identifier is empty or exceeds its bound", parent_session_id);
    error.with_context("field", std::string(name)).with_context("max_bytes", std::to_string(limits.max_id_bytes));
    return std::unexpected(std::move(error));
  }
  bool const valid = std::ranges::all_of(value, [](char ch) {
    auto const byte = static_cast<unsigned char>(ch);
    return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') || ch == '_' || ch == '-' || ch == '.' || ch == ':';
  });
  if (!valid)
  {
    auto error = journal_error(ava::core::ErrorCategory::InvalidArgument, "subagent job journal identifier contains a forbidden character", parent_session_id);
    error.with_context("field", std::string(name));
    return std::unexpected(std::move(error));
  }
  return {};
}

bool has_terminal_text_fields(JobJournalRecord const& record) noexcept
{
  return record.summary || record.error_category || record.error || record.stop_reason || record.summary_truncated || record.error_truncated ||
         record.stop_reason_truncated;
}

bool has_delivery_identity_fields(JobJournalRecord const& record) noexcept
{
  return record.attempt_id || record.prompt_fingerprint || record.committed_turn_id;
}

bool has_accounting_fields(JobJournalRecord const& record) noexcept
{
  return record.provider_iterations || record.tool_calls || record.tool_iterations;
}

ava::core::VoidResult validate_text(std::string_view name, std::string_view value, bool allow_empty, JobJournalRecord const& record)
{
  if ((!allow_empty && value.empty()) || !ava::core::json::is_valid_utf8(value) || has_forbidden_text_control(value))
  {
    auto error = transition_error("subagent job journal terminal text is empty, invalid UTF-8, or contains a forbidden control", record);
    error.with_context("field", std::string(name));
    return std::unexpected(std::move(error));
  }
  return {};
}

ava::core::Result<JobJournalRecord> canonicalize_record(JobJournalRecord record, JobJournalLimits const& limits, bool allow_truncation)
{
  if (record.schema_version != kSubagentJobJournalSchemaVersion)
    return std::unexpected(transition_error("unsupported subagent job journal schema version", record));
  for (auto const& [name, value] : std::array<std::pair<std::string_view, std::string_view>, 5>{{{"job_id", record.identity.job_id},
                                                                                                 {"task_id", record.identity.task_id},
                                                                                                 {"parent_session_id", record.identity.parent_session_id},
                                                                                                 {"child_session_id", record.identity.child_session_id},
                                                                                                 {"delivery_id", record.identity.delivery_id}}})
  {
    if (auto valid = validate_id(name, value, limits, record.identity.parent_session_id); !valid)
      return std::unexpected(std::move(valid.error()));
  }
  if (record.at.empty() || record.at.size() > limits.max_timestamp_bytes || !ava::core::json::is_valid_utf8(record.at) || has_forbidden_control(record.at))
  {
    auto error = transition_error("subagent job journal timestamp is empty, invalid, or exceeds its bound", record);
    error.with_context("max_bytes", std::to_string(limits.max_timestamp_bytes));
    return std::unexpected(std::move(error));
  }

  auto bound_text = [&](std::string_view name, std::optional<std::string>& value, std::optional<bool>& truncated, std::size_t max_bytes,
                        bool allow_empty) -> ava::core::VoidResult {
    if (!value || !truncated)
      return std::unexpected(transition_error("terminal text requires its truncation metadata", record));
    if (auto valid = validate_text(name, *value, allow_empty, record); !valid)
      return std::unexpected(std::move(valid.error()));
    if (value->size() > max_bytes)
    {
      if (!allow_truncation)
      {
        auto error = transition_error("subagent job journal terminal text exceeds its persisted bound", record);
        error.with_context("field", std::string(name)).with_context("max_bytes", std::to_string(max_bytes));
        return std::unexpected(std::move(error));
      }
      *value = utf8_prefix(*value, max_bytes);
      *truncated = true;
      if (!allow_empty && value->empty())
      {
        auto error = transition_error("subagent job journal text bound cannot retain one complete UTF-8 scalar", record);
        error.with_context("field", std::string(name)).with_context("max_bytes", std::to_string(max_bytes));
        return std::unexpected(std::move(error));
      }
    }
    return {};
  };

  switch (record.kind)
  {
    case JobJournalTransitionKind::Started:
      if (!record.mode || record.terminal_state || has_terminal_text_fields(record) || has_delivery_identity_fields(record) || has_accounting_fields(record))
        return std::unexpected(transition_error("started transition requires only mode", record));
      break;
    case JobJournalTransitionKind::Terminal:
      if (record.mode || !record.terminal_state || has_delivery_identity_fields(record))
        return std::unexpected(transition_error("terminal transition carries invalid fields", record));
      if (has_accounting_fields(record) && (!record.provider_iterations || !record.tool_calls || !record.tool_iterations))
        return std::unexpected(transition_error("terminal accounting fields must be present together", record));
      if (record.provider_iterations && (*record.provider_iterations > limits.max_accounting_value || *record.tool_calls > limits.max_accounting_value ||
                                         *record.tool_iterations > limits.max_accounting_value))
        return std::unexpected(transition_error("terminal accounting field exceeds its bounded limit", record));
      if (*record.terminal_state == SubagentTerminalState::Completed)
      {
        if (record.error_category || record.error || record.error_truncated || !record.summary || !record.summary_truncated ||
            (record.stop_reason.has_value() != record.stop_reason_truncated.has_value()))
          return std::unexpected(transition_error("completed transition requires summary and optional stop_reason metadata", record));
        if (auto valid = bound_text("summary", record.summary, record.summary_truncated, limits.max_summary_bytes, true); !valid)
          return std::unexpected(std::move(valid.error()));
        if (record.stop_reason)
          if (auto valid = bound_text("stop_reason", record.stop_reason, record.stop_reason_truncated, limits.max_stop_reason_bytes, false); !valid)
            return std::unexpected(std::move(valid.error()));
      }
      else if (*record.terminal_state == SubagentTerminalState::Failed)
      {
        if (record.summary || record.stop_reason || record.summary_truncated || record.stop_reason_truncated || !record.error || !record.error_truncated)
          return std::unexpected(transition_error("failed transition requires only safe error category/message metadata", record));
        if (!record.error_category)
          record.error_category = "unknown";
        constexpr std::array<std::string_view, 8> safe_categories = {"invalid_argument", "io",      "not_found", "permission_denied",
                                                                     "provider",         "session", "tool",      "unknown"};
        if (std::ranges::find(safe_categories, *record.error_category) == safe_categories.end())
          return std::unexpected(transition_error("failed transition error category is not stable", record));
        if (auto valid = bound_text("error", record.error, record.error_truncated, limits.max_error_bytes, false); !valid)
          return std::unexpected(std::move(valid.error()));
      }
      else
      {
        if (record.summary || record.error_category || record.error || record.summary_truncated || record.error_truncated || !record.stop_reason ||
            !record.stop_reason_truncated)
          return std::unexpected(transition_error("canceled transition requires only stop_reason and stop_reason_truncated", record));
        if (auto valid = bound_text("stop_reason", record.stop_reason, record.stop_reason_truncated, limits.max_stop_reason_bytes, false); !valid)
          return std::unexpected(std::move(valid.error()));
      }
      break;
    case JobJournalTransitionKind::Interrupted:
      if (record.mode || record.terminal_state || record.summary || record.error_category || record.error || record.summary_truncated ||
          record.error_truncated || !record.stop_reason || !record.stop_reason_truncated || has_delivery_identity_fields(record) ||
          has_accounting_fields(record))
        return std::unexpected(transition_error("interrupted transition requires only stop_reason and stop_reason_truncated", record));
      if (auto valid = bound_text("stop_reason", record.stop_reason, record.stop_reason_truncated, limits.max_stop_reason_bytes, false); !valid)
        return std::unexpected(std::move(valid.error()));
      break;
    case JobJournalTransitionKind::DeliveryAttempt:
      if (record.mode || record.terminal_state || has_terminal_text_fields(record) || has_accounting_fields(record) || !record.attempt_id ||
          !record.prompt_fingerprint || record.committed_turn_id)
        return std::unexpected(transition_error("delivery_attempt requires only attempt_id and prompt_fingerprint", record));
      if (auto valid = validate_id("attempt_id", *record.attempt_id, limits, record.identity.parent_session_id); !valid)
        return std::unexpected(std::move(valid.error()));
      if (auto valid = validate_id("prompt_fingerprint", *record.prompt_fingerprint, limits, record.identity.parent_session_id); !valid)
        return std::unexpected(std::move(valid.error()));
      break;
    case JobJournalTransitionKind::DeliveryAck:
      if (record.mode || record.terminal_state || has_terminal_text_fields(record) || has_accounting_fields(record) || !record.attempt_id ||
          record.prompt_fingerprint || !record.committed_turn_id)
        return std::unexpected(transition_error("delivery_ack requires only attempt_id and committed_turn_id", record));
      if (auto valid = validate_id("attempt_id", *record.attempt_id, limits, record.identity.parent_session_id); !valid)
        return std::unexpected(std::move(valid.error()));
      if (auto valid = validate_id("committed_turn_id", *record.committed_turn_id, limits, record.identity.parent_session_id); !valid)
        return std::unexpected(std::move(valid.error()));
      break;
    default:
      if (record.mode || record.terminal_state || has_terminal_text_fields(record) || has_delivery_identity_fields(record) || has_accounting_fields(record))
        return std::unexpected(transition_error("transition carries fields that are not valid for its kind", record));
      break;
  }
  return record;
}

bool identities_equal(SubagentJobIdentity const& left, SubagentJobIdentity const& right) noexcept
{
  return left.job_id == right.job_id && left.task_id == right.task_id && left.parent_session_id == right.parent_session_id &&
         left.child_session_id == right.child_session_id && left.delivery_id == right.delivery_id;
}

bool terminal(SubagentExecutionState state) noexcept
{
  return state == SubagentExecutionState::Completed || state == SubagentExecutionState::Failed || state == SubagentExecutionState::Canceled ||
         state == SubagentExecutionState::Interrupted;
}

ava::core::VoidResult apply_record(SubagentJobProjection& projection, JobJournalRecord const& record, JobJournalLimits const& limits)
{
  if (!projection.jobs.empty() && projection.jobs.front().identity.parent_session_id != record.identity.parent_session_id)
    return std::unexpected(transition_error("journal contains more than one parent session identity", record));

  auto match = std::ranges::find_if(projection.jobs, [&](SubagentJobSnapshot const& job) { return job.identity.job_id == record.identity.job_id; });
  if (record.kind == JobJournalTransitionKind::Started)
  {
    if (match != projection.jobs.end())
      return std::unexpected(transition_error("job has a duplicate started transition", record));
    auto const duplicate_delivery =
        std::ranges::find_if(projection.jobs, [&](SubagentJobSnapshot const& job) { return job.identity.delivery_id == record.identity.delivery_id; });
    if (duplicate_delivery != projection.jobs.end())
      return std::unexpected(transition_error("job reuses a delivery identity", record));
    auto const related_run = std::ranges::find_if(projection.jobs, [&](SubagentJobSnapshot const& job) {
      return job.identity.task_id == record.identity.task_id || job.identity.child_session_id == record.identity.child_session_id;
    });
    if (related_run != projection.jobs.end())
    {
      if (related_run->identity.task_id != record.identity.task_id || related_run->identity.child_session_id != record.identity.child_session_id)
        return std::unexpected(transition_error("job inconsistently reuses a task or child-session identity", record));
      bool const concurrent = std::ranges::any_of(projection.jobs, [&](SubagentJobSnapshot const& job) {
        return (job.identity.task_id == record.identity.task_id || job.identity.child_session_id == record.identity.child_session_id) &&
               !terminal(job.execution);
      });
      if (concurrent)
        return std::unexpected(transition_error("job concurrently reuses a task and child-session identity", record));
    }
    if (projection.jobs.size() >= limits.max_jobs)
      return std::unexpected(transition_error("job count exceeds bounded journal limit", record));
    projection.jobs.push_back(SubagentJobSnapshot{.identity = record.identity,
                                                  .mode = *record.mode,
                                                  .execution = SubagentExecutionState::Running,
                                                  .delivery = SubagentDeliveryState::Direct,
                                                  .started_at = record.at,
                                                  .updated_at = record.at});
    return {};
  }

  if (match == projection.jobs.end())
    return std::unexpected(transition_error("job transition appears before started", record));
  if (!identities_equal(match->identity, record.identity))
    return std::unexpected(transition_error("job transition identity does not match started", record));

  auto& job = *match;
  if (record.at < job.updated_at)
    return std::unexpected(transition_error("job transition timestamp decreases", record));
  switch (record.kind)
  {
    case JobJournalTransitionKind::Started:
      break;
    case JobJournalTransitionKind::Promoted:
      if (job.execution != SubagentExecutionState::Running || job.mode != SubagentJobMode::Foreground || job.was_promoted || job.cancel_requested)
        return std::unexpected(transition_error("promoted transition requires one running foreground job", record));
      job.mode = SubagentJobMode::Background;
      job.was_promoted = true;
      job.promoted_at = record.at;
      break;
    case JobJournalTransitionKind::CancelRequested:
      if (job.execution != SubagentExecutionState::Running || job.cancel_requested)
        return std::unexpected(transition_error("cancel_requested transition requires one running uncanceled job", record));
      job.cancel_requested = true;
      job.cancel_requested_at = record.at;
      break;
    case JobJournalTransitionKind::Terminal:
      if (job.execution != SubagentExecutionState::Running)
        return std::unexpected(transition_error("terminal transition requires a running job", record));
      switch (*record.terminal_state)
      {
        case SubagentTerminalState::Completed:
          job.execution = SubagentExecutionState::Completed;
          job.summary = record.summary;
          job.summary_truncated = *record.summary_truncated;
          break;
        case SubagentTerminalState::Failed:
          job.execution = SubagentExecutionState::Failed;
          job.error_category = record.error_category;
          job.error = record.error;
          job.error_truncated = *record.error_truncated;
          break;
        case SubagentTerminalState::Canceled:
          job.execution = SubagentExecutionState::Canceled;
          job.stop_reason = record.stop_reason;
          job.stop_reason_truncated = *record.stop_reason_truncated;
          break;
      }
      job.terminal_at = record.at;
      if (record.stop_reason)
      {
        job.stop_reason = record.stop_reason;
        job.stop_reason_truncated = *record.stop_reason_truncated;
      }
      job.provider_iterations = record.provider_iterations.value_or(0);
      job.tool_calls = record.tool_calls.value_or(0);
      job.tool_iterations = record.tool_iterations.value_or(0);
      break;
    case JobJournalTransitionKind::Interrupted:
      if (job.execution != SubagentExecutionState::Running)
        return std::unexpected(transition_error("interrupted transition requires a running job", record));
      job.execution = SubagentExecutionState::Interrupted;
      job.terminal_at = record.at;
      job.stop_reason = record.stop_reason;
      job.stop_reason_truncated = *record.stop_reason_truncated;
      break;
    case JobJournalTransitionKind::DeliveryPending:
      if (!terminal(job.execution) || job.mode != SubagentJobMode::Background || job.delivery != SubagentDeliveryState::Direct)
        return std::unexpected(transition_error("delivery_pending requires a terminal background job with direct delivery state", record));
      job.delivery = SubagentDeliveryState::Pending;
      job.delivery_pending_at = record.at;
      break;
    case JobJournalTransitionKind::DeliveryAttempt: {
      if (!terminal(job.execution) || job.mode != SubagentJobMode::Background ||
          (job.delivery != SubagentDeliveryState::Pending && job.delivery != SubagentDeliveryState::Attempting))
        return std::unexpected(transition_error("delivery_attempt requires pending terminal background delivery", record));
      if (job.delivery_attempt_history.size() >= limits.max_delivery_attempts_per_job)
        return std::unexpected(transition_error("delivery attempt count exceeds bounded journal limit", record));
      auto const duplicate_attempt = std::ranges::any_of(projection.jobs, [&](SubagentJobSnapshot const& projected_job) {
        return std::ranges::any_of(projected_job.delivery_attempt_history,
                                   [&](SubagentDeliveryAttemptSnapshot const& attempt) { return attempt.attempt_id == *record.attempt_id; });
      });
      if (duplicate_attempt)
        return std::unexpected(transition_error("delivery attempt_id is duplicated", record));
      if (!job.delivery_attempt_history.empty() && job.delivery_attempt_history.front().prompt_fingerprint != *record.prompt_fingerprint)
        return std::unexpected(transition_error("delivery attempt prompt_fingerprint does not match prior attempts", record));
      job.delivery = SubagentDeliveryState::Attempting;
      job.delivery_attempt_history.push_back(
          SubagentDeliveryAttemptSnapshot{.attempt_id = *record.attempt_id, .prompt_fingerprint = *record.prompt_fingerprint, .attempted_at = record.at});
      job.delivery_attempts = job.delivery_attempt_history.size();
      job.last_delivery_attempt_at = record.at;
      break;
    }
    case JobJournalTransitionKind::DeliveryAck: {
      if (!terminal(job.execution) || job.mode != SubagentJobMode::Background || job.delivery != SubagentDeliveryState::Attempting ||
          job.delivery_attempt_history.empty())
        return std::unexpected(transition_error("delivery_ack requires an attempted terminal background delivery", record));
      if (job.delivery_attempt_history.back().attempt_id != *record.attempt_id)
        return std::unexpected(transition_error("delivery_ack attempt_id does not identify the latest attempt", record));
      auto const duplicate_turn = std::ranges::any_of(projection.jobs, [&](SubagentJobSnapshot const& projected_job) {
        return projected_job.committed_turn_id && *projected_job.committed_turn_id == *record.committed_turn_id;
      });
      if (duplicate_turn)
        return std::unexpected(transition_error("delivery_ack committed_turn_id is duplicated", record));
      job.delivery = SubagentDeliveryState::Acknowledged;
      job.delivery_acknowledged_at = record.at;
      job.acknowledged_attempt_id = record.attempt_id;
      job.committed_turn_id = record.committed_turn_id;
      break;
    }
  }
  job.updated_at = record.at;
  return {};
}

void finish_projection(SubagentJobProjection& projection)
{
  std::ranges::sort(projection.jobs, [](SubagentJobSnapshot const& left, SubagentJobSnapshot const& right) {
    if (left.started_at != right.started_at)
      return left.started_at < right.started_at;
    return left.identity.job_id < right.identity.job_id;
  });
  projection.pending_delivery_ids.clear();
  for (auto const& job : projection.jobs)
  {
    if (job.delivery == SubagentDeliveryState::Pending || job.delivery == SubagentDeliveryState::Attempting)
      projection.pending_delivery_ids.push_back(job.identity.delivery_id);
  }
  std::ranges::sort(projection.pending_delivery_ids);
}

std::string serialize_record(JobJournalRecord const& record)
{
  std::string line = "{\"schema\":" + std::to_string(record.schema_version) + ",\"kind\":\"" + std::string(to_string(record.kind)) + "\",\"job_id\":\"" +
                     ava::core::json::escape(record.identity.job_id) + "\",\"task_id\":\"" + ava::core::json::escape(record.identity.task_id) +
                     "\",\"parent_session_id\":\"" + ava::core::json::escape(record.identity.parent_session_id) + "\",\"child_session_id\":\"" +
                     ava::core::json::escape(record.identity.child_session_id) + "\",\"delivery_id\":\"" +
                     ava::core::json::escape(record.identity.delivery_id) + "\",\"at\":\"" + ava::core::json::escape(record.at) + "\"";
  if (record.mode)
    line += ",\"mode\":\"" + std::string(to_string(*record.mode)) + "\"";
  if (record.terminal_state)
    line += ",\"terminal_state\":\"" + std::string(to_string(*record.terminal_state)) + "\"";
  if (record.summary)
    line += ",\"summary\":\"" + ava::core::json::escape(*record.summary) + "\"";
  if (record.error_category)
    line += ",\"error_category\":\"" + ava::core::json::escape(*record.error_category) + "\"";
  if (record.error)
    line += ",\"error\":\"" + ava::core::json::escape(*record.error) + "\"";
  if (record.stop_reason)
    line += ",\"stop_reason\":\"" + ava::core::json::escape(*record.stop_reason) + "\"";
  if (record.summary_truncated)
    line += std::string(",\"summary_truncated\":") + (*record.summary_truncated ? "true" : "false");
  if (record.error_truncated)
    line += std::string(",\"error_truncated\":") + (*record.error_truncated ? "true" : "false");
  if (record.stop_reason_truncated)
    line += std::string(",\"stop_reason_truncated\":") + (*record.stop_reason_truncated ? "true" : "false");
  if (record.provider_iterations)
    line += ",\"provider_iterations\":" + std::to_string(*record.provider_iterations);
  if (record.tool_calls)
    line += ",\"tool_calls\":" + std::to_string(*record.tool_calls);
  if (record.tool_iterations)
    line += ",\"tool_iterations\":" + std::to_string(*record.tool_iterations);
  if (record.attempt_id)
    line += ",\"attempt_id\":\"" + ava::core::json::escape(*record.attempt_id) + "\"";
  if (record.prompt_fingerprint)
    line += ",\"prompt_fingerprint\":\"" + ava::core::json::escape(*record.prompt_fingerprint) + "\"";
  if (record.committed_turn_id)
    line += ",\"committed_turn_id\":\"" + ava::core::json::escape(*record.committed_turn_id) + "\"";
  line += '}';
  return line;
}

ava::core::Result<JobJournalRecord> parse_record(std::string_view line, JobJournalLimits const& limits, std::string_view expected_parent)
{
  auto const strict = ava::core::validate_strict_json(line, ava::core::json::kMaxNestingDepth);
  if (strict != ava::core::StrictJsonStatus::Valid)
  {
    std::string message = "subagent job journal record is invalid JSON";
    if (strict == ava::core::StrictJsonStatus::DuplicateObjectKey)
      message = "subagent job journal record contains duplicate object member names";
    else if (strict == ava::core::StrictJsonStatus::NestingTooDeep)
      message = "subagent job journal record exceeds JSON nesting limit";
    return std::unexpected(journal_error(ava::core::ErrorCategory::Session, std::move(message), expected_parent));
  }
  Json root = Json::parse(line.begin(), line.end(), nullptr, false, true);
  if (root.is_discarded() || !root.is_object())
    return std::unexpected(journal_error(ava::core::ErrorCategory::Session, "subagent job journal record must be a JSON object", expected_parent));

  static std::set<std::string> const base_keys{"schema", "kind", "job_id", "task_id", "parent_session_id", "child_session_id", "delivery_id", "at"};
  static std::array<std::string_view, 9> const optional_string_keys{
      "mode", "terminal_state", "summary", "error_category", "error", "stop_reason", "attempt_id", "prompt_fingerprint", "committed_turn_id"};
  static std::array<std::string_view, 3> const optional_bool_keys{"summary_truncated", "error_truncated", "stop_reason_truncated"};
  static std::array<std::string_view, 3> const optional_number_keys{"provider_iterations", "tool_calls", "tool_iterations"};
  std::set<std::string> keys;
  for (auto const& [key, _] : root.items()) keys.insert(key);
  auto allowed = base_keys;
  for (auto key : optional_string_keys)
  {
    if (root.contains(key))
      allowed.insert(std::string(key));
  }
  for (auto key : optional_bool_keys)
  {
    if (root.contains(key))
      allowed.insert(std::string(key));
  }
  for (auto key : optional_number_keys)
  {
    if (root.contains(key))
      allowed.insert(std::string(key));
  }
  if (keys != allowed)
    return std::unexpected(journal_error(ava::core::ErrorCategory::Session, "subagent job journal record has missing or unknown fields", expected_parent));
  bool optional_types_valid = true;
  for (auto key : optional_string_keys) optional_types_valid = optional_types_valid && (!root.contains(key) || root[key].is_string());
  for (auto key : optional_bool_keys) optional_types_valid = optional_types_valid && (!root.contains(key) || root[key].is_boolean());
  for (auto key : optional_number_keys) optional_types_valid = optional_types_valid && (!root.contains(key) || root[key].is_number_unsigned());
  if (!root["schema"].is_number_integer() || !root["kind"].is_string() || !root["job_id"].is_string() || !root["task_id"].is_string() ||
      !root["parent_session_id"].is_string() || !root["child_session_id"].is_string() || !root["delivery_id"].is_string() || !root["at"].is_string() ||
      !optional_types_valid)
  {
    return std::unexpected(journal_error(ava::core::ErrorCategory::Session, "subagent job journal record fields have invalid JSON types", expected_parent));
  }

  auto kind = parse_job_journal_transition_kind(root["kind"].get_ref<std::string const&>());
  if (!kind)
    return std::unexpected(std::move(kind.error()));
  JobJournalRecord record{.schema_version = root["schema"].get<long long>(),
                          .kind = *kind,
                          .identity = {.job_id = root["job_id"].get_ref<std::string const&>(),
                                       .task_id = root["task_id"].get_ref<std::string const&>(),
                                       .parent_session_id = root["parent_session_id"].get_ref<std::string const&>(),
                                       .child_session_id = root["child_session_id"].get_ref<std::string const&>(),
                                       .delivery_id = root["delivery_id"].get_ref<std::string const&>()},
                          .at = root["at"].get_ref<std::string const&>()};
  if (root.contains("mode"))
  {
    auto mode = parse_subagent_job_mode(root["mode"].get_ref<std::string const&>());
    if (!mode)
      return std::unexpected(std::move(mode.error()));
    record.mode = *mode;
  }
  if (root.contains("terminal_state"))
  {
    auto state = parse_subagent_terminal_state(root["terminal_state"].get_ref<std::string const&>());
    if (!state)
      return std::unexpected(std::move(state.error()));
    record.terminal_state = *state;
  }
  auto assign_string = [&](std::string_view key, std::optional<std::string>& destination) {
    if (root.contains(key))
      destination = root[key].get_ref<std::string const&>();
  };
  auto assign_bool = [&](std::string_view key, std::optional<bool>& destination) {
    if (root.contains(key))
      destination = root[key].get<bool>();
  };
  auto assign_size = [&](std::string_view key, std::optional<std::size_t>& destination) {
    if (root.contains(key))
      destination = root[key].get<std::size_t>();
  };
  assign_string("summary", record.summary);
  assign_string("error_category", record.error_category);
  assign_string("error", record.error);
  assign_string("stop_reason", record.stop_reason);
  assign_string("attempt_id", record.attempt_id);
  assign_string("prompt_fingerprint", record.prompt_fingerprint);
  assign_string("committed_turn_id", record.committed_turn_id);
  assign_bool("summary_truncated", record.summary_truncated);
  assign_bool("error_truncated", record.error_truncated);
  assign_bool("stop_reason_truncated", record.stop_reason_truncated);
  assign_size("provider_iterations", record.provider_iterations);
  assign_size("tool_calls", record.tool_calls);
  assign_size("tool_iterations", record.tool_iterations);
  if (record.identity.parent_session_id != expected_parent)
    return std::unexpected(transition_error("journal record parent identity does not match its journal", record));
  return canonicalize_record(std::move(record), limits, false);
}

ava::core::VoidResult validate_safe_ancestor(struct stat const& status, bool final_state, std::string_view parent_session_id)
{
  if (!S_ISDIR(status.st_mode))
    return std::unexpected(journal_error(ava::core::ErrorCategory::PermissionDenied, "subagent job journal ancestor is not a directory", parent_session_id));
  auto const uid = ::geteuid();
  if (status.st_uid != uid && status.st_uid != 0)
    return std::unexpected(
        journal_error(ava::core::ErrorCategory::PermissionDenied, "subagent job journal ancestor has an untrusted owner", parent_session_id));
  auto const writable = status.st_mode & (S_IWGRP | S_IWOTH);
  bool const root_sticky = status.st_uid == 0 && (status.st_mode & S_ISVTX) != 0;
  bool const private_primary_group = ava::command::detail::is_current_user_private_primary_group_directory(status);
  if (writable != 0 && !root_sticky && !private_primary_group)
    return std::unexpected(
        journal_error(ava::core::ErrorCategory::PermissionDenied, "subagent job journal ancestor is group/world writable", parent_session_id));
  if (final_state && status.st_uid != uid)
    return std::unexpected(
        journal_error(ava::core::ErrorCategory::PermissionDenied, "AVA state directory is not owned by the current user", parent_session_id));
  return {};
}

ava::core::Result<int> open_jobs_directory(std::filesystem::path const& state_dir, std::string_view parent_session_id)
{
  if (!state_dir.is_absolute() || state_dir.lexically_normal() != state_dir || state_dir == state_dir.root_path())
    return std::unexpected(
        journal_error(ava::core::ErrorCategory::InvalidArgument, "AVA state directory must be a normalized absolute non-root path", parent_session_id));

  ScopedFd current(::open("/", O_RDONLY | O_DIRECTORY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC));
  if (current.get() < 0)
    return std::unexpected(journal_error(ava::core::ErrorCategory::Io, "failed to open filesystem root for subagent job journal", parent_session_id, errno));

  std::vector<std::string> components;
  for (auto const& component : state_dir.relative_path())
  {
    auto name = component.string();
    if (!name.empty() && name != ".")
      components.push_back(std::move(name));
  }
  for (std::size_t index = 0; index < components.size(); ++index)
  {
    auto const& name = components[index];
    int next = ::openat(current.get(), name.c_str(), O_RDONLY | O_DIRECTORY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
    if (next < 0 && errno == ENOENT)
    {
      if (::mkdirat(current.get(), name.c_str(), S_IRWXU) != 0 && errno != EEXIST)
        return std::unexpected(journal_error(ava::core::ErrorCategory::Io, "failed to create AVA state directory component", parent_session_id, errno));
      next = ::openat(current.get(), name.c_str(), O_RDONLY | O_DIRECTORY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
    }
    if (next < 0)
      return std::unexpected(
          journal_error(ava::core::ErrorCategory::PermissionDenied, "failed to securely open AVA state directory component", parent_session_id, errno));
    ScopedFd opened(next);
    struct stat status{};
    if (::fstat(opened.get(), &status) != 0)
      return std::unexpected(journal_error(ava::core::ErrorCategory::Io, "failed to inspect AVA state directory component", parent_session_id, errno));
    if (auto valid = validate_safe_ancestor(status, index + 1 == components.size(), parent_session_id); !valid)
      return std::unexpected(std::move(valid.error()));
    current = std::move(opened);
  }

  std::string const jobs_name(kSubagentJobJournalDirectoryName);
  int jobs = ::openat(current.get(), jobs_name.c_str(), O_RDONLY | O_DIRECTORY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
  bool created_jobs_directory = false;
  if (jobs < 0 && errno == ENOENT)
  {
    if (::mkdirat(current.get(), jobs_name.c_str(), S_IRWXU) == 0)
      created_jobs_directory = true;
    else if (errno != EEXIST)
      return std::unexpected(journal_error(ava::core::ErrorCategory::Io, "failed to create private subagent job journal directory", parent_session_id, errno));
    jobs = ::openat(current.get(), jobs_name.c_str(), O_RDONLY | O_DIRECTORY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
  }
  if (jobs < 0)
    return std::unexpected(
        journal_error(ava::core::ErrorCategory::PermissionDenied, "failed to securely open private subagent job journal directory", parent_session_id, errno));
  ScopedFd jobs_fd(jobs);
  if (created_jobs_directory && ::fchmod(jobs_fd.get(), S_IRWXU) != 0)
    return std::unexpected(
        journal_error(ava::core::ErrorCategory::Io, "failed to set exact private subagent job journal directory mode", parent_session_id, errno));
  struct stat jobs_status{};
  if (::fstat(jobs_fd.get(), &jobs_status) != 0)
    return std::unexpected(journal_error(ava::core::ErrorCategory::Io, "failed to inspect private subagent job journal directory", parent_session_id, errno));
  if (!S_ISDIR(jobs_status.st_mode) || jobs_status.st_uid != ::geteuid() || (jobs_status.st_mode & 07777) != S_IRWXU)
    return std::unexpected(journal_error(ava::core::ErrorCategory::PermissionDenied, "subagent job journal directory must be owner-owned with exact mode 0700",
                                         parent_session_id));
  return jobs_fd.release();
}

ava::core::Result<int> open_private_regular(int directory_fd, std::string const& name, std::string_view parent_session_id)
{
  struct stat metadata{};
  if (::fstatat(directory_fd, name.c_str(), &metadata, AT_SYMLINK_NOFOLLOW) != 0)
  {
    if (errno != ENOENT)
      return std::unexpected(journal_error(ava::core::ErrorCategory::Io, "failed to inspect subagent job journal file", parent_session_id, errno));
    ScopedFd created(::openat(directory_fd, name.c_str(), O_RDWR | O_APPEND | O_CREAT | O_EXCL | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC, S_IRUSR | S_IWUSR));
    if (created.get() >= 0)
    {
      if (::fchmod(created.get(), S_IRUSR | S_IWUSR) != 0 || ::fstat(created.get(), &metadata) != 0)
        return std::unexpected(
            journal_error(ava::core::ErrorCategory::Io, "failed to set or inspect created subagent job journal file", parent_session_id, errno));
      if (!S_ISREG(metadata.st_mode) || metadata.st_uid != ::geteuid() || metadata.st_nlink != 1 || (metadata.st_mode & 07777) != (S_IRUSR | S_IWUSR))
        return std::unexpected(
            journal_error(ava::core::ErrorCategory::PermissionDenied, "created subagent job journal file identity or mode is invalid", parent_session_id));
      if (::fsync(created.get()) != 0 || ::fsync(directory_fd) != 0)
        return std::unexpected(journal_error(ava::core::ErrorCategory::Io, "failed to durably publish subagent job journal file", parent_session_id, errno));
      return created.release();
    }
    if (errno != EEXIST)
      return std::unexpected(journal_error(ava::core::ErrorCategory::Io, "failed to create subagent job journal file", parent_session_id, errno));
    if (::fstatat(directory_fd, name.c_str(), &metadata, AT_SYMLINK_NOFOLLOW) != 0)
      return std::unexpected(
          journal_error(ava::core::ErrorCategory::Io, "failed to inspect concurrently created subagent job journal file", parent_session_id, errno));
  }
  if (!S_ISREG(metadata.st_mode))
    return std::unexpected(journal_error(ava::core::ErrorCategory::PermissionDenied, "subagent job journal path is not a regular file", parent_session_id));

  ScopedFd opened(::openat(directory_fd, name.c_str(), O_RDWR | O_APPEND | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC));
  if (opened.get() < 0)
    return std::unexpected(journal_error(ava::core::ErrorCategory::Io, "failed to securely open subagent job journal file", parent_session_id, errno));
  struct stat status{};
  if (::fstat(opened.get(), &status) != 0)
    return std::unexpected(journal_error(ava::core::ErrorCategory::Io, "failed to inspect opened subagent job journal file", parent_session_id, errno));
  if (!same_identity(metadata, status))
    return std::unexpected(journal_error(ava::core::ErrorCategory::PermissionDenied, "subagent job journal file changed while opening", parent_session_id));
  if (status.st_uid != ::geteuid() || status.st_nlink != 1 || (status.st_mode & 07777) != (S_IRUSR | S_IWUSR))
    return std::unexpected(journal_error(ava::core::ErrorCategory::PermissionDenied,
                                         "subagent job journal files must be singly linked, owner-owned, and exact mode 0600", parent_session_id));
  return opened.release();
}

ava::core::VoidResult acquire_lock(int lock_fd, std::string_view parent_session_id)
{
  int result = 0;
  do
  {
    result = ::flock(lock_fd, LOCK_EX);
  } while (result != 0 && errno == EINTR);
  if (result != 0)
    return std::unexpected(journal_error(ava::core::ErrorCategory::Io, "failed to acquire subagent job journal lock", parent_session_id, errno));
  return {};
}

ava::core::Result<bool> try_acquire_owner_lock(int owner_fd, std::string_view parent_session_id)
{
  int result = 0;
  do
  {
    result = ::flock(owner_fd, LOCK_EX | LOCK_NB);
  } while (result != 0 && errno == EINTR);
  if (result == 0)
    return true;
  if (errno == EWOULDBLOCK || errno == EAGAIN)
    return false;
  return std::unexpected(journal_error(ava::core::ErrorCategory::Io, "failed to acquire subagent job journal owner lease", parent_session_id, errno));
}

ava::core::VoidResult validate_lock_identity(int directory_fd, int lock_fd, std::string const& lock_name, std::string_view parent_session_id)
{
  struct stat locked{};
  struct stat published{};
  if (::fstat(lock_fd, &locked) != 0 || ::fstatat(directory_fd, lock_name.c_str(), &published, AT_SYMLINK_NOFOLLOW) != 0 || !same_identity(locked, published) ||
      locked.st_uid != ::geteuid() || locked.st_nlink != 1 || (locked.st_mode & 07777) != (S_IRUSR | S_IWUSR))
  {
    return std::unexpected(
        journal_error(ava::core::ErrorCategory::PermissionDenied, "subagent job journal lock identity is invalid or was replaced", parent_session_id, errno));
  }
  return {};
}

struct LoadedJournal
{
  ScopedFd fd;
  off_t size = 0;
  std::vector<JobJournalRecord> records;
};

ava::core::Result<LoadedJournal> load_journal(int directory_fd, std::string const& journal_name, std::string_view parent_session_id,
                                              JobJournalLimits const& limits)
{
  auto opened = open_private_regular(directory_fd, journal_name, parent_session_id);
  if (!opened)
    return std::unexpected(std::move(opened.error()));
  LoadedJournal loaded{.fd = ScopedFd(*opened), .records = {}};
  struct stat status{};
  if (::fstat(loaded.fd.get(), &status) != 0)
    return std::unexpected(journal_error(ava::core::ErrorCategory::Io, "failed to inspect subagent job journal snapshot", parent_session_id, errno));
  if (status.st_size < 0 || static_cast<std::uintmax_t>(status.st_size) > limits.max_file_bytes)
    return std::unexpected(journal_error(ava::core::ErrorCategory::Session, "subagent job journal exceeds bounded file limit", parent_session_id));
  loaded.size = status.st_size;
  if (loaded.size == 0)
    return loaded;

  std::string bytes(static_cast<std::size_t>(loaded.size), '\0');
  off_t offset = 0;
  while (offset < loaded.size)
  {
    ssize_t count = ::pread(loaded.fd.get(), bytes.data() + offset, static_cast<std::size_t>(loaded.size - offset), offset);
    if (count < 0 && errno == EINTR)
      continue;
    if (count <= 0)
      return std::unexpected(
          journal_error(ava::core::ErrorCategory::Io, "failed to read bounded subagent job journal snapshot", parent_session_id, count < 0 ? errno : EIO));
    offset += count;
  }
  if (bytes.back() != '\n')
    return std::unexpected(journal_error(ava::core::ErrorCategory::Session, "subagent job journal has a truncated final record", parent_session_id));

  std::size_t start = 0;
  while (start < bytes.size())
  {
    auto const end = bytes.find('\n', start);
    auto const length = end - start;
    if (length == 0 || length > limits.max_line_bytes)
      return std::unexpected(
          journal_error(ava::core::ErrorCategory::Session, "subagent job journal line is empty or exceeds bounded line limit", parent_session_id));
    if (loaded.records.size() >= limits.max_records)
      return std::unexpected(journal_error(ava::core::ErrorCategory::Session, "subagent job journal exceeds bounded record limit", parent_session_id));
    auto record = parse_record(std::string_view(bytes).substr(start, length), limits, parent_session_id);
    if (!record)
      return std::unexpected(std::move(record.error()));
    loaded.records.push_back(std::move(*record));
    start = end + 1;
  }
  return loaded;
}

bool write_all(int fd, std::string_view bytes, int& error_number)
{
  std::size_t offset = 0;
  while (offset < bytes.size())
  {
    ssize_t count = ::write(fd, bytes.data() + offset, bytes.size() - offset);
    if (count < 0 && errno == EINTR)
      continue;
    if (count <= 0)
    {
      error_number = count < 0 ? errno : EIO;
      return false;
    }
    offset += static_cast<std::size_t>(count);
  }
  return true;
}

std::vector<JobJournalRecord> canonical_records(SubagentJobProjection const& projection)
{
  std::vector<JobJournalRecord> records;
  for (auto const& job : projection.jobs)
  {
    records.push_back(JobJournalRecord{.kind = JobJournalTransitionKind::Started,
                                       .identity = job.identity,
                                       .at = job.started_at,
                                       .mode = job.was_promoted ? SubagentJobMode::Foreground : job.mode});
    if (job.was_promoted)
      records.push_back(JobJournalRecord{.kind = JobJournalTransitionKind::Promoted, .identity = job.identity, .at = *job.promoted_at});
    if (job.cancel_requested)
      records.push_back(JobJournalRecord{.kind = JobJournalTransitionKind::CancelRequested, .identity = job.identity, .at = *job.cancel_requested_at});
    if (terminal(job.execution))
    {
      if (job.execution == SubagentExecutionState::Interrupted)
      {
        records.push_back(JobJournalRecord{.kind = JobJournalTransitionKind::Interrupted,
                                           .identity = job.identity,
                                           .at = *job.terminal_at,
                                           .stop_reason = job.stop_reason,
                                           .stop_reason_truncated = job.stop_reason_truncated});
      }
      else
      {
        SubagentTerminalState terminal_state = SubagentTerminalState::Completed;
        if (job.execution == SubagentExecutionState::Failed)
          terminal_state = SubagentTerminalState::Failed;
        else if (job.execution == SubagentExecutionState::Canceled)
          terminal_state = SubagentTerminalState::Canceled;
        records.push_back(JobJournalRecord{.kind = JobJournalTransitionKind::Terminal,
                                           .identity = job.identity,
                                           .at = *job.terminal_at,
                                           .terminal_state = terminal_state,
                                           .summary = job.summary,
                                           .error_category = job.error_category,
                                           .error = job.error,
                                           .stop_reason = job.stop_reason,
                                           .summary_truncated = job.summary ? std::optional<bool>(job.summary_truncated) : std::nullopt,
                                           .error_truncated = job.error ? std::optional<bool>(job.error_truncated) : std::nullopt,
                                           .stop_reason_truncated = job.stop_reason ? std::optional<bool>(job.stop_reason_truncated) : std::nullopt,
                                           .provider_iterations = job.provider_iterations,
                                           .tool_calls = job.tool_calls,
                                           .tool_iterations = job.tool_iterations});
      }
    }
    if (job.delivery != SubagentDeliveryState::Direct)
      records.push_back(JobJournalRecord{.kind = JobJournalTransitionKind::DeliveryPending, .identity = job.identity, .at = *job.delivery_pending_at});
    for (auto const& attempt : job.delivery_attempt_history)
    {
      records.push_back(JobJournalRecord{.kind = JobJournalTransitionKind::DeliveryAttempt,
                                         .identity = job.identity,
                                         .at = attempt.attempted_at,
                                         .attempt_id = attempt.attempt_id,
                                         .prompt_fingerprint = attempt.prompt_fingerprint});
    }
    if (job.delivery == SubagentDeliveryState::Acknowledged)
    {
      records.push_back(JobJournalRecord{.kind = JobJournalTransitionKind::DeliveryAck,
                                         .identity = job.identity,
                                         .at = *job.delivery_acknowledged_at,
                                         .attempt_id = job.acknowledged_attempt_id,
                                         .committed_turn_id = job.committed_turn_id});
    }
  }
  std::ranges::stable_sort(records, [](JobJournalRecord const& left, JobJournalRecord const& right) { return left.at < right.at; });
  return records;
}

bool prune_projection(SubagentJobProjection& projection, JobJournalLimits const& limits)
{
  std::vector<std::size_t> prunable;
  for (std::size_t index = 0; index < projection.jobs.size(); ++index)
  {
    auto const& job = projection.jobs[index];
    if (terminal(job.execution) && (job.delivery == SubagentDeliveryState::Direct || job.delivery == SubagentDeliveryState::Acknowledged))
      prunable.push_back(index);
  }
  if (prunable.size() <= limits.max_retained_terminal_jobs)
    return false;
  std::ranges::sort(prunable, [&](std::size_t left, std::size_t right) {
    auto const& a = projection.jobs[left];
    auto const& b = projection.jobs[right];
    if (a.updated_at != b.updated_at)
      return a.updated_at < b.updated_at;
    return a.identity.job_id < b.identity.job_id;
  });
  std::set<std::size_t> remove(prunable.begin(), prunable.begin() + static_cast<std::ptrdiff_t>(prunable.size() - limits.max_retained_terminal_jobs));
  std::vector<SubagentJobSnapshot> retained;
  retained.reserve(projection.jobs.size() - remove.size());
  for (std::size_t index = 0; index < projection.jobs.size(); ++index)
  {
    if (!remove.contains(index))
      retained.push_back(std::move(projection.jobs[index]));
  }
  projection.jobs = std::move(retained);
  finish_projection(projection);
  return true;
}

ava::core::VoidResult publish_compaction(int directory_fd, int current_fd, std::string const& journal_name, std::string_view parent_session_id,
                                         SubagentJobProjection const& projection, JobJournalLimits const& limits)
{
  auto records = canonical_records(projection);
  if (records.size() > limits.max_records)
    return std::unexpected(journal_error(ava::core::ErrorCategory::Session, "compacted subagent job journal exceeds bounded record limit", parent_session_id));
  std::string bytes;
  for (auto const& record : records)
  {
    auto line = serialize_record(record);
    if (line.size() > limits.max_line_bytes || line.size() >= limits.max_file_bytes || bytes.size() > limits.max_file_bytes - line.size() - 1)
      return std::unexpected(
          journal_error(ava::core::ErrorCategory::Session, "compacted subagent job journal exceeds bounded line or file limit", parent_session_id));
    bytes += line;
    bytes.push_back('\n');
  }

  std::string const temporary_name = "." + journal_name + ".compact";
  struct stat stale{};
  if (::fstatat(directory_fd, temporary_name.c_str(), &stale, AT_SYMLINK_NOFOLLOW) == 0)
  {
    if (!S_ISREG(stale.st_mode) || stale.st_uid != ::geteuid() || stale.st_nlink != 1 || (stale.st_mode & 07777) != (S_IRUSR | S_IWUSR))
      return std::unexpected(
          journal_error(ava::core::ErrorCategory::PermissionDenied, "stale subagent job journal compaction path has an unsafe identity", parent_session_id));
    if (::unlinkat(directory_fd, temporary_name.c_str(), 0) != 0)
      return std::unexpected(
          journal_error(ava::core::ErrorCategory::Io, "failed to remove stale subagent job journal compaction file", parent_session_id, errno));
  }
  else if (errno != ENOENT)
  {
    return std::unexpected(
        journal_error(ava::core::ErrorCategory::Io, "failed to inspect stale subagent job journal compaction path", parent_session_id, errno));
  }

  ScopedFd temporary(::openat(directory_fd, temporary_name.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC, S_IRUSR | S_IWUSR));
  if (temporary.get() < 0)
    return std::unexpected(journal_error(ava::core::ErrorCategory::Io, "failed to create subagent job journal compaction file", parent_session_id, errno));
  if (::fchmod(temporary.get(), S_IRUSR | S_IWUSR) != 0)
  {
    int const cause = errno;
    static_cast<void>(::unlinkat(directory_fd, temporary_name.c_str(), 0));
    return std::unexpected(journal_error(ava::core::ErrorCategory::Io, "failed to set exact subagent job journal compaction mode", parent_session_id, cause));
  }

  int write_error = 0;
  if (!write_all(temporary.get(), bytes, write_error) || ::fsync(temporary.get()) != 0)
  {
    int const cause = write_error != 0 ? write_error : errno;
    static_cast<void>(::unlinkat(directory_fd, temporary_name.c_str(), 0));
    return std::unexpected(journal_error(ava::core::ErrorCategory::Io, "failed to durably write subagent job journal compaction", parent_session_id, cause));
  }

  struct stat current{};
  struct stat published{};
  if (::fstat(current_fd, &current) != 0 || ::fstatat(directory_fd, journal_name.c_str(), &published, AT_SYMLINK_NOFOLLOW) != 0 ||
      !same_identity(current, published))
  {
    static_cast<void>(::unlinkat(directory_fd, temporary_name.c_str(), 0));
    return std::unexpected(
        journal_error(ava::core::ErrorCategory::PermissionDenied, "subagent job journal changed before compaction publication", parent_session_id));
  }
  if (::renameat(directory_fd, temporary_name.c_str(), directory_fd, journal_name.c_str()) != 0)
  {
    int const cause = errno;
    static_cast<void>(::unlinkat(directory_fd, temporary_name.c_str(), 0));
    return std::unexpected(
        journal_error(ava::core::ErrorCategory::Io, "failed to atomically publish subagent job journal compaction", parent_session_id, cause));
  }
  if (::fsync(directory_fd) != 0)
    return std::unexpected(journal_error(ava::core::ErrorCategory::Io, "failed to durably publish subagent job journal compaction", parent_session_id, errno));
  return {};
}

std::string journal_name(std::string_view parent_session_id)
{
  return std::string(parent_session_id) + ".jsonl";
}

std::string lock_name(std::string_view parent_session_id)
{
  return std::string(parent_session_id) + ".lock";
}

std::string owner_name(std::string_view parent_session_id)
{
  return std::string(parent_session_id) + ".owner";
}

}  // namespace

std::string_view to_string(JobJournalTransitionKind value) noexcept
{
  switch (value)
  {
    case JobJournalTransitionKind::Started:
      return "started";
    case JobJournalTransitionKind::Promoted:
      return "promoted";
    case JobJournalTransitionKind::CancelRequested:
      return "cancel_requested";
    case JobJournalTransitionKind::Terminal:
      return "terminal";
    case JobJournalTransitionKind::DeliveryPending:
      return "delivery_pending";
    case JobJournalTransitionKind::DeliveryAttempt:
      return "delivery_attempt";
    case JobJournalTransitionKind::DeliveryAck:
      return "delivery_ack";
    case JobJournalTransitionKind::Interrupted:
      return "interrupted";
  }
  return "unknown";
}

ava::core::Result<JobJournalTransitionKind> parse_job_journal_transition_kind(std::string_view value)
{
  if (value == "started")
    return JobJournalTransitionKind::Started;
  if (value == "promoted")
    return JobJournalTransitionKind::Promoted;
  if (value == "cancel_requested")
    return JobJournalTransitionKind::CancelRequested;
  if (value == "terminal")
    return JobJournalTransitionKind::Terminal;
  if (value == "delivery_pending")
    return JobJournalTransitionKind::DeliveryPending;
  if (value == "delivery_attempt")
    return JobJournalTransitionKind::DeliveryAttempt;
  if (value == "delivery_ack")
    return JobJournalTransitionKind::DeliveryAck;
  if (value == "interrupted")
    return JobJournalTransitionKind::Interrupted;
  auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "unknown subagent job journal transition kind");
  error.with_context("kind", std::string(value));
  return std::unexpected(std::move(error));
}

ava::core::Result<SubagentJobProjection> project_job_journal(std::vector<JobJournalRecord> const& records, JobJournalLimits const& limits)
{
  if (auto valid = validate_limits(limits); !valid)
    return std::unexpected(std::move(valid.error()));
  if (records.size() > limits.max_records)
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Session, "subagent job journal exceeds bounded record limit"));
  SubagentJobProjection projection;
  std::optional<std::string> previous_at;
  for (auto const& record : records)
  {
    auto canonical = canonicalize_record(record, limits, true);
    if (!canonical)
      return std::unexpected(std::move(canonical.error()));
    if (previous_at && canonical->at < *previous_at)
      return std::unexpected(transition_error("journal transition timestamp decreases", *canonical));
    previous_at = canonical->at;
    if (auto applied = apply_record(projection, *canonical, limits); !applied)
      return std::unexpected(std::move(applied.error()));
  }
  finish_projection(projection);
  return projection;
}

static ava::core::Result<std::vector<std::string>> discover_journal_parents(std::filesystem::path const& ava_state_dir, JobJournalLimits const& limits)
{
  if (auto valid = validate_limits(limits); !valid)
    return std::unexpected(std::move(valid.error()));
  auto directory = open_jobs_directory(ava_state_dir, "startup");
  if (!directory)
    return std::unexpected(std::move(directory.error()));
  ScopedFd directory_fd(*directory);
  int const iteration_fd = ::fcntl(directory_fd.get(), F_DUPFD_CLOEXEC, 0);
  if (iteration_fd < 0)
    return std::unexpected(journal_error(ava::core::ErrorCategory::Io, "failed to enumerate subagent job journals", "startup", errno));
  DIR* stream = ::fdopendir(iteration_fd);
  if (stream == nullptr)
  {
    int const cause = errno;
    static_cast<void>(::close(iteration_fd));
    return std::unexpected(journal_error(ava::core::ErrorCategory::Io, "failed to enumerate subagent job journals", "startup", cause));
  }
  std::vector<std::string> parents;
  errno = 0;
  while (auto* entry = ::readdir(stream))
  {
    std::string name(entry->d_name);
    constexpr std::string_view suffix = ".jsonl";
    if (name.size() <= suffix.size() || !name.ends_with(suffix))
      continue;
    auto parent = name.substr(0, name.size() - suffix.size());
    if (auto valid = validate_id("parent_session_id", parent, limits, parent); !valid)
    {
      static_cast<void>(::closedir(stream));
      return std::unexpected(std::move(valid.error()));
    }
    parents.push_back(std::move(parent));
  }
  int const read_error = errno;
  static_cast<void>(::closedir(stream));
  if (read_error != 0)
    return std::unexpected(journal_error(ava::core::ErrorCategory::Io, "failed while enumerating subagent job journals", "startup", read_error));
  std::ranges::sort(parents);
  parents.erase(std::unique(parents.begin(), parents.end()), parents.end());
  return parents;
}

ava::core::Result<std::vector<OwnedJobJournalRecovery>> recover_owned_interrupted_job_journals(std::filesystem::path const& ava_state_dir,
                                                                                               JobJournalLimits const& limits)
{
  auto parents = discover_journal_parents(ava_state_dir, limits);
  if (!parents)
    return std::unexpected(std::move(parents.error()));
  std::vector<OwnedJobJournalRecovery> recovered_journals;
  recovered_journals.reserve(parents->size());
  for (auto const& parent : *parents)
  {
    auto journal = JobJournal::try_open_owned(ava_state_dir, parent, limits);
    if (!journal)
      return std::unexpected(std::move(journal.error()));
    if (!*journal)
      continue;
    auto recovered = (*journal)->recover_interrupted_jobs();
    if (!recovered)
      return std::unexpected(std::move(recovered.error()));
    recovered_journals.push_back(OwnedJobJournalRecovery{.journal = std::move(**journal), .projection = std::move(*recovered)});
  }
  return recovered_journals;
}

ava::core::Result<std::vector<SubagentJobProjection>> recover_all_interrupted_job_journals(std::filesystem::path const& ava_state_dir,
                                                                                           JobJournalLimits const& limits)
{
  auto owned = recover_owned_interrupted_job_journals(ava_state_dir, limits);
  if (!owned)
    return std::unexpected(std::move(owned.error()));
  std::vector<SubagentJobProjection> projections;
  projections.reserve(owned->size());
  for (auto& recovery : *owned) projections.push_back(std::move(recovery.projection));
  return projections;
}

JobJournal::JobJournal(int directory_fd, int lock_fd, int owner_fd, std::string parent_session_id, JobJournalLimits limits)
    : directory_fd_(directory_fd), lock_fd_(lock_fd), owner_fd_(owner_fd), parent_session_id_(std::move(parent_session_id)), limits_(limits)
{
}

JobJournal::JobJournal(JobJournal&& other) noexcept
    : directory_fd_(std::exchange(other.directory_fd_, -1)),
      lock_fd_(std::exchange(other.lock_fd_, -1)),
      owner_fd_(std::exchange(other.owner_fd_, -1)),
      parent_session_id_(std::move(other.parent_session_id_)),
      limits_(other.limits_)
{
}

JobJournal& JobJournal::operator=(JobJournal&& other) noexcept
{
  if (this != &other)
  {
    if (directory_fd_ >= 0)
      static_cast<void>(::close(directory_fd_));
    if (lock_fd_ >= 0)
      static_cast<void>(::close(lock_fd_));
    if (owner_fd_ >= 0)
      static_cast<void>(::close(owner_fd_));
    directory_fd_ = std::exchange(other.directory_fd_, -1);
    lock_fd_ = std::exchange(other.lock_fd_, -1);
    owner_fd_ = std::exchange(other.owner_fd_, -1);
    parent_session_id_ = std::move(other.parent_session_id_);
    limits_ = other.limits_;
  }
  return *this;
}

JobJournal::~JobJournal()
{
  if (directory_fd_ >= 0)
    static_cast<void>(::close(directory_fd_));
  if (lock_fd_ >= 0)
    static_cast<void>(::close(lock_fd_));
  if (owner_fd_ >= 0)
    static_cast<void>(::close(owner_fd_));
}

ava::core::Result<JobJournal> JobJournal::open(std::filesystem::path const& ava_state_dir, std::string parent_session_id, JobJournalLimits limits)
{
  if (auto valid = validate_limits(limits); !valid)
    return std::unexpected(std::move(valid.error()));
  if (auto valid = validate_id("parent_session_id", parent_session_id, limits, parent_session_id); !valid)
    return std::unexpected(std::move(valid.error()));
  auto directory = open_jobs_directory(ava_state_dir, parent_session_id);
  if (!directory)
    return std::unexpected(std::move(directory.error()));
  ScopedFd directory_fd(*directory);
  auto lock = open_private_regular(directory_fd.get(), lock_name(parent_session_id), parent_session_id);
  if (!lock)
    return std::unexpected(std::move(lock.error()));
  JobJournal journal(directory_fd.release(), *lock, -1, std::move(parent_session_id), limits);
  if (auto validated = journal.projection(); !validated)
    return std::unexpected(std::move(validated.error()));
  return journal;
}

ava::core::Result<std::optional<JobJournal>> JobJournal::try_open_owned(std::filesystem::path const& ava_state_dir, std::string parent_session_id,
                                                                        JobJournalLimits limits)
{
  if (auto valid = validate_limits(limits); !valid)
    return std::unexpected(std::move(valid.error()));
  if (auto valid = validate_id("parent_session_id", parent_session_id, limits, parent_session_id); !valid)
    return std::unexpected(std::move(valid.error()));
  auto directory = open_jobs_directory(ava_state_dir, parent_session_id);
  if (!directory)
    return std::unexpected(std::move(directory.error()));
  ScopedFd directory_fd(*directory);
  auto owner = open_private_regular(directory_fd.get(), owner_name(parent_session_id), parent_session_id);
  if (!owner)
    return std::unexpected(std::move(owner.error()));
  ScopedFd owner_fd(*owner);
  auto acquired = try_acquire_owner_lock(owner_fd.get(), parent_session_id);
  if (!acquired)
    return std::unexpected(std::move(acquired.error()));
  if (!*acquired)
    return std::optional<JobJournal>{};
  if (auto valid = validate_lock_identity(directory_fd.get(), owner_fd.get(), owner_name(parent_session_id), parent_session_id); !valid)
    return std::unexpected(std::move(valid.error()));
  auto lock = open_private_regular(directory_fd.get(), lock_name(parent_session_id), parent_session_id);
  if (!lock)
    return std::unexpected(std::move(lock.error()));
  JobJournal journal(directory_fd.release(), *lock, owner_fd.release(), std::move(parent_session_id), limits);
  if (auto validated = journal.projection(); !validated)
    return std::unexpected(std::move(validated.error()));
  return std::optional<JobJournal>(std::move(journal));
}

ava::core::Result<SubagentJobProjection> JobJournal::append(JobJournalRecord const& record)
{
  if (record.identity.parent_session_id != parent_session_id_)
    return std::unexpected(transition_error("append parent identity does not match journal", record));
  if (auto locked = acquire_lock(lock_fd_, parent_session_id_); !locked)
    return std::unexpected(std::move(locked.error()));
  FlockGuard guard(lock_fd_);
  if (auto valid = validate_lock_identity(directory_fd_, lock_fd_, lock_name(parent_session_id_), parent_session_id_); !valid)
    return std::unexpected(std::move(valid.error()));
  auto loaded = load_journal(directory_fd_, journal_name(parent_session_id_), parent_session_id_, limits_);
  if (!loaded)
    return std::unexpected(std::move(loaded.error()));
  auto canonical = canonicalize_record(record, limits_, true);
  if (!canonical)
    return std::unexpected(std::move(canonical.error()));
  auto records = loaded->records;
  records.push_back(*canonical);
  auto proposed = project_job_journal(records, limits_);
  if (!proposed)
    return std::unexpected(std::move(proposed.error()));
  bool const pruned = prune_projection(*proposed, limits_);

  auto line = serialize_record(*canonical);
  if (line.size() > limits_.max_line_bytes)
    return std::unexpected(journal_error(ava::core::ErrorCategory::Session, "subagent job journal append exceeds bounded line limit", parent_session_id_));
  bool const compact_now = pruned || records.size() > limits_.compact_after_records ||
                           static_cast<std::uintmax_t>(loaded->size) + line.size() + 1 > limits_.compact_after_file_bytes;
  if (compact_now)
  {
    if (auto compacted = publish_compaction(directory_fd_, loaded->fd.get(), journal_name(parent_session_id_), parent_session_id_, *proposed, limits_);
        !compacted)
      return std::unexpected(std::move(compacted.error()));
  }
  else
  {
    if (static_cast<std::uintmax_t>(loaded->size) + line.size() + 1 > limits_.max_file_bytes || loaded->records.size() >= limits_.max_records)
      return std::unexpected(
          journal_error(ava::core::ErrorCategory::Session, "subagent job journal append exceeds bounded file or record limit", parent_session_id_));
    line.push_back('\n');
    int write_error = 0;
    if (!write_all(loaded->fd.get(), line, write_error) || ::fsync(loaded->fd.get()) != 0)
      return std::unexpected(journal_error(ava::core::ErrorCategory::Io, "failed to durably append subagent job journal transition", parent_session_id_,
                                           write_error != 0 ? write_error : errno));
  }
  return *proposed;
}

ava::core::Result<SubagentJobProjection> JobJournal::append_batch(std::vector<JobJournalRecord> const& records)
{
  if (records.size() != 2 || records[0].kind != JobJournalTransitionKind::Terminal || records[1].kind != JobJournalTransitionKind::DeliveryPending ||
      !identities_equal(records[0].identity, records[1].identity))
  {
    return std::unexpected(journal_error(ava::core::ErrorCategory::InvalidArgument,
                                         "subagent job journal batch must contain matching terminal and delivery_pending transitions", parent_session_id_));
  }
  if (records[0].identity.parent_session_id != parent_session_id_)
    return std::unexpected(transition_error("append batch parent identity does not match journal", records[0]));
  if (auto locked = acquire_lock(lock_fd_, parent_session_id_); !locked)
    return std::unexpected(std::move(locked.error()));
  FlockGuard guard(lock_fd_);
  if (auto valid = validate_lock_identity(directory_fd_, lock_fd_, lock_name(parent_session_id_), parent_session_id_); !valid)
    return std::unexpected(std::move(valid.error()));
  auto loaded = load_journal(directory_fd_, journal_name(parent_session_id_), parent_session_id_, limits_);
  if (!loaded)
    return std::unexpected(std::move(loaded.error()));

  auto proposed_records = loaded->records;
  for (auto const& record : records)
  {
    auto canonical = canonicalize_record(record, limits_, true);
    if (!canonical)
      return std::unexpected(std::move(canonical.error()));
    proposed_records.push_back(std::move(*canonical));
  }
  auto proposed = project_job_journal(proposed_records, limits_);
  if (!proposed)
    return std::unexpected(std::move(proposed.error()));
  auto const* projected = proposed->find(records[0].identity.job_id);
  if (projected == nullptr || !terminal(projected->execution) || projected->delivery != SubagentDeliveryState::Pending)
    return std::unexpected(
        journal_error(ava::core::ErrorCategory::Session, "subagent terminal batch did not project a pending terminal job", parent_session_id_));
  static_cast<void>(prune_projection(*proposed, limits_));
  if (auto published = publish_compaction(directory_fd_, loaded->fd.get(), journal_name(parent_session_id_), parent_session_id_, *proposed, limits_);
      !published)
    return std::unexpected(std::move(published.error()));
  return *proposed;
}

ava::core::Result<SubagentJobProjection> JobJournal::rollback_unpublished_started(SubagentJobIdentity const& identity)
{
  if (identity.parent_session_id != parent_session_id_)
  {
    JobJournalRecord record{.identity = identity};
    return std::unexpected(transition_error("unpublished-start rollback parent identity does not match journal", record));
  }
  if (auto locked = acquire_lock(lock_fd_, parent_session_id_); !locked)
    return std::unexpected(std::move(locked.error()));
  FlockGuard guard(lock_fd_);
  if (auto valid = validate_lock_identity(directory_fd_, lock_fd_, lock_name(parent_session_id_), parent_session_id_); !valid)
    return std::unexpected(std::move(valid.error()));
  auto loaded = load_journal(directory_fd_, journal_name(parent_session_id_), parent_session_id_, limits_);
  if (!loaded)
    return std::unexpected(std::move(loaded.error()));

  auto matching = std::ranges::find_if(loaded->records, [&](JobJournalRecord const& record) { return record.identity.job_id == identity.job_id; });
  if (matching == loaded->records.end() || matching->kind != JobJournalTransitionKind::Started || !identities_equal(matching->identity, identity) ||
      std::ranges::count_if(loaded->records, [&](JobJournalRecord const& record) { return record.identity.job_id == identity.job_id; }) != 1)
  {
    JobJournalRecord record{.identity = identity};
    return std::unexpected(transition_error("unpublished-start rollback requires exactly one matching started transition", record));
  }
  auto retained = loaded->records;
  retained.erase(retained.begin() + (matching - loaded->records.begin()));
  auto projection = project_job_journal(retained, limits_);
  if (!projection)
    return std::unexpected(std::move(projection.error()));
  if (auto published = publish_compaction(directory_fd_, loaded->fd.get(), journal_name(parent_session_id_), parent_session_id_, *projection, limits_);
      !published)
    return std::unexpected(std::move(published.error()));
  return *projection;
}

ava::core::Result<SubagentJobProjection> JobJournal::projection() const
{
  if (auto locked = acquire_lock(lock_fd_, parent_session_id_); !locked)
    return std::unexpected(std::move(locked.error()));
  FlockGuard guard(lock_fd_);
  if (auto valid = validate_lock_identity(directory_fd_, lock_fd_, lock_name(parent_session_id_), parent_session_id_); !valid)
    return std::unexpected(std::move(valid.error()));
  auto loaded = load_journal(directory_fd_, journal_name(parent_session_id_), parent_session_id_, limits_);
  if (!loaded)
    return std::unexpected(std::move(loaded.error()));
  return project_job_journal(loaded->records, limits_);
}

ava::core::Result<SubagentJobProjection> JobJournal::compact()
{
  if (auto locked = acquire_lock(lock_fd_, parent_session_id_); !locked)
    return std::unexpected(std::move(locked.error()));
  FlockGuard guard(lock_fd_);
  if (auto valid = validate_lock_identity(directory_fd_, lock_fd_, lock_name(parent_session_id_), parent_session_id_); !valid)
    return std::unexpected(std::move(valid.error()));
  auto loaded = load_journal(directory_fd_, journal_name(parent_session_id_), parent_session_id_, limits_);
  if (!loaded)
    return std::unexpected(std::move(loaded.error()));
  auto projection = project_job_journal(loaded->records, limits_);
  if (!projection)
    return std::unexpected(std::move(projection.error()));
  static_cast<void>(prune_projection(*projection, limits_));
  if (auto compacted = publish_compaction(directory_fd_, loaded->fd.get(), journal_name(parent_session_id_), parent_session_id_, *projection, limits_);
      !compacted)
    return std::unexpected(std::move(compacted.error()));
  return *projection;
}

ava::core::Result<SubagentJobProjection> JobJournal::recover_interrupted_jobs()
{
  if (owner_fd_ < 0)
    return std::unexpected(journal_error(ava::core::ErrorCategory::PermissionDenied, "subagent job recovery requires the process-lifetime parent owner lease",
                                         parent_session_id_));
  if (auto valid = validate_lock_identity(directory_fd_, owner_fd_, owner_name(parent_session_id_), parent_session_id_); !valid)
    return std::unexpected(std::move(valid.error()));
  if (auto locked = acquire_lock(lock_fd_, parent_session_id_); !locked)
    return std::unexpected(std::move(locked.error()));
  FlockGuard guard(lock_fd_);
  if (auto valid = validate_lock_identity(directory_fd_, lock_fd_, lock_name(parent_session_id_), parent_session_id_); !valid)
    return std::unexpected(std::move(valid.error()));
  auto loaded = load_journal(directory_fd_, journal_name(parent_session_id_), parent_session_id_, limits_);
  if (!loaded)
    return std::unexpected(std::move(loaded.error()));
  auto projection = project_job_journal(loaded->records, limits_);
  if (!projection)
    return std::unexpected(std::move(projection.error()));

  std::vector<JobJournalRecord> normalization;
  auto recovered_at = ava::session::now_timestamp();
  for (auto const& job : projection->jobs) recovered_at = std::max(recovered_at, job.updated_at);
  for (auto const& job : projection->jobs)
  {
    auto const& at = recovered_at;
    if (job.execution == SubagentExecutionState::Running)
    {
      normalization.push_back(JobJournalRecord{.kind = JobJournalTransitionKind::Interrupted,
                                               .identity = job.identity,
                                               .at = at,
                                               .stop_reason = "coordinator restarted",
                                               .stop_reason_truncated = false});
      if (job.mode == SubagentJobMode::Background)
        normalization.push_back(JobJournalRecord{.kind = JobJournalTransitionKind::DeliveryPending, .identity = job.identity, .at = at});
    }
    else if (terminal(job.execution) && job.mode == SubagentJobMode::Background && job.delivery == SubagentDeliveryState::Direct)
    {
      normalization.push_back(JobJournalRecord{.kind = JobJournalTransitionKind::DeliveryPending, .identity = job.identity, .at = at});
    }
  }
  if (normalization.empty())
    return *projection;
  auto records = loaded->records;
  records.insert(records.end(), normalization.begin(), normalization.end());
  auto normalized = project_job_journal(records, limits_);
  if (!normalized)
    return std::unexpected(std::move(normalized.error()));
  static_cast<void>(prune_projection(*normalized, limits_));
  if (auto compacted = publish_compaction(directory_fd_, loaded->fd.get(), journal_name(parent_session_id_), parent_session_id_, *normalized, limits_);
      !compacted)
    return std::unexpected(std::move(compacted.error()));
  return *normalized;
}

std::string const& JobJournal::parent_session_id() const noexcept
{
  return parent_session_id_;
}

}  // namespace ava::agent
