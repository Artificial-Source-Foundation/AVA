#include "ava/session/validation_model.h"

#include "ava/core/json.h"
#include "ava/session/validation_fields.h"
#include "ava/session/validation_issue.h"

namespace ava::session {

void validate_session_start_entry(SessionReplayValidation& validation, SessionReplayModelState& active_model,
                                  std::size_t index, SessionEntry const& entry)
{
  if (!ava::core::json::is_valid_object(entry.data_json)) {
    add_replay_error(validation, SessionReplayIssueKind::InvalidModelEntry, index, entry, "",
                     "session_start entry data is not valid JSON");
    return;
  }

  auto const mode = ava::core::json::string_field(entry.data_json, "mode").value_or("");
  auto const provider = ava::core::json::string_field(entry.data_json, "provider").value_or("");
  auto const model = ava::core::json::string_field(entry.data_json, "model").value_or("");
  if (!valid_mode(mode) || provider.empty() || model.empty() ||
      !present_integer_matching(entry.data_json, "context_sources", false) ||
      !present_integer_matching(entry.data_json, "context_window_tokens", true) ||
      !present_integer_matching(entry.data_json, "max_output_tokens", true) ||
      !present_boolean(entry.data_json, "prompt_override") || !present_boolean(entry.data_json, "supports_tools") ||
      !present_boolean(entry.data_json, "supports_streaming") ||
      !present_boolean(entry.data_json, "supports_reasoning") || !present_boolean(entry.data_json, "reports_usage")) {
    add_replay_error(validation, SessionReplayIssueKind::InvalidModelEntry, index, entry, "",
                     "session_start entry is missing required model/session metadata");
    return;
  }

  active_model.provider_id = provider;
  active_model.model_id = model;
}

void validate_model_change_entry(SessionReplayValidation& validation, SessionReplayModelState& active_model,
                                 std::size_t index, SessionEntry const& entry)
{
  if (!ava::core::json::is_valid_object(entry.data_json)) {
    add_replay_error(validation, SessionReplayIssueKind::InvalidModelEntry, index, entry, "",
                     "model_change entry data is not valid JSON");
    return;
  }

  auto const previous_provider = ava::core::json::string_field(entry.data_json, "previous_provider").value_or("");
  auto const previous_model = ava::core::json::string_field(entry.data_json, "previous_model").value_or("");
  auto const provider = ava::core::json::string_field(entry.data_json, "provider").value_or("");
  auto const model = ava::core::json::string_field(entry.data_json, "model").value_or("");
  if (previous_provider.empty() || previous_model.empty() || provider.empty() || model.empty() ||
      (previous_provider == provider && previous_model == model) ||
      (!active_model.provider_id.empty() &&
       (previous_provider != active_model.provider_id || previous_model != active_model.model_id)) ||
      !present_integer_matching(entry.data_json, "context_window_tokens", true) ||
      !present_integer_matching(entry.data_json, "max_output_tokens", true) ||
      !present_boolean(entry.data_json, "supports_tools") || !present_boolean(entry.data_json, "supports_streaming") ||
      !present_boolean(entry.data_json, "supports_reasoning") || !present_boolean(entry.data_json, "reports_usage")) {
    add_replay_error(validation, SessionReplayIssueKind::InvalidModelEntry, index, entry, "",
                     "model_change entry is missing required provider/model transition metadata");
    return;
  }

  active_model.provider_id = provider;
  active_model.model_id = model;
}

void validate_reasoning_change_entry(SessionReplayValidation& validation, SessionReplayModelState const& active_model,
                                     std::size_t index, SessionEntry const& entry)
{
  if (!ava::core::json::is_valid_object(entry.data_json)) {
    add_replay_error(validation, SessionReplayIssueKind::InvalidReasoningEntry, index, entry, "",
                     "reasoning_change entry data is not valid JSON");
    return;
  }

  auto const provider = ava::core::json::string_field(entry.data_json, "provider").value_or("");
  auto const model = ava::core::json::string_field(entry.data_json, "model").value_or("");
  if (provider.empty() || model.empty() || !required_boolean(entry.data_json, "enabled") ||
      !present_non_empty_string(entry.data_json, "format") ||
      !present_integer_matching(entry.data_json, "budget_tokens", true) ||
      !present_non_empty_string(entry.data_json, "display")) {
    add_replay_error(validation, SessionReplayIssueKind::InvalidReasoningEntry, index, entry, "",
                     "reasoning_change entry is missing required semantic fields");
    return;
  }

  if (!active_model.provider_id.empty() && (provider != active_model.provider_id || model != active_model.model_id)) {
    add_replay_error(validation, SessionReplayIssueKind::InvalidReasoningEntry, index, entry, "",
                     "reasoning_change provider/model does not match active session model");
    return;
  }

  auto const enabled = bool_field_is_true(entry.data_json, "enabled");
  auto const level = ava::core::json::string_field(entry.data_json, "level").value_or("");
  if (enabled && level.empty()) {
    add_replay_error(validation, SessionReplayIssueKind::InvalidReasoningEntry, index, entry, "",
                     "enabled reasoning_change entry is missing level");
  }
}

void validate_reasoning_block_entry(SessionReplayValidation& validation, std::size_t index, SessionEntry const& entry)
{
  if (!ava::core::json::is_valid_object(entry.data_json)) {
    add_replay_error(validation, SessionReplayIssueKind::InvalidReasoningEntry, index, entry, "",
                     "reasoning_block entry data is not valid JSON");
    return;
  }

  auto const provider = ava::core::json::string_field(entry.data_json, "provider").value_or("");
  auto const model = ava::core::json::string_field(entry.data_json, "model").value_or("");
  auto const text = ava::core::json::string_field(entry.data_json, "text").value_or("");
  auto const signature = ava::core::json::string_field(entry.data_json, "signature").value_or("");
  auto const redacted_data = ava::core::json::string_field(entry.data_json, "redacted_data").value_or("");
  if (provider.empty() || model.empty() || !present_non_empty_string(entry.data_json, "format") ||
      !present_boolean(entry.data_json, "redacted") || (text.empty() && signature.empty() && redacted_data.empty())) {
    add_replay_error(validation, SessionReplayIssueKind::InvalidReasoningEntry, index, entry, "",
                     "reasoning_block entry is missing provider/model/content metadata");
  }
}

}  // namespace ava::session
