#include "ava/app/app.h"

#include <unistd.h>

#include <filesystem>
#include <iostream>
#include <optional>
#include <string_view>
#include <utility>

#include "ava/agent/mode.h"
#include "ava/app/connect_openai.h"
#include "ava/app/headless_policy.h"
#include "ava/app/line_shell.h"
#include "ava/app/print_mode.h"
#include "ava/app/rpc_mode.h"
#include "ava/app/runtime.h"
#include "ava/config/xdg_paths.h"

namespace {

constexpr std::string_view kAvaVersion = "0.32.0";
constexpr std::string_view kAvaDisplayVersion = "0.32";

void print_help() {
  std::cout << "AVA " << kAvaDisplayVersion << "\n\n";
  std::cout << "Usage:\n";
  std::cout << "  ava [--help]\n";
  std::cout << "  ava connect openai\n";
  std::cout << "  ava --version\n";
  std::cout << "  ava --mode build|plan\n";
  std::cout << "  ava --session <id>\n";
  std::cout << "  ava --continue\n";
  std::cout << "  ava --print [prompt] [--json|--output json] [--allow read-only] [--allow-tool list]\n";
  std::cout << "  ava -p [prompt] [--json|--output json] [--allow read-only] [--allow-tool list]\n";
  std::cout << "  ava --rpc [--allow read-only] [--allow-tool list]\n";
  std::cout << "  ava --output rpc [--allow read-only] [--allow-tool list]\n\n";
  std::cout << "0.32 status: ncursesw TUI replacement on the hardened 0.2 backend.\n";
}

bool stdin_is_tty() { return isatty(STDIN_FILENO) == 1; }

bool is_cli_option(std::string_view arg) {
  return arg == "--help" || arg == "-h" || arg == "--version" || arg == "--mode" || arg == "--session" ||
         arg == "--continue" || arg == "-c" || arg == "--print" || arg == "-p" || arg == "--rpc" || arg == "--json" ||
         arg == "--output" || arg == "--allow" || arg == "--allow-tool";
}

}  // namespace

namespace ava::app {

int run(int argc, char** argv) {
  auto mode = ava::agent::Mode::Build;
  std::optional<std::string> requested_session_id;
  bool continue_last_session = false;
  bool print_mode = false;
  bool rpc_mode = false;
  std::optional<std::string> print_prompt;
  auto print_output_format = ava::app::PrintOutputFormat::Text;
  bool print_output_flag_seen = false;
  bool print_permission_flag_seen = false;
  ava::app::HeadlessPermissionPolicyOptions headless_permission_policy;

  const auto paths = ava::config::xdg_paths();

  for (int index = 1; index < argc; ++index) {
    const std::string_view arg(argv[index]);
    if (arg == "connect") {
      if (index + 1 >= argc) {
        std::cerr << "connect requires a provider: openai\n";
        return 2;
      }
      const std::string_view provider(argv[++index]);
      if (provider != "openai") {
        std::cerr << "unsupported connect provider: " << provider << '\n';
        return 2;
      }
      if (index + 1 != argc) {
        std::cerr << "connect openai does not accept extra arguments\n";
        return 2;
      }
      return run_connect_openai(paths);
    }
    if (arg == "--help" || arg == "-h") {
      print_help();
      return 0;
    }
    if (arg == "--version") {
      std::cout << "ava " << kAvaVersion << '\n';
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
    if (arg == "--print" || arg == "-p") {
      print_mode = true;
      if (index + 1 < argc && !is_cli_option(argv[index + 1])) {
        if (print_prompt) {
          std::cerr << "print mode accepts at most one prompt argument\n";
          return 2;
        }
        print_prompt = std::string(argv[++index]);
      }
      continue;
    }
    if (arg == "--rpc") {
      rpc_mode = true;
      continue;
    }
    if (arg == "--json") {
      print_output_format = ava::app::PrintOutputFormat::Json;
      print_output_flag_seen = true;
      continue;
    }
    if (arg == "--output") {
      if (index + 1 >= argc) {
        std::cerr << "--output requires json, text, or rpc\n";
        return 2;
      }
      const std::string_view output(argv[++index]);
      if (output == "json") {
        print_output_format = ava::app::PrintOutputFormat::Json;
      } else if (output == "text") {
        print_output_format = ava::app::PrintOutputFormat::Text;
      } else if (output == "rpc") {
        rpc_mode = true;
      } else {
        std::cerr << "--output requires json, text, or rpc\n";
        return 2;
      }
      print_output_flag_seen = true;
      continue;
    }
    if (arg == "--allow") {
      if (index + 1 >= argc || is_cli_option(argv[index + 1])) {
        std::cerr << "--allow requires read-only\n";
        return 2;
      }
      auto added = ava::app::add_headless_allow_policy(headless_permission_policy, argv[++index]);
      if (!added) {
        std::cerr << added.error().format() << '\n';
        return 2;
      }
      print_permission_flag_seen = true;
      continue;
    }
    if (arg == "--allow-tool") {
      if (index + 1 >= argc || is_cli_option(argv[index + 1])) {
        std::cerr << "--allow-tool requires a comma-separated tool list\n";
        return 2;
      }
      auto added = ava::app::add_headless_allowed_tools(headless_permission_policy, argv[++index]);
      if (!added) {
        std::cerr << added.error().format() << '\n';
        return 2;
      }
      print_permission_flag_seen = true;
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

    if (print_mode && !print_prompt) {
      print_prompt = std::string(arg);
      continue;
    }

    std::cerr << "unknown argument: " << arg << '\n';
    return 2;
  }

  if (print_mode && rpc_mode) {
    std::cerr << "use either --print or --rpc, not both\n";
    return 2;
  }

  if (!print_mode && !rpc_mode && print_output_flag_seen) {
    std::cerr << "--json and --output text/json are only supported with --print; use --rpc or --output rpc for RPC\n";
    return 2;
  }

  if (!print_mode && !rpc_mode && print_permission_flag_seen) {
    std::cerr << "--allow and --allow-tool are only supported with --print or --rpc\n";
    return 2;
  }

  if (requested_session_id && continue_last_session) {
    std::cerr << "use either --session or --continue, not both\n";
    return 2;
  }

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = std::filesystem::current_path();
  open_options.current_dir = open_options.workspace_dir;
  open_options.requested_session_id = requested_session_id;
  open_options.continue_last_session = continue_last_session;
  open_options.mode = mode;
  open_options.paths = paths;

  if (print_mode) {
    return ava::app::run_print_mode(
        ava::app::PrintModeOptions{.open_options = open_options,
                                   .explicit_prompt = print_prompt,
                                   .read_stdin = !stdin_is_tty(),
                                   .output_format = print_output_format,
                                   .permission_policy = std::move(headless_permission_policy),
                                   .provider_override = std::nullopt,
                                   .transport_override = std::nullopt},
        std::cin, std::cout, std::cerr);
  }

  if (rpc_mode) {
    return ava::app::run_rpc_mode(ava::app::RpcModeOptions{.open_options = open_options,
                                                           .permission_policy = std::move(headless_permission_policy)},
                                  std::cin, std::cout, std::cerr);
  }

  auto session = ava::app::open_runtime_session(open_options);
  if (!session) {
    std::cerr << session.error().format() << '\n';
    return 1;
  }

  std::cout << (session->created ? "AVA session started\n" : "AVA session resumed\n");
  std::cout << "mode: " << ava::agent::to_string(session->mode) << '\n';
  std::cout << "provider: " << session->model.provider_id << '\n';
  std::cout << "model: " << session->model.model_id << '\n';
  std::cout << "session: " << session->store.session_id() << '\n';
  std::cout << "path: " << session->store.session_path().string() << '\n';
  return run_interactive(*session);
}

}  // namespace ava::app
