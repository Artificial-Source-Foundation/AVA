#include "ava/app/app.h"
#include "ava/app/connect_openai.h"
#include "ava/app/headless_policy.h"
#include "ava/app/line_shell.h"
#include "ava/app/print_mode.h"
#include "ava/app/rpc_mode.h"
#include "ava/app/runtime.h"
#include "ava/agent/mode.h"
#include "ava/tui/composer.h"
#include "ava/config/xdg_paths.h"
#include "ava/core/version.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <unistd.h>

namespace {

namespace version = ava::core::version;

void print_help()
{
  std::cout << "AVA " << version::kDisplayVersion << "\n\n";
  std::cout << "Usage:\n";
  std::cout << "  ava [--help]\n";
  std::cout << "  ava [prompt]\n";
  std::cout << "  ava @file [prompt]\n";
  std::cout << "  ava login [provider] [--api-key|--browser-oauth|--headless-oauth]\n";
  std::cout << "  ava auth login [provider] [--api-key|--browser-oauth|--headless-oauth]\n";
  std::cout << "  ava connect [provider] [--api-key|--browser-oauth|--headless-oauth]\n";
  std::cout << "  ava connect <provider> --api-key-stdin|--api-key-env <env>\n";
  std::cout << "  ava packages <list|install|remove|update|config>  # deferred\n";
  std::cout << "  ava --version\n";
  std::cout << "  ava --mode build|plan|text|json|rpc\n";
  std::cout << "  ava --thinking off|<reasoning-level>\n";
  std::cout << "  ava --session <id> | --session-id <id>\n";
  std::cout << "  ava --continue | --resume | -r\n";
  std::cout << "  ava --fork <id>\n";
  std::cout << "  ava --name <name>\n";
  std::cout << "  ava --session-dir <dir>\n";
  std::cout << "  ava --no-session\n";
  std::cout << "  ava --offline\n";
  std::cout << "  ava [--system-prompt text] [--append-system-prompt text]\n";
  std::cout << "  ava [--tools list] [--exclude-tools list] [--no-builtin-tools|--no-tools]\n";
  std::cout << "  ava --print [@file ...] [prompt] [--json|--output json] [--allow read-only] [--allow-tool list]\n";
  std::cout << "  ava -p [@file ...] [prompt] [--json|--output json] [--allow read-only] [--allow-tool list]\n";
  std::cout << "  ava --rpc [--allow read-only] [--allow-tool list]\n";
  std::cout << "  ava --output rpc [--allow read-only] [--allow-tool list]\n\n";
  std::cout << version::kDisplayVersion << " status: backend MVP runtime with terminal, print, and RPC workflows.\n";
}

bool stdin_is_tty()
{
  return isatty(STDIN_FILENO) == 1;
}

bool stdout_is_tty()
{
  return isatty(STDOUT_FILENO) == 1;
}

bool is_cli_option(std::string_view arg)
{
  return arg == "--help" || arg == "-h" || arg == "--version" || arg == "--mode" || arg == "--session" || arg == "--session-id" || arg == "--continue" ||
         arg == "--resume" || arg == "-c" || arg == "-r" || arg == "--fork" || arg == "--name" || arg == "-n" || arg == "--session-dir" ||
         arg == "--no-session" || arg == "--offline" || arg == "--thinking" || arg == "--system-prompt" || arg == "--append-system-prompt" ||
         arg == "--print" || arg == "-p" || arg == "--rpc" || arg == "--json" || arg == "--output" || arg == "--allow" || arg == "--allow-tool" ||
         arg == "--tools" || arg == "-t" || arg == "--exclude-tools" || arg == "-xt" || arg == "--no-builtin-tools" || arg == "-nbt" || arg == "--no-tools" ||
         arg == "-nt";
}

bool is_cli_file_argument(std::string_view arg)
{
  return arg.size() > 1 && arg.front() == '@';
}

std::string prompt_reference_for_cli_file_argument(std::string_view path)
{
  bool needs_quotes = false;
  for (char const ch : path)
  {
    if (std::isspace(static_cast<unsigned char>(ch)) != 0)
    {
      needs_quotes = true;
      break;
    }
  }

  if (!needs_quotes)
    return "@" + std::string(path);

  std::string reference = "@\"";
  reference.append(path);
  reference += '"';
  return reference;
}

void append_prompt_argument(std::optional<std::string>& prompt, std::string_view argument)
{
  if (!prompt)
  {
    prompt = std::string(argument);
    return;
  }
  if (!prompt->empty())
    *prompt += ' ';
  prompt->append(argument);
}

std::string_view trim_cli_token(std::string_view token)
{
  while (!token.empty() && std::isspace(static_cast<unsigned char>(token.front())) != 0) token.remove_prefix(1);
  while (!token.empty() && std::isspace(static_cast<unsigned char>(token.back())) != 0) token.remove_suffix(1);
  return token;
}

void append_tool_visibility_names(std::vector<std::string>& target, std::string_view csv)
{
  std::size_t start = 0;
  while (start <= csv.size())
  {
    auto const end = csv.find(',', start);
    auto const token = trim_cli_token(csv.substr(start, end == std::string_view::npos ? csv.size() - start : end - start));
    if (!token.empty())
      target.emplace_back(token);
    if (end == std::string_view::npos)
      break;
    start = end + 1;
  }
}

void prepend_file_arguments_to_prompt(std::optional<std::string>& prompt, std::vector<std::string> const& file_arguments)
{
  if (file_arguments.empty())
    return;

  std::string combined;
  for (auto const& file_argument : file_arguments)
  {
    if (!combined.empty())
      combined += ' ';
    combined += prompt_reference_for_cli_file_argument(file_argument);
  }
  if (prompt && !prompt->empty())
  {
    combined += "\n\n";
    combined += *prompt;
  }
  prompt = std::move(combined);
}

std::string_view exit_status_text(int status, bool sessionless)
{
  if (status == 0)
    return sessionless ? "session discarded" : "session saved";
  if (status == 130)
    return sessionless ? "interrupted, session discarded" : "interrupted, session saved";
  return sessionless ? "session discarded with warnings" : "session saved with warnings";
}

void print_exit_card(ava::app::RuntimeSession const& session, int status)
{
  bool const use_color = stdout_is_tty() && std::getenv("NO_COLOR") == nullptr;
  auto const blue = use_color ? std::string_view("\x1b[38;2;77;158;246m") : std::string_view("");
  auto const muted = use_color ? std::string_view("\x1b[38;2;148;163;184m") : std::string_view("");
  auto const bold = use_color ? std::string_view("\x1b[1m") : std::string_view("");
  auto const reset = use_color ? std::string_view("\x1b[0m") : std::string_view("");
  auto art = [&](std::string_view text) { std::cout << blue << text << reset << '\n'; };

  if (stdout_is_tty())
    std::cout << "\x1b(B\x1b[0m";
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
  std::cout << bold << "AVA " << reset << exit_status_text(status, session.sessionless) << ". " << muted << "Ready when you are." << reset << '\n';
  if (session.sessionless)
  {
    std::cout << muted << "History: " << reset << "not saved (--no-session)\n";
    return;
  }
  std::cout << muted << "Resume: " << reset << "ava --session " << session.store.session_id() << '\n';
  std::cout << muted << "Saved:  " << reset << session.store.session_path().string() << '\n';
}

}  // namespace

namespace ava::app {

int run(int argc, char** argv)
{
  auto mode = ava::agent::Mode::Build;
  std::optional<std::string> requested_session_id;
  std::optional<std::string> fork_session_id;
  std::optional<std::string> initial_session_name;
  std::optional<std::filesystem::path> session_dir;
  bool continue_last_session = false;
  bool sessionless = false;
  bool offline = false;
  bool print_mode = false;
  bool rpc_mode = false;
  std::optional<std::string> print_prompt;
  std::vector<std::string> cli_file_arguments;
  auto print_output_format = ava::app::PrintOutputFormat::Text;
  bool print_output_flag_seen = false;
  bool print_permission_flag_seen = false;
  ava::app::RuntimePromptOverrides prompt_overrides;
  ava::app::HeadlessPermissionPolicyOptions headless_permission_policy;
  ava::agent::ToolVisibilityOptions tool_visibility;
  std::optional<std::string> initial_reasoning_level;

  auto const paths = ava::config::xdg_paths();

  auto parse_connect_like_command = [&](int& index, std::optional<std::string> provider) -> int {
    enum class CredentialSource
    {
      None,
      Stdin,
      Env,
      Prompt,
      BrowserOAuth,
      HeadlessOAuth,
    };
    CredentialSource source = CredentialSource::None;
    std::optional<ava::app::ConnectCredentialType> credential_type;
    std::optional<std::string> env_var;

    auto set_source = [&](CredentialSource next_source, ava::app::ConnectCredentialType next_type) -> bool {
      if (source != CredentialSource::None)
      {
        std::cerr << "connect accepts only one credential source\n";
        return false;
      }
      source = next_source;
      credential_type = next_type;
      return true;
    };

    while (index + 1 < argc)
    {
      std::string_view const option(argv[++index]);
      if (option == "--api-key")
      {
        if (!set_source(CredentialSource::Prompt, ava::app::ConnectCredentialType::ApiKey))
          return 2;
        continue;
      }
      if (option == "--browser-oauth")
      {
        if (!set_source(CredentialSource::BrowserOAuth, ava::app::ConnectCredentialType::ApiKey))
          return 2;
        continue;
      }
      if (option == "--headless-oauth")
      {
        if (!set_source(CredentialSource::HeadlessOAuth, ava::app::ConnectCredentialType::ApiKey))
          return 2;
        continue;
      }
      if (option == "--api-key-stdin")
      {
        if (!set_source(CredentialSource::Stdin, ava::app::ConnectCredentialType::ApiKey))
          return 2;
        continue;
      }
      if (option == "--api-key-env")
      {
        if (!set_source(CredentialSource::Env, ava::app::ConnectCredentialType::ApiKey))
          return 2;
        if (index + 1 >= argc)
        {
          std::cerr << ava::tui::sanitize_terminal_text(std::string(option)) << " requires an environment variable name\n";
          return 2;
        }
        env_var = std::string(argv[++index]);
        continue;
      }
      std::cerr << "unknown connect option\n";
      return 2;
    }

    if (source == CredentialSource::BrowserOAuth || source == CredentialSource::HeadlessOAuth)
    {
      if (!provider || *provider != "openai")
      {
        std::cerr << "OpenAI OAuth flags require provider `openai`\n";
        return 2;
      }
      if (source == CredentialSource::BrowserOAuth)
        return run_connect_openai_browser(paths, std::cout, std::cerr);
      return run_connect_openai_headless(paths, std::cout, std::cerr);
    }

    if (source == CredentialSource::Stdin || source == CredentialSource::Env)
    {
      if (!provider)
      {
        std::cerr << "connect requires a provider with headless credential sources\n";
        return 2;
      }
      return run_connect_provider_credential(
          paths, ava::app::ConnectProviderCredentialOptions{.provider_id = *provider, .credential_type = credential_type.value(), .env_var = env_var}, std::cin,
          std::cout, std::cerr);
    }

    if (source == CredentialSource::Prompt)
    {
      return run_connect_provider_wizard(
          paths, ava::app::ConnectProviderWizardOptions{.provider_id = provider, .credential_type = credential_type, .stdin_is_tty = stdin_is_tty()}, std::cin,
          std::cout, std::cerr);
    }

    if (provider && *provider == "openai")
    {
      return run_connect_openai_wizard(paths, ava::app::ConnectProviderWizardOptions{.provider_id = provider, .stdin_is_tty = stdin_is_tty()}, std::cin,
                                       std::cout, std::cerr);
    }
    return run_connect_provider_wizard(paths, ava::app::ConnectProviderWizardOptions{.provider_id = provider, .stdin_is_tty = stdin_is_tty()}, std::cin,
                                       std::cout, std::cerr);
  };

  for (int index = 1; index < argc; ++index)
  {
    std::string_view const arg(argv[index]);
    if (arg == "connect")
    {
      std::optional<std::string> provider;
      if (index + 1 < argc && !std::string_view(argv[index + 1]).starts_with("--"))
        provider = argv[++index];
      return parse_connect_like_command(index, provider);
    }
    if (arg == "login")
    {
      std::optional<std::string> provider;
      if (index + 1 < argc && !std::string_view(argv[index + 1]).starts_with("--"))
        provider = argv[++index];
      return parse_connect_like_command(index, provider);
    }
    if (arg == "auth")
    {
      if (index + 1 >= argc || std::string_view(argv[++index]) != "login")
      {
        std::cerr << "auth requires login\n";
        return 2;
      }
      std::optional<std::string> provider;
      if (index + 1 < argc && !std::string_view(argv[index + 1]).starts_with("--"))
        provider = argv[++index];
      return parse_connect_like_command(index, provider);
    }
    if (arg == "packages" || arg == "package")
    {
      std::cout << "AVA package manager is deferred pending local-source, provenance, trust, rollback, and compatibility policy.\n"
                   "Install resources manually under $XDG_CONFIG_HOME/ava or trusted project .ava directories; see docs/CONFIG.md and docs/plugin-system.md.\n";
      return 0;
    }
    if (arg == "--help" || arg == "-h")
    {
      print_help();
      return 0;
    }
    if (arg == "--version")
    {
      std::cout << "ava " << version::kFullVersion << '\n';
      return 0;
    }
    if (arg == "--mode")
    {
      if (index + 1 >= argc)
      {
        std::cerr << "--mode requires build, plan, text, json, or rpc\n";
        return 2;
      }
      std::string_view const requested_mode(argv[++index]);
      if (requested_mode == "text")
      {
        print_mode = true;
        print_output_format = ava::app::PrintOutputFormat::Text;
        print_output_flag_seen = true;
        continue;
      }
      if (requested_mode == "json")
      {
        print_mode = true;
        print_output_format = ava::app::PrintOutputFormat::Json;
        print_output_flag_seen = true;
        continue;
      }
      if (requested_mode == "rpc")
      {
        rpc_mode = true;
        continue;
      }
      auto parsed = ava::agent::parse_mode(requested_mode);
      if (!parsed)
      {
        std::cerr << "--mode requires build, plan, text, json, or rpc\n";
        return 2;
      }
      mode = *parsed;
      continue;
    }
    if (arg == "--print" || arg == "-p")
    {
      print_mode = true;
      if (index + 1 < argc && !is_cli_option(argv[index + 1]) && !is_cli_file_argument(argv[index + 1]))
      {
        append_prompt_argument(print_prompt, argv[++index]);
      }
      continue;
    }
    if (arg == "--rpc")
    {
      rpc_mode = true;
      continue;
    }
    if (arg == "--json")
    {
      print_output_format = ava::app::PrintOutputFormat::Json;
      print_output_flag_seen = true;
      continue;
    }
    if (arg == "--output")
    {
      if (index + 1 >= argc)
      {
        std::cerr << "--output requires json, text, or rpc\n";
        return 2;
      }
      std::string_view const output(argv[++index]);
      if (output == "json")
      {
        print_output_format = ava::app::PrintOutputFormat::Json;
      }
      else if (output == "text")
      {
        print_output_format = ava::app::PrintOutputFormat::Text;
      }
      else if (output == "rpc")
      {
        rpc_mode = true;
      }
      else
      {
        std::cerr << "--output requires json, text, or rpc\n";
        return 2;
      }
      print_output_flag_seen = true;
      continue;
    }
    if (arg == "--allow")
    {
      if (index + 1 >= argc || is_cli_option(argv[index + 1]))
      {
        std::cerr << "--allow requires read-only\n";
        return 2;
      }
      auto added = ava::app::add_headless_allow_policy(headless_permission_policy, argv[++index]);
      if (!added)
      {
        std::cerr << added.error().format() << '\n';
        return 2;
      }
      print_permission_flag_seen = true;
      continue;
    }
    if (arg == "--allow-tool")
    {
      if (index + 1 >= argc || is_cli_option(argv[index + 1]))
      {
        std::cerr << "--allow-tool requires a comma-separated tool list\n";
        return 2;
      }
      auto added = ava::app::add_headless_allowed_tools(headless_permission_policy, argv[++index]);
      if (!added)
      {
        std::cerr << added.error().format() << '\n';
        return 2;
      }
      print_permission_flag_seen = true;
      continue;
    }
    if (arg == "--tools" || arg == "-t")
    {
      if (index + 1 >= argc || is_cli_option(argv[index + 1]))
      {
        std::cerr << std::string(arg) << " requires a comma-separated tool list\n";
        return 2;
      }
      append_tool_visibility_names(tool_visibility.included_tools, argv[++index]);
      continue;
    }
    if (arg == "--exclude-tools" || arg == "-xt")
    {
      if (index + 1 >= argc || is_cli_option(argv[index + 1]))
      {
        std::cerr << std::string(arg) << " requires a comma-separated tool list\n";
        return 2;
      }
      append_tool_visibility_names(tool_visibility.excluded_tools, argv[++index]);
      continue;
    }
    if (arg == "--no-builtin-tools" || arg == "-nbt")
    {
      if (tool_visibility.mode != ava::agent::ToolVisibilityMode::NoTools)
        tool_visibility.mode = ava::agent::ToolVisibilityMode::NoBuiltinTools;
      continue;
    }
    if (arg == "--no-tools" || arg == "-nt")
    {
      tool_visibility.mode = ava::agent::ToolVisibilityMode::NoTools;
      continue;
    }
    if (arg == "--session" || arg == "--session-id")
    {
      if (index + 1 >= argc)
      {
        std::cerr << std::string(arg) << " requires a session id\n";
        return 2;
      }
      requested_session_id = std::string(argv[++index]);
      continue;
    }
    if (arg == "--fork")
    {
      if (index + 1 >= argc)
      {
        std::cerr << "--fork requires a session id\n";
        return 2;
      }
      fork_session_id = std::string(argv[++index]);
      continue;
    }
    if (arg == "--name" || arg == "-n")
    {
      if (index + 1 >= argc)
      {
        std::cerr << std::string(arg) << " requires a session name\n";
        return 2;
      }
      initial_session_name = std::string(argv[++index]);
      continue;
    }
    if (arg == "--session-dir")
    {
      if (index + 1 >= argc)
      {
        std::cerr << "--session-dir requires a directory\n";
        return 2;
      }
      session_dir = std::filesystem::path(argv[++index]);
      continue;
    }
    if (arg == "--continue" || arg == "--resume" || arg == "-c" || arg == "-r")
    {
      continue_last_session = true;
      continue;
    }
    if (arg == "--no-session")
    {
      sessionless = true;
      continue;
    }
    if (arg == "--offline")
    {
      offline = true;
      continue;
    }
    if (arg == "--thinking")
    {
      if (index + 1 >= argc || is_cli_option(argv[index + 1]))
      {
        std::cerr << "--thinking requires a reasoning level (off or a level supported by the active model)\n";
        return 2;
      }
      initial_reasoning_level = std::string(argv[++index]);
      continue;
    }
    if (arg == "--system-prompt")
    {
      if (index + 1 >= argc)
      {
        std::cerr << "--system-prompt requires text\n";
        return 2;
      }
      prompt_overrides.system_prompt = std::string(argv[++index]);
      continue;
    }
    if (arg == "--append-system-prompt")
    {
      if (index + 1 >= argc)
      {
        std::cerr << "--append-system-prompt requires text\n";
        return 2;
      }
      prompt_overrides.append_system_prompts.push_back(std::string(argv[++index]));
      continue;
    }

    if (is_cli_file_argument(arg))
    {
      print_mode = true;
      cli_file_arguments.push_back(std::string(arg.substr(1)));
      continue;
    }

    if (!arg.starts_with("-"))
    {
      print_mode = true;
      append_prompt_argument(print_prompt, arg);
      continue;
    }

    std::cerr << "unknown argument: " << arg << '\n';
    return 2;
  }

  if (print_mode && rpc_mode)
  {
    std::cerr << "use either --print or --rpc, not both\n";
    return 2;
  }

  if (!print_mode && !rpc_mode && print_output_flag_seen)
  {
    std::cerr << "--json and --output text/json are only supported with --print; use --rpc or --output rpc for RPC\n";
    return 2;
  }

  if (!print_mode && !rpc_mode && print_permission_flag_seen)
  {
    std::cerr << "--allow and --allow-tool are only supported with --print or --rpc\n";
    return 2;
  }

  auto const selected_persisted_session_mode = (requested_session_id ? 1 : 0) + (continue_last_session ? 1 : 0) + (fork_session_id ? 1 : 0);
  if (selected_persisted_session_mode > 1)
  {
    std::cerr << "use only one of --session/--session-id, --continue/--resume, or --fork\n";
    return 2;
  }
  if (sessionless && selected_persisted_session_mode > 0)
  {
    std::cerr << "use either --no-session or session resume options, not both\n";
    return 2;
  }

  prepend_file_arguments_to_prompt(print_prompt, cli_file_arguments);

  auto runtime_paths = paths;
  if (session_dir)
  {
    std::error_code error;
    auto resolved_session_dir = std::filesystem::absolute(*session_dir, error);
    if (error)
    {
      std::cerr << "failed to resolve --session-dir: " << error.message() << '\n';
      return 2;
    }
    runtime_paths.sessions_dir = resolved_session_dir.lexically_normal();
  }

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = std::filesystem::current_path();
  open_options.current_dir = open_options.workspace_dir;
  open_options.requested_session_id = requested_session_id;
  open_options.fork_session_id = fork_session_id;
  open_options.initial_session_name = initial_session_name;
  open_options.continue_last_session = continue_last_session;
  open_options.sessionless = sessionless;
  open_options.mode = mode;
  open_options.tool_visibility = std::move(tool_visibility);
  open_options.paths = runtime_paths;
  open_options.prompt_overrides = std::move(prompt_overrides);
  open_options.initial_reasoning_level = std::move(initial_reasoning_level);
  open_options.offline = offline;

  if (print_mode)
  {
    return ava::app::run_print_mode(ava::app::PrintModeOptions{.open_options = open_options,
                                                               .explicit_prompt = print_prompt,
                                                               .read_stdin = !stdin_is_tty(),
                                                               .output_format = print_output_format,
                                                               .permission_policy = std::move(headless_permission_policy),
                                                               .provider_override = std::nullopt,
                                                               .transport_override = std::nullopt},
                                    std::cin, std::cout, std::cerr);
  }

  if (rpc_mode)
  {
    return ava::app::run_rpc_mode(ava::app::RpcModeOptions{.open_options = open_options, .permission_policy = std::move(headless_permission_policy)}, std::cin,
                                  std::cout, std::cerr);
  }

  auto session = ava::app::open_runtime_session(open_options);
  if (!session)
  {
    std::cerr << session.error().format() << '\n';
    return 1;
  }

  bool const print_farewell = stdin_is_tty() && stdout_is_tty();
  int const status = run_interactive(*session);
  if (print_farewell)
    print_exit_card(*session, status);
  return status;
}

}  // namespace ava::app
