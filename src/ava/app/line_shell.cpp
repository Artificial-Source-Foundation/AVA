#include "ava/app/line_shell.h"

#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ava/app/command_catalog.h"
#include "ava/app/commands.h"
#include "ava/config/auth.h"
#include "ava/provider/curl_transport.h"
#include "ava/provider/registry.h"
#include "ava/tui/composer.h"
#include "ava/tui/keybindings.h"
#include "ava/tui/runtime.h"
#include "ava/tui/terminal.h"

namespace {

constexpr std::string_view kAvaTuiVersion = "0.32";

void print_shell_help() { std::cout << ava::app::command_help_text() << '\n'; }

std::string git_branch_for_sidebar(const std::filesystem::path& workspace) {
  const auto head_path = workspace / ".git" / "HEAD";
  std::ifstream input(head_path);
  if (!input) return {};
  std::string head;
  std::getline(input, head);
  constexpr std::string_view ref_prefix = "ref: refs/heads/";
  if (head.rfind(ref_prefix, 0) == 0) return head.substr(ref_prefix.size());
  return head.size() > 12 ? head.substr(0, 12) : head;
}

std::vector<ava::app::CommandHotkey> command_hotkeys_from_key_bindings(const ava::tui::TuiKeyBindings& key_bindings) {
  std::vector<ava::app::CommandHotkey> hotkeys;
  for (const auto& item : ava::tui::key_binding_help_items(key_bindings)) {
    hotkeys.push_back(
        ava::app::CommandHotkey{.action = item.action, .description = item.description, .keys = item.keys});
  }
  return hotkeys;
}

ava::tui::ToolTimelineStatus tui_tool_status(ava::agent::ToolTimelineStatus status) {
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

std::vector<ava::tui::ToolTimelineItem> tui_tool_timeline(const std::vector<ava::agent::ToolTimelineEntry>& entries) {
  std::vector<ava::tui::ToolTimelineItem> items;
  items.reserve(entries.size());
  for (const auto& entry : entries) {
    items.push_back(ava::tui::ToolTimelineItem{.status = tui_tool_status(entry.status),
                                               .name = entry.name,
                                               .argument_summary = entry.argument_summary,
                                               .result_summary = entry.result_summary});
  }
  return items;
}

void print_resume_command(const ava::session::SessionStore& store) {
  std::cout << "Resume this session with: ava --session " << store.session_id() << '\n';
}

bool is_compact_command(std::string_view line) noexcept {
  return line == "/compact" || (line.starts_with("/compact") && line.size() > 8 && line[8] == ' ');
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

void add_output(LineResult& result, std::string text) { result.output.push_back(std::move(text)); }

template <typename Callback>
LineResult with_provider_runtime(ShellState& state, std::string_view offline_suffix, Callback callback) {
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
  ava::provider::RetryTransport retry_transport(transport);
  ava::app::RuntimeRunOptions run_options;
  run_options.access_token = (*credential)->access_token;
  run_options.credential_type = (*credential)->credential_type;
  run_options.openai_oauth = (*credential)->provider_id == "openai" && (*credential)->credential_type == "oauth";
  run_options.openai_account_id = (*credential)->account_id;
  return callback(**provider, retry_transport, run_options);
}

LineResult handle_line(ShellState& state, const std::string& line,
                       ava::permissions::PermissionResolver permission_resolver = nullptr,
                       ava::agent::QuestionResolver question_resolver = nullptr,
                       const std::vector<ava::app::CommandHotkey>& hotkeys = {},
                       ava::app::RuntimeEventSink event_sink = nullptr,
                       std::function<bool()> cancel_requested = nullptr) {
  LineResult line_result;
  if (line.empty()) return line_result;
  if (ava::app::is_backend_command(line)) {
    if (is_compact_command(line)) {
      return with_provider_runtime(
          state, "\nother slash tool commands still work offline.",
          [&](const ava::provider::Provider& provider, ava::provider::Transport& transport,
              ava::app::RuntimeRunOptions run_options) {
            run_options.cancel_requested = cancel_requested;
            auto command_result = ava::app::run_command(
                state.session,
                ava::app::CommandRequest{.command = line,
                                         .permission_resolver = permission_resolver,
                                         .compaction_summary_generator =
                                             [&](const std::vector<ava::session::SessionEntry>& entries,
                                                 const ava::session::CompactionConfig& config,
                                                 std::string_view instructions, std::size_t estimated_tokens) {
                                               return ava::app::generate_compaction_summary(
                                                   state.session, entries, config, instructions, estimated_tokens,
                                                   provider, transport, run_options);
                                             },
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
    auto command_result = ava::app::run_command(
        state.session,
        ava::app::CommandRequest{.command = line, .permission_resolver = permission_resolver, .hotkeys = hotkeys});
    if (!command_result) {
      add_output(line_result, command_result.error().format());
      return line_result;
    }
    line_result.quit = command_result->quit;
    line_result.output = std::move(command_result->output);
    line_result.tool_timeline = std::move(command_result->tool_timeline);
    return line_result;
  }

  return with_provider_runtime(state, "\nslash tool commands still work offline.",
                             [&](const ava::provider::Provider& provider, ava::provider::Transport& transport,
                                 ava::app::RuntimeRunOptions run_options) {
                               run_options.permission_resolver = permission_resolver;
                               run_options.question_resolver = question_resolver;
                               run_options.event_sink = std::move(event_sink);
                               run_options.cancel_requested = std::move(cancel_requested);
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

int run_line_shell(ShellState state) {
  std::cout << "AVA 0.32 terminal shell\n";
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

    const auto result = handle_line(state, line);
    for (const auto& output : result.output) {
      for (const auto& output_line : ava::tui::split_lines(output)) {
        std::cout << ava::tui::sanitize_terminal_text(output_line) << '\n';
      }
    }
    if (result.quit) {
      print_resume_command(state.session.store);
      return 0;
    }
  }
}

int run_tui(ShellState state) {
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
      .model = state.session.model.model_id,
      .session_id = state.session.store.session_id(),
      .workspace =
          state.session.current_dir.empty() ? state.session.workspace_dir.string() : state.session.current_dir.string(),
      .git_branch = git_branch_for_sidebar(state.session.workspace_dir),
      .app_version = std::string(kAvaTuiVersion),
      .initial_status = keybind_status,
      .slash_commands = ava::app::command_catalog_slash_items(hotkeys),
      .key_bindings = key_bindings,
      .on_submit =
          [&state, hotkeys](const std::string& submitted,
                            const ava::permissions::PermissionResolver& permission_resolver,
                            const ava::agent::QuestionResolver& question_resolver,
                            ava::app::RuntimeEventSink event_sink, std::function<bool()> cancel_requested) {
            const auto line_result = handle_line(state, submitted, permission_resolver, question_resolver, hotkeys,
                                                 std::move(event_sink), std::move(cancel_requested));
            return ava::tui::TuiSubmitResult{.quit = line_result.quit,
                                             .output = line_result.output,
                                             .tool_timeline = tui_tool_timeline(line_result.tool_timeline)};
          },
      .on_toggle_mode = [&state]() -> ava::core::Result<std::string> {
        auto result = ava::app::run_command(state.session, ava::app::CommandRequest{.command = "/mode"});
        if (!result) return std::unexpected(std::move(result.error()));
        return ava::agent::to_string(state.session.mode);
      }});
  std::cout << std::flush;
  return result;
}

}  // namespace

namespace ava::app {

int run_interactive(RuntimeSession& session) {
  ShellState state{.session = session};
  if (ava::tui::terminal_is_tty()) return run_tui(state);
  return run_line_shell(state);
}

}  // namespace ava::app
