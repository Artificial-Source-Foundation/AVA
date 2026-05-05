#include "ava/app/line_shell_support.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace ava::app::line_shell::detail {
namespace {

void add_token_component(std::optional<long long>& total, std::optional<long long> value)
{
  if (!value) return;
  if (!total) total = 0;
  *total += *value;
}

}  // namespace

std::string git_branch_for_workspace(std::filesystem::path const& workspace)
{
  auto const head_path = workspace / ".git" / "HEAD";
  std::ifstream input(head_path);
  if (!input) return {};
  std::string head;
  std::getline(input, head);
  constexpr std::string_view ref_prefix = "ref: refs/heads/";
  if (head.rfind(ref_prefix, 0) == 0) return head.substr(ref_prefix.size());
  return head.size() > 12 ? head.substr(0, 12) : head;
}

bool is_compact_command(std::string_view line) noexcept
{
  return line == "/compact" || (line.starts_with("/compact") && line.size() > 8 && line[8] == ' ');
}

std::optional<long long> compact_token_total(ava::session::SessionStats const& stats)
{
  if (stats.total_tokens) return stats.total_tokens;

  std::optional<long long> total;
  add_token_component(total, stats.input_tokens);
  add_token_component(total, stats.output_tokens);
  add_token_component(total, stats.reasoning_tokens);
  add_token_component(total, stats.cache_read_tokens);
  add_token_component(total, stats.cache_write_tokens);
  return total;
}

std::string format_compact_token_count(long long value)
{
  if (value < 1000) return std::to_string(value);

  auto const format_scaled = [](long long tenths, std::string_view suffix) {
    std::ostringstream output;
    output << (tenths / 10);
    if (tenths % 10 != 0) output << '.' << (tenths % 10);
    output << suffix;
    return output.str();
  };

  if (value < 1'000'000) return format_scaled(value / 100, "k");
  return format_scaled(value / 100'000, "m");
}

std::optional<std::string> format_context_window_percent(long long tokens,
                                                         std::optional<long long> context_window_tokens)
{
  if (!context_window_tokens || *context_window_tokens <= 0) return std::nullopt;
  if (tokens <= 0) return std::string("0.0%");

  auto const percent = (static_cast<long double>(tokens) * 100.0L) / static_cast<long double>(*context_window_tokens);
  if (percent > 0.0L && percent < 0.1L) return std::string("<0.1%");

  std::ostringstream output;
  output << std::fixed << std::setprecision(1) << percent << '%';
  return output.str();
}

std::optional<std::string> compact_token_status(ava::session::SessionStats const& stats,
                                                std::optional<long long> context_window_tokens)
{
  auto const tokens = compact_token_total(stats);
  if (!tokens) return std::nullopt;

  std::ostringstream output;
  output << format_compact_token_count(*tokens);
  if (auto const percent = format_context_window_percent(*tokens, context_window_tokens)) {
    output << " (" << *percent << ')';
  }
  return output.str();
}

std::optional<std::string> token_status_for_session(ava::app::RuntimeSession const& session)
{
  auto entries = session.store.load();
  if (!entries) return std::nullopt;
  return compact_token_status(ava::session::compute_session_stats(*entries), session.model.context_window_tokens);
}

}  // namespace ava::app::line_shell::detail
