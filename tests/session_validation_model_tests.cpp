#include <algorithm>
#include <string>
#include <utility>

#include "ava/session/validation_model.h"
#include "tests/support/test_harness.h"

namespace {

ava::session::SessionEntry replay_entry(std::string id, ava::session::EntryType type, std::string data_json)
{
  return ava::session::SessionEntry{.id = std::move(id),
                                    .parent_id = "",
                                    .type = type,
                                    .timestamp = "2026-05-05T00:00:00Z",
                                    .data_json = std::move(data_json)};
}

bool has_issue(ava::session::SessionReplayValidation const& validation, ava::session::SessionReplayIssueKind kind)
{
  return std::ranges::any_of(validation.issues,
                             [kind](ava::session::SessionReplayIssue const& issue) { return issue.kind == kind; });
}

void test_session_start_and_model_change_update_active_model()
{
  ava::session::SessionReplayValidation validation;
  ava::session::SessionReplayModelState state;

  validate_session_start_entry(validation, state, 0,
                               replay_entry("entry_start", ava::session::EntryType::SessionStart,
                                            "{\"mode\":\"build\",\"provider\":\"openai\",\"model\":\"gpt-5.5\"}"));
  expect(validation.ok(), "session_start accepts semantic model metadata");
  expect(state.provider_id == "openai" && state.model_id == "gpt-5.5", "session_start updates active model state");

  validate_model_change_entry(validation, state, 1,
                              replay_entry("entry_model_change", ava::session::EntryType::ModelChange,
                                           "{\"previous_provider\":\"openai\",\"previous_model\":\"gpt-5.5\","
                                           "\"provider\":\"openai\",\"model\":\"gpt-5.5-mini\"}"));
  expect(validation.ok(), "model_change accepts a valid active-model transition");
  expect(state.provider_id == "openai" && state.model_id == "gpt-5.5-mini", "model_change updates active model state");
}

void test_invalid_model_metadata_adds_replay_issues()
{
  ava::session::SessionReplayValidation validation;
  ava::session::SessionReplayModelState state;

  validate_session_start_entry(validation, state, 0,
                               replay_entry("entry_bad_start", ava::session::EntryType::SessionStart,
                                            "{\"mode\":\"build\",\"model\":\"gpt-5.5\"}"));
  expect(!validation.ok(), "session_start rejects missing provider metadata");
  expect(has_issue(validation, ava::session::SessionReplayIssueKind::InvalidModelEntry),
         "session_start records invalid model issue");

  ava::session::SessionReplayValidation transition_validation;
  ava::session::SessionReplayModelState active{.provider_id = "openai", .model_id = "gpt-5.5"};
  validate_model_change_entry(transition_validation, active, 1,
                              replay_entry("entry_bad_change", ava::session::EntryType::ModelChange,
                                           "{\"previous_provider\":\"other\",\"previous_model\":\"gpt-4.1\","
                                           "\"provider\":\"openai\",\"model\":\"gpt-5.5-mini\"}"));
  expect(!transition_validation.ok(), "model_change rejects transitions that do not match active state");
  expect(has_issue(transition_validation, ava::session::SessionReplayIssueKind::InvalidModelEntry),
         "model_change records invalid model issue");
}

void test_reasoning_metadata_validation()
{
  ava::session::SessionReplayValidation validation;
  ava::session::SessionReplayModelState active{.provider_id = "openai", .model_id = "gpt-5.5"};

  validate_reasoning_change_entry(
      validation, active, 0,
      replay_entry("entry_reasoning", ava::session::EntryType::ReasoningChange,
                   "{\"provider\":\"openai\",\"model\":\"gpt-5.5\",\"enabled\":true,\"level\":\"medium\"}"));
  validate_reasoning_block_entry(
      validation, 1,
      replay_entry("entry_reasoning_block", ava::session::EntryType::ReasoningBlock,
                   "{\"provider\":\"openai\",\"model\":\"gpt-5.5\",\"text\":\"brief note\"}"));
  expect(validation.ok(), "reasoning change and block accept valid semantic metadata");

  ava::session::SessionReplayValidation mismatch_validation;
  validate_reasoning_change_entry(
      mismatch_validation, active, 2,
      replay_entry("entry_reasoning_mismatch", ava::session::EntryType::ReasoningChange,
                   "{\"provider\":\"openai\",\"model\":\"gpt-5.5-mini\",\"enabled\":true,\"level\":\"medium\"}"));
  expect(!mismatch_validation.ok(), "reasoning_change rejects inactive model metadata");
  expect(has_issue(mismatch_validation, ava::session::SessionReplayIssueKind::InvalidReasoningEntry),
         "reasoning_change records invalid reasoning issue");

  ava::session::SessionReplayValidation missing_content_validation;
  validate_reasoning_block_entry(missing_content_validation, 3,
                                 replay_entry("entry_reasoning_missing", ava::session::EntryType::ReasoningBlock,
                                              "{\"provider\":\"openai\",\"model\":\"gpt-5.5\"}"));
  expect(!missing_content_validation.ok(), "reasoning_block rejects missing content metadata");
  expect(has_issue(missing_content_validation, ava::session::SessionReplayIssueKind::InvalidReasoningEntry),
         "reasoning_block records invalid reasoning issue");
}

}  // namespace

void run_session_validation_model_tests()
{
  test_session_start_and_model_change_update_active_model();
  test_invalid_model_metadata_adds_replay_issues();
  test_reasoning_metadata_validation();
}
