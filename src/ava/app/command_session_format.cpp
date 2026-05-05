#include "ava/app/command_session_format.h"

#include <filesystem>
#include <iomanip>
#include <optional>
#include <sstream>
#include <utility>

#include "ava/app/command_format.h"

namespace ava::app {
namespace {

std::string format_cost_usd(long double value)
{
  std::ostringstream output;
  output << '$' << std::fixed << std::setprecision(6) << value;
  return output.str();
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

}  // namespace

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

}  // namespace ava::app
