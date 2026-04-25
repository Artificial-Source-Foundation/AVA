#include "ava/tools/command_classifier.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <string_view>
#include <vector>

namespace ava::tools {
namespace {

[[nodiscard]] std::string lower_ascii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

[[nodiscard]] bool contains_any(const std::string& value, const std::vector<std::string_view>& needles) {
  return std::any_of(needles.begin(), needles.end(), [&](std::string_view needle) {
    return value.find(needle) != std::string::npos;
  });
}

[[nodiscard]] bool matches_regex(const std::string& value, const std::regex& pattern) {
  return std::regex_search(value, pattern);
}

[[nodiscard]] bool has_shell_control_operator(const std::string& value) {
  return value.find_first_of(";&|\n\r`<>") != std::string::npos || value.find("&&") != std::string::npos ||
         value.find("||") != std::string::npos || value.find("$(") != std::string::npos;
}

[[nodiscard]] bool is_known_low_risk_simple_command(const std::string& value) {
  if(has_shell_control_operator(value)) {
    return false;
  }

  static const std::regex low_command(
      R"re(^[[:space:]]*((ls|pwd)([[:space:]].*)?|cargo[[:space:]]+(test|check|build)([[:space:]].*)?|pnpm[[:space:]]+(lint|typecheck|test)([[:space:]].*)?|git[[:space:]]+(status|diff|log)([[:space:]].*)?)[[:space:]]*$)re"
  );
  return matches_regex(value, low_command);
}

}  // namespace

std::string risk_level_to_string(RiskLevel level) {
  switch(level) {
    case RiskLevel::Safe:
      return "safe";
    case RiskLevel::Low:
      return "low";
    case RiskLevel::Medium:
      return "medium";
    case RiskLevel::High:
      return "high";
    case RiskLevel::Critical:
      return "critical";
  }
  return "high";
}

CommandClassification classify_bash_command(const std::string& command) {
  const auto lower = lower_ascii(command);

  static const std::regex rm_root(R"re((^|[;&|[:space:]])rm[[:space:]]+([^;&|]*[[:space:]])?-[[:alnum:]]*r[[:alnum:]]*f?[[:alnum:]]*([[:space:]]+|=)(/|['"]/['"]?)([[:space:]]|$|[;&|]))re");
  static const std::regex rm_root_reversed(R"re((^|[;&|[:space:]])rm[[:space:]]+([^;&|]*[[:space:]])?-[[:alnum:]]*f[[:alnum:]]*r[[:alnum:]]*([[:space:]]+|=)(/|['"]/['"]?)([[:space:]]|$|[;&|]))re");
  static const std::regex rm_root_long(
      R"re((^|[;&|[:space:]])rm([^;&|]*[[:space:]])(--recursive|-r|-rf|-fr)([^;&|]*[[:space:]])(--force|-f|-rf|-fr)([^;&|]*[[:space:]])['"]?/["']?([[:space:]]|$|[;&|]))re"
  );
  static const std::regex rm_root_no_preserve(
      R"re((^|[;&|[:space:]])rm([^;&|]*[[:space:]])--no-preserve-root([^;&|]*[[:space:]])['"]?/["']?([[:space:]]|$|[;&|]))re"
  );
  static const std::regex rm_recursive_root_any_order(
      R"re((^|[;&|[:space:]])rm(?=[^;&|]*([[:space:]]-r([[:space:]]|$)|[[:space:]]-[[:alnum:]]*r[[:alnum:]]*([[:space:]]|$)|[[:space:]]--recursive([[:space:]]|$)))[^;&|]*[[:space:]]['"]?/["']?([[:space:]]|$|[;&|]))re"
  );
  static const std::regex fork_bomb(R"re(:[[:space:]]*\([[:space:]]*\)[[:space:]]*\{[[:space:]]*:[[:space:]]*\|[[:space:]]*:)re");
  static const std::regex pipe_to_shell(R"re((curl|wget)[^|;]*\|[[:space:]]*([^[:space:]]*/)?(sh|bash|zsh|fish)\b)re");
  static const std::regex reverse_shell(R"re((/dev/tcp/|nc[[:space:]].*(-e|/bin/sh|/bin/bash)|bash[[:space:]]+-i|python[^;&|]*(socket|pty\.spawn)))re");

  if(matches_regex(lower, rm_root) || matches_regex(lower, rm_root_reversed) || matches_regex(lower, rm_root_long) || matches_regex(lower, rm_root_no_preserve) || matches_regex(lower, rm_recursive_root_any_order)) {
    return CommandClassification{.risk_level = RiskLevel::Critical, .reason = "removes the filesystem root"};
  }
  if(matches_regex(lower, fork_bomb)) {
    return CommandClassification{.risk_level = RiskLevel::Critical, .reason = "contains a fork bomb pattern"};
  }
  if(matches_regex(lower, pipe_to_shell)) {
    return CommandClassification{.risk_level = RiskLevel::Critical, .reason = "pipes downloaded code into a shell"};
  }
  if(matches_regex(lower, reverse_shell)) {
    return CommandClassification{.risk_level = RiskLevel::Critical, .reason = "contains a reverse-shell pattern"};
  }

  static const std::regex privileged_or_tampering(
      R"re((^|[;&|[:space:]])(sudo|doas|pkexec|mkfs[^[:space:]]*|crontab)([[:space:]]|$)|(^|[;&|[:space:]])dd[[:space:]][^;&|]*(if=|of=)|>[[:space:]]*/dev/|chmod[[:space:]]+0?777|chown[[:space:]]+-r|/etc/sudoers|ssh-key|authorized_keys)re"
  );
  if(matches_regex(lower, privileged_or_tampering)) {
    return CommandClassification{.risk_level = RiskLevel::Critical, .reason = "uses privileged or system-tampering operations"};
  }
  if(contains_any(lower, {".ava/mcp.json", ".ava/tools", "credentials.json", "trusted_projects.json", "permissions.toml"})) {
    return CommandClassification{.risk_level = RiskLevel::Critical, .reason = "modifies AVA trust or credential surfaces"};
  }

  static const std::regex high_risk_command(
      R"re((^|[;&|[:space:]])(env|printenv|npm[[:space:]]+publish|pnpm[[:space:]]+publish|docker[[:space:]]+(rm|rmi)|kubectl[[:space:]]+delete|git[[:space:]]+(push|reset|checkout|clean)|rm[[:space:]]+.*-(rf|fr|r|f)|curl|wget|scp|rsync)([[:space:]]|$)|/proc/self/environ|/proc/1/environ|cat[[:space:]]+(~/.|\$home))re"
  );
  if(matches_regex(lower, high_risk_command)) {
    return CommandClassification{.risk_level = RiskLevel::High, .reason = "performs destructive, network, credential, or environment-sensitive work"};
  }

  if(is_known_low_risk_simple_command(lower)) {
    return CommandClassification{.risk_level = RiskLevel::Low, .reason = "known read-only or verification command"};
  }

  return CommandClassification{.risk_level = RiskLevel::High, .reason = "unclassified shell command requires approval"};
}

}  // namespace ava::tools
