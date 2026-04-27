#include <sys/ioctl.h>
#include <poll.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ava/agent/agent_loop.h"
#include "ava/agent/mode.h"
#include "ava/config/auth.h"
#include "ava/config/model_config.h"
#include "ava/config/prompt_config.h"
#include "ava/config/xdg_paths.h"
#include "ava/core/ids.h"
#include "ava/provider/curl_transport.h"
#include "ava/provider/openai_provider.h"
#include "ava/session/session_store.h"
#include "ava/tools/bash_tool.h"
#include "ava/tools/file_tools.h"
#include "ava/tools/search_tools.h"
#include "ava/tui/composer.h"
#include "ava/tui/terminal.h"

namespace {

void print_help() {
  std::cout << "AVA 0.1\n\n";
  std::cout << "Usage:\n";
  std::cout << "  ava [--help]\n";
  std::cout << "  ava --version\n";
  std::cout << "  ava --mode build|plan\n";
  std::cout << "  ava --session <id>\n";
  std::cout << "  ava --continue\n\n";
  std::cout << "0.1 status: C++ foundation and session store skeleton.\n";
}

ava::core::VoidResult append_mode_change(ava::session::SessionStore& store, ava::agent::Mode mode) {
  return store.append(ava::session::SessionEntry{
      .id = ava::core::make_id("entry"),
      .parent_id = "",
      .type = ava::session::EntryType::ModeChange,
      .timestamp = ava::session::now_timestamp(),
      .data_json = "{\"mode\":\"" + ava::agent::to_string(mode) + "\"}",
  });
}

void print_shell_help() {
  std::cout << "Commands:\n";
  std::cout << "  /help               Show this help\n";
  std::cout << "  /mode               Toggle build/plan mode\n";
  std::cout << "  /sessions           List sessions for this workspace\n";
  std::cout << "  /glob <pattern>     List files matching a glob pattern\n";
  std::cout << "  /grep <text> [glob] Search matching files for literal text\n";
  std::cout << "  /read <path>        Read a file through the permissioned read tool\n";
  std::cout << "  /write <path> <txt> Write text through the permissioned write tool\n";
  std::cout << "  /bash <command>     Run a permissioned shell command\n";
  std::cout << "  /quit               Exit\n";
}

std::string display_path(const std::filesystem::path& path) {
  std::error_code error;
  const auto relative = std::filesystem::relative(path, std::filesystem::current_path(), error);
  if (!error) {
    return relative.generic_string();
  }
  return path.generic_string();
}

void print_resume_command(const ava::session::SessionStore& store) {
  std::cout << "Resume this session with: ava --session " << store.session_id() << '\n';
}

ava::core::Result<std::string> resolve_session_id(const std::filesystem::path& workspace_dir,
                                                  const std::filesystem::path& root_dir,
                                                  std::string_view requested_id) {
  auto sessions = ava::session::SessionStore::list_sessions(workspace_dir, root_dir);
  if (!sessions) return std::unexpected(sessions.error());

  std::vector<std::string> matches;
  for (const auto& session : *sessions) {
    if (session.session_id == requested_id || session.session_id.starts_with(requested_id)) {
      matches.push_back(session.session_id);
    }
  }
  if (matches.empty()) {
    auto error = ava::core::Error(ava::core::ErrorCategory::NotFound, "session not found");
    error.with_context("session_id", std::string(requested_id));
    return std::unexpected(std::move(error));
  }
  if (matches.size() > 1) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "session id prefix is ambiguous");
    error.with_context("session_id", std::string(requested_id));
    error.with_context("matches", std::to_string(matches.size()));
    return std::unexpected(std::move(error));
  }
  return matches.front();
}

struct ShellState {
  // Lifetime contract: references are stack-scoped and must outlive each run loop invocation.
  ava::session::SessionStore& store;
  ava::agent::Mode mode;
  const ava::config::ModelInfo& model;
  const ava::config::XdgPaths& paths;
  const ava::config::PromptSelection& prompt;
};

struct LineResult {
  bool quit = false;
  std::vector<std::string> output;
};

void add_output(LineResult& result, std::string text) { result.output.push_back(std::move(text)); }

ava::tools::ToolContext make_tool_context(const ShellState& state) {
  return ava::tools::ToolContext{.workspace_dir = std::filesystem::current_path(), .mode = state.mode};
}

LineResult handle_line(ShellState& state, const std::string& line) {
  LineResult line_result;
  if (line.empty()) return line_result;
  if (line == "/quit" || line == "/exit") {
    line_result.quit = true;
    return line_result;
  }
  if (line == "/help") {
    add_output(line_result,
               "Commands:\n  /help               Show this help\n  /mode               Toggle build/plan mode\n  "
               "/sessions           List sessions for this workspace\n  /glob <pattern>     List files matching a glob "
               "pattern\n  /grep <text> [glob] Search matching files for literal text\n  /read <path>        Read a "
                "file through the permissioned read tool\n  /write <path> <txt> Write text through the permissioned write "
                "tool\n  /bash <command>     Run a permissioned shell command\n  "
                "/quit               Exit");
    return line_result;
  }
  if (line == "/sessions") {
    auto sessions = ava::session::SessionStore::list_sessions(std::filesystem::current_path(), state.paths.sessions_dir);
    if (!sessions) {
      add_output(line_result, sessions.error().format());
      return line_result;
    }
    if (sessions->empty()) {
      add_output(line_result, "No sessions for this workspace.");
      return line_result;
    }
    std::string output;
    for (const auto& session : *sessions) {
      output += session.session_id + "  entries=" + std::to_string(session.entry_count);
      if (!session.last_updated.empty()) output += "  updated=" + session.last_updated;
      output += '\n';
    }
    add_output(line_result, std::move(output));
    return line_result;
  }
  if (line == "/mode") {
    const auto new_mode = ava::agent::toggle_mode(state.mode);
    if (auto result = append_mode_change(state.store, new_mode); !result) {
      add_output(line_result, result.error().format());
      return line_result;
    }
    state.mode = new_mode;
    add_output(line_result, "mode switched to " + ava::agent::to_string(state.mode));
    return line_result;
  }
  if (line.starts_with("/read ")) {
    const auto path = std::filesystem::current_path() / line.substr(6);
    const auto output = ava::tools::read_file(make_tool_context(state), path);
    if (!output) {
      add_output(line_result, output.error().format());
      return line_result;
    }
    std::string text = output->content;
    if (output->truncated) {
      text += "\n[truncated " + std::to_string(output->output_bytes) + '/' + std::to_string(output->total_bytes) +
              " bytes]";
    }
    add_output(line_result, std::move(text));
    return line_result;
  }
  if (line.starts_with("/glob ")) {
    const auto pattern = line.substr(6);
    const auto result = ava::tools::glob_files(make_tool_context(state), pattern);
    if (!result) {
      add_output(line_result, result.error().format());
      return line_result;
    }
    std::string output;
    for (const auto& path : result->paths) output += display_path(path) + '\n';
    if (result->truncated) {
      output += "[truncated " + std::to_string(result->paths.size()) + '/' + std::to_string(result->total_matches) +
                " matches]\n";
    }
    add_output(line_result, std::move(output));
    return line_result;
  }
  if (line.starts_with("/grep ")) {
    const auto rest = line.substr(6);
    const auto split = rest.find(' ');
    const auto pattern = split == std::string::npos ? rest : rest.substr(0, split);
    const auto include = split == std::string::npos ? std::string("**/*") : rest.substr(split + 1);
    const auto result = ava::tools::grep_files(make_tool_context(state), pattern, include);
    if (!result) {
      add_output(line_result, result.error().format());
      return line_result;
    }
    std::string output;
    for (const auto& match : result->matches) {
      output += display_path(match.path) + ':' + std::to_string(match.line_number) + ": " + match.line;
      if (match.line_truncated) output += " [line truncated]";
      output += '\n';
    }
    if (result->truncated) {
      output += "[truncated " + std::to_string(result->matches.size()) + '/' + std::to_string(result->total_matches) +
                " matches]\n";
    }
    add_output(line_result, std::move(output));
    return line_result;
  }
  if (line.starts_with("/write ")) {
    const auto rest = line.substr(7);
    const auto split = rest.find(' ');
    if (split == std::string::npos) {
      add_output(line_result, "usage: /write <path> <text>");
      return line_result;
    }
    const auto path = std::filesystem::current_path() / rest.substr(0, split);
    const auto text = rest.substr(split + 1);
    const auto result = ava::tools::write_file(make_tool_context(state), path, text);
    if (!result) {
      add_output(line_result, result.error().format());
      return line_result;
    }
    add_output(line_result, "wrote " + std::to_string(result->bytes_written) + " bytes to " + result->path.string());
    return line_result;
  }
  if (line.starts_with("/bash ")) {
    const auto command = line.substr(6);
    const auto result = ava::tools::run_bash(make_tool_context(state), command);
    if (!result) {
      add_output(line_result, result.error().format());
      return line_result;
    }
    std::string output = "exit: " + std::to_string(result->exit_code);
    if (result->timed_out) output += " (timed out)";
    if (result->truncated) {
      output += " (output truncated to last " + std::to_string(result->output.size()) + '/' +
                std::to_string(result->total_bytes) + " bytes)";
    }
    output += '\n' + result->output;
    add_output(line_result, std::move(output));
    return line_result;
  }

  auto credential = ava::config::load_openai_credential(state.paths);
  if (!credential) {
    add_output(line_result, credential.error().format());
    return line_result;
  }
  if (!*credential) {
    add_output(line_result, "chat requires OpenAI auth. Configure an OpenAI credential in " +
                                state.paths.auth_file.string() + "; slash tool commands still work offline.");
    return line_result;
  }
  auto token = ava::config::openai_access_token_for_request(**credential);
  if (!token) {
    add_output(line_result, token.error().format() + "\nslash tool commands still work offline.");
    return line_result;
  }
  auto prompt = ava::config::select_prompt(state.paths, state.model, state.mode);
  if (!prompt) {
    add_output(line_result, prompt.error().format());
    return line_result;
  }
  ava::provider::OpenAIProvider provider;
  ava::provider::CurlCliTransport transport;
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = std::filesystem::current_path(),
                                                          .mode = state.mode,
                                                          .provider_id = state.model.provider_id,
                                                          .model_id = state.model.model_id,
                                                          .system_prompt = prompt->text,
                                                          .access_token = *token});
  auto result = loop.run_turn(line, state.store, provider, transport);
  if (!result) {
    add_output(line_result, result.error().format());
    return line_result;
  }
  if (!result->final_text.empty()) {
    add_output(line_result, result->final_text);
  } else {
    add_output(line_result, "done");
  }
  return line_result;
}

std::pair<std::size_t, std::size_t> terminal_size() {
  winsize size{};
  int result = 0;
  do {
    result = ioctl(STDOUT_FILENO, TIOCGWINSZ, &size);
  } while (result < 0 && errno == EINTR);
  if (result == 0 && size.ws_col > 0 && size.ws_row > 0) {
    return {size.ws_col, size.ws_row};
  }
  return {80, 24};
}

bool consume_escape_sequence_tail() {
  pollfd descriptor{.fd = STDIN_FILENO, .events = POLLIN, .revents = 0};
  int ready = 0;
  do {
    ready = poll(&descriptor, 1, 75);
  } while (ready < 0 && errno == EINTR);
  if (ready <= 0 || (descriptor.revents & POLLIN) == 0) return false;

  unsigned char byte = 0;
  std::size_t consumed = 0;
  while (consumed < 16) {
    ssize_t count = 0;
    do {
      count = read(STDIN_FILENO, &byte, 1);
    } while (count < 0 && errno == EINTR);
    if (count <= 0) break;
    ++consumed;
    descriptor.revents = 0;
    do {
      ready = poll(&descriptor, 1, 0);
    } while (ready < 0 && errno == EINTR);
    if (ready <= 0 || (descriptor.revents & POLLIN) == 0) break;
  }
  return consumed > 0;
}

void push_transcript(ava::tui::ComposerSnapshot& snapshot, ava::tui::TranscriptItem item) {
  constexpr std::size_t kMaxTranscriptItems = 1000;
  snapshot.transcript.push_back(std::move(item));
  if (snapshot.transcript.size() > kMaxTranscriptItems) {
    snapshot.transcript.erase(snapshot.transcript.begin(),
                              snapshot.transcript.begin() +
                                  static_cast<std::ptrdiff_t>(snapshot.transcript.size() - kMaxTranscriptItems));
  }
}

int run_line_shell(ShellState state) {
  std::cout << "AVA 0.1 terminal shell\n";
  std::cout << "mode: " << ava::agent::to_string(state.mode) << " | session: " << state.store.session_id() << "\n";
  std::cout << "provider: " << state.model.provider_id << " | model: " << state.model.model_id << "\n";
  print_shell_help();

  std::string line;
  while (true) {
    std::cout << "\n[" << ava::agent::to_string(state.mode) << "] ava> " << std::flush;
    if (!std::getline(std::cin, line)) {
      std::cout << '\n';
      print_resume_command(state.store);
      return 0;
    }

    const auto result = handle_line(state, line);
    for (const auto& output : result.output) {
      for (const auto& output_line : ava::tui::split_lines(output)) {
        std::cout << ava::tui::sanitize_terminal_text(output_line) << '\n';
      }
    }
    if (result.quit) {
      print_resume_command(state.store);
      return 0;
    }
  }
}

int run_tui(ShellState state) {
  auto raw = ava::tui::RawTerminalGuard::enable(STDIN_FILENO);
  if (!raw) {
    std::cerr << raw.error().format() << '\n';
    return 1;
  }

  ava::tui::ComposerSnapshot snapshot{.mode = ava::agent::to_string(state.mode),
                                      .provider = state.model.provider_id,
                                      .model = state.model.model_id,
                                      .session_id = state.store.session_id(),
                                      .input = "",
                                      .status = "Type /help for commands. Enter submits. Tab toggles mode.",
                                      .transcript = {ava::tui::TranscriptItem{
                                          .label = "ava", .text = "AVA 0.1 simple terminal composer"}}};

  auto render = [&]() -> bool {
    const auto [width, height] = terminal_size();
    snapshot.width = width;
    snapshot.height = height;
    snapshot.mode = ava::agent::to_string(state.mode);
    return ava::tui::write_terminal(ava::tui::render_screen(snapshot)).has_value();
  };

  if (!ava::tui::write_terminal("\x1b[2J").has_value()) return 1;
  if (!render()) return 1;

  bool terminal_write_failed = false;

  while (true) {
    unsigned char byte = 0;
    ssize_t count = 0;
    do {
      count = read(STDIN_FILENO, &byte, 1);
    } while (count < 0 && errno == EINTR);
    if (count == 0) break;
    if (count < 0) {
      snapshot.status = "terminal read failed";
      break;
    }
    const auto event = ava::tui::parse_input_byte(byte);
    if (event.key == ava::tui::Key::Character) {
      snapshot.input.push_back(event.character);
    } else if (event.key == ava::tui::Key::Backspace) {
      ava::tui::erase_last_utf8_codepoint(snapshot.input);
    } else if (event.key == ava::tui::Key::Tab) {
      const auto new_mode = ava::agent::toggle_mode(state.mode);
      if (auto result = append_mode_change(state.store, new_mode); !result) {
        snapshot.status = result.error().format();
      } else {
        state.mode = new_mode;
        snapshot.status = "mode switched to " + ava::agent::to_string(state.mode);
      }
    } else if (event.key == ava::tui::Key::CtrlC) {
      if (snapshot.input.empty()) break;
      snapshot.input.clear();
      snapshot.status = "input cleared";
    } else if (event.key == ava::tui::Key::CtrlD) {
      break;
    } else if (event.key == ava::tui::Key::Escape) {
      snapshot.status = consume_escape_sequence_tail() ? "ignored terminal escape sequence" : "escape ignored";
    } else if (event.key == ava::tui::Key::Enter) {
      const auto submitted = snapshot.input;
      snapshot.input.clear();
      if (!submitted.empty()) {
        push_transcript(snapshot, ava::tui::TranscriptItem{.label = submitted.starts_with('/') ? "cmd" : "you",
                                                          .text = submitted});
        const auto result = handle_line(state, submitted);
        for (const auto& output : result.output) {
          push_transcript(snapshot, ava::tui::TranscriptItem{.label = "ava", .text = output});
        }
        snapshot.status = result.output.empty() ? "ok" : "done";
        if (result.quit) break;
      }
    }
    if (!render()) {
      terminal_write_failed = true;
      break;
    }
  }

  std::cout << std::flush;
  static_cast<void>(ava::tui::write_terminal("\x1b[?25h\x1b[2J\x1b[H"));
  print_resume_command(state.store);
  return terminal_write_failed ? 1 : 0;
}

int run_interactive(ava::session::SessionStore& store, ava::agent::Mode mode, const ava::config::ModelInfo& model,
                    const ava::config::XdgPaths& paths, const ava::config::PromptSelection& prompt) {
  ShellState state{.store = store, .mode = mode, .model = model, .paths = paths, .prompt = prompt};
  if (ava::tui::terminal_is_tty()) return run_tui(state);
  return run_line_shell(state);
}

}  // namespace

int main(int argc, char** argv) {
  auto mode = ava::agent::Mode::Build;
  std::optional<std::string> requested_session_id;
  bool continue_last_session = false;

  for (int index = 1; index < argc; ++index) {
    const std::string_view arg(argv[index]);
    if (arg == "--help" || arg == "-h") {
      print_help();
      return 0;
    }
    if (arg == "--version") {
      std::cout << "ava 0.1.0\n";
      return 0;
    }
    if (arg == "--mode") {
      if (index + 1 >= argc) {
        std::cerr << "--mode requires build or plan\n";
        return 2;
      }
      auto parsed = ava::agent::parse_mode(argv[++index]);
      if (!parsed) {
        std::cerr << parsed.error().format() << '\n';
        return 2;
      }
      mode = *parsed;
      continue;
    }
    if (arg == "--session") {
      if (index + 1 >= argc) {
        std::cerr << "--session requires a session id\n";
        return 2;
      }
      requested_session_id = std::string(argv[++index]);
      continue;
    }
    if (arg == "--continue" || arg == "-c") {
      continue_last_session = true;
      continue;
    }

    std::cerr << "unknown argument: " << arg << '\n';
    return 2;
  }

  if (requested_session_id && continue_last_session) {
    std::cerr << "use either --session or --continue, not both\n";
    return 2;
  }

  const auto workspace_dir = std::filesystem::current_path();
  const auto paths = ava::config::xdg_paths();

  bool created_session = true;
  ava::core::Result<ava::session::SessionStore> store = std::unexpected(
      ava::core::Error(ava::core::ErrorCategory::Unknown, "session was not initialized"));
  if (requested_session_id) {
    auto resolved = resolve_session_id(workspace_dir, paths.sessions_dir, *requested_session_id);
    if (!resolved) {
      std::cerr << resolved.error().format() << '\n';
      return 1;
    }
    store = ava::session::SessionStore::open(workspace_dir, *resolved, paths.sessions_dir);
    created_session = false;
  } else if (continue_last_session) {
    auto sessions = ava::session::SessionStore::list_sessions(workspace_dir, paths.sessions_dir);
    if (!sessions) {
      std::cerr << sessions.error().format() << '\n';
      return 1;
    }
    if (!sessions->empty()) {
      store = ava::session::SessionStore::open(workspace_dir, sessions->front().session_id, paths.sessions_dir);
      created_session = false;
    } else {
      store = ava::session::SessionStore::create(workspace_dir, paths.sessions_dir);
    }
  } else {
    store = ava::session::SessionStore::create(workspace_dir, paths.sessions_dir);
  }
  if (!store) {
    std::cerr << store.error().format() << '\n';
    return 1;
  }

  auto registry = ava::config::load_model_registry(paths);
  if (!registry) {
    std::cerr << registry.error().format() << '\n';
    return 1;
  }
  const auto selected_model = ava::config::select_default_model(*registry);
  auto prompt = ava::config::select_prompt(paths, selected_model, mode);
  if (!prompt) {
    std::cerr << prompt.error().format() << '\n';
    return 1;
  }

  if (created_session) {
    const auto entry_id = ava::core::make_id("entry");
    const auto result = store->append(ava::session::SessionEntry{
        .id = entry_id,
        .parent_id = "",
        .type = ava::session::EntryType::SessionStart,
        .timestamp = ava::session::now_timestamp(),
        .data_json = "{\"mode\":\"" + ava::agent::to_string(mode) + "\",\"provider\":\"" +
                     ava::session::json_escape(selected_model.provider_id) + "\",\"model\":\"" +
                     ava::session::json_escape(selected_model.model_id) + "\"}",
    });
    if (!result) {
      std::cerr << result.error().format() << '\n';
      return 1;
    }
  }

  std::cout << (created_session ? "AVA session started\n" : "AVA session resumed\n");
  std::cout << "mode: " << ava::agent::to_string(mode) << '\n';
  std::cout << "provider: " << selected_model.provider_id << '\n';
  std::cout << "model: " << selected_model.model_id << '\n';
  std::cout << "session: " << store->session_id() << '\n';
  std::cout << "path: " << store->session_path().string() << '\n';
  return run_interactive(*store, mode, selected_model, paths, *prompt);
}
