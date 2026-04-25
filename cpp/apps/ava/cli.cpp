#include "cli.hpp"

#include <charconv>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>

#include <CLI/CLI.hpp>

namespace ava::app {
namespace {

[[nodiscard]] std::optional<std::string> env_string(const char* name) {
  const char* value = std::getenv(name);
  if(value == nullptr || value[0] == '\0') {
    return std::nullopt;
  }
  return std::string(value);
}

[[nodiscard]] PostCompleteMessageOption parse_post_complete_group_message(std::string_view value) {
  const auto separator = value.find(':');
  if(separator == std::string_view::npos || separator == 0) {
    throw std::invalid_argument("--later-group must use '<group>:<message>'");
  }
  if(separator + 1 >= value.size()) {
    throw std::invalid_argument("--later-group must include a message after '<group>:'");
  }

  std::uint32_t group = 0;
  const auto* begin = value.data();
  const auto* end = begin + separator;
  const auto [parsed_end, error] = std::from_chars(begin, end, group);
  if(error == std::errc::result_out_of_range) {
    throw std::invalid_argument("--later-group group is too large");
  }
  if(error != std::errc{} || parsed_end != end) {
    throw std::invalid_argument("--later-group group must be numeric");
  }

  return PostCompleteMessageOption{.group = group == 0 ? 1 : group, .text = std::string(value.substr(separator + 1))};
}

}  // namespace

CliOptions parse_cli_or_throw(int argc, char** argv) {
  CliOptions options;

  CLI::App app{"AVA C++ headless CLI"};
  app.add_option("goal", options.goal, "Goal to execute immediately");

  app.add_option("--provider", options.provider, "Provider override");
  app.add_option("--model", options.model, "Model override");
  app.add_option("--cwd", options.cwd, "Workspace directory override");
  app.add_option("--agent", options.agent, "Primary agent profile override");
  app.add_flag("--trust", options.trust, "Trust the selected workspace for project-local config");

  app.add_flag("-c,--continue", options.resume, "Continue latest session");
  app.add_option("--session", options.session_id, "Continue a specific session id");

  app.add_flag("--json", options.json, "Emit runtime events as NDJSON");
  auto* max_turns = app.add_option("--max-turns", options.max_turns, "Maximum runtime turns");
  max_turns->check(CLI::Range(1, 10000));
  app.add_flag("--auto-approve", options.auto_approve, "Allow tool approvals without interaction (M9 scope)");
  auto* max_budget = app.add_option("--max-budget", options.max_budget_usd, "Maximum run budget in USD");
  max_budget->check(CLI::Range(0.0, 1000000.0));
  app.add_option("--follow-up", options.follow_up_messages, "Queue a follow-up message after the main request");
  app.add_option("--later", options.post_complete_messages, "Queue a post-complete message after follow-ups");
  app.add_option_function<std::string>(
      "--later-group",
      [&](const std::string& value) {
        options.post_complete_group_messages.push_back(parse_post_complete_group_message(value));
      },
      "Queue a post-complete message in an explicit group as '<group>:<message>'"
  );

  app.add_flag("--version", options.show_version, "Print version/build information");
  app.add_flag("--smoke", options.smoke_mode, "Run foundational smoke path");

  try {
    app.parse(argc, argv);
  } catch(const CLI::CallForHelp&) {
    throw;
  } catch(const CLI::ParseError&) {
    throw std::invalid_argument("invalid CLI arguments");
  }

  options.max_turns_explicit = max_turns->count() > 0;
  if(!options.provider.has_value()) {
    options.provider = env_string("AVA_PROVIDER");
  }
  if(!options.model.has_value()) {
    options.model = env_string("AVA_MODEL");
  }
  if(!options.cwd.has_value()) {
    if(const auto env_cwd = env_string("AVA_WORKING_DIRECTORY"); env_cwd.has_value()) {
      options.cwd = std::filesystem::path(*env_cwd);
    }
  }
  if(!options.agent.has_value()) {
    options.agent = env_string("AVA_AGENT");
  }
  if(options.resume && options.session_id.has_value()) {
    throw std::invalid_argument("--continue and --session cannot be used together");
  }

  return options;
}

}  // namespace ava::app
