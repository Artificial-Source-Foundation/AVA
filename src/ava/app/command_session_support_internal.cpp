#include "sys.h"
#include "ava/app/command_session_support_internal.h"
#include "ava/app/runtime/Session.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace ava::app::session_command_support {

std::string trim_ascii(std::string_view text)
{
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) text.remove_prefix(1);
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) text.remove_suffix(1);
  return std::string(text);
}

std::string lower_ascii(std::string_view text)
{
  std::string lowered(text);
  std::ranges::transform(lowered, lowered.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return lowered;
}

bool contains_ascii_case_insensitive(std::string_view text, std::string_view query)
{
  if (query.empty())
    return true;
  return lower_ascii(text).find(lower_ascii(query)) != std::string::npos;
}

ava::core::Result<std::vector<ava::session::SessionEntry>> load_runtime_entries(runtime::Session const& session)
{
  auto read_authority = session.read_authority();
  if (!read_authority)
    return std::unexpected(std::move(read_authority.error()));
  return read_authority->load();
}

ava::core::Result<ava::session::SessionMetadataView> load_runtime_metadata(runtime::Session const& session)
{
  auto entries = load_runtime_entries(session);
  if (!entries)
    return std::unexpected(std::move(entries.error()));
  return ava::session::session_metadata_from_entries(*entries);
}

std::string labels_text(std::vector<std::string> const& labels)
{
  std::string text;
  for (std::size_t index = 0; index < labels.size(); ++index)
  {
    if (index > 0)
      text += ",";
    text += labels[index];
  }
  return text;
}

std::string shorten_middle(std::string text, std::size_t max_columns)
{
  if (text.size() <= max_columns || max_columns < 8)
    return text;
  auto const front = (max_columns - 3) / 2;
  auto const back = max_columns - 3 - front;
  return text.substr(0, front) + "..." + text.substr(text.size() - back);
}

ava::core::Result<runtime::Session> reopen_session(runtime::Session const& current, std::string_view session_id)
{
  runtime::OpenOptions options;
  options.workspace_dir = current.workspace_dir();
  options.current_dir = current.current_dir();
  options.requested_session_id = std::string(session_id);
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

runtime::OpenOptions owned_replacement_options(runtime::Session const& current)
{
  runtime::OpenOptions options;
  options.workspace_dir = current.workspace_dir();
  options.current_dir = current.current_dir();
  options.mode = current.mode();
  options.tool_visibility = current.tool_visibility();
  options.paths = current.paths();
  options.prompt_overrides = current.prompt_overrides();
  options.offline = current.is_offline();
  options.subagent_coordinator = current.subagent_coordinator();
  options.subagent_delivery_manager = current.subagent_delivery_manager();
  options.session_title_coordinator = current.session_title_coordinator();
  return options;
}

}  // namespace ava::app::session_command_support
