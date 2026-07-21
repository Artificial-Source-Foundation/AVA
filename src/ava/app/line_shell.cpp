#include "sys.h"
#include "ava/app/clipboard_image.h"
#include "ava/app/command_jobs.h"
#include "ava/app/command_palette.h"
#include "ava/app/commands.h"
#include "ava/app/display_settings.h"
#include "ava/app/interactive_run_queue.h"
#include "ava/app/line_shell.h"
#include "ava/app/onboarding.h"
#include "ava/app/project_trust.h"
#include "ava/app/reasoning_controls.h"
#include "ava/app/rpc/runtime_navigation.h"
#include "ava/app/runtime_sessions.h"
#include "ava/tui/composer.h"
#include "ava/tui/keybindings.h"
#include "ava/tui/runtime.h"
#include "ava/tui/terminal.h"
#include "ava/tui/theme.h"
#include "ava/config/auth.h"
#include "ava/config/model_config.h"
#include "ava/config/model_profiles.h"
#include "ava/session/session_tree.h"
#include "ava/session/stats.h"
#include "ava/permissions/permission_rules.h"
#include "ava/provider/curl_transport.h"
#include "ava/provider/registry.h"
#include "ava/core/ids.h"
#include "ava/core/version.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

namespace version = ava::core::version;

// Delegate to the single app-owned helper so path logic is not duplicated.
using ava::app::permission_rule_store_for_session;

constexpr std::uintmax_t kExternalEditorMaxBytes = 1024 * 1024;

void print_shell_help()
{
  std::cout << ava::app::command_help_text() << '\n';
}

void append_status_line(std::string& target, std::string line)
{
  if (line.empty())
    return;
  if (!target.empty())
    target += '\n';
  target += std::move(line);
}

std::string git_branch_for_sidebar(std::filesystem::path const& workspace)
{
  auto const head_path = workspace / ".git" / "HEAD";
  std::ifstream input(head_path);
  if (!input)
    return {};
  std::string head;
  std::getline(input, head);
  constexpr std::string_view ref_prefix = "ref: refs/heads/";
  if (head.rfind(ref_prefix, 0) == 0)
    return head.substr(ref_prefix.size());
  return head.size() > 12 ? head.substr(0, 12) : head;
}

std::vector<ava::app::CommandHotkey> command_hotkeys_from_key_bindings(ava::tui::TuiKeyBindings const& key_bindings)
{
  std::vector<ava::app::CommandHotkey> hotkeys;
  for (auto const& item : ava::tui::key_binding_help_items(key_bindings))
  {
    hotkeys.push_back(ava::app::CommandHotkey{.action = item.action, .description = item.description, .keys = item.keys});
  }
  return hotkeys;
}

std::string display_theme_status(std::string_view prefix)
{
  auto const active = ava::tui::active_tui_theme();
  return std::string(prefix) + ": " + active.name + " (" + active.badge + ")";
}

ava::tui::ProjectTrustSnapshot project_trust_snapshot(ava::app::ProjectTrustState const& state)
{
  return ava::tui::ProjectTrustSnapshot{.decision = std::string(ava::app::to_string(state.decision)),
                                        .project_resources = ava::app::project_resources_trusted(state) ? std::string("enabled") : std::string("skipped"),
                                        .workspace = state.workspace_dir.string(),
                                        .matched_path = state.matched_path.string(),
                                        .trust_file = state.trust_file.string(),
                                        .protected_resource_count = state.protected_resources.size(),
                                        .diagnostic = state.diagnostic};
}

ava::core::Error errno_line_shell_error(ava::core::ErrorCategory category, std::string message)
{
  auto error = ava::core::Error(category, std::move(message));
  error.with_context("cause", std::strerror(errno));
  return error;
}

std::optional<std::string> env_value(std::string_view name)
{
  auto const* value = std::getenv(std::string(name).c_str());
  if (value == nullptr || *value == '\0')
    return std::nullopt;
  return std::string(value);
}

class ScopedEnvVar
{
 public:
  ScopedEnvVar(std::string name, std::string value) : name_(std::move(name))
  {
    if (auto const* previous = std::getenv(name_.c_str()))
      previous_ = std::string(previous);
    set_ = ::setenv(name_.c_str(), value.c_str(), 1) == 0;
  }

  ScopedEnvVar(ScopedEnvVar const&) = delete;
  ScopedEnvVar& operator=(ScopedEnvVar const&) = delete;

  ~ScopedEnvVar()
  {
    if (!set_)
      return;
    if (previous_)
      static_cast<void>(::setenv(name_.c_str(), previous_->c_str(), 1));
    else
      static_cast<void>(::unsetenv(name_.c_str()));
  }

  [[nodiscard]] bool ok() const { return set_; }

 private:
  std::string name_;
  std::optional<std::string> previous_;
  bool set_ = false;
};

class ScopedTempFile
{
 public:
  explicit ScopedTempFile(std::filesystem::path path, int fd) : path_(std::move(path)), fd_(fd) { }
  ScopedTempFile(ScopedTempFile const&) = delete;
  ScopedTempFile& operator=(ScopedTempFile const&) = delete;

  ~ScopedTempFile()
  {
    if (fd_ >= 0)
      static_cast<void>(::close(fd_));
    if (!path_.empty())
    {
      std::error_code ignored;
      std::filesystem::remove(path_, ignored);
    }
  }

  [[nodiscard]] int fd() const { return fd_; }
  [[nodiscard]] std::filesystem::path const& path() const { return path_; }

  [[nodiscard]] bool close()
  {
    if (fd_ < 0)
      return true;
    if (::close(fd_) != 0)
      return false;
    fd_ = -1;
    return true;
  }

 private:
  std::filesystem::path path_;
  int fd_ = -1;
};

bool write_all_fd(int fd, std::string_view text)
{
  std::size_t offset = 0;
  while (offset < text.size())
  {
    auto const written = ::write(fd, text.data() + offset, text.size() - offset);
    if (written < 0)
    {
      if (errno == EINTR)
        continue;
      return false;
    }
    if (written == 0)
      return false;
    offset += static_cast<std::size_t>(written);
  }
  return true;
}

ava::core::Result<std::optional<std::string>> edit_text_with_external_editor(std::string_view initial_text)
{
  auto editor = env_value("VISUAL");
  if (!editor)
    editor = env_value("EDITOR");
  if (!editor)
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "external editor requires VISUAL or EDITOR"));
  }

  std::error_code temp_error;
  auto const temp_dir = std::filesystem::temp_directory_path(temp_error);
  if (temp_error)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to resolve temporary directory");
    error.with_context("cause", temp_error.message());
    return std::unexpected(std::move(error));
  }

  auto path_template = (temp_dir / "ava-editor-XXXXXX").string();
  std::vector<char> mutable_path(path_template.begin(), path_template.end());
  mutable_path.push_back('\0');
  int const fd = ::mkstemp(mutable_path.data());
  if (fd < 0)
    return std::unexpected(errno_line_shell_error(ava::core::ErrorCategory::Io, "failed to create external editor temp file"));

  ScopedTempFile temp_file(std::filesystem::path(mutable_path.data()), fd);
  if (::fchmod(temp_file.fd(), S_IRUSR | S_IWUSR) != 0)
    return std::unexpected(errno_line_shell_error(ava::core::ErrorCategory::Io, "failed to secure external editor temp file"));
  if (!write_all_fd(temp_file.fd(), initial_text))
    return std::unexpected(errno_line_shell_error(ava::core::ErrorCategory::Io, "failed to write external editor temp file"));
  if (!temp_file.close())
    return std::unexpected(errno_line_shell_error(ava::core::ErrorCategory::Io, "failed to close external editor temp file"));

  ScopedEnvVar file_env("AVA_EXTERNAL_EDITOR_FILE", temp_file.path().string());
  if (!file_env.ok())
    return std::unexpected(errno_line_shell_error(ava::core::ErrorCategory::Io, "failed to prepare external editor file environment"));

  auto const command = std::string("exec ") + *editor + " \"$AVA_EXTERNAL_EDITOR_FILE\"";
  int const status = std::system(command.c_str());
  if (status == -1)
    return std::unexpected(errno_line_shell_error(ava::core::ErrorCategory::Io, "failed to launch external editor"));
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
    return std::optional<std::string>{};

  std::error_code size_error;
  auto const edited_size = std::filesystem::file_size(temp_file.path(), size_error);
  if (size_error)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to inspect external editor temp file");
    error.with_context("cause", size_error.message());
    return std::unexpected(std::move(error));
  }
  if (edited_size > kExternalEditorMaxBytes)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "external editor draft is too large");
    error.with_context("limit", std::to_string(kExternalEditorMaxBytes));
    return std::unexpected(std::move(error));
  }

  std::ifstream input(temp_file.path(), std::ios::binary);
  if (!input)
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to read external editor temp file"));
  std::string edited((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  return std::optional<std::string>{std::move(edited)};
}

ava::tui::ToolTimelineStatus tui_tool_status(ava::agent::ToolTimelineStatus status)
{
  switch (status)
  {
    case ava::agent::ToolTimelineStatus::Running:
      return ava::tui::ToolTimelineStatus::Running;
    case ava::agent::ToolTimelineStatus::Success:
      return ava::tui::ToolTimelineStatus::Success;
    case ava::agent::ToolTimelineStatus::Canceled:
      return ava::tui::ToolTimelineStatus::Canceled;
    case ava::agent::ToolTimelineStatus::Error:
      return ava::tui::ToolTimelineStatus::Error;
  }
  return ava::tui::ToolTimelineStatus::Error;
}

std::string permission_summary_field(std::string_view summary, std::string_view label)
{
  for (auto line : ava::tui::split_lines(std::string(summary)))
  {
    std::string_view view(line);
    while (!view.empty() && (view.front() == ' ' || view.front() == '\t')) view.remove_prefix(1);
    if (!view.starts_with(label))
      continue;
    view.remove_prefix(label.size());
    while (!view.empty() && (view.front() == ' ' || view.front() == '\t')) view.remove_prefix(1);
    return std::string(view);
  }
  return {};
}

std::vector<ava::tui::ToolPermissionAuditItem> tui_permission_audits(ava::agent::ToolTimelineEntry const& entry)
{
  std::vector<ava::tui::ToolPermissionAuditItem> audits;
  audits.reserve(entry.permission_request_ids.size());
  auto const resolution = permission_summary_field(entry.result_summary, "resolution:");
  auto const decision = resolution == "deny" || entry.result_summary.find("permission_denied:") != std::string::npos ? std::string("deny") : std::string{};
  auto const reason = permission_summary_field(entry.result_summary, "reason:");
  auto const risk = permission_summary_field(entry.result_summary, "risk:");
  auto const command = permission_summary_field(entry.result_summary, "command:");
  for (auto const& id : entry.permission_request_ids)
  {
    audits.push_back(ava::tui::ToolPermissionAuditItem{
        .permission_request_id = id, .decision = decision, .risk = risk, .reason = reason, .command = command, .resolution_reason = resolution});
  }
  return audits;
}

std::vector<ava::tui::ToolTimelineItem> tui_tool_timeline(std::vector<ava::agent::ToolTimelineEntry> const& entries)
{
  std::vector<ava::tui::ToolTimelineItem> items;
  items.reserve(entries.size());
  for (auto const& entry : entries)
  {
    auto item = ava::tui::ToolTimelineItem{.status = tui_tool_status(entry.status),
                                           .name = entry.name,
                                           .argument_summary = entry.argument_summary,
                                           .result_summary = entry.result_summary,
                                           .arguments_json = entry.arguments_json,
                                           .result_json = entry.result_json,
                                           .call_id = entry.call_id,
                                           .lifecycle = entry.status == ava::agent::ToolTimelineStatus::Running ? ava::tui::ToolLifecycleState::ExecutionStarted
                                                                                                                : ava::tui::ToolLifecycleState::Complete,
                                           .permission_request_ids = entry.permission_request_ids,
                                           .permissions = tui_permission_audits(entry),
                                           .diff = entry.diff,
                                           .diff_truncated = entry.diff_truncated,
                                           .changed_paths = entry.changed_paths,
                                           .truncated = entry.truncated,
                                           .byte_limited = entry.byte_limited,
                                           .line_limited = entry.line_limited,
                                           .output_bytes = entry.output_bytes,
                                           .total_bytes = entry.total_bytes,
                                           .output_lines = entry.output_lines,
                                           .total_lines = entry.total_lines,
                                           .start_line = entry.start_line,
                                           .end_line = entry.end_line,
                                           .next_offset_line = entry.next_offset_line,
                                           .omitted_bytes = entry.omitted_bytes,
                                           .omitted_lines = entry.omitted_lines,
                                           .visible_matches = entry.visible_matches,
                                           .total_matches = entry.total_matches,
                                           .spill_path = entry.spill_path,
                                           .spill_truncated = entry.spill_truncated};
    if (entry.status == ava::agent::ToolTimelineStatus::Error)
      item.lifecycle = ava::tui::ToolLifecycleState::Error;
    if (entry.status == ava::agent::ToolTimelineStatus::Canceled)
      item.lifecycle = ava::tui::ToolLifecycleState::Canceled;
    items.push_back(std::move(item));
  }
  return items;
}

void print_resume_command(ava::session::SessionStore const& store)
{
  if (store.is_ephemeral())
  {
    std::cout << "Session history was not saved (--no-session)\n";
    return;
  }
  std::cout << "Resume this session with: ava --session " << store.session_id() << '\n';
}

bool is_compact_command(std::string_view line) noexcept
{
  return line == "/compact" || (line.starts_with("/compact") && line.size() > 8 && line[8] == ' ');
}

bool is_display_settings_command(std::string_view line) noexcept
{
  while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.remove_prefix(1);
  while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) line.remove_suffix(1);
  return line == "/theme" || (line.starts_with("/theme") && line.size() > 6 && line[6] == ' ') || line == "/reload theme" || line == "/reload themes" ||
         line == "/reload display";
}

void add_token_component(std::optional<long long>& total, std::optional<long long> value)
{
  if (!value || *value < 0)
    return;
  if (!total)
    total = 0;
  constexpr auto maximum = std::numeric_limits<long long>::max();
  *total = *total > maximum - *value ? maximum : *total + *value;
}

std::optional<long long> compact_token_total(ava::session::SessionStats const& stats)
{
  if (stats.total_tokens)
    return stats.total_tokens;

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
  if (value < 1000)
    return std::to_string(value);

  auto const format_scaled = [](long long tenths, std::string_view suffix) {
    std::ostringstream output;
    output << (tenths / 10);
    if (tenths % 10 != 0)
      output << '.' << (tenths % 10);
    output << suffix;
    return output.str();
  };

  if (value < 1'000'000)
    return format_scaled(value / 100, "k");
  return format_scaled(value / 100'000, "m");
}

std::optional<std::string> format_context_window_percent(long long tokens, std::optional<long long> context_window_tokens)
{
  if (!context_window_tokens || *context_window_tokens <= 0)
    return std::nullopt;
  if (tokens <= 0)
    return std::string("0.0%");

  auto const percent = (static_cast<long double>(tokens) * 100.0L) / static_cast<long double>(*context_window_tokens);
  if (percent > 0.0L && percent < 0.1L)
    return std::string("<0.1%");

  std::ostringstream output;
  output << std::fixed << std::setprecision(1) << percent << '%';
  return output.str();
}

std::optional<std::string> compact_token_status(ava::session::SessionStats const& stats, std::optional<long long> context_window_tokens)
{
  auto const tokens = compact_token_total(stats);
  if (!tokens)
    return std::nullopt;

  std::ostringstream output;
  output << format_compact_token_count(*tokens);
  if (auto const percent = format_context_window_percent(*tokens, context_window_tokens))
  {
    output << " (" << *percent << ')';
  }
  return output.str();
}

std::optional<std::string> token_status_for_session(ava::app::runtime::Session const& session)
{
  auto read_authority = session.read_authority();
  if (!read_authority)
    return std::nullopt;
  auto entries = read_authority->load();
  if (!entries)
    return std::nullopt;
  auto stats = ava::session::compute_session_stats(*entries);
  if (!stats)
    return std::nullopt;
  return compact_token_status(*stats, session.model.context_window_tokens);
}

std::string session_selector_footer_hint(ava::app::SessionSelectorSort sort, bool named_only, bool show_paths, bool show_archived, bool show_label_time)
{
  return "Enter open session · Ctrl+S/Ctrl+T sort (" + ava::app::session_selector_sort_label(sort) + ") · Ctrl+N " +
         (named_only ? std::string("show all") : std::string("named only")) + " · Ctrl+P " +
         (show_paths ? std::string("hide paths") : std::string("show paths")) + " · Ctrl+A " +
         (show_archived ? std::string("hide archived") : std::string("show archived")) + " · Shift+T " +
         (show_label_time ? std::string("hide label time") : std::string("show label time")) +
         " · Alt+Left/Right branch · PgUp/PgDn page · Ctrl+R rename · Ctrl+L labels · Ctrl+D archive/restore · type to filter · Esc cancel";
}

std::string scoped_model_selector_footer_hint()
{
  return "Enter toggle · Ctrl+A enable visible · Ctrl+X clear visible · Ctrl+P provider · Alt+Up/Down reorder · Ctrl+S save · type to filter · Esc cancel";
}

bool contains_value(std::vector<std::string> const& values, std::string_view value)
{
  return std::ranges::find_if(values, [&](auto const& existing) { return existing == value; }) != values.end();
}

std::vector<std::string> registered_model_cycle_values(ava::app::runtime::Session const& session)
{
  auto view = ava::app::scoped_model_selector_view(session, {});
  std::vector<std::string> values;
  for (auto const& item : view.items)
  {
    if (item.enabled && !item.value.empty() && !contains_value(values, item.value))
      values.push_back(item.value);
  }
  return values;
}

std::vector<std::string> normalized_model_scope(std::vector<std::string> const& candidate, std::vector<std::string> const& all_values)
{
  std::vector<std::string> normalized;
  normalized.reserve(candidate.size());
  for (auto const& value : candidate)
  {
    if (contains_value(all_values, value) && !contains_value(normalized, value))
      normalized.push_back(value);
  }
  return normalized;
}

void store_model_scope(ava::app::runtime::Session& session, std::vector<std::string> candidate, std::vector<std::string> const& all_values,
                       bool reset_full_scope_to_all)
{
  auto normalized = normalized_model_scope(candidate, all_values);
  if (reset_full_scope_to_all && normalized.size() == all_values.size())
  {
    session.scoped_model_cycle = std::nullopt;
    return;
  }
  session.scoped_model_cycle = std::move(normalized);
}

std::vector<std::string> active_model_scope_or_all(ava::app::runtime::Session const& session, std::vector<std::string> const& all_values)
{
  if (session.scoped_model_cycle)
    return normalized_model_scope(*session.scoped_model_cycle, all_values);
  return all_values;
}

std::string provider_from_model_value(std::string_view value)
{
  auto const slash = value.find('/');
  if (slash == std::string_view::npos)
    return {};
  return std::string(value.substr(0, slash));
}

ava::tui::SelectListView preserve_scoped_model_selector_state(ava::tui::SelectListView view, ava::tui::SelectListView const& previous)
{
  view.query = previous.query;
  std::string selected_value;
  if (previous.selected_item_index < previous.items.size())
    selected_value = previous.items[previous.selected_item_index].value;
  if (!selected_value.empty())
  {
    for (std::size_t index = 0; index < view.items.size(); ++index)
    {
      if (view.items[index].value == selected_value)
      {
        view.selected_item_index = index;
        break;
      }
    }
  }
  view.selected_item_index = ava::tui::clamp_select_list_selection(view, view.selected_item_index);
  return view;
}

ava::tui::SelectListView refreshed_scoped_model_selector(ava::app::runtime::Session const& session, ava::tui::SelectListView const& previous)
{
  return preserve_scoped_model_selector_state(ava::app::scoped_model_selector_view(session, scoped_model_selector_footer_hint()), previous);
}

ava::core::Result<ava::tui::SelectListView> toggle_scoped_model(ava::app::runtime::Session& session, ava::tui::SelectListView const& previous,
                                                                std::string_view value)
{
  auto const all_values = registered_model_cycle_values(session);
  if (!contains_value(all_values, value))
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "model is not available for scoped cycling"));
  if (!session.scoped_model_cycle)
  {
    session.scoped_model_cycle = std::vector<std::string>{std::string(value)};
    return refreshed_scoped_model_selector(session, previous);
  }
  auto next = normalized_model_scope(*session.scoped_model_cycle, all_values);
  if (contains_value(next, value))
    std::erase(next, std::string(value));
  else
    next.push_back(std::string(value));
  store_model_scope(session, std::move(next), all_values, true);
  return refreshed_scoped_model_selector(session, previous);
}

ava::core::Result<ava::tui::SelectListView> enable_scoped_models(ava::app::runtime::Session& session, ava::tui::SelectListView const& previous,
                                                                 std::vector<std::string> targets)
{
  auto const all_values = registered_model_cycle_values(session);
  auto next = active_model_scope_or_all(session, all_values);
  for (auto const& target : targets)
  {
    if (contains_value(all_values, target) && !contains_value(next, target))
      next.push_back(target);
  }
  store_model_scope(session, std::move(next), all_values, true);
  return refreshed_scoped_model_selector(session, previous);
}

ava::core::Result<ava::tui::SelectListView> clear_scoped_models(ava::app::runtime::Session& session, ava::tui::SelectListView const& previous,
                                                                std::vector<std::string> targets)
{
  auto const all_values = registered_model_cycle_values(session);
  auto next = active_model_scope_or_all(session, all_values);
  for (auto const& target : targets)
  {
    std::erase(next, target);
  }
  store_model_scope(session, std::move(next), all_values, true);
  return refreshed_scoped_model_selector(session, previous);
}

ava::core::Result<ava::tui::SelectListView> toggle_scoped_model_provider(ava::app::runtime::Session& session, ava::tui::SelectListView const& previous,
                                                                         std::string_view selected_value)
{
  auto const provider = provider_from_model_value(selected_value);
  if (provider.empty())
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "model selection is missing provider"));
  auto const all_values = registered_model_cycle_values(session);
  std::vector<std::string> provider_values;
  for (auto const& value : all_values)
  {
    if (provider_from_model_value(value) == provider)
      provider_values.push_back(value);
  }
  auto next = active_model_scope_or_all(session, all_values);
  bool const provider_enabled =
      !provider_values.empty() && std::ranges::all_of(provider_values, [&](auto const& value) { return contains_value(next, value); });
  if (provider_enabled)
  {
    for (auto const& value : provider_values) std::erase(next, value);
  }
  else
  {
    for (auto const& value : provider_values)
    {
      if (!contains_value(next, value))
        next.push_back(value);
    }
  }
  store_model_scope(session, std::move(next), all_values, true);
  return refreshed_scoped_model_selector(session, previous);
}

ava::core::Result<ava::tui::SelectListView> reorder_scoped_model(ava::app::runtime::Session& session, ava::tui::SelectListView const& previous,
                                                                 std::string_view selected_value, bool up)
{
  auto const all_values = registered_model_cycle_values(session);
  auto next = active_model_scope_or_all(session, all_values);
  auto const selected = std::string(selected_value);
  auto const found = std::ranges::find(next, selected);
  if (found == next.end())
    return refreshed_scoped_model_selector(session, previous);
  auto const index = static_cast<std::size_t>(found - next.begin());
  if ((up && index == 0) || (!up && index + 1 >= next.size()))
    return refreshed_scoped_model_selector(session, previous);
  auto const other = up ? index - 1 : index + 1;
  std::swap(next[index], next[other]);
  store_model_scope(session, std::move(next), all_values, false);
  return refreshed_scoped_model_selector(session, previous);
}

ava::core::Result<std::string> save_scoped_model_cycle(ava::app::runtime::Session& session)
{
  std::optional<std::vector<std::string>> scope_to_save = std::nullopt;
  if (session.scoped_model_cycle)
  {
    auto const all_values = registered_model_cycle_values(session);
    scope_to_save = normalized_model_scope(*session.scoped_model_cycle, all_values);
    session.scoped_model_cycle = scope_to_save;
  }

  auto saved = ava::config::store_scoped_model_cycle(session.paths, scope_to_save);
  if (!saved)
    return std::unexpected(std::move(saved.error()));

  if (!scope_to_save)
    return std::string("scoped model cycle saved: all registered models enabled");
  if (scope_to_save->size() == 1)
    return std::string("scoped model cycle saved: 1 model enabled");
  return std::string("scoped model cycle saved: ") + std::to_string(scope_to_save->size()) + " models enabled";
}

ava::permissions::PermissionRuleMode permission_rule_mode_for_agent_mode(ava::agent::Mode mode)
{
  switch (mode)
  {
    case ava::agent::Mode::Build:
      return ava::permissions::PermissionRuleMode::Build;
    case ava::agent::Mode::Plan:
      return ava::permissions::PermissionRuleMode::Plan;
  }
  return ava::permissions::PermissionRuleMode::Any;
}

ava::core::Result<ava::tui::TuiRememberedPermissionRule> remember_permission_rule_for_prompt(ava::app::runtime::Session const& session,
                                                                                             ava::permissions::PermissionPrompt const& prompt,
                                                                                             ava::permissions::PermissionAction action)
{
  auto reason = prompt.reason.empty() ? std::string("remembered from TUI permission prompt") : prompt.reason;
  std::string recipe_key;
  std::string recipe_display;
  if (prompt.operation == ava::permissions::Operation::RunCommand)
  {
    auto const reusable = prompt.command_metadata && ava::permissions::command_permission_allows_reusable_grant(*prompt.command_metadata);
    if (action == ava::permissions::PermissionAction::Allow && !ava::permissions::command_prompt_allows_persistent_allow(prompt))
    {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::PermissionDenied,
                                              "this command cannot be remembered because no reusable sealed workspace recipe is available"));
    }
    if (reusable)
    {
      recipe_key = prompt.command_metadata->workspace_recipe_key;
      recipe_display = prompt.command_metadata->recipe_display;
    }
  }
  auto added = ava::permissions::add_persistent_permission_rule(
      permission_rule_store_for_session(session),
      ava::permissions::PermissionRuleDraft{
          .scope = ava::permissions::PermissionRuleScope::Workspace,
          .action = action,
          .operation = prompt.operation,
          .mode = permission_rule_mode_for_agent_mode(prompt.mode),
          .tool_name = prompt.tool_name,
          .target_path = prompt.target_path,
          .command = prompt.operation == ava::permissions::Operation::RunCommand && !recipe_key.empty() ? std::string{} : prompt.command,
          .command_recipe_key = std::move(recipe_key),
          .recipe_display = std::move(recipe_display),
          .reason = std::move(reason),
          .actor = "tui_prompt"});
  if (!added)
    return std::unexpected(std::move(added.error()));
  return ava::tui::TuiRememberedPermissionRule{.rule_id = added->rule_id};
}

struct ShellState
{
  // Lifetime contract: references are stack-scoped and must outlive each run loop invocation.
  ava::app::runtime::Session& session;
};

struct LineResult
{
  bool quit = false;
  std::vector<std::string> output;
  std::vector<ava::agent::ToolTimelineEntry> tool_timeline;
};

void add_output(LineResult& result, std::string text)
{
  result.output.push_back(std::move(text));
}

template <typename Callback>
LineResult with_provider_runtime(ShellState& state, std::string_view offline_suffix, Callback callback, std::string_view provider_override = {})
{
  LineResult line_result;
  if (state.session.offline)
  {
    add_output(line_result, ava::app::offline_provider_error("prompt").format() + std::string(offline_suffix));
    return line_result;
  }
  auto const provider_id = provider_override.empty() ? std::string_view(state.session.model.provider_id) : provider_override;
  ava::provider::CurlCliTransport transport;
  auto credential = ava::config::provider_credential_for_request(state.session.paths, provider_id, transport);
  if (!credential)
  {
    add_output(line_result, credential.error().format() + std::string(offline_suffix));
    return line_result;
  }
  if (!*credential)
  {
    if (provider_override.empty())
    {
      add_output(line_result, ava::app::provider_auth_required_message(state.session, offline_suffix));
    }
    else
    {
      add_output(line_result, "Auth is required for compaction provider `" + std::string(provider_id) + "`. Run `ava connect " + std::string(provider_id) +
                                  "` or configure its API-key environment variable." + std::string(offline_suffix));
    }
    return line_result;
  }
  auto registry = ava::provider::builtin_provider_registry();
  auto provider = registry.create(provider_id);
  if (!provider)
  {
    add_output(line_result, provider.error().format() + std::string(offline_suffix));
    return line_result;
  }
  ava::app::runtime::RunOptions run_options;
  run_options.access_token = (*credential)->access_token;
  run_options.credential_type = (*credential)->credential_type;
  run_options.openai_oauth = (*credential)->provider_id == "openai" && (*credential)->credential_type == "oauth";
  run_options.openai_account_id = (*credential)->account_id;
  run_options.enable_transport_retries = true;
  return callback(**provider, transport, run_options);
}

LineResult handle_line(ShellState& state, std::string const& line, ava::permissions::PermissionResolver permission_resolver = nullptr,
                       ava::agent::QuestionResolver question_resolver = nullptr, std::vector<ava::app::CommandHotkey> const& hotkeys = {},
                       ava::app::runtime::EventSink event_sink = nullptr, std::function<bool()> cancel_requested = nullptr,
                       std::function<ava::core::Result<std::vector<std::string>>()> take_steering_messages = nullptr,
                       std::vector<ava::session::ImageAttachmentRef> image_attachments = {})
{
  LineResult line_result;
  if (line.empty())
    return line_result;
  if (ava::app::is_backend_command(line, state.session))
  {
    if (is_compact_command(line))
    {
      auto loaded_config = ava::session::load_compaction_config(state.session.paths);
      if (!loaded_config)
      {
        add_output(line_result, loaded_config.error().format());
        return line_result;
      }
      auto config = ava::app::resolve_compaction_config(state.session, std::move(*loaded_config));
      if (!config)
      {
        add_output(line_result, config.error().format());
        return line_result;
      }
      auto const summary_provider_id = config->provider_id;
      return with_provider_runtime(
          state, "\nother slash tool commands still work offline.",
          [&](ava::provider::Provider const& provider, ava::provider::Transport& transport, ava::app::runtime::RunOptions run_options) {
            run_options.cancel_requested = cancel_requested;
            run_options.event_sink = event_sink;
            auto command_result = ava::app::run_command(
                state.session,
                ava::app::CommandRequest{.command = line,
                                         .event_sink = event_sink,
                                         .permission_resolver = permission_resolver,
                                         .question_resolver = question_resolver,
                                         .compaction_summary_generator =
                                             [&](std::vector<ava::session::SessionEntry> const& entries, ava::session::CompactionConfig const& config,
                                                 std::string_view instructions, std::size_t estimated_tokens) {
                                               return ava::app::generate_compaction_summary(state.session, entries, config, instructions, estimated_tokens,
                                                                                            provider, transport, run_options);
                                             },
                                         .cancel_requested = cancel_requested,
                                         .hotkeys = hotkeys});
            if (!command_result)
            {
              LineResult compact_result;
              add_output(compact_result, command_result.error().format());
              return compact_result;
            }
            return LineResult{
                .quit = command_result->quit, .output = std::move(command_result->output), .tool_timeline = std::move(command_result->tool_timeline)};
          },
          summary_provider_id);
    }
    auto command_result = ava::app::run_command(state.session, ava::app::CommandRequest{.command = line,
                                                                                        .permission_resolver = permission_resolver,
                                                                                        .question_resolver = question_resolver,
                                                                                        .cancel_requested = cancel_requested,
                                                                                        .hotkeys = hotkeys});
    if (!command_result)
    {
      add_output(line_result, command_result.error().format());
      return line_result;
    }
    line_result.quit = command_result->quit;
    line_result.output = std::move(command_result->output);
    line_result.tool_timeline = std::move(command_result->tool_timeline);
    if (command_result->prompt_message)
    {
      return with_provider_runtime(
          state, "\nthis command expands to a prompt and needs provider auth.",
          [&](ava::provider::Provider const& provider, ava::provider::Transport& transport, ava::app::runtime::RunOptions run_options) {
            run_options.permission_resolver = permission_resolver;
            run_options.question_resolver = question_resolver;
            run_options.event_sink = std::move(event_sink);
            run_options.cancel_requested = std::move(cancel_requested);
            run_options.take_steering_messages = std::move(take_steering_messages);
            auto result = ava::app::run_prompt(state.session, *command_result->prompt_message, provider, transport, run_options);
            LineResult prompt_result;
            if (!result)
            {
              add_output(prompt_result, result.error().format());
              return prompt_result;
            }
            prompt_result.tool_timeline = std::move(result->tool_timeline);
            if (!result->final_text.empty())
            {
              add_output(prompt_result, result->final_text);
            }
            else
            {
              add_output(prompt_result, "done");
            }
            return prompt_result;
          });
    }
    return line_result;
  }
  if (line.starts_with('/'))
  {
    auto const end = line.find_first_of(" \t\r\n");
    auto const command = line.substr(0, end == std::string::npos ? line.size() : end);
    add_output(line_result, "Unknown command: " + command + ". Type /help to list commands.");
    return line_result;
  }

  return with_provider_runtime(state, "\nslash tool commands still work offline.",
                               [&](ava::provider::Provider const& provider, ava::provider::Transport& transport, ava::app::runtime::RunOptions run_options) {
                                 run_options.permission_resolver = permission_resolver;
                                 run_options.question_resolver = question_resolver;
                                 run_options.event_sink = std::move(event_sink);
                                 run_options.cancel_requested = std::move(cancel_requested);
                                 run_options.take_steering_messages = std::move(take_steering_messages);
                                 run_options.image_attachments = std::move(image_attachments);
                                 auto result = ava::app::run_prompt(state.session, line, provider, transport, run_options);
                                 LineResult prompt_result;
                                 if (!result)
                                 {
                                   add_output(prompt_result, result.error().format());
                                   return prompt_result;
                                 }
                                 prompt_result.tool_timeline = std::move(result->tool_timeline);
                                 if (!result->final_text.empty())
                                 {
                                   add_output(prompt_result, result->final_text);
                                 }
                                 else
                                 {
                                   add_output(prompt_result, "done");
                                 }
                                 return prompt_result;
                               });
}

int run_line_shell(ShellState state)
{
  std::cout << "AVA " << version::kDisplayVersion << " terminal shell\n";
  std::cout << "mode: " << ava::agent::to_string(state.session.mode) << " | session: " << state.session.store.session_id() << "\n";
  std::cout << "provider: " << state.session.model.provider_id << " | model: " << state.session.model.model_id << "\n";
  print_shell_help();

  std::string line;
  while (true)
  {
    std::cout << "\n[" << ava::agent::to_string(state.session.mode) << "] ava> " << std::flush;
    if (!std::getline(std::cin, line))
    {
      std::cout << '\n';
      print_resume_command(state.session.store);
      return 0;
    }

    auto permission_resolver = ava::permissions::build_persistent_permission_rule_resolver(permission_rule_store_for_session(state.session), nullptr);
    auto const result = handle_line(state, line, permission_resolver);
    for (auto const& output : result.output)
    {
      for (auto const& output_line : ava::tui::split_lines(output))
      {
        std::cout << ava::tui::sanitize_terminal_text(output_line) << '\n';
      }
    }
    if (result.quit)
    {
      print_resume_command(state.session.store);
      return 0;
    }
  }
}

int run_tui(ShellState state)
{
  auto key_bindings = ava::tui::default_key_bindings();
  std::string keybind_status;
  if (auto loaded = ava::tui::load_key_bindings(state.session.paths.ava_config_dir / "keybinds.json"); loaded)
  {
    key_bindings = std::move(*loaded);
  }
  else
  {
    keybind_status = loaded.error().format();
  }
  auto display_watch_state = std::make_shared<std::optional<ava::app::TuiDisplaySettingsWatchState>>();
  auto display_watch_mutex = std::make_shared<std::mutex>();
  auto refresh_display_watch_state = [&state, display_watch_state, display_watch_mutex]() -> ava::core::VoidResult {
    auto watched = ava::app::load_tui_display_settings_watch_state(state.session.paths);
    if (!watched)
      return std::unexpected(std::move(watched.error()));
    std::lock_guard lock(*display_watch_mutex);
    *display_watch_state = std::move(*watched);
    return {};
  };
  if (auto display_settings = ava::app::apply_tui_display_settings(state.session.paths); !display_settings)
  {
    append_status_line(keybind_status, display_settings.error().format());
  }
  else if (auto watched = refresh_display_watch_state(); !watched)
  {
    append_status_line(keybind_status, watched.error().format());
  }
  auto hotkeys = command_hotkeys_from_key_bindings(key_bindings);
  auto model_display = [](ava::config::ModelInfo const& model) {
    return model.display_name.empty() ? ava::config::model_display_label(model.model_id) : model.display_name;
  };
  auto custom_theme_options = [&state]() {
    std::vector<ava::tui::ThemeOptionItem> themes;
    for (auto const& theme : ava::app::available_tui_custom_themes(state.session.paths))
    {
      themes.push_back(ava::tui::ThemeOptionItem{.name = theme.name, .detail = theme.path.string()});
    }
    return themes;
  };
  auto runtime_open_options = [&state]() {
    ava::app::runtime::OpenOptions options;
    options.workspace_dir = state.session.workspace_dir;
    options.current_dir = state.session.current_dir;
    options.mode = state.session.mode;
    options.tool_visibility = state.session.tool_visibility;
    options.paths = state.session.paths;
    options.sessionless = state.session.sessionless;
    options.offline = state.session.offline;
    return options;
  };
  auto state_snapshot = [&state, &hotkeys, &model_display, &custom_theme_options](std::string status) {
    return ava::tui::TuiRuntimeStateSnapshot{
        .mode = ava::agent::to_string(state.session.mode),
        .provider = state.session.model.provider_id,
        .model = model_display(state.session.model),
        .session_id = state.session.store.session_id(),
        .session_path = state.session.store.session_path().string(),
        .workspace = state.session.current_dir.empty() ? state.session.workspace_dir.string() : state.session.current_dir.string(),
        .git_branch = git_branch_for_sidebar(state.session.workspace_dir),
        .context_source_count = state.session.context_sources.size(),
        .status = std::move(status),
        .slash_commands = ava::app::command_catalog_slash_items(state.session, hotkeys),
        .file_references = ava::app::file_reference_items(state.session),
        .custom_themes = custom_theme_options(),
        .project_trust = project_trust_snapshot(state.session.project_trust)};
  };
  auto session_selector_sort = std::make_shared<ava::app::SessionSelectorSort>(ava::app::SessionSelectorSort::Recent);
  auto session_selector_named_only = std::make_shared<bool>(false);
  auto session_selector_show_paths = std::make_shared<bool>(true);
  auto session_selector_show_archived = std::make_shared<bool>(false);
  auto session_selector_show_label_time = std::make_shared<bool>(false);
  auto open_session_selector_target = [&state, &runtime_open_options, &state_snapshot](
                                          std::string target_session_id, std::string status_prefix) -> ava::core::Result<ava::tui::TuiRuntimeStateSnapshot> {
    if (target_session_id.empty())
    {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "session branch target is missing session id"));
    }
    if (target_session_id == state.session.store.session_id())
      return state_snapshot(status_prefix + target_session_id + " (already open)");
    auto opened = ava::app::rpc::open_requested_session(state.session, runtime_open_options(), target_session_id);
    if (!opened)
      return std::unexpected(std::move(opened.error()));
    if (auto replaced = ava::app::replace_runtime_session(state.session, std::move(*opened)); !replaced)
      return std::unexpected(std::move(replaced.error()));
    return state_snapshot(status_prefix + target_session_id);
  };
  auto open_selector_branch = [&state, &open_session_selector_target, session_selector_sort, session_selector_show_archived](
                                  std::string_view selected_session_id, bool parent) -> ava::core::Result<ava::tui::TuiRuntimeStateSnapshot> {
    if (selected_session_id.empty())
    {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "session branch navigation is missing session id"));
    }
    auto tree = ava::session::build_session_tree(state.session.workspace_dir, state.session.paths.sessions_dir, state.session.store.session_id());
    if (!tree)
      return std::unexpected(std::move(tree.error()));
    auto target = parent ? ava::app::session_selector_parent_target(*tree, selected_session_id)
                         : ava::app::session_selector_child_target(*tree, selected_session_id, *session_selector_sort, *session_selector_show_archived);
    if (!target)
    {
      auto error =
          ava::core::Error(ava::core::ErrorCategory::NotFound, parent ? "selected session has no parent branch" : "selected session has no child branch");
      error.with_context("session_id", std::string(selected_session_id));
      return std::unexpected(std::move(error));
    }
    return open_session_selector_target(std::move(*target), parent ? std::string("opened parent branch ") : std::string("opened child branch "));
  };
  std::vector<ava::tui::TranscriptItem> initial_transcript;
  if (auto onboarding = ava::app::first_run_auth_onboarding_message(state.session))
  {
    initial_transcript.push_back(ava::tui::TranscriptItem{.label = "setup", .text = std::move(*onboarding)});
  }
  auto result = ava::tui::run_interactive_composer(ava::tui::TuiRuntimeOptions{
      .mode = ava::agent::to_string(state.session.mode),
      .provider = state.session.model.provider_id,
      .model = model_display(state.session.model),
      .session_id = state.session.store.session_id(),
      .session_path = state.session.store.session_path().string(),
      .workspace = state.session.current_dir.empty() ? state.session.workspace_dir.string() : state.session.current_dir.string(),
      .git_branch = git_branch_for_sidebar(state.session.workspace_dir),
      .app_version = std::string(version::kDisplayVersion),
      .context_source_count = state.session.context_sources.size(),
      .initial_status = keybind_status,
      .initial_transcript = std::move(initial_transcript),
      .slash_commands = ava::app::command_catalog_slash_items(state.session, hotkeys),
      .file_references = ava::app::file_reference_items(state.session),
      .custom_themes = custom_theme_options(),
      .project_trust = project_trust_snapshot(state.session.project_trust),
      .key_bindings = key_bindings,
      .token_status_provider = [&state]() { return token_status_for_session(state.session); },
      .reasoning_status_provider = [&state]() { return ava::app::reasoning_status_for_session(state.session); },
      .create_active_run_queues =
          [&state](ava::app::EventEnvelopeSink event_sink) {
            auto const active_job_coordinator = state.session.subagent_coordinator;
            auto const active_job_owner = state.session.store.session_id();
            auto queue = std::make_shared<ava::app::InteractiveRunQueue>(active_job_owner, ava::core::make_id("request"), std::move(event_sink));
            return ava::tui::TuiActiveRunQueues{
                .active_request_id = queue->active_request_id(),
                .queue_steering = [queue](std::string message) { return queue->queue_steering(std::move(message)); },
                .queue_follow_up = [queue](std::string message) { return queue->queue_follow_up(std::move(message)); },
                .take_steering_messages = [queue]() { return queue->take_steering_messages(); },
                .skip_active_steering = [queue](std::string_view reason) { return queue->skip_active_steering(reason); },
                .take_next_follow_up = [queue]() -> std::optional<ava::tui::TuiQueuedFollowUp> {
                  auto next = queue->take_next_follow_up();
                  if (!next)
                    return std::nullopt;
                  return ava::tui::TuiQueuedFollowUp{.request_id = next->request_id, .message = next->message};
                },
                .mark_follow_up_started =
                    [queue](ava::tui::TuiQueuedFollowUp const& follow_up) {
                      return queue->mark_follow_up_started(ava::app::InteractiveQueuedMessage{
                          .request_id = follow_up.request_id, .correlation_id = follow_up.request_id, .message = follow_up.message});
                    },
                .restore_latest = [queue]() -> ava::core::Result<ava::tui::TuiRestoredQueuedMessage> {
                  auto restored = queue->restore_latest();
                  if (!restored)
                    return std::unexpected(std::move(restored.error()));
                  return ava::tui::TuiRestoredQueuedMessage{.message = restored->message, .steering = restored->steering};
                },
                .run_nonblocking_command = [active_job_coordinator, active_job_owner](std::string const& submitted) -> std::optional<std::vector<std::string>> {
                  auto arguments = ava::app::active_jobs_command_arguments(submitted);
                  if (!arguments)
                    return std::nullopt;
                  auto command = ava::app::run_jobs_command(active_job_coordinator, active_job_owner, *arguments, true);
                  if (!command)
                    return std::vector<std::string>{command.error().format()};
                  return std::move(command->output);
                },
                .finish = [queue](bool canceled) { return queue->finish(canceled); }};
          },
      .on_submit =
          [&state, &hotkeys, &refresh_display_watch_state, &state_snapshot](std::string const& submitted, ava::tui::TuiSubmitContext context) {
            // Persistent rules resolve before the TUI fallback resolver in
            // context, so an exact durable Deny never reaches the in-memory
            // session-grant registry.
            auto permission_resolver =
                ava::permissions::build_persistent_permission_rule_resolver(permission_rule_store_for_session(state.session), context.permission_resolver);
            auto line_result = handle_line(state, submitted, permission_resolver, context.question_resolver, hotkeys, context.event_sink,
                                           context.cancel_requested, context.take_steering_messages, std::move(context.image_attachments));
            if (is_display_settings_command(submitted))
            {
              if (auto watched = refresh_display_watch_state(); !watched)
              {
                add_output(line_result, watched.error().format());
              }
            }
            auto append_result = [](LineResult& target, LineResult next) {
              target.quit = target.quit || next.quit;
              target.output.insert(target.output.end(), std::make_move_iterator(next.output.begin()), std::make_move_iterator(next.output.end()));
              target.tool_timeline.insert(target.tool_timeline.end(), std::make_move_iterator(next.tool_timeline.begin()),
                                          std::make_move_iterator(next.tool_timeline.end()));
            };
            while (!line_result.quit && (!context.cancel_requested || !context.cancel_requested()))
            {
              if (context.skip_active_steering)
              {
                if (auto skipped = context.skip_active_steering("run_completed_before_safe_point"); !skipped)
                {
                  add_output(line_result, skipped.error().format());
                  break;
                }
              }
              if (!context.take_next_follow_up)
                break;
              auto follow_up = context.take_next_follow_up();
              if (!follow_up)
                break;
              if (context.mark_follow_up_started)
              {
                if (auto started = context.mark_follow_up_started(*follow_up); !started)
                {
                  add_output(line_result, started.error().format());
                  break;
                }
              }
              append_result(line_result, handle_line(state, follow_up->message, permission_resolver, context.question_resolver, hotkeys, context.event_sink,
                                                     context.cancel_requested, context.take_steering_messages));
            }
            return ava::tui::TuiSubmitResult{.quit = line_result.quit,
                                             .output = line_result.output,
                                             .tool_timeline = tui_tool_timeline(line_result.tool_timeline),
                                             .context_source_count = state.session.context_sources.size(),
                                             .state_snapshot = state_snapshot({})};
          },
      .on_attach_image = [&state](std::string const& path) -> ava::core::Result<ava::session::ImageAttachmentRef> {
        auto source = std::filesystem::path(path);
        if (source.empty())
        {
          return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "usage: /attach <image-path>"));
        }
        if (!source.is_absolute())
        {
          auto const base = state.session.current_dir.empty() ? state.session.workspace_dir : state.session.current_dir;
          source = base / source;
        }
        return ava::session::import_image_attachment(state.session.store, source);
      },
      .on_paste_clipboard_image = [&state]() -> ava::core::Result<std::optional<ava::session::ImageAttachmentRef>> {
        return ava::app::import_clipboard_image_attachment(state.session.store);
      },
      .on_external_editor = [](std::string_view initial_text) -> ava::core::Result<std::optional<std::string>> {
        return edit_text_with_external_editor(initial_text);
      },
      .on_load_image_attachment = [&state](ava::session::ImageAttachmentRef const& attachment) -> ava::core::Result<ava::session::LoadedImageAttachment> {
        return ava::session::load_image_attachment(state.session.store, attachment);
      },
      .on_toggle_mode = [&state]() -> ava::core::Result<std::string> {
        auto result = ava::app::run_command(state.session, ava::app::CommandRequest{.command = "/mode"});
        if (!result)
          return std::unexpected(std::move(result.error()));
        return ava::agent::to_string(state.session.mode);
      },
      .on_cycle_reasoning = [&state]() -> ava::core::Result<std::string> { return ava::app::cycle_runtime_reasoning(state.session); },
      .on_cycle_model = [&state, &state_snapshot](bool forward) -> ava::core::Result<ava::tui::TuiRuntimeStateSnapshot> {
        auto model = forward ? ava::app::rpc::next_runtime_model(state.session) : ava::app::rpc::previous_runtime_model(state.session);
        if (!model)
          return std::unexpected(std::move(model.error()));
        auto switched = ava::app::switch_runtime_model(state.session, std::move(*model));
        if (!switched)
          return std::unexpected(std::move(switched.error()));
        return state_snapshot(*switched ? "model cycled" : "model already selected");
      },
      .on_reload_key_bindings = [&state, &key_bindings, &hotkeys, &state_snapshot]() -> ava::core::Result<ava::tui::TuiKeyBindingReloadResult> {
        auto loaded = ava::tui::load_key_bindings(state.session.paths.ava_config_dir / "keybinds.json");
        if (!loaded)
          return std::unexpected(std::move(loaded.error()));
        key_bindings = std::move(*loaded);
        hotkeys = command_hotkeys_from_key_bindings(key_bindings);
        return ava::tui::TuiKeyBindingReloadResult{.key_bindings = key_bindings, .state = state_snapshot("keybindings reloaded")};
      },
      .on_reload_display_settings = [&state, &state_snapshot, &refresh_display_watch_state]() -> ava::core::Result<ava::tui::TuiRuntimeStateSnapshot> {
        auto loaded = ava::app::apply_tui_display_settings(state.session.paths);
        if (!loaded)
          return std::unexpected(std::move(loaded.error()));
        if (auto watched = refresh_display_watch_state(); !watched)
          return std::unexpected(std::move(watched.error()));
        return state_snapshot(display_theme_status("display theme reloaded"));
      },
      .on_maybe_reload_display_settings = [&state, &state_snapshot, display_watch_state,
                                           display_watch_mutex]() -> ava::core::Result<std::optional<ava::tui::TuiRuntimeStateSnapshot>> {
        auto watched = ava::app::load_tui_display_settings_watch_state(state.session.paths);
        if (!watched)
          return std::unexpected(std::move(watched.error()));
        std::lock_guard lock(*display_watch_mutex);
        if (*display_watch_state && !ava::app::tui_display_settings_watch_state_changed(**display_watch_state, *watched))
        {
          return std::optional<ava::tui::TuiRuntimeStateSnapshot>{};
        }
        auto loaded = ava::app::apply_tui_display_settings(state.session.paths);
        if (!loaded)
          return std::unexpected(std::move(loaded.error()));
        *display_watch_state = std::move(*watched);
        return state_snapshot(display_theme_status("display theme auto-reloaded"));
      },
      .model_selector_view = [&state]() { return ava::app::model_selector_view(state.session, "Enter switch model · type to filter · Esc cancel"); },
      .scoped_model_selector_view = [&state]() { return ava::app::scoped_model_selector_view(state.session, scoped_model_selector_footer_hint()); },
      .session_selector_view =
          [&state, session_selector_sort, session_selector_named_only, session_selector_show_paths, session_selector_show_archived,
           session_selector_show_label_time]() {
            *session_selector_sort = ava::app::SessionSelectorSort::Recent;
            *session_selector_named_only = false;
            *session_selector_show_paths = true;
            *session_selector_show_archived = false;
            *session_selector_show_label_time = false;
            return ava::app::session_selector_view(
                state.session, *session_selector_sort,
                session_selector_footer_hint(*session_selector_sort, *session_selector_named_only, *session_selector_show_paths,
                                             *session_selector_show_archived, *session_selector_show_label_time),
                *session_selector_named_only, *session_selector_show_paths, *session_selector_show_archived, *session_selector_show_label_time);
          },
      .on_session_selector_sort_cycle =
          [&state, session_selector_sort, session_selector_named_only, session_selector_show_paths, session_selector_show_archived,
           session_selector_show_label_time]() {
            *session_selector_sort = ava::app::next_session_selector_sort(*session_selector_sort);
            return ava::app::session_selector_view(
                state.session, *session_selector_sort,
                session_selector_footer_hint(*session_selector_sort, *session_selector_named_only, *session_selector_show_paths,
                                             *session_selector_show_archived, *session_selector_show_label_time),
                *session_selector_named_only, *session_selector_show_paths, *session_selector_show_archived, *session_selector_show_label_time);
          },
      .on_session_selector_named_filter_toggle =
          [&state, session_selector_sort, session_selector_named_only, session_selector_show_paths, session_selector_show_archived,
           session_selector_show_label_time]() {
            *session_selector_named_only = !*session_selector_named_only;
            return ava::app::session_selector_view(
                state.session, *session_selector_sort,
                session_selector_footer_hint(*session_selector_sort, *session_selector_named_only, *session_selector_show_paths,
                                             *session_selector_show_archived, *session_selector_show_label_time),
                *session_selector_named_only, *session_selector_show_paths, *session_selector_show_archived, *session_selector_show_label_time);
          },
      .on_session_selector_path_display_toggle =
          [&state, session_selector_sort, session_selector_named_only, session_selector_show_paths, session_selector_show_archived,
           session_selector_show_label_time]() {
            *session_selector_show_paths = !*session_selector_show_paths;
            return ava::app::session_selector_view(
                state.session, *session_selector_sort,
                session_selector_footer_hint(*session_selector_sort, *session_selector_named_only, *session_selector_show_paths,
                                             *session_selector_show_archived, *session_selector_show_label_time),
                *session_selector_named_only, *session_selector_show_paths, *session_selector_show_archived, *session_selector_show_label_time);
          },
      .on_session_selector_archived_filter_toggle =
          [&state, session_selector_sort, session_selector_named_only, session_selector_show_paths, session_selector_show_archived,
           session_selector_show_label_time]() {
            *session_selector_show_archived = !*session_selector_show_archived;
            return ava::app::session_selector_view(
                state.session, *session_selector_sort,
                session_selector_footer_hint(*session_selector_sort, *session_selector_named_only, *session_selector_show_paths,
                                             *session_selector_show_archived, *session_selector_show_label_time),
                *session_selector_named_only, *session_selector_show_paths, *session_selector_show_archived, *session_selector_show_label_time);
          },
      .on_session_selector_label_timestamp_toggle =
          [&state, session_selector_sort, session_selector_named_only, session_selector_show_paths, session_selector_show_archived,
           session_selector_show_label_time]() {
            *session_selector_show_label_time = !*session_selector_show_label_time;
            return ava::app::session_selector_view(
                state.session, *session_selector_sort,
                session_selector_footer_hint(*session_selector_sort, *session_selector_named_only, *session_selector_show_paths,
                                             *session_selector_show_archived, *session_selector_show_label_time),
                *session_selector_named_only, *session_selector_show_paths, *session_selector_show_archived, *session_selector_show_label_time);
          },
      .on_session_selector_archive = [&state, session_selector_sort, session_selector_named_only, session_selector_show_paths, session_selector_show_archived,
                                      session_selector_show_label_time](std::string_view session_id) -> ava::core::Result<ava::tui::SelectListView> {
        auto command = std::string("/sessions archive ") + std::string(session_id) + " --confirm";
        auto archived = ava::app::run_command(state.session, ava::app::CommandRequest{.command = std::move(command)});
        if (!archived)
          return std::unexpected(std::move(archived.error()));
        return ava::app::session_selector_view(state.session, *session_selector_sort,
                                               session_selector_footer_hint(*session_selector_sort, *session_selector_named_only, *session_selector_show_paths,
                                                                            *session_selector_show_archived, *session_selector_show_label_time),
                                               *session_selector_named_only, *session_selector_show_paths, *session_selector_show_archived,
                                               *session_selector_show_label_time);
      },
      .on_session_selector_unarchive = [&state, session_selector_sort, session_selector_named_only, session_selector_show_paths, session_selector_show_archived,
                                        session_selector_show_label_time](std::string_view session_id) -> ava::core::Result<ava::tui::SelectListView> {
        auto command = std::string("/sessions unarchive ") + std::string(session_id);
        auto unarchived = ava::app::run_command(state.session, ava::app::CommandRequest{.command = std::move(command)});
        if (!unarchived)
          return std::unexpected(std::move(unarchived.error()));
        return ava::app::session_selector_view(state.session, *session_selector_sort,
                                               session_selector_footer_hint(*session_selector_sort, *session_selector_named_only, *session_selector_show_paths,
                                                                            *session_selector_show_archived, *session_selector_show_label_time),
                                               *session_selector_named_only, *session_selector_show_paths, *session_selector_show_archived,
                                               *session_selector_show_label_time);
      },
      .on_session_selector_branch_parent = [open_selector_branch](std::string_view session_id) -> ava::core::Result<ava::tui::TuiRuntimeStateSnapshot> {
        return open_selector_branch(session_id, true);
      },
      .on_session_selector_branch_child = [open_selector_branch](std::string_view session_id) -> ava::core::Result<ava::tui::TuiRuntimeStateSnapshot> {
        return open_selector_branch(session_id, false);
      },
      .remember_permission_rule = [&state](
                                      ava::permissions::PermissionPrompt const& prompt,
                                      ava::permissions::PermissionAction action) { return remember_permission_rule_for_prompt(state.session, prompt, action); },
      .on_settings_selected = [&state, &state_snapshot,
                               &refresh_display_watch_state](std::string_view value) -> ava::core::Result<ava::tui::TuiRuntimeStateSnapshot> {
        if (value == "settings:keybindings.validate")
        {
          auto validated = ava::app::run_command(state.session, ava::app::CommandRequest{.command = "/keybindings validate"});
          if (!validated)
            return std::unexpected(std::move(validated.error()));
          auto status = validated->output.empty() ? std::string("keybindings validation complete") : validated->output.front();
          return state_snapshot(std::move(status));
        }
        constexpr std::string_view trust_prefix = "settings:trust.";
        if (value.starts_with(trust_prefix))
        {
          auto action = value.substr(trust_prefix.size());
          auto command = std::string("/trust ") + std::string(action);
          auto trusted = ava::app::run_command(state.session, ava::app::CommandRequest{.command = std::move(command)});
          if (!trusted)
            return std::unexpected(std::move(trusted.error()));
          auto status = trusted->output.empty() ? std::string("trust action complete") : trusted->output.front();
          return state_snapshot(std::move(status));
        }
        constexpr std::string_view theme_prefix = "theme:";
        if (!value.starts_with(theme_prefix))
          return state_snapshot("view closed");
        auto command = std::string("/theme ") + std::string(value.substr(theme_prefix.size()));
        auto themed = ava::app::run_command(state.session, ava::app::CommandRequest{.command = std::move(command)});
        if (!themed)
          return std::unexpected(std::move(themed.error()));
        if (auto watched = refresh_display_watch_state(); !watched)
          return std::unexpected(std::move(watched.error()));
        auto status = themed->output.empty() ? std::string("theme updated") : themed->output.front();
        if (auto const newline = status.find('\n'); newline != std::string::npos)
          status.erase(newline);
        return state_snapshot(std::move(status));
      },
      .on_model_selected = [&state, &state_snapshot](std::string_view value) -> ava::core::Result<ava::tui::TuiRuntimeStateSnapshot> {
        auto const separator = value.find('/');
        if (separator == std::string_view::npos || separator == 0 || separator + 1 >= value.size())
        {
          return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "model selection is missing provider/model"));
        }
        auto model = ava::app::resolve_runtime_model(state.session.paths, value.substr(0, separator), value.substr(separator + 1));
        if (!model)
          return std::unexpected(std::move(model.error()));
        auto switched = ava::app::switch_runtime_model(state.session, std::move(*model));
        if (!switched)
          return std::unexpected(std::move(switched.error()));
        return state_snapshot(*switched ? "model switched" : "model already selected");
      },
      .on_scoped_model_toggled = [&state](ava::tui::SelectListView const& previous, std::string_view value) -> ava::core::Result<ava::tui::SelectListView> {
        return toggle_scoped_model(state.session, previous, value);
      },
      .on_scoped_model_enable_all = [&state](ava::tui::SelectListView const& previous, std::vector<std::string> values)
          -> ava::core::Result<ava::tui::SelectListView> { return enable_scoped_models(state.session, previous, std::move(values)); },
      .on_scoped_model_clear_all = [&state](ava::tui::SelectListView const& previous, std::vector<std::string> values)
          -> ava::core::Result<ava::tui::SelectListView> { return clear_scoped_models(state.session, previous, std::move(values)); },
      .on_scoped_model_toggle_provider = [&state](ava::tui::SelectListView const& previous, std::string_view value)
          -> ava::core::Result<ava::tui::SelectListView> { return toggle_scoped_model_provider(state.session, previous, value); },
      .on_scoped_model_reorder = [&state](ava::tui::SelectListView const& previous, std::string_view value, bool up)
          -> ava::core::Result<ava::tui::SelectListView> { return reorder_scoped_model(state.session, previous, value, up); },
      .on_scoped_model_save = [&state]() -> ava::core::Result<std::string> { return save_scoped_model_cycle(state.session); },
      .on_session_selected = [&state, &runtime_open_options, &state_snapshot](std::string_view value) -> ava::core::Result<ava::tui::TuiRuntimeStateSnapshot> {
        if (value.empty())
        {
          return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "session selection is missing session id"));
        }
        if (value == state.session.store.session_id())
          return state_snapshot("session already open");
        auto opened = ava::app::rpc::open_requested_session(state.session, runtime_open_options(), value);
        if (!opened)
          return std::unexpected(std::move(opened.error()));
        if (auto replaced = ava::app::replace_runtime_session(state.session, std::move(*opened)); !replaced)
          return std::unexpected(std::move(replaced.error()));
        return state_snapshot("session opened");
      }});
  std::cout << std::flush;
  return result;
}

}  // namespace

namespace ava::app {

int run_interactive(runtime::Session& session)
{
  ShellState state{.session = session};
  if (ava::tui::terminal_is_tty())
    return run_tui(state);
  return run_line_shell(state);
}

}  // namespace ava::app
