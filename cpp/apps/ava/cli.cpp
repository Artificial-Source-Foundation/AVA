#include "cli.hpp"

#include <stdexcept>

#include <CLI/CLI.hpp>

namespace ava::app {

CliOptions parse_cli_or_throw(int argc, char** argv) {
  CliOptions options;

  CLI::App app{"AVA C++ Milestone 9 headless CLI"};
  app.add_option("goal", options.goal, "Goal to execute immediately");

  app.add_option("--provider", options.provider, "Provider override");
  app.add_option("--model", options.model, "Model override");

  app.add_flag("-c,--continue", options.resume, "Continue latest session");
  app.add_option("--session", options.session_id, "Continue a specific session id");

  app.add_flag("--json", options.json, "Emit runtime events as NDJSON");
  auto* max_turns = app.add_option("--max-turns", options.max_turns, "Maximum runtime turns");
  max_turns->check(CLI::Range(1, 10000));
  app.add_flag("--auto-approve", options.auto_approve, "Allow tool approvals without interaction (M9 scope)");

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
  if(options.resume && options.session_id.has_value()) {
    throw std::invalid_argument("--continue and --session cannot be used together");
  }

  return options;
}

}  // namespace ava::app
