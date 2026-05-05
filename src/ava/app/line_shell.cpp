#include "ava/app/line_shell.h"

#include "ava/app/command_palette.h"
#include "ava/app/commands.h"
#include "ava/app/interactive_run_queue.h"
#include "ava/app/reasoning_controls.h"

#include "ava/tui/composer.h"
#include "ava/tui/keybindings.h"
#include "ava/tui/runtime.h"
#include "ava/tui/terminal.h"

#include "ava/config/auth.h"
#include "ava/config/model_profiles.h"

#include "ava/session/stats.h"

#include "ava/provider/curl_transport.h"
#include "ava/provider/registry.h"

#include "ava/core/ids.h"
#include "ava/core/version.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace version = ava::core::version;

void print_shell_help()
{
  std::cout << ava::app::command_help_text() << '\n';
}

std::string git_branch_for_sidebar(std::filesystem::path const& workspace)
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

std::vector<ava::app::CommandHotkey> command_hotkeys_from_key_bindings(ava::tui::TuiKeyBindings const& key_bindings)
{
  std::vector<ava::app::CommandHotkey> hotkeys;
  for (auto const& item : ava::tui::key_binding_help_items(key_bindings)) {
    hotkeys.push_back(
        ava::app::CommandHotkey{.action = item.action, .description = item.description, .keys = item.keys});
  }
  return hotkeys;
}

ava::tui::ToolTimelineStatus tui_tool_status(ava::agent::ToolTimelineStatus status)
{
  switch (status) {
    case ava::agent::ToolTimelineStatus::Running:
      return ava::tui::ToolTimelineStatus::Running;
    case ava::agent::ToolTimelineStatus::Success:
      return ava::tui::ToolTimelineStatus::Success;
    case ava::agent::ToolTimelineStatus::Error:
      return ava::tui::ToolTimelineStatus::Error;
  }
  return ava::tui::ToolTimelineStatus::Error;
}

std::vector<ava::tui::ToolTimelineItem> tui_tool_timeline(std::vector<ava::agent::ToolTimelineEntry> const& entries)
{
  std::vector<ava::tui::ToolTimelineItem> items;
  items.reserve(entries.size());
  for (auto const& entry : entries) {
    items.push_back(ava::tui::ToolTimelineItem{.status = tui_tool_status(entry.status),
                                               .name = entry.name,
                                               .argument_summary = entry.argument_summary,
                                               .result_summary = entry.result_summary});
  }
  return items;
}

void print_resume_command(ava::session::SessionStore const& store)
{
  std::cout << "Resume this session with: ava --session " << store.session_id() << '\n';
}

bool is_compact_command(std::string_view line) noexcept
{
  return line == "/compact" || (line.starts_with("/compact") && line.size() > 8 && line[8] == ' ');
}

void add_token_component(std::optional<long long>& total, std::optional<long long> value)
{
  if (!value) return;
  if (!total) total = 0;
  *total += *value;
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

struct ShellState {
  // Lifetime contract: references are stack-scoped and must outlive each run loop invocation.
  ava::app::RuntimeSession& session;
};

struct LineResult {
  bool quit = false;
  std::vector<std::string> output;
  std::vector<ava::agent::ToolTimelineEntry> tool_timeline;
};

void add_output(LineResult& result, std::string text)
{
  result.output.push_back(std::move(text));
}

template <typename Callback>
LineResult with_provider_runtime(ShellState& state, std::string_view offline_suffix, Callback callback)
{
  LineResult line_result;
  ava::provider::CurlCliTransport transport;
  auto credential =
      ava::config::provider_credential_for_request(state.session.paths, state.session.model.provider_id, transport);
  if (!credential) {
    add_output(line_result, credential.error().format() + std::string(offline_suffix));
    return line_result;
  }
  if (!*credential) {
    add_output(line_result, "Auth is required for provider `" + state.session.model.provider_id +
                                "`. Configure a credential in " + state.session.paths.auth_file.string() +
                                std::string(offline_suffix));
    return line_result;
  }
  auto registry = ava::provider::builtin_provider_registry();
  auto provider = registry.create(state.session.model.provider_id);
  if (!provider) {
    add_output(line_result, provider.error().format() + std::string(offline_suffix));
    return line_result;
  }
  ava::app::RuntimeRunOptions run_options;
  run_options.access_token = (*credential)->access_token;
  run_options.credential_type = (*credential)->credential_type;
  run_options.openai_oauth = (*credential)->provider_id == "openai" && (*credential)->credential_type == "oauth";
  run_options.openai_account_id = (*credential)->account_id;
  run_options.enable_transport_retries = true;
  return callback(**provider, transport, run_options);
}

LineResult handle_line(ShellState& state, std::string const& line,
                       ava::permissions::PermissionResolver permission_resolver = nullptr,
                       ava::agent::QuestionResolver question_resolver = nullptr,
                       std::vector<ava::app::CommandHotkey> const& hotkeys = {},
                       ava::app::RuntimeEventSink event_sink = nullptr,
                       std::function<bool()> cancel_requested = nullptr,
                       std::function<ava::core::Result<std::vector<std::string>>()> take_steering_messages = nullptr)
{
  LineResult line_result;
  if (line.empty()) return line_result;
  if (ava::app::is_backend_command(line)) {
    if (is_compact_command(line)) {
      return with_provider_runtime(
          state, "\nother slash tool commands still work offline.",
          [&](ava::provider::Provider const& provider, ava::provider::Transport& transport,
              ava::app::RuntimeRunOptions run_options) {
            run_options.cancel_requested = cancel_requested;
            run_options.event_sink = event_sink;
            auto command_result = ava::app::run_command(
                state.session,
                ava::app::CommandRequest{.command = line,
                                         .event_sink = event_sink,
                                         .permission_resolver = permission_resolver,
                                         .question_resolver = question_resolver,
                                         .compaction_summary_generator =
                                             [&](std::vector<ava::session::SessionEntry> const& entries,
                                                 ava::session::CompactionConfig const& config,
                                                 std::string_view instructions, std::size_t estimated_tokens) {
                                               return ava::app::generate_compaction_summary(
                                                   state.session, entries, config, instructions, estimated_tokens,
                                                   provider, transport, run_options);
                                             },
                                         .cancel_requested = cancel_requested,
                                         .hotkeys = hotkeys});
            if (!command_result) {
              LineResult compact_result;
              add_output(compact_result, command_result.error().format());
              return compact_result;
            }
            return LineResult{.quit = command_result->quit,
                              .output = std::move(command_result->output),
                              .tool_timeline = std::move(command_result->tool_timeline)};
          });
    }
    auto command_result =
        ava::app::run_command(state.session, ava::app::CommandRequest{.command = line,
                                                                      .permission_resolver = permission_resolver,
                                                                      .question_resolver = question_resolver,
                                                                      .cancel_requested = cancel_requested,
                                                                      .hotkeys = hotkeys});
    if (!command_result) {
      add_output(line_result, command_result.error().format());
      return line_result;
    }
    line_result.quit = command_result->quit;
    line_result.output = std::move(command_result->output);
    line_result.tool_timeline = std::move(command_result->tool_timeline);
    return line_result;
  }
  if (line.starts_with('/')) {
    auto const end = line.find_first_of(" \t\r\n");
    auto const command = line.substr(0, end == std::string::npos ? line.size() : end);
    add_output(line_result, "Unknown command: " + command + ". Type /help to list commands.");
    return line_result;
  }

  return with_provider_runtime(state, "\nslash tool commands still work offline.",
                               [&](ava::provider::Provider const& provider, ava::provider::Transport& transport,
                                   ava::app::RuntimeRunOptions run_options) {
                                 run_options.permission_resolver = permission_resolver;
                                 run_options.question_resolver = question_resolver;
                                 run_options.event_sink = std::move(event_sink);
                                 run_options.cancel_requested = std::move(cancel_requested);
                                 run_options.take_steering_messages = std::move(take_steering_messages);
                                 auto result =
                                     ava::app::run_prompt(state.session, line, provider, transport, run_options);
                                 LineResult prompt_result;
                                 if (!result) {
                                   add_output(prompt_result, result.error().format());
                                   return prompt_result;
                                 }
                                 prompt_result.tool_timeline = std::move(result->tool_timeline);
                                 if (!result->final_text.empty()) {
                                   add_output(prompt_result, result->final_text);
                                 } else {
                                   add_output(prompt_result, "done");
                                 }
                                 return prompt_result;
                               });
}

int run_line_shell(ShellState state)
{
  std::cout << "AVA " << version::kDisplayVersion << " terminal shell\n";
  std::cout << "mode: " << ava::agent::to_string(state.session.mode)
            << " | session: " << state.session.store.session_id() << "\n";
  std::cout << "provider: " << state.session.model.provider_id << " | model: " << state.session.model.model_id << "\n";
  print_shell_help();

  std::string line;
  while (true) {
    std::cout << "\n[" << ava::agent::to_string(state.session.mode) << "] ava> " << std::flush;
    if (!std::getline(std::cin, line)) {
      std::cout << '\n';
      print_resume_command(state.session.store);
      return 0;
    }

    auto const result = handle_line(state, line);
    for (auto const& output : result.output) {
      for (auto const& output_line : ava::tui::split_lines(output)) {
        std::cout << ava::tui::sanitize_terminal_text(output_line) << '\n';
      }
    }
    if (result.quit) {
      print_resume_command(state.session.store);
      return 0;
    }
  }
}

int run_tui(ShellState state)
{
  auto key_bindings = ava::tui::default_key_bindings();
  std::string keybind_status;
  if (auto loaded = ava::tui::load_key_bindings(state.session.paths.ava_config_dir / "keybinds.json"); loaded) {
    key_bindings = std::move(*loaded);
  } else {
    keybind_status = loaded.error().format();
  }
  auto hotkeys = command_hotkeys_from_key_bindings(key_bindings);
  auto result = ava::tui::run_interactive_composer(ava::tui::TuiRuntimeOptions{
      .mode = ava::agent::to_string(state.session.mode),
      .provider = state.session.model.provider_id,
      .model = state.session.model.display_name.empty() ? ava::config::model_display_label(state.session.model.model_id)
                                                        : state.session.model.display_name,
      .session_id = state.session.store.session_id(),
      .workspace =
          state.session.current_dir.empty() ? state.session.workspace_dir.string() : state.session.current_dir.string(),
      .git_branch = git_branch_for_sidebar(state.session.workspace_dir),
      .app_version = std::string(version::kDisplayVersion),
      .context_source_count = state.session.context_sources.size(),
      .initial_status = keybind_status,
      .slash_commands = ava::app::command_catalog_slash_items(state.session, hotkeys),
      .key_bindings = key_bindings,
      .token_status_provider = [&state]() { return token_status_for_session(state.session); },
      .reasoning_status_provider = [&state]() { return ava::app::reasoning_status_for_session(state.session); },
      .create_active_run_queues =
          [&state](ava::app::EventEnvelopeSink event_sink) {
            auto queue = std::make_shared<ava::app::InteractiveRunQueue>(
                state.session.store.session_id(), ava::core::make_id("request"), std::move(event_sink));
            return ava::tui::TuiActiveRunQueues{
                .active_request_id = queue->active_request_id(),
                .queue_steering = [queue](std::string message) { return queue->queue_steering(std::move(message)); },
                .queue_follow_up = [queue](std::string message) { return queue->queue_follow_up(std::move(message)); },
                .take_steering_messages = [queue]() { return queue->take_steering_messages(); },
                .skip_active_steering =
                    [queue](std::string_view reason) { return queue->skip_active_steering(reason); },
                .take_next_follow_up = [queue]() -> std::optional<ava::tui::TuiQueuedFollowUp> {
                  auto next = queue->take_next_follow_up();
                  if (!next) return std::nullopt;
                  return ava::tui::TuiQueuedFollowUp{.request_id = next->request_id, .message = next->message};
                },
                .mark_follow_up_started =
                    [queue](ava::tui::TuiQueuedFollowUp const& follow_up) {
                      return queue->mark_follow_up_started(
                          ava::app::InteractiveQueuedMessage{.request_id = follow_up.request_id,
                                                             .correlation_id = follow_up.request_id,
                                                             .message = follow_up.message});
                    },
                .restore_latest = [queue]() -> ava::core::Result<ava::tui::TuiRestoredQueuedMessage> {
                  auto restored = queue->restore_latest();
                  if (!restored) return std::unexpected(std::move(restored.error()));
                  return ava::tui::TuiRestoredQueuedMessage{.message = restored->message,
                                                            .steering = restored->steering};
                },
                .finish = [queue](bool canceled) { return queue->finish(canceled); }};
          },
      .on_submit =
          [&state, hotkeys](std::string const& submitted, ava::tui::TuiSubmitContext context) {
            auto line_result =
                handle_line(state, submitted, context.permission_resolver, context.question_resolver, hotkeys,
                            context.event_sink, context.cancel_requested, context.take_steering_messages);
            auto append_result = [](LineResult& target, LineResult next) {
              target.quit = target.quit || next.quit;
              target.output.insert(target.output.end(), std::make_move_iterator(next.output.begin()),
                                   std::make_move_iterator(next.output.end()));
              target.tool_timeline.insert(target.tool_timeline.end(),
                                          std::make_move_iterator(next.tool_timeline.begin()),
                                          std::make_move_iterator(next.tool_timeline.end()));
            };
            while (!line_result.quit && (!context.cancel_requested || !context.cancel_requested())) {
              if (context.skip_active_steering) {
                if (auto skipped = context.skip_active_steering("run_completed_before_safe_point"); !skipped) {
                  add_output(line_result, skipped.error().format());
                  break;
                }
              }
              if (!context.take_next_follow_up) break;
              auto follow_up = context.take_next_follow_up();
              if (!follow_up) break;
              if (context.mark_follow_up_started) {
                if (auto started = context.mark_follow_up_started(*follow_up); !started) {
                  add_output(line_result, started.error().format());
                  break;
                }
              }
              append_result(line_result, handle_line(state, follow_up->message, context.permission_resolver,
                                                     context.question_resolver, hotkeys, context.event_sink,
                                                     context.cancel_requested, context.take_steering_messages));
            }
            return ava::tui::TuiSubmitResult{.quit = line_result.quit,
                                             .output = line_result.output,
                                             .tool_timeline = tui_tool_timeline(line_result.tool_timeline)};
          },
      .on_toggle_mode = [&state]() -> ava::core::Result<std::string> {
        auto result = ava::app::run_command(state.session, ava::app::CommandRequest{.command = "/mode"});
        if (!result) return std::unexpected(std::move(result.error()));
        return ava::agent::to_string(state.session.mode);
      },
      .on_cycle_reasoning = [&state]() -> ava::core::Result<std::string> {
        return ava::app::cycle_runtime_reasoning(state.session);
      }});
  std::cout << std::flush;
  return result;
}

}  // namespace

namespace ava::app {

int run_interactive(RuntimeSession& session)
{
  ShellState state{.session = session};
  if (ava::tui::terminal_is_tty()) return run_tui(state);
  return run_line_shell(state);
}

}  // namespace ava::app
