#include "ava/app/line_shell.h"

#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "ava/app/commands.h"
#include "ava/config/auth.h"
#include "ava/config/openai_oauth.h"
#include "ava/provider/curl_transport.h"
#include "ava/provider/openai_provider.h"
#include "ava/tui/composer.h"
#include "ava/tui/runtime.h"
#include "ava/tui/terminal.h"

namespace {

void print_shell_help() { std::cout << ava::app::command_help_text() << '\n'; }

std::vector<ava::tui::SlashCommandItem> slash_command_palette_items() {
  return {
      ava::tui::SlashCommandItem{.command = "/help", .description = "Show this help", .category = "General"},
      ava::tui::SlashCommandItem{.command = "/mode", .description = "Toggle build/plan mode", .category = "General"},
      ava::tui::SlashCommandItem{.command = "/quit", .description = "Exit", .category = "General"},
      ava::tui::SlashCommandItem{
          .command = "/sessions", .description = "List sessions for this workspace", .category = "Sessions"},
      ava::tui::SlashCommandItem{
          .command = "/context", .description = "List loaded context sources", .category = "Sessions"},
      ava::tui::SlashCommandItem{.command = "/compact",
                                 .description = "Record a manual compaction entry",
                                 .hint = "[instructions]",
                                 .category = "Sessions"},
      ava::tui::SlashCommandItem{
          .command = "/export", .description = "Export this session as markdown", .category = "Sessions"},
      ava::tui::SlashCommandItem{.command = "/glob",
                                 .description = "List files matching a glob pattern",
                                 .hint = "<pattern>",
                                 .category = "Files"},
      ava::tui::SlashCommandItem{.command = "/grep",
                                 .description = "Search matching files for literal text",
                                 .hint = "<text> [glob]",
                                 .category = "Files"},
      ava::tui::SlashCommandItem{.command = "/read",
                                 .description = "Read a file through the permissioned read tool",
                                 .hint = "<path>",
                                 .category = "Files"},
      ava::tui::SlashCommandItem{.command = "/write",
                                 .description = "Write text through the permissioned write tool",
                                 .hint = "<path> <txt>",
                                 .category = "Files"},
      ava::tui::SlashCommandItem{.command = "/bash",
                                 .description = "Run a permissioned shell command",
                                 .hint = "<command>",
                                 .category = "Shell"}};
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

LineResult handle_line(ShellState& state, const std::string& line,
                       ava::permissions::PermissionResolver permission_resolver = nullptr) {
  LineResult line_result;
  if (line.empty()) return line_result;
  if (ava::app::is_backend_command(line)) {
    auto command_result = ava::app::run_command(
        state.session, ava::app::CommandRequest{.command = line, .permission_resolver = permission_resolver});
    if (!command_result) {
      add_output(line_result, command_result.error().format());
      return line_result;
    }
    line_result.quit = command_result->quit;
    line_result.output = std::move(command_result->output);
    line_result.tool_timeline = std::move(command_result->tool_timeline);
    return line_result;
  }

  auto credential = ava::config::load_openai_credential(state.session.paths);
  if (!credential) {
    add_output(line_result, credential.error().format());
    return line_result;
  }
  if (!*credential) {
    add_output(line_result, "chat requires OpenAI auth. Configure an OpenAI credential in " +
                                state.session.paths.auth_file.string() + "; slash tool commands still work offline.");
    return line_result;
  }
  ava::provider::CurlCliTransport transport;
  auto request_credential = ava::config::openai_credential_for_request(state.session.paths, **credential, transport);
  if (!request_credential) {
    add_output(line_result, request_credential.error().format() + "\nslash tool commands still work offline.");
    return line_result;
  }
  auto token = ava::config::openai_access_token_for_request(*request_credential);
  if (!token) {
    add_output(line_result, token.error().format() + "\nslash tool commands still work offline.");
    return line_result;
  }
  std::string openai_account_id = request_credential->account_id;
  if (request_credential->type == ava::config::OpenAICredentialType::OAuth && openai_account_id.empty()) {
    openai_account_id = ava::config::openai_oauth_account_id_from_token(request_credential->access_token).value_or("");
  }
  ava::provider::OpenAIProvider provider;
  ava::app::RuntimeRunOptions run_options;
  run_options.access_token = *token;
  run_options.openai_oauth = request_credential->type == ava::config::OpenAICredentialType::OAuth;
  run_options.openai_account_id = openai_account_id;
  run_options.permission_resolver = permission_resolver;
  auto result = ava::app::run_prompt(state.session, line, provider, transport, run_options);
  if (!result) {
    add_output(line_result, result.error().format());
    return line_result;
  }
  line_result.tool_timeline = std::move(result->tool_timeline);
  if (!result->final_text.empty()) {
    add_output(line_result, result->final_text);
  } else {
    add_output(line_result, "done");
  }
  return line_result;
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
  auto result = ava::tui::run_interactive_composer(ava::tui::TuiRuntimeOptions{
      .mode = ava::agent::to_string(state.session.mode),
      .provider = state.session.model.provider_id,
      .model = state.session.model.model_id,
      .session_id = state.session.store.session_id(),
      .slash_commands = slash_command_palette_items(),
      .on_submit =
          [&state](const std::string& submitted, const ava::permissions::PermissionResolver& permission_resolver) {
            const auto line_result = handle_line(state, submitted, permission_resolver);
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
  print_resume_command(state.session.store);
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
