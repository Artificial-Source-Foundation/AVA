#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ava::app {

struct PostCompleteMessageOption {
  std::uint32_t group{1};
  std::string text;
};

struct CliOptions {
  std::optional<std::string> goal;
  std::optional<std::string> provider;
  std::optional<std::string> model;
  std::optional<std::filesystem::path> cwd;
  std::optional<std::string> agent;
  bool trust{false};
  bool resume{false};
  std::optional<std::string> session_id;
  bool json{false};
  std::size_t max_turns{16};
  bool max_turns_explicit{false};
  bool auto_approve{false};
  double max_budget_usd{0.0};
  std::vector<std::string> follow_up_messages;
  std::vector<std::string> post_complete_messages;
  std::vector<PostCompleteMessageOption> post_complete_group_messages;
  bool show_version{false};
  bool smoke_mode{false};
};

[[nodiscard]] CliOptions parse_cli_or_throw(int argc, char** argv);

}  // namespace ava::app
