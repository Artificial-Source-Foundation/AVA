#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ava::platform {

struct CommandOutput {
  std::string stdout_text;
  std::string stderr_text;
  int exit_code{0};
  std::chrono::milliseconds duration{0};
};

struct ExecuteOptions {
  std::optional<std::chrono::milliseconds> timeout;
  std::optional<std::filesystem::path> working_dir;
  std::vector<std::pair<std::string, std::string>> env_vars;
  bool scrub_env{false};
};

}  // namespace ava::platform
