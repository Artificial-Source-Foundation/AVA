#include "ava/permissions/command_policy_support.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace ava::permissions::detail {
namespace {

std::string lowercase(std::string_view value)
{
  std::string result(value);
  std::ranges::transform(result, result.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return result;
}

bool contains_any(std::string_view value, std::vector<std::string_view> const& needles)
{
  return std::ranges::any_of(needles,
                             [value](std::string_view needle) { return value.find(needle) != std::string_view::npos; });
}

bool equals_any(std::string_view value, std::vector<std::string_view> const& candidates)
{
  return std::ranges::any_of(candidates, [value](std::string_view candidate) { return value == candidate; });
}

bool is_shell_metacharacter(char ch)
{
  switch (ch) {
    case ';':
    case '&':
    case '|':
    case '<':
    case '>':
    case '`':
    case '$':
    case '(':
    case ')':
      return true;
    default:
      return false;
  }
}

bool is_secret_path(std::filesystem::path const& path)
{
  for (auto const& part : path.lexically_normal()) {
    auto const component = lowercase(part.string());
    if (equals_any(component, {".ssh", ".aws", ".gnupg", ".config/gcloud"})) {
      return true;
    }
  }

  auto const filename = lowercase(path.filename().string());
  if (filename == ".env" || filename.starts_with(".env.")) {
    return filename != ".env.example";
  }
  if (equals_any(filename, {".npmrc", ".netrc", ".pypirc", ".gem/credentials", ".dockerconfigjson", "credentials.json",
                            "auth.json", "config.json"})) {
    auto const full = lowercase(path.lexically_normal().string());
    return filename != "config.json" || contains_any(full, {"/.docker/", "\\.docker\\", ".docker/", ".docker\\"});
  }

  auto const full = lowercase(path.string());
  return contains_any(full, {"id_rsa", "id_ed25519", "id_ecdsa", "id_dsa", "/.ssh/", "\\.ssh\\", "/.aws/", "\\.aws\\",
                             "/.gnupg/", "\\.gnupg\\", "credentials", "secret", "token"});
}

}  // namespace

ParsedCommand parse_command_argv(std::string_view command)
{
  ParsedCommand parsed;
  std::string current;
  char quote = '\0';
  bool escaping = false;

  for (char const ch : command) {
    auto const byte = static_cast<unsigned char>(ch);
    if (byte < 0x20 || byte == 0x7F) {
      parsed.reason = "command contains a forbidden control byte";
      return parsed;
    }
    if (escaping) {
      current.push_back(ch);
      escaping = false;
      continue;
    }
    if (ch == '\\') {
      escaping = true;
      continue;
    }
    if (quote != '\0') {
      if (ch == quote) {
        quote = '\0';
      } else {
        current.push_back(ch);
      }
      continue;
    }
    if (ch == '\'' || ch == '"') {
      quote = ch;
      continue;
    }
    if (is_shell_metacharacter(ch)) {
      parsed.reason = "shell metacharacters are not supported by the command tool";
      return parsed;
    }
    if (std::isspace(byte) != 0) {
      if (!current.empty()) {
        parsed.argv.push_back(current);
        current.clear();
      }
      continue;
    }
    current.push_back(ch);
  }

  if (escaping || quote != '\0') {
    parsed.reason = "unterminated command escape or quote";
    return parsed;
  }
  if (!current.empty()) parsed.argv.push_back(current);
  if (parsed.argv.empty()) {
    parsed.reason = "empty command";
    return parsed;
  }
  parsed.ok = true;
  return parsed;
}

bool is_safe_relative_path_arg(std::string_view value)
{
  if (value.empty() || value.starts_with("-")) return true;
  std::filesystem::path const path(value);
  if (path.is_absolute()) return false;
  for (auto const& part : path.lexically_normal()) {
    if (part == "..") return false;
  }
  return !is_secret_path(path);
}

}  // namespace ava::permissions::detail
