#include "sys.h"
#include "ava/app/command_format.h"
#include "ava/app/command_session_support_internal.h"
#include "ava/app/command_sessions.h"
#include "ava/app/runtime.h"
#include "ava/app/runtime/Session.h"
#include "ava/session/session_branch.h"
#include "ava/session/session_metadata.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ava::app {
using session_command_support::labels_text;
using session_command_support::owned_replacement_options;
using session_command_support::reopen_session;
using session_command_support::trim_ascii;

namespace {

ava::core::Result<runtime::Session> create_fresh_session(runtime::Session const& current)
{
  runtime::OpenOptions options;
  options.workspace_dir = current.workspace_dir();
  options.current_dir = current.current_dir();
  options.continue_last_session = false;
  options.sessionless = current.sessionless();
  options.mode = current.mode();
  options.tool_visibility = current.tool_visibility();
  options.paths = current.paths();
  options.subagent_coordinator = current.subagent_coordinator();
  options.subagent_delivery_manager = current.subagent_delivery_manager();
  options.session_title_coordinator = current.session_title_coordinator();
  return open_runtime_session(options);
}

ava::core::Result<CommandResult> run_branch_command(runtime::Session& session, std::string_view name, ava::session::SessionBranchMode mode)
{
  CommandResult result;
  result.handled = true;

  auto const source_session_id = session.store.session_id();
  if (session.sessionless())
  {
    add_output(result, "Cannot branch a sessionless session.");
    return result;
  }
  auto const trimmed_name = trim_ascii(name);
  auto recovered = session.store.recover_torn_tail(session.lease(), session.session_read_limits());
  if (!recovered)
    return std::unexpected(std::move(recovered.error()));
  auto staged_recovery = session.store.recover_incomplete_assistant_output_suffix(session.lease(), session.session_read_limits());
  if (!staged_recovery)
    return std::unexpected(std::move(staged_recovery.error()));
  auto branched = ava::session::create_session_branch(
      ava::session::SessionBranchOptions{.workspace_dir = session.workspace_dir(),
                                         .root_dir = session.paths().sessions_dir,
                                         .source_session_id = source_session_id,
                                         .branch_from_entry_id = {},
                                         .name = trimmed_name.empty() ? std::nullopt : std::optional<std::string>(trimmed_name),
                                         .labels = std::nullopt,
                                         .read_limits = session.session_read_limits(),
                                         .source_lease = &session.lease(),
                                         .mode = mode,
                                         .actor = "tui"});
  if (!branched)
    return std::unexpected(std::move(branched.error()));

  auto const created_session_id = branched->store.session_id();
  auto const branch_from_entry_id = branched->branch_from_entry_id;
  auto owned_options = owned_replacement_options(session);
  auto opened = open_owned_runtime_session(owned_options, branched->store, branched->lease, true);
  if (!opened)
  {
    auto error = std::move(opened.error());
    ava::session::rollback_created_session_with_context(branched->store, branched->lease, error);
    return std::unexpected(std::move(error));
  }
  opened->created = true;
  if (auto replaced = session.replace_with(std::move(*opened)); !replaced)
    return std::unexpected(std::move(replaced.error()));

  result.session_tree_changed = true;
  auto const mode_text = mode == ava::session::SessionBranchMode::Clone ? std::string("cloned") : std::string("forked");
  std::string output = mode_text + " session " + created_session_id + " from " + source_session_id;
  if (!branch_from_entry_id.empty())
    output += " at " + branch_from_entry_id;
  if (!trimmed_name.empty())
    output += " name=\"" + sanitize_inline_text(trimmed_name) + "\"";
  output += "\nswitched to " + session.store.session_id();
  add_output(result, std::move(output));
  return result;
}

}  // namespace

ava::core::Result<CommandResult> run_fork_command(runtime::Session& session, std::string_view name)
{
  return run_branch_command(session, name, ava::session::SessionBranchMode::Fork);
}

ava::core::Result<CommandResult> run_clone_command(runtime::Session& session, std::string_view name)
{
  return run_branch_command(session, name, ava::session::SessionBranchMode::Clone);
}

ava::core::Result<CommandResult> run_new_session_command(runtime::Session& session, std::string_view name)
{
  CommandResult result;
  result.handled = true;

  auto const previous_session_id = session.store.session_id();
  auto previous_session_title = std::string("Untitled session");
  if (auto metadata = session.load_runtime_metadata(); metadata && !metadata->effective_title().empty())
    previous_session_title = sanitize_inline_text(metadata->effective_title());

  auto const trimmed_name = trim_ascii(name);
  auto const created_session_title = trimmed_name.empty() ? std::string("Untitled session") : sanitize_inline_text(trimmed_name);
  auto opened = create_fresh_session(session);
  if (!opened)
    return std::unexpected(std::move(opened.error()));

  auto const created_session_id = opened->store.session_id();
  if (!trimmed_name.empty())
  {
    auto metadata =
        opened->append_runtime_session_metadata(ava::session::SessionMetadataUpdate{.name = std::optional<std::string>(trimmed_name), .actor = "tui"});
    if (!metadata)
      return std::unexpected(std::move(metadata.error()));
  }

  if (auto replaced = session.replace_with(std::move(*opened)); !replaced)
    return std::unexpected(std::move(replaced.error()));

  result.session_tree_changed = true;
  std::string output = "started session \"" + created_session_title + "\" · id " + created_session_id;
  output += "\nprevious session \"" + previous_session_title + "\" · id " + previous_session_id;
  output += "\nswitched to \"" + created_session_title + "\"";
  add_output(result, std::move(output));
  return result;
}

ava::core::Result<CommandResult> run_resume_command(runtime::Session& session, std::string_view session_id)
{
  CommandResult result;
  result.handled = true;

  auto const trimmed_session_id = trim_ascii(session_id);
  if (trimmed_session_id.empty())
  {
    add_output(result, missing_argument("/resume <id>"));
    return result;
  }

  auto opened = reopen_session(session, trimmed_session_id);
  if (!opened)
    return std::unexpected(std::move(opened.error()));

  if (auto replaced = session.replace_with(std::move(*opened)); !replaced)
    return std::unexpected(std::move(replaced.error()));
  add_output(result, "resumed session " + session.store.session_id());
  return result;
}

ava::core::Result<CommandResult> run_name_command(runtime::Session& session, std::string_view name)
{
  CommandResult result;
  result.handled = true;

  auto const trimmed_name = trim_ascii(name);
  if (trimmed_name.empty())
  {
    add_output(result, missing_argument("/name <name|--clear>"));
    return result;
  }

  auto const clear_name = trimmed_name == "--clear";
  ava::session::SessionMetadataUpdate update;
  update.name = clear_name ? std::optional<std::string>(std::string{}) : std::optional<std::string>(trimmed_name);
  update.actor = "tui";
  auto metadata = session.append_runtime_session_metadata(std::move(update));
  if (!metadata)
    return std::unexpected(std::move(metadata.error()));

  result.session_tree_changed = true;
  if (clear_name)
    add_output(result, "session name cleared");
  else
    add_output(result, "session name set: \"" + sanitize_inline_text(metadata->name) + "\"");
  return result;
}

ava::core::Result<CommandResult> run_labels_command(runtime::Session& session, std::string_view labels)
{
  CommandResult result;
  result.handled = true;

  auto const parts = split_command_arguments(labels);
  if (parts.empty())
  {
    auto metadata = session.load_runtime_metadata();
    if (!metadata)
      return std::unexpected(std::move(metadata.error()));
    auto text = metadata->labels.empty() ? std::string("session labels: <none>") : std::string("session labels: ") + labels_text(metadata->labels);
    text += "\nusage: /labels <label> [label...] | /labels --clear";
    add_output(result, std::move(text));
    return result;
  }

  if (std::ranges::find(parts, "--clear") != parts.end() && parts.size() != 1)
  {
    add_output(result, missing_argument("/labels <label> [label...] | /labels --clear"));
    return result;
  }

  auto next_labels = parts.size() == 1 && parts[0] == "--clear" ? std::vector<std::string>{} : parts;
  ava::session::SessionMetadataUpdate update;
  update.labels = next_labels;
  update.actor = "tui";
  auto metadata = session.append_runtime_session_metadata(std::move(update));
  if (!metadata)
    return std::unexpected(std::move(metadata.error()));

  result.session_tree_changed = true;
  if (metadata->labels.empty())
    add_output(result, "session labels cleared");
  else
    add_output(result, "session labels set: " + sanitize_inline_text(labels_text(metadata->labels)));
  return result;
}

ava::core::Result<CommandResult> run_mode_command(runtime::Session& session)
{
  CommandResult result;
  result.handled = true;
  auto const new_mode = ava::agent::toggle_mode(session.mode());
  auto prompt_state = select_runtime_prompt_state(session, new_mode);
  if (!prompt_state)
    return std::unexpected(std::move(prompt_state.error()));
  if (auto appended = append_runtime_mode_change(session, new_mode); !appended)
  {
    return std::unexpected(std::move(appended.error()));
  }
  if (auto refreshed = apply_runtime_prompt_state(session, std::move(*prompt_state)); !refreshed)
    return std::unexpected(std::move(refreshed.error()));
  add_output(result, "mode switched to " + ava::agent::to_string(session.mode()));
  return result;
}

}  // namespace ava::app
