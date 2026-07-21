#include "sys.h"
#include "tests/support/test_harness.h"
#include "ava/agent/job_journal.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

using ava::agent::JobJournal;
using ava::agent::JobJournalLimits;
using ava::agent::JobJournalRecord;
using ava::agent::JobJournalTransitionKind;
using ava::agent::SubagentDeliveryState;
using ava::agent::SubagentExecutionState;
using ava::agent::SubagentJobIdentity;
using ava::agent::SubagentJobMode;
using ava::agent::SubagentTerminalState;

std::filesystem::path fresh_root(std::string_view name)
{
  auto root = temp_root() / ("job_journal_" + std::string(name));
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  std::filesystem::create_directories(root);
  static_cast<void>(::chmod(root.c_str(), 0700));
  return root;
}

SubagentJobIdentity identity(std::string suffix = "1", std::string parent = "parent_1")
{
  return {.job_id = "job_" + suffix,
          .task_id = "task_" + suffix,
          .parent_session_id = std::move(parent),
          .child_session_id = "child_" + suffix,
          .delivery_id = "delivery_" + suffix};
}

JobJournalRecord record(JobJournalTransitionKind kind, SubagentJobIdentity id, std::string at = "2026-07-20T00:00:00.000Z")
{
  JobJournalRecord result{.kind = kind, .identity = std::move(id), .at = std::move(at)};
  if (kind == JobJournalTransitionKind::Started)
    result.mode = SubagentJobMode::Foreground;
  else if (kind == JobJournalTransitionKind::Terminal)
  {
    result.terminal_state = SubagentTerminalState::Completed;
    result.summary = "completed summary";
    result.summary_truncated = false;
  }
  else if (kind == JobJournalTransitionKind::Interrupted)
  {
    result.stop_reason = "coordinator restarted";
    result.stop_reason_truncated = false;
  }
  else if (kind == JobJournalTransitionKind::DeliveryAttempt)
  {
    result.attempt_id = "attempt_" + result.identity.job_id;
    result.prompt_fingerprint = "fingerprint_1";
  }
  else if (kind == JobJournalTransitionKind::DeliveryAck)
  {
    result.attempt_id = "attempt_" + result.identity.job_id;
    result.committed_turn_id = "turn_" + result.identity.job_id;
  }
  return result;
}

void set_terminal(JobJournalRecord& result, SubagentTerminalState state, std::string text)
{
  result.terminal_state = state;
  result.summary = std::nullopt;
  result.error_category = std::nullopt;
  result.error = std::nullopt;
  result.stop_reason = std::nullopt;
  result.summary_truncated = std::nullopt;
  result.error_truncated = std::nullopt;
  result.stop_reason_truncated = std::nullopt;
  if (state == SubagentTerminalState::Completed)
  {
    result.summary = std::move(text);
    result.summary_truncated = false;
  }
  else if (state == SubagentTerminalState::Failed)
  {
    result.error_category = "unknown";
    result.error = std::move(text);
    result.error_truncated = false;
  }
  else
  {
    result.stop_reason = std::move(text);
    result.stop_reason_truncated = false;
  }
}

JobJournalRecord delivery_attempt(SubagentJobIdentity id, std::string at, std::string attempt_id, std::string fingerprint = "fingerprint_1")
{
  auto result = record(JobJournalTransitionKind::DeliveryAttempt, std::move(id), std::move(at));
  result.attempt_id = std::move(attempt_id);
  result.prompt_fingerprint = std::move(fingerprint);
  return result;
}

JobJournalRecord delivery_ack(SubagentJobIdentity id, std::string at, std::string attempt_id, std::string committed_turn_id)
{
  auto result = record(JobJournalTransitionKind::DeliveryAck, std::move(id), std::move(at));
  result.attempt_id = std::move(attempt_id);
  result.committed_turn_id = std::move(committed_turn_id);
  return result;
}

void expect_projection_fails(std::vector<JobJournalRecord> records, std::string const& message, JobJournalLimits limits = {})
{
  expect(!ava::agent::project_job_journal(records, limits), message);
}

void write_bytes(std::filesystem::path const& path, std::string const& bytes)
{
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::string read_bytes(std::filesystem::path const& path)
{
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::filesystem::path journal_path(std::filesystem::path const& state, std::string_view parent)
{
  return state / ava::agent::kSubagentJobJournalDirectoryName / (std::string(parent) + ".jsonl");
}

void test_valid_projection_and_contract()
{
  auto id = identity();
  auto started = record(JobJournalTransitionKind::Started, id, "01");
  auto promoted = record(JobJournalTransitionKind::Promoted, id, "02");
  auto cancel = record(JobJournalTransitionKind::CancelRequested, id, "03");
  auto terminal = record(JobJournalTransitionKind::Terminal, id, "04");
  set_terminal(terminal, SubagentTerminalState::Failed, "bounded failure");
  auto pending = record(JobJournalTransitionKind::DeliveryPending, id, "05");
  auto attempt_one = delivery_attempt(id, "06", "attempt_1");
  auto attempt_two = delivery_attempt(id, "07", "attempt_2");
  auto ack = delivery_ack(id, "08", "attempt_2", "turn_1");

  auto projected = ava::agent::project_job_journal({started, promoted, cancel, terminal, pending, attempt_one, attempt_two, ack});
  expect(projected && projected->jobs.size() == 1, "valid journal transitions project one stable job");
  if (!projected || projected->jobs.empty())
    return;
  auto const& job = projected->jobs.front();
  expect(job.schema_version == 1 && job.identity.delivery_id == "delivery_1" && job.mode == SubagentJobMode::Background && job.was_promoted,
         "projection retains schema and stable identities across promotion");
  expect(job.execution == SubagentExecutionState::Failed && job.error == "bounded failure" && job.cancel_requested &&
             job.delivery == SubagentDeliveryState::Acknowledged && job.delivery_attempts == 2 && job.delivery_attempt_history[0].attempt_id == "attempt_1" &&
             job.delivery_attempt_history[1].attempted_at == "07" && job.acknowledged_attempt_id == "attempt_2" && job.committed_turn_id == "turn_1" &&
             projected->pending_delivery_ids.empty(),
         "projection exposes exact terminal and delivery-attempt metadata");
}

void test_every_transition_rejects_invalid_order()
{
  auto id = identity();
  for (auto kind : {JobJournalTransitionKind::Promoted, JobJournalTransitionKind::CancelRequested, JobJournalTransitionKind::Terminal,
                    JobJournalTransitionKind::DeliveryPending, JobJournalTransitionKind::DeliveryAttempt, JobJournalTransitionKind::DeliveryAck,
                    JobJournalTransitionKind::Interrupted})
  {
    expect_projection_fails({record(kind, id)}, "non-start transition is rejected before started: " + std::string(ava::agent::to_string(kind)));
  }

  auto foreground = record(JobJournalTransitionKind::Started, id);
  expect_projection_fails({foreground, foreground}, "duplicate started transition is rejected");
  expect_projection_fails({foreground, record(JobJournalTransitionKind::DeliveryPending, id)}, "delivery_pending is rejected before terminal");
  expect_projection_fails({foreground, record(JobJournalTransitionKind::DeliveryAttempt, id)}, "delivery_attempt is rejected before pending");
  expect_projection_fails({foreground, record(JobJournalTransitionKind::DeliveryAck, id)}, "delivery_ack is rejected before attempt");

  auto cancel = record(JobJournalTransitionKind::CancelRequested, id);
  expect_projection_fails({foreground, cancel, cancel}, "duplicate cancel_requested is rejected");
  auto promoted = record(JobJournalTransitionKind::Promoted, id);
  expect_projection_fails({foreground, promoted, promoted}, "duplicate promoted is rejected");

  auto background = foreground;
  background.mode = SubagentJobMode::Background;
  expect_projection_fails({background, promoted}, "direct background job cannot be promoted");
  auto terminal = record(JobJournalTransitionKind::Terminal, id);
  auto interrupted = record(JobJournalTransitionKind::Interrupted, id);
  expect_projection_fails({foreground, terminal, terminal}, "duplicate terminal is rejected");
  expect_projection_fails({foreground, terminal, interrupted}, "interrupted is rejected after terminal");
  expect_projection_fails({foreground, interrupted, terminal}, "terminal is rejected after interrupted");
  expect_projection_fails({foreground, terminal, promoted}, "promotion is rejected after terminal");
  expect_projection_fails({foreground, terminal, record(JobJournalTransitionKind::DeliveryPending, id)},
                          "foreground terminal job cannot become delivery pending");

  auto pending = record(JobJournalTransitionKind::DeliveryPending, id);
  auto attempt = record(JobJournalTransitionKind::DeliveryAttempt, id);
  auto ack = record(JobJournalTransitionKind::DeliveryAck, id);
  expect_projection_fails({background, terminal, pending, pending}, "duplicate delivery_pending is rejected");
  expect_projection_fails({background, terminal, pending, ack}, "delivery_ack is rejected without attempt");
  expect_projection_fails({background, terminal, pending, attempt, ack, attempt}, "delivery_attempt is rejected after acknowledgement");
  expect_projection_fails({background, terminal, pending, attempt, ack, ack}, "duplicate delivery_ack is rejected");
}

void test_identity_mismatch_and_duplicates()
{
  auto original = identity();
  auto started = record(JobJournalTransitionKind::Started, original);
  for (int field = 0; field < 5; ++field)
  {
    auto changed = original;
    if (field == 0)
      changed.job_id = "job_other";
    else if (field == 1)
      changed.task_id = "task_other";
    else if (field == 2)
      changed.parent_session_id = "parent_other";
    else if (field == 3)
      changed.child_session_id = "child_other";
    else
      changed.delivery_id = "delivery_other";
    expect_projection_fails({started, record(JobJournalTransitionKind::CancelRequested, changed)}, "transition identity mismatch is rejected");
  }

  auto second = identity("2");
  for (int field = 0; field < 3; ++field)
  {
    auto duplicate = second;
    if (field == 0)
      duplicate.task_id = original.task_id;
    else if (field == 1)
      duplicate.child_session_id = original.child_session_id;
    else
      duplicate.delivery_id = original.delivery_id;
    expect_projection_fails({started, record(JobJournalTransitionKind::Started, duplicate)}, "cross-job stable identity reuse is rejected");
  }
  auto same_job = record(JobJournalTransitionKind::Started, second);
  same_job.identity.job_id = original.job_id;
  expect_projection_fails({started, same_job}, "duplicate job id is rejected");
}

void test_sequential_task_and_child_session_reuse()
{
  auto first = identity("resume_first");
  auto second = identity("resume_second");
  second.task_id = first.task_id;
  second.child_session_id = first.child_session_id;

  expect_projection_fails({record(JobJournalTransitionKind::Started, first, "01"), record(JobJournalTransitionKind::Started, second, "02")},
                          "concurrent duplicate child runs are rejected");

  auto projection = ava::agent::project_job_journal(
      {record(JobJournalTransitionKind::Started, first, "01"), record(JobJournalTransitionKind::Terminal, first, "02"),
       record(JobJournalTransitionKind::Started, second, "03"), record(JobJournalTransitionKind::Promoted, second, "04"),
       record(JobJournalTransitionKind::Terminal, second, "05"), record(JobJournalTransitionKind::DeliveryPending, second, "05")});
  auto resumed = projection ? projection->find(second.job_id) : nullptr;
  expect(projection && projection->jobs.size() == 2 && resumed && resumed->identity.task_id == first.task_id &&
             resumed->identity.child_session_id == first.child_session_id && resumed->identity.delivery_id != first.delivery_id && resumed->was_promoted &&
             resumed->delivery == ava::agent::SubagentDeliveryState::Pending,
         "terminal child sessions permit sequential foreground resume and later promotion with fresh job/delivery IDs");

  auto inconsistent = second;
  inconsistent.job_id = "job_resume_inconsistent";
  inconsistent.delivery_id = "delivery_resume_inconsistent";
  inconsistent.child_session_id = "child_other";
  expect_projection_fails({record(JobJournalTransitionKind::Started, first, "01"), record(JobJournalTransitionKind::Terminal, first, "02"),
                           record(JobJournalTransitionKind::Started, inconsistent, "03")},
                          "sequential reuse must preserve the task-to-child-session identity pair");
}

void test_explicit_recovery_and_ordinary_reopen()
{
  auto state = fresh_root("restart");
  auto opened = JobJournal::try_open_owned(state, "parent_1");
  expect(opened && opened->has_value(), "restart test opens and owns journal");
  if (!opened || !*opened)
    return;
  auto started = record(JobJournalTransitionKind::Started, identity(), "01");
  started.mode = SubagentJobMode::Background;
  expect((*opened)->append(started).has_value(), "restart test durably starts background job");
  auto const before_reopen = read_bytes(journal_path(state, "parent_1"));

  auto second_handle = JobJournal::open(state, "parent_1");
  auto still_running =
      second_handle ? second_handle->projection() : ava::core::Result<ava::agent::SubagentJobProjection>(std::unexpected(second_handle.error()));
  expect(
      still_running && still_running->jobs.front().execution == SubagentExecutionState::Running && read_bytes(journal_path(state, "parent_1")) == before_reopen,
      "ordinary second open is non-mutating and does not interrupt a live job");
  if (!second_handle)
    return;

  auto recovered = (*opened)->recover_interrupted_jobs();
  expect(recovered && recovered->jobs.front().execution == SubagentExecutionState::Interrupted &&
             recovered->jobs.front().stop_reason == "coordinator restarted" && recovered->jobs.front().delivery == SubagentDeliveryState::Pending &&
             recovered->pending_delivery_ids == std::vector<std::string>{"delivery_1"},
         "explicit coordinator recovery interrupts running work and retains pending delivery identity");
  auto const after_recovery = read_bytes(journal_path(state, "parent_1"));
  auto recovered_again = (*opened)->recover_interrupted_jobs();
  expect(recovered_again && recovered_again->jobs.front().delivery_attempts == 0 && recovered_again->pending_delivery_ids.size() == 1 &&
             read_bytes(journal_path(state, "parent_1")) == after_recovery,
         "explicit recovery is idempotent and does not invent delivery attempts");
}

void test_pending_attempt_ack_replay()
{
  auto state = fresh_root("delivery");
  auto opened = JobJournal::open(state, "parent_1");
  expect(opened.has_value(), "delivery journal opens");
  if (!opened)
    return;
  auto id = identity();
  auto started = record(JobJournalTransitionKind::Started, id, "01");
  started.mode = SubagentJobMode::Background;
  auto terminal = record(JobJournalTransitionKind::Terminal, id, "02");
  set_terminal(terminal, SubagentTerminalState::Completed, "durable delivery summary");
  expect(opened->append(started).has_value() && opened->append(terminal).has_value() &&
             opened->append(record(JobJournalTransitionKind::DeliveryPending, id, "03")).has_value() &&
             opened->append(record(JobJournalTransitionKind::DeliveryAttempt, id, "04")).has_value(),
         "pending and attempt transitions append durably");
  auto reopened = JobJournal::open(state, "parent_1");
  auto attempting = reopened ? reopened->projection() : ava::core::Result<ava::agent::SubagentJobProjection>(std::unexpected(reopened.error()));
  expect(attempting && attempting->pending_delivery_ids == std::vector<std::string>{"delivery_1"} &&
             attempting->jobs.front().delivery == SubagentDeliveryState::Attempting && attempting->jobs.front().summary == "durable delivery summary",
         "pending delivery summary survives ordinary reopen");
  expect(opened->append(record(JobJournalTransitionKind::DeliveryAck, id, "05")).has_value(), "delivery acknowledgement appends durably");
  auto acknowledged = opened->projection();
  expect(acknowledged && acknowledged->pending_delivery_ids.empty() && acknowledged->jobs.front().delivery == SubagentDeliveryState::Acknowledged,
         "acknowledged delivery leaves pending identity set");
}

void test_corruption_truncation_and_duplicate_json()
{
  auto state = fresh_root("corruption");
  {
    auto opened = JobJournal::open(state, "parent_1");
    expect(opened.has_value(), "corruption fixture creates journal files");
  }
  auto path = journal_path(state, "parent_1");
  write_bytes(path, "{bad}\n");
  expect(!JobJournal::open(state, "parent_1"), "invalid JSON corruption is rejected");
  write_bytes(path,
              "{\"schema\":1,\"schema\":1,\"kind\":\"started\",\"job_id\":\"job_1\",\"task_id\":\"task_1\",\"parent_session_id\":\"parent_1\","
              "\"child_session_id\":\"child_1\",\"delivery_id\":\"delivery_1\",\"at\":\"01\",\"mode\":\"foreground\"}\n");
  expect(!JobJournal::open(state, "parent_1"), "duplicate-key JSON corruption is rejected");
  write_bytes(path,
              "{\"schema\":1,\"kind\":\"started\",\"job_id\":\"job_1\",\"task_id\":\"task_1\",\"parent_session_id\":\"parent_1\","
              "\"child_session_id\":\"child_1\",\"delivery_id\":\"delivery_1\",\"at\":\"01\",\"mode\":\"foreground\"}");
  expect(!JobJournal::open(state, "parent_1"), "unterminated final journal record is rejected rather than repaired");
}

void test_path_type_ownership_and_modes()
{
  {
    auto state = fresh_root("jobs_symlink");
    auto target = state / "target";
    std::filesystem::create_directory(target);
    static_cast<void>(::chmod(target.c_str(), 0700));
    std::filesystem::create_directory_symlink(target, state / ava::agent::kSubagentJobJournalDirectoryName);
    expect(!JobJournal::open(state, "parent_1"), "symlink journal directory is rejected");
  }
  {
    auto state = fresh_root("jobs_mode");
    auto jobs = state / ava::agent::kSubagentJobJournalDirectoryName;
    std::filesystem::create_directory(jobs);
    static_cast<void>(::chmod(jobs.c_str(), 0755));
    expect(!JobJournal::open(state, "parent_1"), "journal directory without exact mode 0700 is rejected");
  }
  {
    auto state = fresh_root("file_fifo");
    auto jobs = state / ava::agent::kSubagentJobJournalDirectoryName;
    std::filesystem::create_directory(jobs);
    static_cast<void>(::chmod(jobs.c_str(), 0700));
    auto path = journal_path(state, "parent_1");
    static_cast<void>(::mkfifo(path.c_str(), 0600));
    expect(!JobJournal::open(state, "parent_1"), "FIFO journal final component is rejected without blocking");
  }
  {
    auto state = fresh_root("file_mode");
    {
      auto opened = JobJournal::open(state, "parent_1");
      expect(opened.has_value(), "mode fixture creates journal");
    }
    struct stat jobs_status{};
    struct stat journal_status{};
    struct stat lock_status{};
    auto const jobs_path = state / ava::agent::kSubagentJobJournalDirectoryName;
    auto const lock_path = jobs_path / "parent_1.lock";
    expect(::stat(jobs_path.c_str(), &jobs_status) == 0 && (jobs_status.st_mode & 07777) == 0700 &&
               ::stat(journal_path(state, "parent_1").c_str(), &journal_status) == 0 && (journal_status.st_mode & 07777) == 0600 &&
               ::stat(lock_path.c_str(), &lock_status) == 0 && (lock_status.st_mode & 07777) == 0600,
           "created journal directory, journal, and lock have exact private modes");
    static_cast<void>(::chmod(journal_path(state, "parent_1").c_str(), 0644));
    expect(!JobJournal::open(state, "parent_1"), "journal file without exact mode 0600 is rejected");
  }
  {
    auto root = fresh_root("state_symlink");
    auto actual = root / "actual";
    std::filesystem::create_directory(actual);
    static_cast<void>(::chmod(actual.c_str(), 0700));
    auto linked = root / "linked";
    std::filesystem::create_directory_symlink(actual, linked);
    expect(!JobJournal::open(linked, "parent_1"), "symlink AVA state ancestor is rejected");
  }
}

void test_trusted_logical_state_anchor_preserves_path_identity()
{
  auto const root = create_empty_root("job-journal-logical-state");
  auto const state = root / "state";
  std::filesystem::create_directories(state);
  std::filesystem::permissions(state, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace);
  auto anchors = ava::core::AnchorSet::open({state});
  expect(anchors.has_value(), "logical-state fixture opens one trusted startup AnchorSet");
  if (!anchors)
    return;
  auto opened = JobJournal::open(state, "parent_1", {}, anchors->get());
  expect(opened.has_value(), "journal uses the exact logical state anchor without canonicalizing a trusted symlinked ancestor");
  expect(!JobJournal::open(state, "parent_2"), "unanchored journal opening retains strict symlink-ancestor rejection");
}

void test_bounds_utf8_and_controls()
{
  JobJournalLimits limits;
  limits.max_id_bytes = 8;
  expect_projection_fails({record(JobJournalTransitionKind::Started, identity())}, "identifier byte bound is enforced", limits);

  auto id = identity();
  id.job_id = "bad/id";
  expect_projection_fails({record(JobJournalTransitionKind::Started, id)}, "path-like identifier is rejected");
  id = identity();
  id.job_id = std::string("job_") + static_cast<char>(1);
  expect_projection_fails({record(JobJournalTransitionKind::Started, id)}, "identifier control byte is rejected");
  auto invalid_utf8 = record(JobJournalTransitionKind::Started, identity());
  invalid_utf8.at = std::string("time_") + static_cast<char>(0xFF);
  expect_projection_fails({invalid_utf8}, "invalid UTF-8 timestamp is rejected");
  auto control = record(JobJournalTransitionKind::Started, identity());
  control.at = "time\nother";
  expect_projection_fails({control}, "timestamp control characters are rejected");
  JobJournalLimits timestamp_limits;
  timestamp_limits.max_timestamp_bytes = 4;
  expect_projection_fails({record(JobJournalTransitionKind::Started, identity())}, "timestamp byte bound is enforced", timestamp_limits);

  JobJournalLimits job_limits;
  job_limits.max_jobs = 1;
  job_limits.max_retained_terminal_jobs = 1;
  expect_projection_fails({record(JobJournalTransitionKind::Started, identity("1")), record(JobJournalTransitionKind::Started, identity("2"))},
                          "projected job count bound is enforced", job_limits);
  JobJournalLimits record_limits;
  record_limits.max_records = 1;
  record_limits.compact_after_records = 1;
  expect_projection_fails({record(JobJournalTransitionKind::Started, identity()), record(JobJournalTransitionKind::CancelRequested, identity())},
                          "journal record count bound is enforced", record_limits);

  JobJournalLimits attempt_limits;
  attempt_limits.max_delivery_attempts_per_job = 1;
  auto background = record(JobJournalTransitionKind::Started, identity());
  background.mode = SubagentJobMode::Background;
  auto terminal = record(JobJournalTransitionKind::Terminal, identity());
  auto pending = record(JobJournalTransitionKind::DeliveryPending, identity());
  auto attempt = delivery_attempt(identity(), "2026-07-20T00:00:00.000Z", "attempt_1");
  auto second_attempt = delivery_attempt(identity(), "2026-07-20T00:00:00.000Z", "attempt_2");
  expect_projection_fails({background, terminal, pending, attempt, second_attempt}, "per-job delivery attempt bound is enforced", attempt_limits);

  auto state = fresh_root("line_bound");
  JobJournalLimits line_limits;
  line_limits.max_line_bytes = 64;
  line_limits.compact_after_file_bytes = 64;
  line_limits.max_file_bytes = 128;
  auto opened = JobJournal::open(state, "parent_1", line_limits);
  expect(opened.has_value(), "small valid journal limits open");
  if (opened)
    expect(!opened->append(record(JobJournalTransitionKind::Started, identity())), "serialized record line bound is enforced before write");

  auto file_state = fresh_root("file_bound");
  {
    auto created = JobJournal::open(file_state, "parent_1");
    expect(created.has_value(), "file-bound fixture creates journal");
  }
  write_bytes(journal_path(file_state, "parent_1"), std::string(128, 'x') + "\n");
  expect(!JobJournal::open(file_state, "parent_1", line_limits), "journal file byte bound is enforced before parsing");
}

void test_terminal_text_requirements_truncation_and_timestamp_order()
{
  auto id = identity();
  auto started = record(JobJournalTransitionKind::Started, id, "01");

  auto completed_missing = record(JobJournalTransitionKind::Terminal, id, "02");
  completed_missing.summary = std::nullopt;
  expect_projection_fails({started, completed_missing}, "completed terminal requires summary text and truncation metadata");
  auto failed_wrong_fields = record(JobJournalTransitionKind::Terminal, id, "02");
  failed_wrong_fields.terminal_state = SubagentTerminalState::Failed;
  expect_projection_fails({started, failed_wrong_fields}, "failed terminal rejects completed-summary fields");
  auto canceled_missing = record(JobJournalTransitionKind::Terminal, id, "02");
  set_terminal(canceled_missing, SubagentTerminalState::Canceled, "canceled");
  canceled_missing.stop_reason_truncated = std::nullopt;
  expect_projection_fails({started, canceled_missing}, "canceled terminal requires stop-reason truncation metadata");
  auto interrupted_missing = record(JobJournalTransitionKind::Interrupted, id, "02");
  interrupted_missing.stop_reason = std::nullopt;
  expect_projection_fails({started, interrupted_missing}, "interrupted terminal requires stop reason text");

  JobJournalLimits limits;
  limits.max_summary_bytes = 5;
  limits.max_error_bytes = 5;
  limits.max_stop_reason_bytes = 5;
  auto completed = record(JobJournalTransitionKind::Terminal, id, "02");
  set_terminal(completed, SubagentTerminalState::Completed, "abcdé");
  auto completed_projection = ava::agent::project_job_journal({started, completed}, limits);
  expect(completed_projection && completed_projection->jobs.front().summary == "abcd" && completed_projection->jobs.front().summary_truncated,
         "summary truncation is exact and ends on a UTF-8 boundary");
  auto failed = record(JobJournalTransitionKind::Terminal, id, "02");
  set_terminal(failed, SubagentTerminalState::Failed, "abéé");
  auto failed_projection = ava::agent::project_job_journal({started, failed}, limits);
  expect(failed_projection && failed_projection->jobs.front().error == "abé" && failed_projection->jobs.front().error_truncated,
         "error truncation is exact and records truncation metadata");
  auto interrupted = record(JobJournalTransitionKind::Interrupted, id, "02");
  interrupted.stop_reason = "abcdé";
  auto interrupted_projection = ava::agent::project_job_journal({started, interrupted}, limits);
  expect(interrupted_projection && interrupted_projection->jobs.front().stop_reason == "abcd" && interrupted_projection->jobs.front().stop_reason_truncated,
         "stop-reason truncation is exact and records truncation metadata");

  auto accounted = record(JobJournalTransitionKind::Terminal, id, "02");
  accounted.provider_iterations = 3;
  accounted.tool_calls = 4;
  accounted.tool_iterations = 2;
  auto accounted_projection = ava::agent::project_job_journal({started, accounted});
  expect(accounted_projection && accounted_projection->jobs.front().provider_iterations == 3 && accounted_projection->jobs.front().tool_calls == 4 &&
             accounted_projection->jobs.front().tool_iterations == 2,
         "terminal accounting fields round-trip through the durable projection");
  auto partial_accounting = accounted;
  partial_accounting.tool_calls = std::nullopt;
  expect_projection_fails({started, partial_accounting}, "terminal accounting fields must be present together");
  JobJournalLimits accounting_limits;
  accounting_limits.max_accounting_value = 2;
  expect_projection_fails({started, accounted}, "terminal accounting fields are numerically bounded", accounting_limits);

  auto invalid_utf8 = record(JobJournalTransitionKind::Terminal, id, "02");
  set_terminal(invalid_utf8, SubagentTerminalState::Completed, std::string("bad") + static_cast<char>(0xFF));
  expect_projection_fails({started, invalid_utf8}, "terminal text rejects invalid UTF-8 before truncation");
  auto control = record(JobJournalTransitionKind::Terminal, id, "02");
  set_terminal(control, SubagentTerminalState::Failed, std::string("bad") + static_cast<char>(1));
  expect_projection_fails({started, control}, "terminal text rejects unsafe control bytes");
  expect_projection_fails({started, record(JobJournalTransitionKind::CancelRequested, id, "00")}, "per-job transition timestamps must be nondecreasing");
  expect_projection_fails({record(JobJournalTransitionKind::Started, identity("1"), "02"), record(JobJournalTransitionKind::Started, identity("2"), "01")},
                          "journal transition timestamps must be globally nondecreasing");
}

void test_delivery_identity_validation_and_compaction()
{
  auto id = identity();
  auto started = record(JobJournalTransitionKind::Started, id, "01");
  started.mode = SubagentJobMode::Background;
  auto terminal = record(JobJournalTransitionKind::Terminal, id, "02");
  set_terminal(terminal, SubagentTerminalState::Completed, "summary for exact replay");
  auto pending = record(JobJournalTransitionKind::DeliveryPending, id, "03");
  auto first = delivery_attempt(id, "04", "attempt_1", "fingerprint_A");
  auto second = delivery_attempt(id, "05", "attempt_2", "fingerprint_A");

  expect_projection_fails({started, terminal, pending, first, first}, "duplicate delivery attempt IDs are rejected");
  auto mismatched_fingerprint = delivery_attempt(id, "05", "attempt_2", "fingerprint_B");
  expect_projection_fails({started, terminal, pending, first, mismatched_fingerprint}, "retry prompt fingerprint mismatch is rejected");
  expect_projection_fails({started, terminal, pending, first, second, delivery_ack(id, "06", "attempt_1", "turn_1")},
                          "acknowledgement of a nonlatest attempt is rejected");
  expect_projection_fails({started, terminal, pending, first, second, delivery_ack(id, "06", "attempt_unknown", "turn_1")},
                          "acknowledgement of an unknown attempt is rejected");

  auto second_id = identity("2");
  auto second_started = record(JobJournalTransitionKind::Started, second_id, "06");
  second_started.mode = SubagentJobMode::Background;
  auto second_terminal = record(JobJournalTransitionKind::Terminal, second_id, "07");
  auto second_pending = record(JobJournalTransitionKind::DeliveryPending, second_id, "08");
  auto other_attempt = delivery_attempt(second_id, "09", "attempt_other", "fingerprint_other");
  expect_projection_fails({started, terminal, pending, first, delivery_ack(id, "05", "attempt_1", "turn_shared"), second_started, second_terminal,
                           second_pending, other_attempt, delivery_ack(second_id, "10", "attempt_other", "turn_shared")},
                          "acknowledgements cannot reuse a committed turn identity");

  auto state = fresh_root("delivery_compaction");
  auto opened = JobJournal::open(state, "parent_1");
  expect(opened.has_value(), "delivery compaction journal opens");
  if (!opened)
    return;
  auto ack = delivery_ack(id, "06", "attempt_2", "turn_1");
  expect(opened->append(started).has_value() && opened->append(terminal).has_value() && opened->append(pending).has_value() &&
             opened->append(first).has_value() && opened->append(second).has_value() && opened->append(ack).has_value(),
         "delivery identity fixture appends exact attempt and acknowledgement records");
  auto compacted = opened->compact();
  auto replayed = opened->projection();
  expect(compacted && replayed && replayed->jobs.front().summary == "summary for exact replay" && replayed->jobs.front().delivery_attempt_history.size() == 2 &&
             replayed->jobs.front().delivery_attempt_history[0].attempt_id == "attempt_1" &&
             replayed->jobs.front().delivery_attempt_history[0].attempted_at == "04" &&
             replayed->jobs.front().delivery_attempt_history[1].attempt_id == "attempt_2" &&
             replayed->jobs.front().delivery_attempt_history[1].prompt_fingerprint == "fingerprint_A" &&
             replayed->jobs.front().acknowledged_attempt_id == "attempt_2" && replayed->jobs.front().committed_turn_id == "turn_1",
         "compaction preserves exact bounded summary, attempt history, and ack identity");
}

void test_concurrent_appends_are_serialized()
{
  auto state = fresh_root("concurrent");
  constexpr std::size_t count = 12;
  std::vector<JobJournal> journals;
  journals.reserve(count);
  for (std::size_t index = 0; index < count; ++index)
  {
    auto opened = JobJournal::open(state, "parent_1");
    expect(opened.has_value(), "concurrency fixture opens independent journal handle");
    if (!opened)
      return;
    journals.push_back(std::move(*opened));
  }

  int barrier[2] = {-1, -1};
  expect(::pipe2(barrier, O_CLOEXEC) == 0, "concurrency fixture creates process barrier");
  std::vector<pid_t> children;
  for (std::size_t index = 0; index < count; ++index)
  {
    pid_t const child = ::fork();
    if (child == 0)
    {
      static_cast<void>(::close(barrier[1]));
      char token = 0;
      ssize_t const read_count = ::read(barrier[0], &token, 1);
      auto started = record(JobJournalTransitionKind::Started, identity(std::to_string(index + 1)), "same-time");
      bool const appended = read_count == 1 && journals[index].append(started).has_value();
      ::_exit(appended ? 0 : 1);
    }
    expect(child > 0, "concurrency fixture forks append process");
    if (child > 0)
      children.push_back(child);
  }
  static_cast<void>(::close(barrier[0]));
  std::string tokens(children.size(), 'x');
  expect(::write(barrier[1], tokens.data(), tokens.size()) == static_cast<ssize_t>(tokens.size()), "concurrency fixture releases all append processes");
  static_cast<void>(::close(barrier[1]));
  bool children_succeeded = true;
  for (pid_t child : children)
  {
    int status = 0;
    children_succeeded = children_succeeded && ::waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
  }
  expect(children_succeeded, "concurrent independent journal append processes all succeed");
  auto projected = journals.front().projection();
  expect(projected && projected->jobs.size() == count, "interprocess lock serializes concurrent appends without lost records");
}

void test_unpublished_start_rollback_is_strict()
{
  auto state = fresh_root("unpublished_rollback");
  auto opened = JobJournal::open(state, "parent_1");
  expect(opened.has_value(), "unpublished rollback journal opens");
  if (!opened)
    return;
  auto id = identity();
  auto started = record(JobJournalTransitionKind::Started, id, "01");
  started.mode = SubagentJobMode::Background;
  expect(opened->append(started).has_value(), "unpublished rollback fixture appends Started");
  auto rolled_back = opened->rollback_unpublished_started(id);
  expect(rolled_back && rolled_back->jobs.empty(), "unpublished rollback removes exactly one sole Started transition");

  expect(opened->append(started).has_value() && opened->append(record(JobJournalTransitionKind::CancelRequested, id, "02")).has_value(),
         "strict rollback fixture appends a published follow-up transition");
  auto rejected = opened->rollback_unpublished_started(id);
  auto retained = opened->projection();
  expect(!rejected && retained && retained->find(id.job_id) && retained->find(id.job_id)->execution == SubagentExecutionState::Running &&
             retained->find(id.job_id)->cancel_requested,
         "unpublished rollback cannot erase a job with any transition after Started");
}

void test_terminal_delivery_batch_is_atomic()
{
  auto state = fresh_root("terminal_batch");
  auto opened = JobJournal::open(state, "parent_1");
  expect(opened.has_value(), "terminal batch journal opens");
  if (!opened)
    return;
  auto id = identity();
  auto started = record(JobJournalTransitionKind::Started, id, "01");
  started.mode = SubagentJobMode::Background;
  expect(opened->append(started).has_value(), "terminal batch fixture appends started transition");

  auto terminal = record(JobJournalTransitionKind::Terminal, id, "02");
  auto pending = record(JobJournalTransitionKind::DeliveryPending, id, "02");
  auto malformed = opened->append_batch({terminal});
  auto still_running = opened->projection();
  expect(!malformed && still_running && still_running->jobs.front().execution == SubagentExecutionState::Running &&
             still_running->jobs.front().delivery == SubagentDeliveryState::Direct,
         "invalid terminal batch leaves no partial terminal transition");

  auto batched = opened->append_batch({terminal, pending});
  auto const bytes = read_bytes(journal_path(state, "parent_1"));
  expect(batched && batched->jobs.front().execution == SubagentExecutionState::Completed && batched->jobs.front().delivery == SubagentDeliveryState::Pending &&
             std::ranges::count(bytes, '\n') == 3,
         "terminal and delivery pending publish together in one canonical journal update");
}

void test_process_lifetime_owner_lease_guards_recovery()
{
  auto state = fresh_root("owner_process");
  {
    auto journal = JobJournal::open(state, "parent_1");
    expect(journal.has_value(), "owner-process fixture opens journal");
    if (!journal)
      return;
    auto started = record(JobJournalTransitionKind::Started, identity(), "01");
    started.mode = SubagentJobMode::Background;
    expect(journal->append(started).has_value(), "owner-process fixture persists running job");
  }

  int ready[2] = {-1, -1};
  int release[2] = {-1, -1};
  expect(::pipe2(ready, O_CLOEXEC) == 0 && ::pipe2(release, O_CLOEXEC) == 0, "owner-process fixture creates CLOEXEC synchronization pipes");
  if (ready[0] < 0 || release[0] < 0)
    return;
  auto owner_pid = ::fork();
  if (owner_pid == 0)
  {
    ::alarm(5);
    static_cast<void>(::close(ready[0]));
    static_cast<void>(::close(release[1]));
    auto owned = JobJournal::try_open_owned(state, "parent_1");
    char const status = owned && owned->has_value() ? '1' : '0';
    static_cast<void>(::write(ready[1], &status, 1));
    char wake = 0;
    if (status == '1')
      static_cast<void>(::read(release[0], &wake, 1));
    ::_exit(status == '1' ? 0 : 2);
  }
  static_cast<void>(::close(ready[1]));
  static_cast<void>(::close(release[0]));
  char owner_ready = 0;
  auto const ready_bytes = ::read(ready[0], &owner_ready, 1);
  static_cast<void>(::close(ready[0]));
  expect(owner_pid > 0 && ready_bytes == 1 && owner_ready == '1', "process A acquires the parent owner lease before recovery races");
  if (owner_pid <= 0 || owner_ready != '1')
    return;

  auto blocked_pid = ::fork();
  if (blocked_pid == 0)
  {
    ::alarm(5);
    auto recovered = ava::agent::recover_owned_interrupted_job_journals(state);
    auto unrelated = JobJournal::try_open_owned(state, "unrelated_parent");
    auto journal = JobJournal::open(state, "parent_1");
    auto projection = journal ? journal->projection() : ava::core::Result<ava::agent::SubagentJobProjection>(std::unexpected(journal.error()));
    bool const safe = recovered && recovered->empty() && unrelated && unrelated->has_value() && projection && !projection->jobs.empty() &&
                      projection->jobs.front().execution == SubagentExecutionState::Running;
    ::_exit(safe ? 0 : 3);
  }
  int blocked_status = 0;
  static_cast<void>(::waitpid(blocked_pid, &blocked_status, 0));
  expect(WIFEXITED(blocked_status) && WEXITSTATUS(blocked_status) == 0,
         "process B skips A's owned parent without interruption and may own an unrelated parent");

  char const wake = 'x';
  static_cast<void>(::write(release[1], &wake, 1));
  static_cast<void>(::close(release[1]));
  int owner_status = 0;
  static_cast<void>(::waitpid(owner_pid, &owner_status, 0));
  expect(WIFEXITED(owner_status) && WEXITSTATUS(owner_status) == 0, "process A exits and releases its process-lifetime owner lease");

  auto recovery_pid = ::fork();
  if (recovery_pid == 0)
  {
    ::alarm(5);
    auto recovered = ava::agent::recover_owned_interrupted_job_journals(state);
    bool const interrupted = recovered && std::ranges::any_of(*recovered, [](auto const& item) {
                               return !item.projection.jobs.empty() && item.projection.jobs.front().identity.parent_session_id == "parent_1" &&
                                      item.projection.jobs.front().execution == SubagentExecutionState::Interrupted;
                             });
    ::_exit(interrupted ? 0 : 4);
  }
  int recovery_status = 0;
  static_cast<void>(::waitpid(recovery_pid, &recovery_status, 0));
  expect(WIFEXITED(recovery_status) && WEXITSTATUS(recovery_status) == 0, "process B recovers the parent only after process A exits");
}

void test_retention_and_atomic_compaction()
{
  auto state = fresh_root("retention");
  JobJournalLimits limits;
  limits.max_retained_terminal_jobs = 2;
  limits.compact_after_records = 3;
  limits.compact_after_file_bytes = 512;
  auto opened = JobJournal::open(state, "parent_1", limits);
  expect(opened.has_value(), "retention journal opens");
  if (!opened)
    return;
  for (int index = 1; index <= 5; ++index)
  {
    auto id = identity(std::to_string(index));
    auto const started_at = index * 2 - 1 < 10 ? "0" + std::to_string(index * 2 - 1) : std::to_string(index * 2 - 1);
    auto const terminal_at = index * 2 < 10 ? "0" + std::to_string(index * 2) : std::to_string(index * 2);
    expect(opened->append(record(JobJournalTransitionKind::Started, id, started_at)).has_value(), "retention start appends");
    expect(opened->append(record(JobJournalTransitionKind::Terminal, id, terminal_at)).has_value(), "retention terminal appends");
  }
  auto const stale_compaction = state / ava::agent::kSubagentJobJournalDirectoryName / ".parent_1.jsonl.compact";
  write_bytes(stale_compaction, "stale");
  static_cast<void>(::chmod(stale_compaction.c_str(), 0600));
  auto compacted = opened->compact();
  expect(compacted && !std::filesystem::exists(stale_compaction), "compaction safely replaces one bounded stale private temporary file");
  expect(compacted && compacted->jobs.size() == 2 && compacted->find("job_4") != nullptr && compacted->find("job_5") != nullptr,
         "retention compaction keeps only the newest bounded terminal jobs");
  struct stat status{};
  expect(::stat(journal_path(state, "parent_1").c_str(), &status) == 0 && status.st_size > 0 &&
             static_cast<std::uintmax_t>(status.st_size) <= limits.max_file_bytes,
         "atomic canonical compaction leaves a bounded regular journal");
  auto replayed = opened->projection();
  expect(replayed && replayed->jobs.size() == 2, "compacted journal strictly replays to the retained projection");
}

}  // namespace

void run_job_journal_tests()
{
  test_valid_projection_and_contract();
  test_every_transition_rejects_invalid_order();
  test_identity_mismatch_and_duplicates();
  test_sequential_task_and_child_session_reuse();
  test_explicit_recovery_and_ordinary_reopen();
  test_pending_attempt_ack_replay();
  test_corruption_truncation_and_duplicate_json();
  test_path_type_ownership_and_modes();
  test_trusted_logical_state_anchor_preserves_path_identity();
  test_bounds_utf8_and_controls();
  test_terminal_text_requirements_truncation_and_timestamp_order();
  test_delivery_identity_validation_and_compaction();
  test_concurrent_appends_are_serialized();
  test_unpublished_start_rollback_is_strict();
  test_terminal_delivery_batch_is_atomic();
  test_process_lifetime_owner_lease_guards_recovery();
  test_retention_and_atomic_compaction();
}
