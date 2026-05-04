#include "ava/app/command_format.h"

#include <cctype>
#include <system_error>
#include <utility>

#include "ava/app/commands.h"

namespace ava::app {

void add_output(CommandResult& result, std::string text) { result.output.push_back(std::move(text)); }

std::string display_path(std::filesystem::path const& path, std::filesystem::path const& base) {
  std::error_code error;
  auto const relative = std::filesystem::relative(path, base, error);
  if (!error) return relative.generic_string();
  return path.generic_string();
}

std::string sanitize_inline_text(std::string text) {
  for (auto& ch : text) {
    auto const byte = static_cast<unsigned char>(ch);
    if (byte < 0x20 || byte == 0x7F) ch = '?';
  }
  return text;
}

std::string joined_strings(std::vector<std::string> const& values, std::string_view separator) {
  std::string output;
  for (auto const& value : values) {
    if (!output.empty()) output += separator;
    output += value;
  }
  return output;
}

std::string missing_argument(std::string_view usage) { return "usage: " + std::string(usage); }

std::string command_argument(std::string_view line, std::string_view command) {
  if (line.size() <= command.size() || line[command.size()] != ' ') return {};
  return std::string(line.substr(command.size() + 1));
}

std::vector<std::string> split_command_arguments(std::string_view text) {
  std::vector<std::string> parts;
  std::size_t index = 0;
  while (index < text.size()) {
    while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index])) != 0) ++index;
    auto const start = index;
    while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index])) == 0) ++index;
    if (start < index) parts.emplace_back(text.substr(start, index - start));
  }
  return parts;
}

}  // namespace ava::app
