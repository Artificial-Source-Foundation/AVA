#include "options.hpp"

#include <stdexcept>

#include <CLI/CLI.hpp>

namespace ava::tui {

TuiOptions parse_tui_options_or_throw(int argc, char** argv) {
  TuiOptions options;

  CLI::App app{"AVA C++ TUI"};
  app.add_option("--provider", options.provider, "Provider override");
  app.add_option("--model", options.model, "Model override");
  app.add_flag("-c,--continue", options.resume, "Continue latest session");
  app.add_option("--session", options.session_id, "Continue a specific session id");
  auto* max_turns = app.add_option("--max-turns", options.max_turns, "Maximum runtime turns");
  max_turns->check(CLI::Range(1, 10000));
  app.add_flag("--auto-approve", options.auto_approve, "Allow tool approvals without interaction");

  try {
    app.parse(argc, argv);
  } catch(const CLI::CallForHelp&) {
    throw;
  } catch(const CLI::ParseError& error) {
    throw std::invalid_argument(std::string("invalid CLI arguments: ") + error.what() + "\n" + app.help());
  }

  options.max_turns_explicit = max_turns->count() > 0;
  if(options.resume && options.session_id.has_value()) {
    throw std::invalid_argument("--continue and --session cannot be used together");
  }

  return options;
}

}  // namespace ava::tui
