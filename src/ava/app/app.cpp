#include "ava/app/app.h"

#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
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
#include "ava/core/version.h"
#include "ava/tui/composer.h"

namespace {

namespace version = ava::core::version;

void print_help() {
  std::cout << "AVA " << version::kDisplayVersion << "\n\n";
  std::cout << "Usage:\n";
  std::cout << "  ava [--help]\n";
  std::cout << "  ava login [provider] [--api-key|--oauth-token]\n";
  std::cout << "  ava auth login [provider] [--api-key|--oauth-token]\n";
  std::cout << "  ava connect [provider] [--api-key|--oauth-token]\n";
  std::cout << "  ava connect openai\n";
  std::cout << "  ava connect <provider> --api-key-stdin|--api-key-env <env>\n";
  std::cout << "  ava connect <provider> --oauth-token-stdin|--oauth-token-env <env>\n";
  std::cout << "  ava --version\n";
  std::cout << "  ava --mode build|plan\n";
  std::cout << "  ava --session <id>\n";
  std::cout << "  ava --continue\n";
  std::cout << "  ava --print [prompt] [--json|--output json] [--allow read-only] [--allow-tool list]\n";
  std::cout << "  ava -p [prompt] [--json|--output json] [--allow read-only] [--allow-tool list]\n";
  std::cout << "  ava --rpc [--allow read-only] [--allow-tool list]\n";
  std::cout << "  ava --output rpc [--allow read-only] [--allow-tool list]\n\n";
  std::cout << version::kDisplayVersion << " status: ncursesw TUI replacement on the hardened 0.2 backend.\n";
}

bool stdin_is_tty() { return isatty(STDIN_FILENO) == 1; }

bool stdout_is_tty() { return isatty(STDOUT_FILENO) == 1; }

bool is_cli_option(std::string_view arg) {
  return arg == "--help" || arg == "-h" || arg == "--version" || arg == "--mode" || arg == "--session" ||
         arg == "--continue" || arg == "-c" || arg == "--print" || arg == "-p" || arg == "--rpc" || arg == "--json" ||
         arg == "--output" || arg == "--allow" || arg == "--allow-tool";
}

std::string_view exit_status_text(int status) {
  if (status == 0) return "session saved";
  if (status == 130) return "interrupted, session saved";
  return "session saved with warnings";
}

void print_exit_card(ava::app::RuntimeSession const& session, int status) {
  bool const use_color = stdout_is_tty() && std::getenv("NO_COLOR") == nullptr;
  auto const blue = use_color ? std::string_view("\x1b[38;2;77;158;246m") : std::string_view("");
  auto const muted = use_color ? std::string_view("\x1b[38;2;148;163;184m") : std::string_view("");
  auto const bold = use_color ? std::string_view("\x1b[1m") : std::string_view("");
  auto const reset = use_color ? std::string_view("\x1b[0m") : std::string_view("");
  auto art = [&](std::string_view text) { std::cout << blue << text << reset << '\n'; };

  if (stdout_is_tty()) std::cout << "\x1b(B\x1b[0m";
  std::cout << '\n';
  art("  █████████   █████   █████   █████████");
  art("  ███░░░░░███ ░░███   ░░███   ███░░░░░███");
  art(" ░███    ░███  ░███    ░███  ░███    ░███");
  art(" ░███████████  ░███    ░███  ░███████████");
  art(" ░███░░░░░███  ░░███   ███   ░███░░░░░███");
  art(" ░███    ░███   ░░░█████░    ░███    ░███");
  art(" █████   █████    ░░███      █████   █████");
  art("░░░░░   ░░░░░      ░░░      ░░░░░   ░░░░░");
  std::cout << '\n';
  std::cout << bold << "AVA " << reset << exit_status_text(status) << ". " << muted << "Ready when you are." << reset
            << '\n';
  std::cout << muted << "Resume: " << reset << "ava --session " << session.store.session_id() << '\n';
  std::cout << muted << "Saved:  " << reset << session.store.session_path().string() << '\n';
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

  auto const paths = ava::config::xdg_paths();

  auto parse_connect_like_command = [&](int& index, std::optional<std::string> provider,
                                        bool preserve_openai_browser_default) -> int {
    enum class CredentialSource {
      None,
      Stdin,
      Env,
      Prompt,
    };
    CredentialSource source = CredentialSource::None;
    std::optional<ava::app::ConnectCredentialType> credential_type;
    std::optional<std::string> env_var;

    auto set_source = [&](CredentialSource next_source, ava::app::ConnectCredentialType next_type) -> bool {
      if (source != CredentialSource::None) {
        std::cerr << "connect accepts only one credential source\n";
        return false;
      }
      source = next_source;
      credential_type = next_type;
      return true;
    };

    while (index + 1 < argc) {
      std::string_view const option(argv[++index]);
      if (option == "--api-key") {
        if (!set_source(CredentialSource::Prompt, ava::app::ConnectCredentialType::ApiKey)) return 2;
        continue;
      }
      if (option == "--oauth-token") {
        if (!set_source(CredentialSource::Prompt, ava::app::ConnectCredentialType::OAuthToken)) return 2;
        continue;
      }
      if (option == "--api-key-stdin") {
        if (!set_source(CredentialSource::Stdin, ava::app::ConnectCredentialType::ApiKey)) return 2;
        continue;
      }
      if (option == "--oauth-token-stdin") {
        if (!set_source(CredentialSource::Stdin, ava::app::ConnectCredentialType::OAuthToken)) return 2;
        continue;
      }
      if (option == "--api-key-env" || option == "--oauth-token-env") {
        if (!set_source(option == "--oauth-token-env" ? CredentialSource::Env : CredentialSource::Env,
                        option == "--oauth-token-env" ? ava::app::ConnectCredentialType::OAuthToken
                                                      : ava::app::ConnectCredentialType::ApiKey)) {
          return 2;
        }
        if (index + 1 >= argc) {
          std::cerr << ava::tui::sanitize_terminal_text(std::string(option))
                    << " requires an environment variable name\n";
          return 2;
        }
        env_var = std::string(argv[++index]);
        continue;
      }
      std::cerr << "unknown connect option\n";
      return 2;
    }

    if (source == CredentialSource::Stdin || source == CredentialSource::Env) {
      if (!provider) {
        std::cerr << "connect requires a provider with headless credential sources\n";
        return 2;
      }
      return run_connect_provider_credential(
          paths,
          ava::app::ConnectProviderCredentialOptions{
              .provider_id = *provider, .credential_type = credential_type.value(), .env_var = env_var},
          std::cin, std::cout, std::cerr);
    }

    if (source == CredentialSource::Prompt) {
      return run_connect_provider_wizard(
          paths,
          ava::app::ConnectProviderWizardOptions{
              .provider_id = provider, .credential_type = credential_type, .stdin_is_tty = stdin_is_tty()},
          std::cin, std::cout, std::cerr);
    }

    if (preserve_openai_browser_default && provider && *provider == "openai") return run_connect_openai(paths);
    return run_connect_provider_wizard(
        paths, ava::app::ConnectProviderWizardOptions{.provider_id = provider, .stdin_is_tty = stdin_is_tty()},
        std::cin, std::cout, std::cerr);
  };

  for (int index = 1; index < argc; ++index) {
    std::string_view const arg(argv[index]);
    if (arg == "connect") {
      std::optional<std::string> provider;
      if (index + 1 < argc && !std::string_view(argv[index + 1]).starts_with("--")) provider = argv[++index];
      return parse_connect_like_command(index, provider, true);
    }
    if (arg == "login") {
      std::optional<std::string> provider;
      if (index + 1 < argc && !std::string_view(argv[index + 1]).starts_with("--")) provider = argv[++index];
      return parse_connect_like_command(index, provider, false);
    }
    if (arg == "auth") {
      if (index + 1 >= argc || std::string_view(argv[++index]) != "login") {
        std::cerr << "auth requires login\n";
        return 2;
      }
      std::optional<std::string> provider;
      if (index + 1 < argc && !std::string_view(argv[index + 1]).starts_with("--")) provider = argv[++index];
      return parse_connect_like_command(index, provider, false);
    }
    if (arg == "--help" || arg == "-h") {
      print_help();
      return 0;
    }
    if (arg == "--version") {
      std::cout << "ava " << version::kFullVersion << '\n';
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
      std::string_view const output(argv[++index]);
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

  bool const print_farewell = stdin_is_tty() && stdout_is_tty();
  int const status = run_interactive(*session);
  if (print_farewell) print_exit_card(*session, status);
  return status;
}

}  // namespace ava::app
