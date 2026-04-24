#include <exception>
#include <iostream>

#include <CLI/CLI.hpp>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include "cli.hpp"
#include "ava/control_plane/control_plane.hpp"
#include "ava/config/config.hpp"
#include "ava/core/build_config.hpp"
#include "headless_run.hpp"
#include "ava/platform/platform.hpp"
#include "ava/types/types.hpp"

int main(int argc, char** argv) {
  ava::app::CliOptions cli;
  try {
    cli = ava::app::parse_cli_or_throw(argc, argv);
  } catch(const CLI::CallForHelp& e) {
    std::cout << e.what() << std::endl;
    return 0;
  } catch(const std::exception& ex) {
    fmt::print(stderr, "error: {}\n", ex.what());
    return 2;
  }

  const auto build = ava::types::current_build_info();
  if(cli.show_version) {
    fmt::print("{} {}\n", build.name, build.version);
    return 0;
  }

  spdlog::info(
      "starting {} on {} (ftxui={}, cpr={})",
      build.version,
      ava::platform::platform_tag(),
      ava::core::kWithFtxui,
      ava::core::kWithCpr
  );

  if(cli.smoke_mode) {
    fmt::print("foundation=types+control_plane+platform+config+session+llm+tools\n");
    const auto config = ava::config::summary();
    fmt::print(
        "config=xdg:{} trust:{} creds:{} models:{}\n",
        config.xdg_paths,
        config.trust_store,
        config.credential_store,
        config.embedded_model_registry
    );
    if(const auto* spec = ava::control_plane::command_spec_by_name("submit_goal")) {
      fmt::print(
          "command={} mode={}\n",
          spec->name,
          ava::control_plane::completion_mode_to_string(spec->completion_mode)
      );
    }
    fmt::print("headless_cli=m16_blocking_non_interactive\n");
    return 0;
  }

  try {
    return ava::app::run_headless_blocking(cli);
  } catch(const std::exception& ex) {
    fmt::print(stderr, "error: {}\n", ex.what());
    return 2;
  }
}
