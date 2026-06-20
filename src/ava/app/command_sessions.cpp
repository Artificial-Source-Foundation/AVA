#include "ava/app/command_sessions.h"

#include "ava/app/command_format.h"

#include "ava/session/compaction.h"
#include "ava/session/export.h"
#include "ava/session/stats.h"
#include "ava/session/session_branch.h"
#include "ava/session/session_metadata.h"
#include "ava/session/session_tree.h"

#include "ava/context/context_loader.h"

#include "ava/core/ids.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ava::app {
namespace {

ava::core::VoidResult append_mode_change(ava::session::SessionStore& store, ava::agent::Mode mode)
{
  return store.append(ava::session::SessionEntry{
      .id = ava::core::make_id("entry"),
      .parent_id = "",
      .type = ava::session::EntryType::ModeChange,
      .timestamp = ava::session::now_timestamp(),
      .data_json = "{\"mode\":\"" + ava::agent::to_string(mode) + "\"}",
  });
}

std::string format_cost_usd(long double value)
{
  std::ostringstream output;
  output << '$' << std::fixed << std::setprecision(6) << value;
  return output.str();
}

std::string trim_ascii(std::string_view text)
{
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) text.remove_prefix(1);
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) text.remove_suffix(1);
  return std::string(text);
}

std::string lower_ascii(std::string_view text)
{
  std::string lowered(text);
  std::ranges::transform(lowered, lowered.begin(),
                         [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return lowered;
}

bool contains_ascii_case_insensitive(std::string_view text, std::string_view query)
{
  if (query.empty()) return true;
  return lower_ascii(text).find(lower_ascii(query)) != std::string::npos;
}

std::string labels_text(std::vector<std::string> const& labels)
{
  std::string text;
  for (std::size_t index = 0; index < labels.size(); ++index)
  {
    if (index > 0) text += ",";
    text += labels[index];
  }
  return text;
}

bool metadata_labels_match(std::vector<std::string> const& labels, std::string_view query)
{
  return std::ranges::any_of(labels, [&](std::string const& label) {
    return contains_ascii_case_insensitive(label, query);
  });
}

bool session_matches_query(ava::session::SessionTreeNode const& node, std::string_view query)
{
  return contains_ascii_case_insensitive(node.summary.session_id, query) ||
         contains_ascii_case_insensitive(node.summary.last_updated, query) ||
         contains_ascii_case_insensitive(node.summary.path.generic_string(), query) ||
         contains_ascii_case_insensitive(node.metadata.name, query) ||
         contains_ascii_case_insensitive(node.metadata.parent_session_id, query) ||
         contains_ascii_case_insensitive(node.metadata.source_session_id, query) ||
         contains_ascii_case_insensitive(node.metadata.branch_from_entry_id, query) ||
         contains_ascii_case_insensitive(node.metadata.branch_origin, query) ||
         (node.metadata.archived && contains_ascii_case_insensitive("archived", query)) ||
         contains_ascii_case_insensitive(node.metadata.actor, query) ||
         metadata_labels_match(node.metadata.labels, query);
}

bool context_source_matches_query(ContextSourceMetadata const& source, std::string_view query)
{
  return contains_ascii_case_insensitive(source.path.generic_string(), query) ||
         contains_ascii_case_insensitive(ava::context::to_string(source.source_type), query);
}

RuntimeEvent base_command_event(RuntimeSession const& session, RuntimeEventType type)
{
  RuntimeEvent event;
  event.type = type;
  event.timestamp = ava::session::now_timestamp();
  event.session_id = session.store.session_id();
  event.mode = session.mode;
  event.provider_id = session.model.provider_id;
  event.model_id = session.model.model_id;
  return event;
}

ava::core::VoidResult emit_command_event(CommandRequest const& request, RuntimeEvent event)
{
  if (!request.event_sink) return {};
  return emit_event(request.event_sink, event);
}

bool command_canceled(CommandRequest const& request)
{
  return request.cancel_requested && request.cancel_requested();
}

ava::core::Error command_canceled_error()
{
  return ava::core::Error(ava::core::ErrorCategory::Unknown, "agent loop canceled");
}

template <typename Value>
void append_known_value(std::ostringstream& output, bool& wrote_any, std::string_view label,
                        std::optional<Value> const& value)
{
  if (!value) return;
  if (wrote_any) output << ' ';
  output << label << '=' << *value;
  wrote_any = true;
}

std::string shorten_middle(std::string text, std::size_t max_columns)
{
  if (text.size() <= max_columns || max_columns < 8) return text;
  auto const front = (max_columns - 3) / 2;
  auto const back = max_columns - 3 - front;
  return text.substr(0, front) + "..." + text.substr(text.size() - back);
}

std::string compact_workspace_label(std::filesystem::path const& workspace)
{
  auto const filename = workspace.filename().generic_string();
  if (!filename.empty()) return shorten_middle(filename, 32);
  return shorten_middle(workspace.generic_string(), 48);
}

std::string compact_cwd_label(std::filesystem::path const& cwd, std::filesystem::path const& workspace)
{
  auto text = display_path(cwd, workspace);
  if (text.empty()) text = ".";
  return shorten_middle(std::move(text), 48);
}

std::unordered_map<std::string, std::size_t> tree_index_by_id(std::vector<ava::session::SessionTreeNode> const& nodes)
{
  std::unordered_map<std::string, std::size_t> index;
  for (std::size_t node_index = 0; node_index < nodes.size(); ++node_index)
  {
    index.emplace(nodes[node_index].summary.session_id, node_index);
  }
  return index;
}

std::string format_session_tree_line(ava::session::SessionTreeNode const& node, std::size_t depth, bool current_path)
{
  std::string output(depth * 2, ' ');
  output += node.current ? "* " : "- ";
  if (!node.metadata.name.empty())
  {
    output += sanitize_inline_text(node.metadata.name);
    output += "  id=" + sanitize_inline_text(node.summary.session_id);
  }
  else
  {
    output += sanitize_inline_text(node.summary.session_id);
  }
  output += "  entries=" + std::to_string(node.summary.entry_count);
  if (!node.summary.last_updated.empty())
    output += "  updated=" + sanitize_inline_text(node.summary.last_updated);
  if (!node.metadata.branch_origin.empty())
    output += "  origin=" + sanitize_inline_text(node.metadata.branch_origin);
  if (node.metadata.archived)
    output += "  archived";
  if (!node.metadata.parent_session_id.empty())
    output += "  parent=" + sanitize_inline_text(shorten_middle(node.metadata.parent_session_id, 24));
  if (!node.metadata.branch_from_entry_id.empty())
    output += "  from=" + sanitize_inline_text(shorten_middle(node.metadata.branch_from_entry_id, 24));
  if (!node.metadata.labels.empty())
    output += "  labels=" + sanitize_inline_text(labels_text(node.metadata.labels));
  if (current_path && !node.current)
    output += "  current_path";
  return output;
}

ava::core::Result<RuntimeSession> reopen_session(RuntimeSession const& current, std::string_view session_id)
{
  RuntimeOpenOptions options;
  options.workspace_dir = current.workspace_dir;
  options.current_dir = current.current_dir;
  options.requested_session_id = std::string(session_id);
  options.continue_last_session = false;
  options.mode = current.mode;
  options.paths = current.paths;
  return open_runtime_session(options);
}

ava::core::Result<RuntimeSession> create_fresh_session(RuntimeSession const& current)
{
  RuntimeOpenOptions options;
  options.workspace_dir = current.workspace_dir;
  options.current_dir = current.current_dir;
  options.continue_last_session = false;
  options.mode = current.mode;
  options.paths = current.paths;
  return open_runtime_session(options);
}

ava::core::Result<CommandResult> run_branch_command(RuntimeSession& session, std::string_view name,
                                                    ava::session::SessionBranchMode mode)
{
  CommandResult result;
  result.handled = true;

  auto const source_session_id = session.store.session_id();
  auto const trimmed_name = trim_ascii(name);
  auto branched = ava::session::create_session_branch(ava::session::SessionBranchOptions{
      .workspace_dir = session.workspace_dir,
      .root_dir = session.paths.sessions_dir,
      .source_session_id = source_session_id,
      .branch_from_entry_id = {},
      .name = trimmed_name.empty() ? std::nullopt : std::optional<std::string>(trimmed_name),
      .labels = std::nullopt,
      .mode = mode,
      .actor = "tui"});
  if (!branched)
    return std::unexpected(std::move(branched.error()));

  auto const created_session_id = branched->store.session_id();
  auto const branch_from_entry_id = branched->branch_from_entry_id;
  auto opened = reopen_session(session, created_session_id);
  if (!opened)
    return std::unexpected(std::move(opened.error()));
  opened->created = true;
  session = std::move(*opened);

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

ava::core::Result<CommandResult> run_sessions_rename_command(RuntimeSession& session, std::string_view arguments)
{
  CommandResult result;
  result.handled = true;

  auto rest = std::string_view(arguments);
  auto const subcommand = std::string_view("rename");
  if (rest.starts_with(subcommand))
    rest.remove_prefix(subcommand.size());
  while (!rest.empty() && std::isspace(static_cast<unsigned char>(rest.front())) != 0) rest.remove_prefix(1);
  auto const id_end = rest.find_first_of(" \t\r\n");
  auto const requested_session_id = id_end == std::string_view::npos ? rest : rest.substr(0, id_end);
  if (requested_session_id.empty())
  {
    add_output(result, missing_argument("/sessions rename <id> <name|--clear>"));
    return result;
  }
  rest = id_end == std::string_view::npos ? std::string_view{} : rest.substr(id_end);
  auto const trimmed_name = trim_ascii(rest);
  if (trimmed_name.empty())
  {
    add_output(result, missing_argument("/sessions rename <id> <name|--clear>"));
    return result;
  }

  auto target = reopen_session(session, requested_session_id);
  if (!target)
    return std::unexpected(std::move(target.error()));

  auto const clear_name = trimmed_name == "--clear";
  ava::session::SessionMetadataUpdate update;
  update.name = clear_name ? std::optional<std::string>(std::string{}) : std::optional<std::string>(trimmed_name);
  update.actor = "tui";
  auto metadata = ava::session::append_session_metadata(target->store, std::move(update));
  if (!metadata)
    return std::unexpected(std::move(metadata.error()));

  if (clear_name)
    add_output(result, "session " + target->store.session_id() + " name cleared");
  else
    add_output(result, "session " + target->store.session_id() + " name set: \"" +
                           sanitize_inline_text(metadata->name) + "\"");
  return result;
}

ava::core::Result<CommandResult> run_sessions_labels_command(RuntimeSession& session, std::string_view arguments)
{
  CommandResult result;
  result.handled = true;

  auto rest = std::string_view(arguments);
  if (rest.starts_with("labels"))
    rest.remove_prefix(std::string_view("labels").size());
  else if (rest.starts_with("label"))
    rest.remove_prefix(std::string_view("label").size());

  auto const parts = split_command_arguments(rest);
  if (parts.empty())
  {
    add_output(result, missing_argument("/sessions labels <id> <label...|--clear>"));
    return result;
  }

  auto const requested_session_id = parts.front();
  auto target = reopen_session(session, requested_session_id);
  if (!target)
    return std::unexpected(std::move(target.error()));

  if (parts.size() == 1)
  {
    auto metadata = ava::session::load_session_metadata(target->store);
    if (!metadata)
      return std::unexpected(std::move(metadata.error()));
    auto text = metadata->labels.empty() ? std::string("session " + target->store.session_id() + " labels: <none>")
                                         : std::string("session " + target->store.session_id() + " labels: " + labels_text(metadata->labels));
    text += "\nusage: /sessions labels <id> <label...|--clear>";
    add_output(result, std::move(text));
    return result;
  }

  std::vector<std::string> label_parts(parts.begin() + 1, parts.end());
  if (std::ranges::find(label_parts, "--clear") != label_parts.end() && label_parts.size() != 1)
  {
    add_output(result, missing_argument("/sessions labels <id> <label...|--clear>"));
    return result;
  }

  auto next_labels = label_parts.size() == 1 && label_parts[0] == "--clear" ? std::vector<std::string>{} : label_parts;
  ava::session::SessionMetadataUpdate update;
  update.labels = next_labels;
  update.actor = "tui";
  auto metadata = ava::session::append_session_metadata(target->store, std::move(update));
  if (!metadata)
    return std::unexpected(std::move(metadata.error()));

  if (metadata->labels.empty())
    add_output(result, "session " + target->store.session_id() + " labels cleared");
  else
    add_output(result, "session " + target->store.session_id() + " labels set: " +
                           sanitize_inline_text(labels_text(metadata->labels)));
  return result;
}

ava::core::Result<CommandResult> run_sessions_archive_command(RuntimeSession& session, std::string_view arguments,
                                                              bool archived)
{
  CommandResult result;
  result.handled = true;

  auto rest = std::string_view(arguments);
  auto const subcommand = archived ? std::string_view("archive") : std::string_view("unarchive");
  if (rest.starts_with(subcommand))
    rest.remove_prefix(subcommand.size());
  auto const parts = split_command_arguments(rest);
  if (parts.empty())
  {
    add_output(result, missing_argument(archived ? "/sessions archive <id> --confirm" : "/sessions unarchive <id>"));
    return result;
  }
  auto const requested_session_id = parts.front();
  if (requested_session_id == session.store.session_id())
  {
    add_output(result, "Cannot archive the active session. Switch sessions first.");
    return result;
  }
  if (archived && std::ranges::find(parts, "--confirm") == parts.end())
  {
    add_output(result,
               "Archive hides a session from the default selector and /sessions view but keeps its JSONL file intact.\n"
               "Run /sessions archive " +
                   sanitize_inline_text(requested_session_id) + " --confirm to archive it.");
    return result;
  }

  auto target = reopen_session(session, requested_session_id);
  if (!target)
    return std::unexpected(std::move(target.error()));

  auto current_metadata = ava::session::load_session_metadata(target->store);
  if (!current_metadata)
    return std::unexpected(std::move(current_metadata.error()));
  if (current_metadata->archived == archived)
  {
    add_output(result, "session " + target->store.session_id() + (archived ? " already archived" : " is not archived"));
    return result;
  }

  ava::session::SessionMetadataUpdate update;
  update.archived = archived;
  update.actor = "tui";
  auto metadata = ava::session::append_session_metadata(target->store, std::move(update));
  if (!metadata)
    return std::unexpected(std::move(metadata.error()));

  add_output(result, "session " + target->store.session_id() + (metadata->archived ? " archived" : " unarchived"));
  return result;
}

void append_session_tree_lines(std::string& output, std::vector<ava::session::SessionTreeNode> const& nodes,
                               std::unordered_map<std::string, std::size_t> const& index_by_id,
                               std::vector<std::string> const& ids, std::vector<std::string> const& current_path,
                               std::string_view query, std::size_t depth, bool include_archived, bool& wrote_any)
{
  for (auto const& id : ids)
  {
    auto const found = index_by_id.find(id);
    if (found == index_by_id.end())
      continue;
    auto const& node = nodes[found->second];
    auto const query_match = session_matches_query(node, query);
    auto const current_path_node = std::ranges::find(current_path, node.summary.session_id) != current_path.end();
    auto const visible = include_archived || !node.metadata.archived;
    if (visible && (query.empty() || query_match))
    {
      output += format_session_tree_line(node, depth, current_path_node);
      output += '\n';
      wrote_any = true;
    }
    append_session_tree_lines(output, nodes, index_by_id, node.children, current_path, query,
                              depth + (visible ? 1 : 0), include_archived, wrote_any);
  }
}

std::string known_values_text(ava::session::SessionStats const& stats)
{
  std::ostringstream output;
  bool wrote_any = false;
  append_known_value(output, wrote_any, "input", stats.input_tokens);
  append_known_value(output, wrote_any, "output", stats.output_tokens);
  append_known_value(output, wrote_any, "reasoning", stats.reasoning_tokens);
  append_known_value(output, wrote_any, "cache_read", stats.cache_read_tokens);
  append_known_value(output, wrote_any, "cache_write", stats.cache_write_tokens);
  append_known_value(output, wrote_any, "total", stats.total_tokens);
  return wrote_any ? output.str() : std::string("unavailable");
}

std::string estimated_bytes_text(ava::session::SessionStats const& stats)
{
  std::ostringstream output;
  bool wrote_any = false;
  append_known_value(output, wrote_any, "input", stats.estimated_input_bytes);
  append_known_value(output, wrote_any, "output", stats.estimated_output_bytes);
  append_known_value(output, wrote_any, "total", stats.estimated_total_bytes);
  return wrote_any ? output.str() : std::string("unavailable");
}

std::string cost_text(ava::session::SessionStats const& stats)
{
  if (stats.cost_complete) return stats.total_cost_usd ? format_cost_usd(*stats.total_cost_usd) : "unavailable";
  if (stats.known_cost_usd) {
    return "at least " + format_cost_usd(*stats.known_cost_usd) + " (" + std::to_string(stats.unknown_cost_entries) +
           " unknown)";
  }
  return "incomplete (" + std::to_string(stats.unknown_cost_entries) + " unknown)";
}

std::string format_session_stats_text(RuntimeSession const& session, ava::session::SessionStats const& stats)
{
  std::ostringstream output;
  output << "Session stats\n";
  output << "  session: " << shorten_middle(session.store.session_id(), 32) << "   entries: " << stats.entry_count
         << '\n';
  output << "  model: " << session.model.provider_id << '/' << session.model.model_id
         << "   mode: " << ava::agent::to_string(session.mode) << '\n';
  output << "  workspace: " << compact_workspace_label(session.workspace_dir)
         << "   cwd: " << compact_cwd_label(session.current_dir, session.workspace_dir) << '\n';
  if (!stats.first_timestamp.empty() || !stats.last_timestamp.empty()) {
    output << "  time: " << (stats.first_timestamp.empty() ? "unknown" : stats.first_timestamp) << " -> "
           << (stats.last_timestamp.empty() ? "unknown" : stats.last_timestamp) << '\n';
  }

  output << "\nMessages:\n";
  output << "  user " << stats.counts.user_message << "   assistant " << stats.counts.assistant_message << "   tools "
         << stats.counts.tool_call << '/' << stats.counts.tool_result << "   permissions "
         << stats.counts.permission_decision << '\n';
  output << "  compactions " << stats.counts.compaction << "   mode/model " << stats.counts.mode_change << '/'
         << stats.counts.model_change << "   errors/cancels " << stats.counts.error << '/' << stats.counts.cancel
         << '\n';

  output << "\nUsage:\n";
  output << "  tokens: " << known_values_text(stats) << '\n';
  output << "  est bytes: " << estimated_bytes_text(stats) << '\n';
  output << "  cost: " << cost_text(stats) << "   usage entries exact/estimated " << stats.exact_usage_entries << '/'
         << stats.estimated_usage_entries << '\n';

  output << "\nHints:\n";
  output << "  export: /export   resume: ava --session " << session.store.session_id();
  return output.str();
}

}  // namespace

ava::core::Result<CommandResult> run_sessions_command(RuntimeSession& session, std::string_view query)
{
  CommandResult result;
  result.handled = true;
  auto const trimmed_query = trim_ascii(query);
  auto const query_parts = split_command_arguments(trimmed_query);
  if (!query_parts.empty() && query_parts.front() == "rename")
  {
    return run_sessions_rename_command(session, trimmed_query);
  }
  if (!query_parts.empty() && (query_parts.front() == "labels" || query_parts.front() == "label"))
  {
    return run_sessions_labels_command(session, trimmed_query);
  }
  if (!query_parts.empty() && query_parts.front() == "archive")
  {
    return run_sessions_archive_command(session, trimmed_query, true);
  }
  if (!query_parts.empty() && query_parts.front() == "unarchive")
  {
    return run_sessions_archive_command(session, trimmed_query, false);
  }
  auto list_query = trimmed_query;
  bool include_archived = false;
  if (!query_parts.empty() && query_parts.front() == "--archived")
  {
    include_archived = true;
    list_query = trimmed_query.substr(std::string_view("--archived").size());
    list_query = trim_ascii(list_query);
  }
  auto tree = ava::session::build_session_tree(session.workspace_dir, session.paths.sessions_dir, session.store.session_id());
  if (!tree) {
    add_output(result, tree.error().format());
    return result;
  }
  if (tree->sessions.empty()) {
    add_output(result, "No sessions for this workspace.");
    return result;
  }
  std::string output;
  output += include_archived ? "Sessions (including archived):\n" : "Sessions:\n";
  auto const index_by_id = tree_index_by_id(tree->sessions);
  bool wrote_any = false;
  append_session_tree_lines(output, tree->sessions, index_by_id, tree->roots, tree->current_path, list_query, 0,
                            include_archived, wrote_any);
  if (output.empty() && !list_query.empty()) {
    add_output(result, "No sessions matching: " + sanitize_inline_text(list_query));
    return result;
  }
  if (!wrote_any && !list_query.empty()) {
    add_output(result, "No sessions matching: " + sanitize_inline_text(list_query));
    return result;
  }
  if (!wrote_any && !include_archived) {
    add_output(result, "No active sessions for this workspace. Use /sessions --archived to include archived sessions.");
    return result;
  }
  add_output(result, std::move(output));
  return result;
}

ava::core::Result<CommandResult> run_fork_command(RuntimeSession& session, std::string_view name)
{
  return run_branch_command(session, name, ava::session::SessionBranchMode::Fork);
}

ava::core::Result<CommandResult> run_clone_command(RuntimeSession& session, std::string_view name)
{
  return run_branch_command(session, name, ava::session::SessionBranchMode::Clone);
}

ava::core::Result<CommandResult> run_new_session_command(RuntimeSession& session, std::string_view name)
{
  CommandResult result;
  result.handled = true;

  auto const previous_session_id = session.store.session_id();
  auto const trimmed_name = trim_ascii(name);
  auto opened = create_fresh_session(session);
  if (!opened)
    return std::unexpected(std::move(opened.error()));

  auto const created_session_id = opened->store.session_id();
  if (!trimmed_name.empty())
  {
    auto metadata = ava::session::append_session_metadata(
        opened->store, ava::session::SessionMetadataUpdate{.name = std::optional<std::string>(trimmed_name),
                                                           .actor = "tui"});
    if (!metadata)
      return std::unexpected(std::move(metadata.error()));
  }

  session = std::move(*opened);

  std::string output = "started session " + created_session_id;
  if (!trimmed_name.empty())
    output += " name=\"" + sanitize_inline_text(trimmed_name) + "\"";
  output += "\nprevious session " + previous_session_id;
  output += "\nswitched to " + session.store.session_id();
  add_output(result, std::move(output));
  return result;
}

ava::core::Result<CommandResult> run_resume_command(RuntimeSession& session, std::string_view session_id)
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

  session = std::move(*opened);
  add_output(result, "resumed session " + session.store.session_id());
  return result;
}

ava::core::Result<CommandResult> run_name_command(RuntimeSession& session, std::string_view name)
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
  auto metadata = ava::session::append_session_metadata(session.store, std::move(update));
  if (!metadata)
    return std::unexpected(std::move(metadata.error()));

  if (clear_name)
    add_output(result, "session name cleared");
  else
    add_output(result, "session name set: \"" + sanitize_inline_text(metadata->name) + "\"");
  return result;
}

ava::core::Result<CommandResult> run_labels_command(RuntimeSession& session, std::string_view labels)
{
  CommandResult result;
  result.handled = true;

  auto const parts = split_command_arguments(labels);
  if (parts.empty())
  {
    auto metadata = ava::session::load_session_metadata(session.store);
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
  auto metadata = ava::session::append_session_metadata(session.store, std::move(update));
  if (!metadata)
    return std::unexpected(std::move(metadata.error()));

  if (metadata->labels.empty())
    add_output(result, "session labels cleared");
  else
    add_output(result, "session labels set: " + sanitize_inline_text(labels_text(metadata->labels)));
  return result;
}

ava::core::Result<CommandResult> run_mode_command(RuntimeSession& session)
{
  CommandResult result;
  result.handled = true;
  auto const new_mode = ava::agent::toggle_mode(session.mode);
  auto prompt_state = select_runtime_prompt_state(session, new_mode);
  if (!prompt_state) return std::unexpected(std::move(prompt_state.error()));
  if (auto appended = append_mode_change(session.store, new_mode); !appended) {
    return std::unexpected(std::move(appended.error()));
  }
  apply_runtime_prompt_state(session, std::move(*prompt_state));
  add_output(result, "mode switched to " + ava::agent::to_string(session.mode));
  return result;
}

ava::core::Result<CommandResult> run_context_command(RuntimeSession& session, std::string_view query)
{
  CommandResult result;
  result.handled = true;
  auto const trimmed_query = trim_ascii(query);
  if (session.context_sources.empty()) {
    add_output(result, "No context sources loaded.");
    return result;
  }
  std::string output;
  for (auto const& source : session.context_sources) {
    if (!context_source_matches_query(source, trimmed_query)) continue;
    output += ava::context::to_string(source.source_type) + "  " + source.path.string() +
              "  bytes=" + std::to_string(source.byte_count) + '\n';
  }
  if (output.empty() && !trimmed_query.empty()) {
    add_output(result, "No context sources matching: " + sanitize_inline_text(trimmed_query));
    return result;
  }
  add_output(result, std::move(output));
  return result;
}

ava::core::Result<CommandResult> run_stats_command(RuntimeSession& session)
{
  CommandResult result;
  result.handled = true;
  auto entries = session.store.load();
  if (!entries) {
    add_output(result, entries.error().format());
    return result;
  }
  add_output(result, format_session_stats_text(session, ava::session::compute_session_stats(*entries)));
  return result;
}

ava::core::Result<CommandResult> run_compact_command(RuntimeSession& session, CommandRequest const& request)
{
  CommandResult result;
  result.handled = true;
  auto fail_compaction = [&](ava::core::Error error) -> ava::core::Result<CommandResult> {
    if (request.propagate_compaction_errors) return std::unexpected(std::move(error));
    add_output(result, error.format());
    return result;
  };
  auto const instructions = command_argument(request.command, "/compact");
  if (!request.compaction_summary_generator) {
    return fail_compaction(ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                            "/compact requires provider-backed summary generation"));
  }
  auto config = ava::session::load_compaction_config(session.paths);
  if (!config) {
    return fail_compaction(std::move(config.error()));
  }

  constexpr std::size_t max_compaction_attempts = 2;
  std::size_t last_snapshot_entries = 0;
  std::size_t last_current_entries = 0;
  for (std::size_t attempt = 0; attempt < max_compaction_attempts; ++attempt) {
    if (command_canceled(request)) return fail_compaction(command_canceled_error());
    ava::core::Result<std::vector<ava::session::SessionEntry>> entries =
        std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "session entries were not loaded"));
    if (request.session_mutex) {
      std::lock_guard lock(*request.session_mutex);
      entries = session.store.load();
    } else {
      entries = session.store.load();
    }
    if (!entries) {
      return fail_compaction(std::move(entries.error()));
    }
    auto const estimated_tokens = ava::session::estimate_session_tokens(*entries);
    auto start_event = base_command_event(session, RuntimeEventType::CompactionStart);
    start_event.trigger = "manual";
    start_event.status = "started";
    start_event.attempt = attempt + 1;
    start_event.max_attempts = max_compaction_attempts;
    start_event.estimated_tokens = estimated_tokens;
    if (auto emitted = emit_command_event(request, std::move(start_event)); !emitted) {
      return fail_compaction(std::move(emitted.error()));
    }
    auto summary = request.compaction_summary_generator(*entries, *config, instructions, estimated_tokens);
    if (!summary) {
      return fail_compaction(std::move(summary.error()));
    }
    if (command_canceled(request)) return fail_compaction(command_canceled_error());
    if (summary->empty()) {
      return fail_compaction(ava::core::Error(ava::core::ErrorCategory::Provider,
                                              "compaction summary generation returned an empty summary"));
    }
    if (summary->size() > config->max_summary_bytes) {
      auto error =
          ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "generated compaction summary is too large");
      error.with_context("max_summary_bytes", std::to_string(config->max_summary_bytes));
      error.with_context("summary_bytes", std::to_string(summary->size()));
      return fail_compaction(std::move(error));
    }

    bool snapshot_stale = false;
    auto validate_and_append = [&]() -> ava::core::VoidResult {
      auto current_entries = session.store.load();
      if (!current_entries) return std::unexpected(std::move(current_entries.error()));
      if (command_canceled(request)) return std::unexpected(command_canceled_error());
      if (!same_session_snapshot(*entries, *current_entries)) {
        snapshot_stale = true;
        last_snapshot_entries = entries->size();
        last_current_entries = current_entries->size();
        return {};
      }
      return ava::session::append_manual_compaction(
          session.store, ava::session::ManualCompactionRequest{.summary = *summary,
                                                               .instructions = instructions,
                                                               .config = *config,
                                                               .estimated_tokens = estimated_tokens,
                                                               .threshold_tokens = 0,
                                                               .trigger = "manual",
                                                               .recent_context = ""});
    };
    ava::core::VoidResult appended;
    if (request.session_mutex) {
      std::lock_guard lock(*request.session_mutex);
      appended = validate_and_append();
    } else {
      appended = validate_and_append();
    }
    if (!appended) {
      return fail_compaction(std::move(appended.error()));
    }
    if (!snapshot_stale) {
      auto end_event = base_command_event(session, RuntimeEventType::CompactionEnd);
      end_event.trigger = "manual";
      end_event.status = "completed";
      end_event.attempt = attempt + 1;
      end_event.max_attempts = max_compaction_attempts;
      end_event.estimated_tokens = estimated_tokens;
      end_event.summary_bytes = summary->size();
      if (auto emitted = emit_command_event(request, std::move(end_event)); !emitted) {
        return fail_compaction(std::move(emitted.error()));
      }
      add_output(result, "compaction summary recorded");
      return result;
    }
    if (attempt + 1 < max_compaction_attempts) {
      auto retry_event = base_command_event(session, RuntimeEventType::Retry);
      retry_event.trigger = "manual";
      retry_event.reason = "stale_compaction_snapshot";
      retry_event.status = "started";
      retry_event.attempt = attempt + 2;
      retry_event.max_attempts = max_compaction_attempts;
      retry_event.snapshot_entries = last_snapshot_entries;
      retry_event.current_entries = last_current_entries;
      if (auto emitted = emit_command_event(request, std::move(retry_event)); !emitted) {
        return fail_compaction(std::move(emitted.error()));
      }
    }
  }
  return fail_compaction(stale_compaction_snapshot_error("manual", last_snapshot_entries, last_current_entries));
}

ava::core::Result<CommandResult> run_export_command(RuntimeSession& session)
{
  CommandResult result;
  result.handled = true;
  auto entries = session.store.load();
  if (!entries) {
    add_output(result, entries.error().format());
    return result;
  }
  add_output(result, ava::session::format_session_markdown(*entries));
  return result;
}

}  // namespace ava::app
