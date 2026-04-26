#include "ava/tools/command_classifier.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <regex>
#include <string_view>
#include <utility>
#include <vector>

#include "ava/core/string_utils.hpp"

namespace ava::tools {

[[nodiscard]] CommandClassification classify_bash_command_impl(const std::string& command, std::size_t depth);

namespace {

[[nodiscard]] bool contains_any(const std::string& value, const std::vector<std::string_view>& needles) {
  return std::any_of(needles.begin(), needles.end(), [&](std::string_view needle) {
    return value.find(needle) != std::string::npos;
  });
}

[[nodiscard]] bool matches_regex(const std::string& value, const std::regex& pattern) {
  return std::regex_search(value, pattern);
}

enum class ShellQuoteMode {
  None,
  Single,
  Double,
};

constexpr std::size_t kMaxShellNestingDepth = 4;

constexpr std::array<std::string_view, 16> kCriticalRmPathPrefixes = {
    "/", "~", "/home", "/root", "/etc", "/usr", "/var", "/boot", "/sys", "/proc", "/dev", "/lib",
    "/lib64", "/bin", "/sbin", "/opt",
};

constexpr std::array<std::string_view, 16> kUnicodeWhitespaceUtf8 = {
    "\xC2\xA0", "\xE1\x9A\x80", "\xE2\x80\x80", "\xE2\x80\x81", "\xE2\x80\x82", "\xE2\x80\x83",
    "\xE2\x80\x84", "\xE2\x80\x85", "\xE2\x80\x86", "\xE2\x80\x87", "\xE2\x80\x88", "\xE2\x80\x89",
    "\xE2\x80\x8A", "\xE2\x80\xAF", "\xE2\x81\x9F", "\xE3\x80\x80",
};

[[nodiscard]] bool is_shell_newline(char ch) {
  return ch == '\n' || ch == '\r';
}

[[nodiscard]] bool next_is_shell_newline(std::string_view value, std::size_t index) {
  return index + 1 < value.size() && is_shell_newline(value.at(index + 1));
}

[[nodiscard]] std::size_t consume_shell_newline(std::string_view value, std::size_t index) {
  if(index < value.size() && value.at(index) == '\r' && index + 1 < value.size() && value.at(index + 1) == '\n') {
    return index + 1;
  }
  return index;
}

[[nodiscard]] std::string shell_dequote_token(std::string_view token) {
  std::string dequoted;
  dequoted.reserve(token.size());

  auto mode = ShellQuoteMode::None;
  bool escaped = false;

  for(std::size_t index = 0; index < token.size(); ++index) {
    const auto ch = token.at(index);

    if(mode == ShellQuoteMode::Single) {
      if(ch == '\'') {
        mode = ShellQuoteMode::None;
      } else {
        dequoted.push_back(ch);
      }
      continue;
    }

    if(mode == ShellQuoteMode::Double) {
      if(escaped) {
        if(is_shell_newline(ch)) {
          index = consume_shell_newline(token, index);
          escaped = false;
          continue;
        }

        if(ch == '$' || ch == '`' || ch == '"' || ch == '\\') {
          dequoted.push_back(ch);
        } else {
          dequoted.push_back('\\');
          dequoted.push_back(ch);
        }
        escaped = false;
        continue;
      }

      if(ch == '\\') {
        if(next_is_shell_newline(token, index)) {
          ++index;
          index = consume_shell_newline(token, index);
          continue;
        }
        escaped = true;
        continue;
      }
      if(ch == '"') {
        mode = ShellQuoteMode::None;
        continue;
      }

      dequoted.push_back(ch);
      continue;
    }

    if(escaped) {
      if(is_shell_newline(ch)) {
        index = consume_shell_newline(token, index);
        escaped = false;
        continue;
      }

      dequoted.push_back(ch);
      escaped = false;
      continue;
    }

    if(ch == '\\') {
      if(next_is_shell_newline(token, index)) {
        ++index;
        index = consume_shell_newline(token, index);
        continue;
      }
      escaped = true;
      continue;
    }
    if(ch == '\'') {
      mode = ShellQuoteMode::Single;
      continue;
    }
    if(ch == '"') {
      mode = ShellQuoteMode::Double;
      continue;
    }

    dequoted.push_back(ch);
  }

  if(escaped) {
    dequoted.push_back('\\');
  }

  return dequoted;
}

[[nodiscard]] std::vector<std::string> tokenize_shell_command(std::string_view command) {
  std::vector<std::string> tokens;
  std::string current;
  current.reserve(command.size());

  auto flush_current = [&]() {
    if(!current.empty()) {
      tokens.push_back(current);
      current.clear();
    }
  };

  auto mode = ShellQuoteMode::None;
  bool escaped = false;

  for(std::size_t index = 0; index < command.size(); ++index) {
    const auto ch = command.at(index);

    if(mode == ShellQuoteMode::Single) {
      current.push_back(ch);
      if(ch == '\'') {
        mode = ShellQuoteMode::None;
      }
      continue;
    }

    if(mode == ShellQuoteMode::Double) {
      if(escaped) {
        if(is_shell_newline(ch)) {
          index = consume_shell_newline(command, index);
          escaped = false;
          continue;
        }

        if(ch == '$' || ch == '`' || ch == '"' || ch == '\\') {
          current.push_back(ch);
        } else {
          current.push_back('\\');
          current.push_back(ch);
        }
        escaped = false;
        continue;
      }

      if(ch == '\\') {
        if(next_is_shell_newline(command, index)) {
          ++index;
          index = consume_shell_newline(command, index);
          continue;
        }

        current.push_back(ch);
        escaped = true;
        continue;
      }

      current.push_back(ch);
      if(ch == '"') {
        mode = ShellQuoteMode::None;
      }
      continue;
    }

    if(escaped) {
      if(is_shell_newline(ch)) {
        index = consume_shell_newline(command, index);
        escaped = false;
        continue;
      }

      current.push_back(ch);
      escaped = false;
      continue;
    }

    if(ch == '\\') {
      if(next_is_shell_newline(command, index)) {
        ++index;
        index = consume_shell_newline(command, index);
        continue;
      }

      current.push_back(ch);
      escaped = true;
      continue;
    }

    if(ch == '\'') {
      current.push_back(ch);
      mode = ShellQuoteMode::Single;
      continue;
    }

    if(ch == '"') {
      current.push_back(ch);
      mode = ShellQuoteMode::Double;
      continue;
    }

    if(ch == '\n' || ch == '\r') {
      flush_current();
      if(ch == '\r' && index + 1 < command.size() && command.at(index + 1) == '\n') {
        ++index;
      }
      tokens.emplace_back(";");
      continue;
    }

    if(std::isspace(static_cast<unsigned char>(ch)) != 0) {
      flush_current();
      continue;
    }

    if(ch == '&' && index + 1 < command.size() && command.at(index + 1) == '>') {
      flush_current();
      if(index + 2 < command.size() && command.at(index + 2) == '>') {
        tokens.emplace_back("&>>");
        index += 2;
      } else {
        tokens.emplace_back("&>");
        ++index;
      }
      continue;
    }

    if(ch == '&') {
      flush_current();
      if(index + 1 < command.size() && command.at(index + 1) == '&') {
        tokens.emplace_back("&&");
        ++index;
      } else {
        tokens.emplace_back("&");
      }
      continue;
    }

    if(ch == '|') {
      flush_current();
      if(index + 1 < command.size() && command.at(index + 1) == '|') {
        tokens.emplace_back("||");
        ++index;
      } else if(index + 1 < command.size() && command.at(index + 1) == '&') {
        tokens.emplace_back("|&");
        ++index;
      } else {
        tokens.emplace_back("|");
      }
      continue;
    }

    if(ch == '<' || ch == '>') {
      flush_current();
      if(index + 1 < command.size() && (command.at(index + 1) == '&' || command.at(index + 1) == '|')) {
        tokens.emplace_back(std::string{ch, command.at(index + 1)});
        ++index;
      } else if(index + 1 < command.size() && command.at(index + 1) == ch) {
        if(index + 2 < command.size() && command.at(index + 2) == '&') {
          tokens.emplace_back(std::string{ch, ch, '&'});
          index += 2;
        } else {
          tokens.emplace_back(std::string(2, ch));
          ++index;
        }
      } else {
        tokens.emplace_back(std::string(1, ch));
      }
      continue;
    }

    if(ch == ';') {
      flush_current();
      tokens.emplace_back(";");
      continue;
    }

    current.push_back(ch);
  }

  flush_current();
  return tokens;
}

[[nodiscard]] bool token_contains_shell_metacharacters(std::string_view token) {
  return token.find_first_of(";|`$&") != std::string_view::npos;
}

struct RmFlags {
  bool has_recursive{false};
  bool has_force{false};
};

[[nodiscard]] bool token_matches_long_option_abbreviation(
    std::string_view token,
    std::string_view canonical,
    std::string_view minimum_abbreviation
) {
  if(token.size() < minimum_abbreviation.size() || !token.starts_with("--")) {
    return false;
  }

  const auto separator = token.find('=');
  const auto option_name = separator == std::string_view::npos ? token : token.substr(0, separator);
  if(option_name.size() < minimum_abbreviation.size()) {
    return false;
  }

  return canonical.starts_with(option_name);
}

[[nodiscard]] RmFlags parse_rm_flags(const std::vector<std::string_view>& tokens) {
  RmFlags flags;

  for(std::size_t index = 1; index < tokens.size(); ++index) {
    const auto token_string = shell_dequote_token(tokens.at(index));
    const auto token = std::string_view(token_string);
    if(token == "--") {
      break;
    }

    if(token_matches_long_option_abbreviation(token, "--recursive", "--r")) {
      flags.has_recursive = true;
      continue;
    }
    if(token_matches_long_option_abbreviation(token, "--force", "--f")) {
      flags.has_force = true;
      continue;
    }

    if(token.size() > 1 && token.front() == '-' && token.at(1) != '-') {
      for(std::size_t char_index = 1; char_index < token.size(); ++char_index) {
        const auto flag = token.at(char_index);
        if(flag == 'r' || flag == 'R') {
          flags.has_recursive = true;
        }
        if(flag == 'f') {
          flags.has_force = true;
        }
      }
    }
  }

  return flags;
}

[[nodiscard]] bool rm_has_recursive_force_flags(const std::vector<std::string_view>& tokens) {
  const auto flags = parse_rm_flags(tokens);
  return flags.has_recursive && flags.has_force;
}

[[nodiscard]] bool is_redirection_operator(std::string_view token) {
  return token == "<" || token == ">" || token == "<<" || token == ">>" || token == "<&" || token == ">&" ||
         token == ">|" || token == ">>&" || token == "&>" || token == "&>>";
}

[[nodiscard]] bool is_command_separator_token(std::string_view token) {
  return token == "&&" || token == "||" || token == ";" || token == "|" || token == "|&" || token == "&";
}

[[nodiscard]] bool is_env_assignment_token(std::string_view token) {
  token = ava::core::trim_ascii_view(token);
  if(token.empty()) {
    return false;
  }

  const auto equal_pos = token.find('=');
  if(equal_pos == std::string_view::npos || equal_pos == 0) {
    return false;
  }

  auto name = token.substr(0, equal_pos);
  if(!name.empty() && name.back() == '+') {
    name.remove_suffix(1);
  }
  if(name.empty()) {
    return false;
  }

  const auto first = static_cast<unsigned char>(name.front());
  if(std::isalpha(first) == 0 && name.front() != '_') {
    return false;
  }

  return std::all_of(name.begin() + 1, name.end(), [](char ch) {
    const auto as_unsigned = static_cast<unsigned char>(ch);
    return std::isalnum(as_unsigned) != 0 || ch == '_';
  });
}

[[nodiscard]] std::string token_binary_name(std::string_view token) {
  auto candidate = shell_dequote_token(token);
  while(!candidate.empty() && (candidate.back() == ';' || candidate.back() == '+')) {
    candidate.pop_back();
  }

  const auto slash = candidate.find_last_of('/');
  if(slash != std::string::npos) {
    candidate = candidate.substr(slash + 1);
  }
  return candidate;
}

[[nodiscard]] bool command_token_matches_binary(std::string_view token, std::string_view binary_name) {
  return token_binary_name(token) == binary_name;
}

[[nodiscard]] std::size_t find_command_segment_end(const std::vector<std::string>& tokens, std::size_t command_index) {
  std::size_t index = command_index + 1;
  while(index < tokens.size() && !is_command_separator_token(tokens.at(index))) {
    ++index;
  }
  return index;
}

[[nodiscard]] std::vector<std::string> extract_rm_path_candidates(const std::vector<std::string_view>& tokens) {
  std::vector<std::string> candidates;

  bool past_double_dash = false;
  for(std::size_t index = 1; index < tokens.size(); ++index) {
    const auto token_string = shell_dequote_token(tokens.at(index));
    const auto token = std::string_view(token_string);
    if(token == "--") {
      past_double_dash = true;
      continue;
    }

    if(is_redirection_operator(token)) {
      if(index + 1 < tokens.size()) {
        ++index;
      }
      continue;
    }

    if(!past_double_dash && !token.empty() && token.front() == '-') {
      continue;
    }

    const auto candidate = token_string;
    if(candidate.empty() || token_contains_shell_metacharacters(candidate)) {
      continue;
    }
    candidates.push_back(candidate);
  }

  return candidates;
}

[[nodiscard]] bool rm_has_unmodeled_expansion_path_operand(const std::vector<std::string_view>& tokens) {
  bool past_double_dash = false;
  for(std::size_t index = 1; index < tokens.size(); ++index) {
    const auto token_string = shell_dequote_token(tokens.at(index));
    const auto token = std::string_view(token_string);
    if(token == "--") {
      past_double_dash = true;
      continue;
    }
    if(is_redirection_operator(token)) {
      if(index + 1 < tokens.size()) {
        ++index;
      }
      continue;
    }
    if(!past_double_dash && !token.empty() && token.front() == '-') {
      continue;
    }
    if(token.find('$') != std::string_view::npos) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] std::string normalize_rm_path(std::string path) {
  path = ava::core::trim_copy(path);
  if(path.empty()) {
    return path;
  }

  std::string collapsed;
  collapsed.reserve(path.size());
  bool previous_was_slash = false;
  for(const auto ch : path) {
    if(ch == '/') {
      if(!previous_was_slash) {
        collapsed.push_back(ch);
      }
      previous_was_slash = true;
      continue;
    }

    collapsed.push_back(ch);
    previous_was_slash = false;
  }
  path = std::move(collapsed);

  std::string tilde_prefix;
  bool is_tilde_prefixed = false;
  bool is_absolute = false;

  std::size_t start = 0;
  if(path.front() == '~') {
    is_tilde_prefixed = true;
    const auto slash = path.find('/', 1);
    if(slash == std::string::npos) {
      tilde_prefix = path;
      start = path.size();
    } else {
      tilde_prefix = path.substr(0, slash);
      start = slash + 1;
    }
  } else if(path.front() == '/') {
    is_absolute = true;
    start = 1;
  }

  std::vector<std::string> components;
  std::size_t index = start;
  while(index <= path.size()) {
    const auto slash = path.find('/', index);
    const auto end = slash == std::string::npos ? path.size() : slash;
    const auto part = path.substr(index, end - index);

    if(part.empty() || part == ".") {
      // noop
    } else if(part == "..") {
      if(!components.empty() && components.back() != "..") {
        components.pop_back();
      } else if(!is_absolute && !is_tilde_prefixed) {
        components.push_back(part);
      }
    } else {
      components.push_back(part);
    }

    if(slash == std::string::npos) {
      break;
    }
    index = slash + 1;
  }

  std::string joined;
  for(std::size_t component_index = 0; component_index < components.size(); ++component_index) {
    if(component_index > 0) {
      joined.push_back('/');
    }
    joined += components.at(component_index);
  }

  if(is_absolute) {
    return joined.empty() ? "/" : "/" + joined;
  }
  if(is_tilde_prefixed) {
    if(tilde_prefix.empty()) {
      tilde_prefix = "~";
    }
    return joined.empty() ? tilde_prefix : tilde_prefix + "/" + joined;
  }

  return joined;
}

[[nodiscard]] bool path_contains_recursive_glob(std::string_view path) {
  return path == "**" || path == "/**" || path.find("/**/") != std::string_view::npos || path.ends_with("/**");
}

[[nodiscard]] std::vector<std::string> split_brace_expanded_path_candidates(std::string_view path) {
  const auto open = path.find('{');
  if(open == std::string_view::npos) {
    return {};
  }

  const auto close = path.find('}', open + 1);
  if(close == std::string_view::npos) {
    return {};
  }

  const auto inner = path.substr(open + 1, close - open - 1);
  if(inner.find(',') == std::string_view::npos) {
    return {};
  }

  const auto prefix = path.substr(0, open);
  const auto suffix = path.substr(close + 1);

  std::vector<std::string> candidates;
  std::size_t part_start = 0;
  while(part_start <= inner.size()) {
    const auto comma = inner.find(',', part_start);
    const auto part_end = comma == std::string_view::npos ? inner.size() : comma;
    const auto part = ava::core::trim_ascii_view(inner.substr(part_start, part_end - part_start));

    std::string candidate;
    candidate.reserve(prefix.size() + part.size() + suffix.size());
    candidate.append(prefix);
    candidate.append(part);
    candidate.append(suffix);
    candidates.push_back(std::move(candidate));

    if(comma == std::string_view::npos) {
      break;
    }
    part_start = comma + 1;
  }

  return candidates;
}

[[nodiscard]] bool recursive_glob_targets_critical_prefix(std::string_view normalized_path) {
  if(path_contains_recursive_glob(normalized_path) && (normalized_path == "/**" || normalized_path.starts_with("/**/"))) {
    return true;
  }

  for(const auto prefix : kCriticalRmPathPrefixes) {
    if(prefix == "/" || prefix == "~") {
      continue;
    }

    const auto recursive_prefix = std::string(prefix) + "/**";
    if(normalized_path == recursive_prefix || normalized_path.starts_with(recursive_prefix + "/")) {
      return true;
    }
  }

  return false;
}

[[nodiscard]] bool rm_path_is_critical_impl(std::string_view path, std::size_t depth) {
  constexpr std::size_t kMaxBraceExpansionDepth = 4;
  if(depth < kMaxBraceExpansionDepth) {
    const auto expanded_candidates = split_brace_expanded_path_candidates(path);
    for(const auto& candidate : expanded_candidates) {
      if(rm_path_is_critical_impl(candidate, depth + 1)) {
        return true;
      }
    }
  }

  const auto normalized = normalize_rm_path(std::string(path));
  if(normalized.empty()) {
    return false;
  }
  if(normalized == "*" || normalized == "/*") {
    return true;
  }

  const auto normalized_view = std::string_view(normalized);
  if(!normalized_view.empty() && normalized_view.front() == '~') {
    const auto slash = normalized_view.find('/');
    if(slash == std::string_view::npos) {
      return true;
    }
    const auto rest = normalized_view.substr(slash);
    if(rest == "/*" || rest == "/**" || rest.starts_with("/**/")) {
      return true;
    }
  }

  if(normalized_view.starts_with('/')) {
    const auto next_slash = normalized_view.find('/', 1);
    const auto first_component = normalized_view.substr(1, next_slash == std::string_view::npos ? std::string_view::npos : next_slash - 1);
    if(first_component.find_first_of("*?[{}!+@()|") != std::string_view::npos) {
      return true;
    }
  }

  for(const auto prefix : kCriticalRmPathPrefixes) {
    if(prefix == "/" || prefix == "~") {
      continue;
    }
    if(normalized_view.starts_with(prefix)) {
      const auto rest = normalized_view.substr(prefix.size());
      if(rest.starts_with('/')) {
        const auto child = rest.substr(1, rest.find('/', 1) == std::string_view::npos ? std::string_view::npos : rest.find('/', 1) - 1);
        if(child.find_first_of("*?[") != std::string_view::npos) {
          return true;
        }
      }
    }
  }

  if(recursive_glob_targets_critical_prefix(normalized_view)) {
    return true;
  }

  for(const auto prefix : kCriticalRmPathPrefixes) {
    if(normalized == prefix) {
      return true;
    }

    if(prefix == "/") {
      continue;
    }

    const auto prefix_string = std::string(prefix);
    if(normalized == prefix_string + "/" || normalized == prefix_string + "/*") {
      return true;
    }

    if(normalized_view.starts_with(prefix_string + "/")) {
      const auto rest = normalized_view.substr(prefix_string.size() + 1);
      const auto next_slash = rest.find('/');
      const auto first_descendant = rest.substr(0, next_slash == std::string_view::npos ? std::string_view::npos : next_slash);
      if(first_descendant.find_first_of("*?[{}!+@()|") != std::string_view::npos) {
        return true;
      }
    }

    if(normalized_view.size() >= prefix.size() && normalized_view.substr(0, prefix.size()) == prefix) {
      const auto rest = normalized_view.substr(prefix.size());
      if(rest == "/*" || rest == "*") {
        return true;
      }
    }
  }

  return false;
}

[[nodiscard]] bool rm_path_is_critical(std::string_view path) {
  return rm_path_is_critical_impl(path, 0);
}

struct ResolvedCommandSegment {
  std::optional<std::size_t> command_index;
  std::vector<std::string> recursive_payloads;
};

[[nodiscard]] bool is_option_token(std::string_view token) {
  return token.size() > 1 && token.front() == '-';
}

[[nodiscard]] bool is_signed_numeric_token(std::string_view token) {
  if(token.size() < 2 || (token.front() != '-' && token.front() != '+')) {
    return false;
  }

  return std::all_of(token.begin() + 1, token.end(), [](char ch) {
    return std::isdigit(static_cast<unsigned char>(ch)) != 0;
  });
}

[[nodiscard]] bool is_timeout_duration_token(std::string_view token) {
  if(token.empty()) {
    return false;
  }

  std::size_t index = 0;
  bool has_digit = false;
  bool has_decimal = false;

  while(index < token.size()) {
    const auto ch = token.at(index);
    if(std::isdigit(static_cast<unsigned char>(ch)) != 0) {
      has_digit = true;
      ++index;
      continue;
    }

    if(ch == '.' && !has_decimal) {
      has_decimal = true;
      ++index;
      continue;
    }

    break;
  }

  if(!has_digit) {
    return false;
  }
  if(index == token.size()) {
    return true;
  }
  if(index + 1 == token.size()) {
    const auto suffix = token.at(index);
    return suffix == 's' || suffix == 'm' || suffix == 'h' || suffix == 'd';
  }

  return false;
}

struct EnvWrapperResolution {
  std::size_t next_index{0};
  std::vector<std::string> split_payloads;
};

[[nodiscard]] bool env_split_payload_uses_unmodeled_expansion(std::string_view payload) {
  return payload.find("\\_") != std::string_view::npos || payload.find('$') != std::string_view::npos;
}

[[nodiscard]] bool env_split_payload_is_bare_rm_on_critical_path(std::string_view payload) {
  const auto lower_payload = ava::core::lowercase_ascii(payload);
  const auto payload_tokens = tokenize_shell_command(lower_payload);
  if(payload_tokens.empty() || !command_token_matches_binary(payload_tokens.front(), "rm")) {
    return false;
  }

  std::vector<std::string_view> rm_tokens;
  rm_tokens.reserve(payload_tokens.size());
  for(const auto& token : payload_tokens) {
    rm_tokens.push_back(token);
  }

  const auto flags = parse_rm_flags(rm_tokens);
  if(flags.has_recursive || flags.has_force) {
    return false;
  }

  for(const auto& candidate : extract_rm_path_candidates(rm_tokens)) {
    if(rm_path_is_critical(candidate)) {
      return true;
    }
  }

  return false;
}

[[nodiscard]] bool is_shell_like_binary(std::string_view binary);

[[nodiscard]] std::string env_split_payload_for_recursive_classification(std::string payload) {
  if(env_split_payload_uses_unmodeled_expansion(payload)) {
    return "rm -rf /";
  }
  const auto payload_tokens = tokenize_shell_command(ava::core::lowercase_ascii(payload));
  if(!payload_tokens.empty() && (payload_tokens.front() == "--" || payload_tokens.front() == "-" ||
                                 is_option_token(payload_tokens.front()) ||
                                 is_env_assignment_token(payload_tokens.front()))) {
    return "rm -rf /";
  }
  if(!payload_tokens.empty() && is_shell_like_binary(token_binary_name(payload_tokens.front())) &&
     std::find(payload_tokens.begin() + 1, payload_tokens.end(), "-c") != payload_tokens.end()) {
    return "rm -rf /";
  }
  if(env_split_payload_is_bare_rm_on_critical_path(payload)) {
    return "rm -rf /";
  }
  return payload;
}

[[nodiscard]] bool is_shell_like_binary(std::string_view binary) {
  static constexpr std::array<std::string_view, 9> kShellBinaries = {
      "sh", "bash", "zsh", "dash", "fish", "ksh", "csh", "tcsh", "ash",
  };
  return std::find(kShellBinaries.begin(), kShellBinaries.end(), binary) != kShellBinaries.end();
}

[[nodiscard]] EnvWrapperResolution skip_env_wrapper(const std::vector<std::string>& tokens, std::size_t index, std::size_t segment_end) {
  std::size_t cursor = index + 1;
  std::vector<std::string> split_payloads;

  while(cursor < segment_end) {
    const auto token = shell_dequote_token(tokens.at(cursor));
    const auto normalized_token = ava::core::lowercase_ascii(token);
    if(token == "--") {
      ++cursor;
      break;
    }

    if(token == "-") {
      ++cursor;
      continue;
    }

    if(token.find('$') != std::string::npos) {
      split_payloads.push_back("rm -rf /");
      ++cursor;
      break;
    }

    if(is_env_assignment_token(token)) {
      ++cursor;
      continue;
    }

    if(!is_option_token(token)) {
      break;
    }

    if(token_matches_long_option_abbreviation(normalized_token, "--split-string", "--s")) {
      const auto separator = token.find('=');
      if(separator != std::string::npos) {
        split_payloads.push_back(token.substr(separator + 1));
      } else {
        ++cursor;
        if(cursor < segment_end) {
          split_payloads.push_back(shell_dequote_token(tokens.at(cursor)));
        }
      }
      ++cursor;
      break;
    }

    if(token == "-S") {
      ++cursor;
      if(cursor < segment_end) {
        split_payloads.push_back(shell_dequote_token(tokens.at(cursor)));
        ++cursor;
      }
      break;
    }

    if((token.starts_with("-u") || token.starts_with("-C")) && token.size() > 2) {
      ++cursor;
      continue;
    }

    if(normalized_token.size() > 2 && normalized_token.starts_with('-') && !normalized_token.starts_with("--")) {
      const auto split_pos = token.find('S', 1);
      if(split_pos != std::string::npos) {
        if(split_pos + 1 < token.size()) {
          split_payloads.push_back(token.substr(split_pos + 1));
          ++cursor;
        } else {
          ++cursor;
          if(cursor < segment_end) {
            split_payloads.push_back(shell_dequote_token(tokens.at(cursor)));
            ++cursor;
          }
        }
        break;
      }
    }

    const auto is_unset_option = token_matches_long_option_abbreviation(normalized_token, "--unset", "--un");
    const auto is_chdir_option = token_matches_long_option_abbreviation(normalized_token, "--chdir", "--ch");
    const auto is_argv0_option = token_matches_long_option_abbreviation(normalized_token, "--argv0", "--ar");
    const auto is_signal_option = token_matches_long_option_abbreviation(normalized_token, "--default-signal", "--default-s") ||
                                  token_matches_long_option_abbreviation(normalized_token, "--ignore-signal", "--ignore-s") ||
                                  token_matches_long_option_abbreviation(normalized_token, "--block-signal", "--block-s");
    if(normalized_token == "-u" || normalized_token == "-c" || is_unset_option || is_chdir_option || is_argv0_option ||
       is_signal_option) {
      const auto has_inline_value = normalized_token.find('=') != std::string::npos;
      ++cursor;
      if(!is_signal_option && !has_inline_value && cursor < segment_end) {
        ++cursor;
      }
      continue;
    }

    ++cursor;
  }

  while(cursor < segment_end && is_env_assignment_token(shell_dequote_token(tokens.at(cursor)))) {
    ++cursor;
  }

  if(!split_payloads.empty() && cursor < segment_end) {
    std::string remaining;
    for(std::size_t token_index = cursor; token_index < segment_end; ++token_index) {
      if(!remaining.empty()) {
        remaining.push_back(' ');
      }
      remaining += shell_dequote_token(tokens.at(token_index));
    }
    for(auto& payload : split_payloads) {
      if(!payload.empty() && !remaining.empty()) {
        payload.push_back(' ');
      }
      payload += remaining;
    }
  }

  for(auto& payload : split_payloads) {
    payload = env_split_payload_for_recursive_classification(std::move(payload));
  }

  return EnvWrapperResolution{.next_index = cursor, .split_payloads = std::move(split_payloads)};
}

[[nodiscard]] std::size_t skip_command_wrapper(const std::vector<std::string>& tokens, std::size_t index, std::size_t segment_end) {
  std::size_t cursor = index + 1;
  while(cursor < segment_end) {
    const auto token = shell_dequote_token(tokens.at(cursor));
    if(token == "--") {
      ++cursor;
      break;
    }
    if(token == "-p" || token == "-v" || token == "-V" || is_option_token(token)) {
      ++cursor;
      continue;
    }
    break;
  }
  return cursor;
}

[[nodiscard]] std::size_t skip_exec_wrapper(const std::vector<std::string>& tokens, std::size_t index, std::size_t segment_end) {
  std::size_t cursor = index + 1;
  while(cursor < segment_end) {
    const auto token = shell_dequote_token(tokens.at(cursor));
    if(token == "--") {
      ++cursor;
      break;
    }
    if(token == "-a") {
      ++cursor;
      if(cursor < segment_end) {
        ++cursor;
      }
      continue;
    }
    if(token == "-c" || token == "-l" || is_option_token(token)) {
      ++cursor;
      continue;
    }
    break;
  }
  return cursor;
}

[[nodiscard]] std::size_t skip_time_wrapper(const std::vector<std::string>& tokens, std::size_t index, std::size_t segment_end) {
  std::size_t cursor = index + 1;
  while(cursor < segment_end) {
    const auto token = shell_dequote_token(tokens.at(cursor));
    if(token == "--") {
      ++cursor;
      break;
    }

    if(token == "-f" || token == "--format" || token == "-o" || token == "--output") {
      ++cursor;
      if(cursor < segment_end) {
        ++cursor;
      }
      continue;
    }

    if((token.starts_with("-f") || token.starts_with("-o")) && token.size() > 2) {
      ++cursor;
      continue;
    }

    if(token.starts_with("--format=") || token.starts_with("--output=")) {
      ++cursor;
      continue;
    }

    if(is_option_token(token)) {
      ++cursor;
      continue;
    }
    break;
  }
  return cursor;
}

[[nodiscard]] std::size_t skip_nice_wrapper(const std::vector<std::string>& tokens, std::size_t index, std::size_t segment_end) {
  std::size_t cursor = index + 1;
  while(cursor < segment_end) {
    const auto token = shell_dequote_token(tokens.at(cursor));
    if(token == "--") {
      ++cursor;
      break;
    }
    if(token == "-n" || token == "--adjustment") {
      ++cursor;
      if(cursor < segment_end) {
        ++cursor;
      }
      continue;
    }
    if(token.starts_with("-n") || token.starts_with("--adjustment=") || is_signed_numeric_token(token) || is_option_token(token)) {
      ++cursor;
      continue;
    }
    break;
  }
  return cursor;
}

[[nodiscard]] std::size_t skip_timeout_wrapper(const std::vector<std::string>& tokens, std::size_t index, std::size_t segment_end) {
  std::size_t cursor = index + 1;
  while(cursor < segment_end) {
    const auto token = shell_dequote_token(tokens.at(cursor));
    if(token == "--") {
      ++cursor;
      break;
    }
    if(!is_option_token(token)) {
      break;
    }

    if(token == "-k" || token == "--kill-after" || token == "-s" || token == "--signal") {
      ++cursor;
      if(cursor < segment_end) {
        ++cursor;
      }
      continue;
    }

    if((token.starts_with("-k") || token.starts_with("-s")) && token.size() > 2) {
      ++cursor;
      continue;
    }

    if(token.starts_with("--kill-after=") || token.starts_with("--signal=")) {
      ++cursor;
      continue;
    }

    ++cursor;
  }

  if(cursor < segment_end && is_timeout_duration_token(shell_dequote_token(tokens.at(cursor)))) {
    ++cursor;  // duration
  }
  if(cursor < segment_end && shell_dequote_token(tokens.at(cursor)) == "--") {
    ++cursor;
  }

  return cursor;
}

[[nodiscard]] std::size_t skip_nohup_wrapper(const std::vector<std::string>& tokens, std::size_t index, std::size_t segment_end) {
  std::size_t cursor = index + 1;
  while(cursor < segment_end) {
    const auto token = shell_dequote_token(tokens.at(cursor));
    if(token == "--") {
      ++cursor;
      break;
    }
    if(is_option_token(token)) {
      ++cursor;
      continue;
    }
    break;
  }
  return cursor;
}

[[nodiscard]] std::size_t skip_setsid_wrapper(const std::vector<std::string>& tokens, std::size_t index, std::size_t segment_end) {
  std::size_t cursor = index + 1;
  while(cursor < segment_end) {
    const auto token = shell_dequote_token(tokens.at(cursor));
    if(token == "--") {
      ++cursor;
      break;
    }
    if(is_option_token(token)) {
      ++cursor;
      continue;
    }
    break;
  }
  return cursor;
}

[[nodiscard]] bool is_shell_option_bundle_with_c(std::string_view option) {
  static constexpr std::string_view kAllowedShellOptionLetters = "abefhilmnprstuvxzc";
  if(option.size() <= 1 || option.front() != '-' || option.at(1) == '-') {
    return false;
  }

  bool has_c = false;
  for(std::size_t index = 1; index < option.size(); ++index) {
    const auto ch = option.at(index);
    if(kAllowedShellOptionLetters.find(ch) == std::string_view::npos) {
      return false;
    }
    if(ch == 'c') {
      has_c = true;
    }
  }
  return has_c;
}

[[nodiscard]] bool is_shell_body_reserved_word(std::string_view token) {
  const auto word = shell_dequote_token(token);
  return word == "then" || word == "do" || word == "else" || word == "elif" || word == "if" || word == "while" ||
         word == "until" || word == "for" || word == "select";
}

[[nodiscard]] bool shell_payload_contains_positional_expansion(std::string_view payload) {
  if(payload.find("$@") != std::string_view::npos || payload.find("$*") != std::string_view::npos ||
     payload.find("${@}") != std::string_view::npos || payload.find("${*}") != std::string_view::npos) {
    return true;
  }
  for(std::size_t index = 0; index + 1 < payload.size(); ++index) {
    if(payload[index] == '$' && payload[index + 1] >= '0' && payload[index + 1] <= '9') {
      return true;
    }
    if(index + 2 < payload.size() && payload[index] == '$' && payload[index + 1] == '{' &&
       ((payload[index + 2] >= '0' && payload[index + 2] <= '9') || payload[index + 2] == '@' ||
        payload[index + 2] == '*')) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool shell_c_payload_should_fail_closed(std::string_view payload) {
  if(!shell_payload_contains_positional_expansion(payload)) {
    return false;
  }
  const auto lower_payload = ava::core::lowercase_ascii(payload);
  const auto payload_tokens = tokenize_shell_command(lower_payload);
  if(!payload_tokens.empty() && payload_tokens.front().starts_with('$')) {
    return true;
  }
  for(auto token = payload_tokens.begin(); token != payload_tokens.end(); ++token) {
    if(token->starts_with('$')) {
      const std::vector<std::string> expansion_command_tokens(token, payload_tokens.end());
      std::vector<std::string_view> expansion_command_token_views;
      expansion_command_token_views.reserve(expansion_command_tokens.size());
      for(const auto& expansion_command_token : expansion_command_tokens) {
        expansion_command_token_views.emplace_back(expansion_command_token);
      }
      const auto flags = parse_rm_flags(expansion_command_token_views);
      if(flags.has_recursive && flags.has_force) {
        return true;
      }
    }
  }
  const auto command = std::find_if(payload_tokens.begin(), payload_tokens.end(), [](const auto& token) {
    return token_binary_name(token) == "rm";
  });
  if(command == payload_tokens.end()) {
    return false;
  }
  std::vector<std::string_view> rm_tokens;
  rm_tokens.reserve(static_cast<std::size_t>(std::distance(command, payload_tokens.end())));
  for(auto iter = command; iter != payload_tokens.end(); ++iter) {
    rm_tokens.emplace_back(*iter);
  }
  const auto flags = parse_rm_flags(rm_tokens);
  return flags.has_recursive && flags.has_force;
}

[[nodiscard]] std::optional<std::string> extract_shell_c_payload(
    const std::vector<std::string>& tokens,
    std::size_t shell_index,
    std::size_t segment_end
) {
  for(std::size_t cursor = shell_index + 1; cursor < segment_end; ++cursor) {
    const auto token = shell_dequote_token(tokens.at(cursor));
    if(token == "--") {
      continue;
    }

    if(token.starts_with("-c") && token.size() > 2) {
      const auto payload = shell_dequote_token(token.substr(2));
      return shell_c_payload_should_fail_closed(payload) ? std::optional<std::string>{"rm -rf /"}
                                                        : std::optional<std::string>{payload};
    }

    if(token == "-c" || token == "--command" || is_shell_option_bundle_with_c(token)) {
      if(cursor + 1 < segment_end) {
        const auto payload = shell_dequote_token(tokens.at(cursor + 1));
        return shell_c_payload_should_fail_closed(payload) ? std::optional<std::string>{"rm -rf /"}
                                                          : std::optional<std::string>{payload};
      }
      return std::nullopt;
    }
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<ResolvedCommandSegment> resolve_segment_command(
    const std::vector<std::string>& tokens,
    std::size_t segment_start,
    std::size_t segment_end
) {
  std::size_t command_index = segment_start;
  std::vector<std::string> recursive_payloads;

  constexpr std::size_t kMaxUnwrapSteps = 16;
  for(std::size_t unwrap_index = 0; unwrap_index < kMaxUnwrapSteps && command_index < segment_end; ++unwrap_index) {
    while(command_index < segment_end && is_env_assignment_token(shell_dequote_token(tokens.at(command_index)))) {
      ++command_index;
    }
    while(command_index < segment_end && is_shell_body_reserved_word(tokens.at(command_index))) {
      ++command_index;
    }
    if(command_index >= segment_end) {
      return std::nullopt;
    }

    const auto binary = token_binary_name(tokens.at(command_index));
    std::size_t next_index = command_index;

    if(binary == "env") {
      auto env_resolution = skip_env_wrapper(tokens, command_index, segment_end);
      next_index = env_resolution.next_index;
      for(auto& payload : env_resolution.split_payloads) {
        if(!payload.empty()) {
          recursive_payloads.push_back(std::move(payload));
        }
      }
    } else if(binary == "command") {
      next_index = skip_command_wrapper(tokens, command_index, segment_end);
    } else if(binary == "exec") {
      next_index = skip_exec_wrapper(tokens, command_index, segment_end);
    } else if(binary == "time") {
      next_index = skip_time_wrapper(tokens, command_index, segment_end);
    } else if(binary == "nice") {
      next_index = skip_nice_wrapper(tokens, command_index, segment_end);
    } else if(binary == "timeout") {
      next_index = skip_timeout_wrapper(tokens, command_index, segment_end);
    } else if(binary == "nohup") {
      next_index = skip_nohup_wrapper(tokens, command_index, segment_end);
    } else if(binary == "setsid") {
      next_index = skip_setsid_wrapper(tokens, command_index, segment_end);
    } else if(is_shell_like_binary(binary)) {
      if(const auto shell_payload = extract_shell_c_payload(tokens, command_index, segment_end); shell_payload.has_value()) {
        recursive_payloads.push_back(*shell_payload);
      }
      return ResolvedCommandSegment{
          .command_index = command_index,
          .recursive_payloads = std::move(recursive_payloads),
      };
    } else {
      return ResolvedCommandSegment{
          .command_index = command_index,
          .recursive_payloads = std::move(recursive_payloads),
      };
    }

    if(next_index <= command_index) {
      break;
    }
    command_index = next_index;
  }

  if(command_index < segment_end) {
    return ResolvedCommandSegment{
        .command_index = command_index,
        .recursive_payloads = std::move(recursive_payloads),
    };
  }

  if(!recursive_payloads.empty()) {
    return ResolvedCommandSegment{.command_index = std::nullopt, .recursive_payloads = std::move(recursive_payloads)};
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<std::string> classify_rm_critical_path_impl(const std::string& lower_command, std::size_t depth) {
  const auto tokens = tokenize_shell_command(lower_command);

  std::size_t index = 0;
  while(index < tokens.size()) {
    while(index < tokens.size() && is_command_separator_token(tokens.at(index))) {
      ++index;
    }
    if(index >= tokens.size()) {
      break;
    }

    const auto segment_end = find_command_segment_end(tokens, index);
    if(const auto resolved = resolve_segment_command(tokens, index, segment_end); resolved.has_value()) {
      if(depth < kMaxShellNestingDepth) {
        for(const auto& payload : resolved->recursive_payloads) {
          const auto nested_command = ava::core::lowercase_ascii(payload);
          if(const auto nested_reason = classify_rm_critical_path_impl(nested_command, depth + 1); nested_reason.has_value()) {
            return nested_reason;
          }
        }
      }

      if(resolved->command_index.has_value()) {
        std::vector<std::string_view> rm_tokens;
        rm_tokens.reserve(segment_end - *resolved->command_index);
        for(std::size_t token_index = *resolved->command_index; token_index < segment_end; ++token_index) {
          rm_tokens.push_back(tokens.at(token_index));
        }

        const auto command_token = shell_dequote_token(tokens.at(*resolved->command_index));
        const auto command_is_rm = command_token_matches_binary(tokens.at(*resolved->command_index), "rm");
        const auto command_is_unmodeled_expansion = command_token.find('$') != std::string::npos;
        if((command_is_rm || command_is_unmodeled_expansion) && rm_has_recursive_force_flags(rm_tokens)) {
          if(rm_has_unmodeled_expansion_path_operand(rm_tokens)) {
            return "rm -rf on critical path: shell-expanded operand";
          }
          for(const auto& candidate : extract_rm_path_candidates(rm_tokens)) {
            if(rm_path_is_critical(candidate)) {
              return "rm -rf on critical path: " + normalize_rm_path(candidate);
            }
          }
        }
      }
    }

    index = segment_end < tokens.size() ? segment_end + 1 : segment_end;
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<std::string> classify_rm_critical_path(const std::string& lower_command) {
  return classify_rm_critical_path_impl(lower_command, 0);
}

[[nodiscard]] bool command_contains_recursive_force_rm_impl(const std::string& lower_command, std::size_t depth) {
  const auto tokens = tokenize_shell_command(lower_command);

  std::size_t index = 0;
  while(index < tokens.size()) {
    while(index < tokens.size() && is_command_separator_token(tokens.at(index))) {
      ++index;
    }
    if(index >= tokens.size()) {
      break;
    }

    const auto segment_end = find_command_segment_end(tokens, index);
    if(const auto resolved = resolve_segment_command(tokens, index, segment_end); resolved.has_value()) {
      if(depth < kMaxShellNestingDepth) {
        for(const auto& payload : resolved->recursive_payloads) {
          const auto nested_command = ava::core::lowercase_ascii(payload);
          if(command_contains_recursive_force_rm_impl(nested_command, depth + 1)) {
            return true;
          }
        }
      }

      if(resolved->command_index.has_value() && command_token_matches_binary(tokens.at(*resolved->command_index), "rm")) {
        std::vector<std::string_view> rm_tokens;
        rm_tokens.reserve(segment_end - *resolved->command_index);
        for(std::size_t token_index = *resolved->command_index; token_index < segment_end; ++token_index) {
          rm_tokens.push_back(tokens.at(token_index));
        }

        if(rm_has_recursive_force_flags(rm_tokens)) {
          return true;
        }
      }
    }

    index = segment_end < tokens.size() ? segment_end + 1 : segment_end;
  }

  return false;
}

[[nodiscard]] bool token_is_exec_terminator(std::string_view token) {
  const auto dequoted = shell_dequote_token(token);
  return dequoted == ";" || dequoted == "\\;" || dequoted == "+";
}

[[nodiscard]] std::string trim_exec_token_copy(std::string_view token) {
  auto trimmed = shell_dequote_token(token);
  while(!trimmed.empty() && (trimmed.back() == ';' || trimmed.back() == '+')) {
    trimmed.pop_back();
  }
  return trimmed;
}

[[nodiscard]] std::optional<std::string> classify_find_semantic_delete_impl(const std::string& lower_command, std::size_t depth) {
  const auto tokens = tokenize_shell_command(lower_command);

  std::size_t index = 0;
  while(index < tokens.size()) {
    while(index < tokens.size() && is_command_separator_token(tokens.at(index))) {
      ++index;
    }
    if(index >= tokens.size()) {
      break;
    }

    const auto segment_end = find_command_segment_end(tokens, index);
    if(const auto resolved = resolve_segment_command(tokens, index, segment_end); resolved.has_value()) {
      if(depth < kMaxShellNestingDepth) {
        for(const auto& payload : resolved->recursive_payloads) {
          const auto nested_command = ava::core::lowercase_ascii(payload);
          if(const auto nested_reason = classify_find_semantic_delete_impl(nested_command, depth + 1); nested_reason.has_value()) {
            return nested_reason;
          }
        }
      }

      if(resolved->command_index.has_value() && command_token_matches_binary(tokens.at(*resolved->command_index), "find")) {
        for(std::size_t token_index = *resolved->command_index + 1; token_index < segment_end; ++token_index) {
          const auto trimmed_token_string = trim_exec_token_copy(tokens.at(token_index));
          const auto trimmed_token = std::string_view(trimmed_token_string);
          if(trimmed_token == "-delete") {
            return "find -delete can recursively delete files";
          }

          if(trimmed_token != "-exec" && trimmed_token != "-execdir") {
            continue;
          }

          std::size_t exec_index = token_index + 1;
          if(exec_index >= segment_end) {
            break;
          }

          std::vector<std::string> exec_tokens;
          for(; exec_index < segment_end; ++exec_index) {
            const auto exec_trimmed_string = trim_exec_token_copy(tokens.at(exec_index));
            const auto exec_trimmed = std::string_view(exec_trimmed_string);
            if(token_is_exec_terminator(tokens.at(exec_index)) || token_is_exec_terminator(exec_trimmed)) {
              break;
            }
            exec_tokens.push_back(tokens.at(exec_index));
          }

          if(!exec_tokens.empty()) {
            std::string exec_command;
            for(const auto& exec_token : exec_tokens) {
              if(!exec_command.empty()) {
                exec_command.push_back(' ');
              }
              exec_command += exec_token;
            }
            if(command_contains_recursive_force_rm_impl(ava::core::lowercase_ascii(exec_command), depth + 1)) {
              return "find -exec nested payload is equivalent to rm -rf";
            }

            if(const auto exec_resolved = resolve_segment_command(exec_tokens, 0, exec_tokens.size()); exec_resolved.has_value()) {
              if(depth < kMaxShellNestingDepth) {
                for(const auto& payload : exec_resolved->recursive_payloads) {
                  const auto nested_command = ava::core::lowercase_ascii(payload);
                  if(const auto nested_reason = classify_rm_critical_path_impl(nested_command, depth + 1); nested_reason.has_value()) {
                    return "find -exec nested shell payload is equivalent to rm -rf";
                  }
                  if(command_contains_recursive_force_rm_impl(nested_command, depth + 1)) {
                    return "find -exec nested shell payload is equivalent to rm -rf";
                  }
                }
              }

              if(exec_resolved->command_index.has_value() &&
                 command_token_matches_binary(exec_tokens.at(*exec_resolved->command_index), "rm")) {
                std::vector<std::string_view> rm_tokens;
                rm_tokens.reserve(exec_tokens.size() - *exec_resolved->command_index);
                for(std::size_t rm_index = *exec_resolved->command_index; rm_index < exec_tokens.size(); ++rm_index) {
                  rm_tokens.push_back(exec_tokens.at(rm_index));
                }

                if(rm_has_recursive_force_flags(rm_tokens)) {
                  return "find -exec rm -rf is equivalent to rm -rf";
                }
              }
            }
          }

          token_index = exec_index;
        }
      }
    }

    index = segment_end < tokens.size() ? segment_end + 1 : segment_end;
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<std::string> classify_find_semantic_delete(const std::string& lower_command) {
  return classify_find_semantic_delete_impl(lower_command, 0);
}

[[nodiscard]] bool brace_part_contains_dangerous_command(std::string_view part) {
  static constexpr std::array<std::string_view, 16> kDangerousBraceCommands = {
      "rm", "dd", "mkfs", "chmod", "chown", "sudo", "curl", "wget", "nc", "ncat", "bash", "sh", "zsh", "python", "perl", "ruby",
  };

  const auto binary = token_binary_name(part);
  return std::find(kDangerousBraceCommands.begin(), kDangerousBraceCommands.end(), std::string_view(binary)) !=
         kDangerousBraceCommands.end();
}

[[nodiscard]] std::optional<std::string> detect_dangerous_brace_expansion(const std::string& command) {
  std::size_t index = 0;
  while(index < command.size()) {
    const auto open = command.find('{', index);
    if(open == std::string::npos) {
      break;
    }
    const auto close = command.find('}', open + 1);
    if(close == std::string::npos) {
      break;
    }

    const auto inner = std::string_view(command).substr(open + 1, close - open - 1);
    if(inner.find(',') != std::string_view::npos) {
      std::size_t part_start = 0;
      while(part_start <= inner.size()) {
        const auto comma = inner.find(',', part_start);
        const auto part_end = comma == std::string_view::npos ? inner.size() : comma;
        const auto part = ava::core::trim_ascii_view(inner.substr(part_start, part_end - part_start));
        if(!part.empty()) {
          const auto lower_part = ava::core::lowercase_ascii(part);
          if(brace_part_contains_dangerous_command(lower_part)) {
            return "parser differential risk: brace expansion contains dangerous command {" + std::string(inner) + "}";
          }
        }

        if(comma == std::string_view::npos) {
          break;
        }
        part_start = comma + 1;
      }
    }

    index = close + 1;
  }

  return std::nullopt;
}

[[nodiscard]] bool contains_unicode_whitespace_utf8(std::string_view command) {
  return std::any_of(kUnicodeWhitespaceUtf8.begin(), kUnicodeWhitespaceUtf8.end(), [&](std::string_view value) {
    return command.find(value) != std::string_view::npos;
  });
}

[[nodiscard]] std::string replace_unicode_whitespace_with_ascii(std::string command) {
  for(const auto whitespace : kUnicodeWhitespaceUtf8) {
    std::size_t position = 0;
    while((position = command.find(whitespace, position)) != std::string::npos) {
      command.replace(position, whitespace.size(), " ");
      ++position;
    }
  }

  return command;
}

[[nodiscard]] std::string replace_ifs_expansions_with_space(std::string command) {
  std::string normalized;
  normalized.reserve(command.size());

  std::size_t index = 0;
  while(index < command.size()) {
    if(index + 5 <= command.size() && command.at(index) == '$' && command.at(index + 1) == '{' &&
       command.substr(index + 2, 3) == "ifs") {
      const auto close = command.find('}', index + 5);
      if(close != std::string::npos) {
        normalized.push_back(' ');
        index = close + 1;
        continue;
      }
    }

    if(index + 4 <= command.size() && command.substr(index, 4) == "$ifs") {
      const auto next_index = index + 4;
      if(next_index == command.size()) {
        normalized.push_back(' ');
        index = next_index;
        continue;
      }

      const auto next_char = static_cast<unsigned char>(command.at(next_index));
      if(std::isalnum(next_char) == 0 && command.at(next_index) != '_') {
        normalized.push_back(' ');
        index = next_index;
        continue;
      }
    }

    normalized.push_back(command.at(index));
    ++index;
  }

  return normalized;
}

[[nodiscard]] int hex_digit_value(char ch) {
  if(ch >= '0' && ch <= '9') {
    return ch - '0';
  }
  if(ch >= 'a' && ch <= 'f') {
    return 10 + (ch - 'a');
  }
  if(ch >= 'A' && ch <= 'F') {
    return 10 + (ch - 'A');
  }
  return -1;
}

[[nodiscard]] bool parse_hex_digits(std::string_view input, std::size_t start, std::size_t digits, unsigned int* value) {
  if(start + digits > input.size()) {
    return false;
  }

  unsigned int parsed = 0;
  for(std::size_t index = 0; index < digits; ++index) {
    const auto digit = hex_digit_value(input.at(start + index));
    if(digit < 0) {
      return false;
    }
    parsed = (parsed << 4U) + static_cast<unsigned int>(digit);
  }

  *value = parsed;
  return true;
}

[[nodiscard]] std::string decode_ansi_c_quoted_fragments(std::string_view command) {
  std::string decoded;
  decoded.reserve(command.size());

  std::size_t index = 0;
  while(index < command.size()) {
    if(index + 1 < command.size() && command.at(index) == '$' && command.at(index + 1) == '\'') {
      index += 2;
      while(index < command.size()) {
        const auto ch = command.at(index);
        if(ch == '\'') {
          ++index;
          break;
        }

        if(ch == '\\' && index + 1 < command.size()) {
          const auto escaped = command.at(index + 1);
          if((escaped == 'x' && index + 3 < command.size()) || (escaped == 'u' && index + 5 < command.size()) ||
             (escaped == 'U' && index + 9 < command.size())) {
            std::size_t digits = escaped == 'x' ? 2 : (escaped == 'u' ? 4 : 8);
            if(escaped == 'u' && index + 9 < command.size()) {
              bool eight_hex_digits = true;
              for(std::size_t digit_index = 0; digit_index < 8; ++digit_index) {
                if(hex_digit_value(command.at(index + 2 + digit_index)) < 0) {
                  eight_hex_digits = false;
                  break;
                }
              }
              if(eight_hex_digits && command.substr(index + 2, 4) == "0000") {
                digits = 8;
              }
            }
            unsigned int value = 0;
            if(parse_hex_digits(command, index + 2, digits, &value)) {
              if(value <= 0x7FU) {
                decoded.push_back(static_cast<char>(value));
              } else {
                decoded.push_back(' ');
              }
              index += 2 + digits;
              continue;
            }
          }

          if(escaped >= '0' && escaped <= '7') {
            unsigned int value = static_cast<unsigned int>(escaped - '0');
            std::size_t octal_digits = 1;
            while(octal_digits < 3 && index + 1 + octal_digits < command.size()) {
              const auto digit = command.at(index + 1 + octal_digits);
              if(digit < '0' || digit > '7') {
                break;
              }
              value = (value << 3U) + static_cast<unsigned int>(digit - '0');
              ++octal_digits;
            }

            if(value <= 0x7FU) {
              decoded.push_back(static_cast<char>(value));
            } else {
              decoded.push_back(' ');
            }

            index += 1 + octal_digits;
            continue;
          }

          switch(escaped) {
            case 'n':
            case 't':
            case 'r':
            case 'v':
            case 'f':
              decoded.push_back(' ');
              break;
            case '\\':
            case '\'':
            case '"':
              decoded.push_back(escaped);
              break;
            default:
              decoded.push_back(escaped);
              break;
          }

          index += 2;
          continue;
        }

        decoded.push_back(ch);
        ++index;
      }
      continue;
    }

    decoded.push_back(command.at(index));
    ++index;
  }

  return decoded;
}

[[nodiscard]] std::string materialize_simple_rm_brace_expansion(std::string command) {
  auto materialized = std::move(command);

  const auto is_ascii_word_char = [](char ch) {
    const auto as_unsigned = static_cast<unsigned char>(ch);
    return std::isalnum(as_unsigned) != 0 || ch == '_';
  };

  const auto replace_inline_pattern = [&](std::string_view pattern) {
    std::size_t position = 0;
    while((position = materialized.find(pattern, position)) != std::string::npos) {
      const auto end = position + pattern.size();
      const bool left_boundary = position == 0 || !is_ascii_word_char(materialized.at(position - 1));
      const bool right_boundary = end >= materialized.size() || !is_ascii_word_char(materialized.at(end));
      if(left_boundary && right_boundary) {
        materialized.replace(position, pattern.size(), "rm");
        position += 2;
      } else {
        ++position;
      }
    }
  };

  replace_inline_pattern("r{m,}");
  replace_inline_pattern("{r,}m");

  std::string normalized;
  normalized.reserve(materialized.size());

  std::size_t index = 0;
  while(index < materialized.size()) {
    const auto open = materialized.find('{', index);
    if(open == std::string::npos) {
      normalized.append(materialized.substr(index));
      break;
    }

    const auto close = materialized.find('}', open + 1);
    if(close == std::string::npos) {
      normalized.append(materialized.substr(index));
      break;
    }

    normalized.append(materialized.substr(index, open - index));

    const auto inner = std::string_view(materialized).substr(open + 1, close - open - 1);
    std::vector<std::string> parts;
    std::size_t part_start = 0;
    while(part_start <= inner.size()) {
      const auto comma = inner.find(',', part_start);
      const auto part_end = comma == std::string_view::npos ? inner.size() : comma;
      parts.push_back(std::string(ava::core::trim_ascii_view(inner.substr(part_start, part_end - part_start))));
      if(comma == std::string_view::npos) {
        break;
      }
      part_start = comma + 1;
    }

    bool materialized_rm = false;
    if(parts.size() >= 3 && command_token_matches_binary(parts.at(0), "rm")) {
      const bool has_target = std::any_of(parts.begin() + 1, parts.end(), [](const std::string& part) {
        return !part.empty() && part.front() != '-';
      });
      if(has_target) {
        normalized.append("rm");
        for(std::size_t part_index = 1; part_index < parts.size(); ++part_index) {
          if(parts.at(part_index).empty()) {
            continue;
          }
          normalized.push_back(' ');
          normalized.append(parts.at(part_index));
        }
        materialized_rm = true;
      }
    }

    if(!materialized_rm) {
      normalized.push_back('{');
      normalized.append(std::string(inner));
      normalized.push_back('}');
    }

    index = close + 1;
  }

  return normalized;
}

[[nodiscard]] std::string normalize_parser_differential_for_rm(std::string lower_command) {
  lower_command = decode_ansi_c_quoted_fragments(lower_command);
  lower_command = replace_ifs_expansions_with_space(lower_command);
  lower_command = replace_unicode_whitespace_with_ascii(lower_command);
  lower_command = materialize_simple_rm_brace_expansion(lower_command);
  return lower_command;
}

[[nodiscard]] std::optional<std::string> classify_parser_differential_rm_critical(const std::string& lower_command) {
  const auto normalized = normalize_parser_differential_for_rm(lower_command);
  if(normalized == lower_command) {
    return std::nullopt;
  }

  if(const auto rm_critical = classify_rm_critical_path(normalized); rm_critical.has_value()) {
    return "parser differential materializes " + *rm_critical;
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<CommandClassification> classify_parser_differential(const std::string& command) {
  const auto lower_command = ava::core::lowercase_ascii(command);

  if(const auto materialized_rm = classify_parser_differential_rm_critical(lower_command); materialized_rm.has_value()) {
    return CommandClassification{.risk_level = RiskLevel::Critical, .reason = *materialized_rm};
  }

  if(contains_any(lower_command, {"ifs=", "${ifs", "$ifs"})) {
    return CommandClassification{
        .risk_level = RiskLevel::High,
        .reason = "parser differential risk: IFS manipulation can alter shell word splitting",
    };
  }

  if(const auto brace_reason = detect_dangerous_brace_expansion(lower_command); brace_reason.has_value()) {
    return CommandClassification{.risk_level = RiskLevel::High, .reason = *brace_reason};
  }

  static const std::regex ansi_c_encoded_tokens(R"re(\$'[^']*\\([xXuU]|[0-7]))re");
  if(matches_regex(command, ansi_c_encoded_tokens)) {
    return CommandClassification{
        .risk_level = RiskLevel::High,
        .reason = "parser differential risk: ANSI-C quoting can encode hidden commands",
    };
  }

  if(contains_unicode_whitespace_utf8(command)) {
    return CommandClassification{
        .risk_level = RiskLevel::High,
        .reason = "parser differential risk: unicode whitespace can hide shell separators",
    };
  }

  return std::nullopt;
}

[[nodiscard]] bool has_shell_control_operator(const std::string& value) {
  return value.find_first_of(";&|\n\r`<>") != std::string::npos || value.find("&&") != std::string::npos ||
         value.find("||") != std::string::npos || value.find("$(") != std::string::npos;
}

[[nodiscard]] std::optional<std::size_t> find_matching_shell_delimiter(
    std::string_view command,
    std::size_t open_index,
    char open_char,
    char close_char
) {
  if(open_index >= command.size() || command.at(open_index) != open_char) {
    return std::nullopt;
  }

  auto mode = ShellQuoteMode::None;
  bool escaped = false;
  std::size_t depth = 1;

  for(std::size_t index = open_index + 1; index < command.size(); ++index) {
    const auto ch = command.at(index);

    if(mode == ShellQuoteMode::Single) {
      if(ch == '\'') {
        mode = ShellQuoteMode::None;
      }
      continue;
    }

    if(mode == ShellQuoteMode::Double) {
      if(escaped) {
        escaped = false;
        continue;
      }
      if(ch == '\\') {
        escaped = true;
        continue;
      }
      if(ch == '"') {
        mode = ShellQuoteMode::None;
      }
      continue;
    }

    if(escaped) {
      escaped = false;
      continue;
    }

    if(ch == '\\') {
      escaped = true;
      continue;
    }
    if(ch == '\'') {
      mode = ShellQuoteMode::Single;
      continue;
    }
    if(ch == '"') {
      mode = ShellQuoteMode::Double;
      continue;
    }

    if(ch == open_char) {
      ++depth;
      continue;
    }
    if(ch == close_char) {
      --depth;
      if(depth == 0) {
        return index;
      }
    }
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<std::size_t> find_matching_backtick(std::string_view command, std::size_t open_index) {
  if(open_index >= command.size() || command.at(open_index) != '`') {
    return std::nullopt;
  }

  bool escaped = false;
  for(std::size_t index = open_index + 1; index < command.size(); ++index) {
    const auto ch = command.at(index);
    if(escaped) {
      escaped = false;
      continue;
    }
    if(ch == '\\') {
      escaped = true;
      continue;
    }
    if(ch == '`') {
      return index;
    }
  }

  return std::nullopt;
}

[[nodiscard]] bool is_probable_subshell_start(std::string_view command, std::size_t open_index) {
  if(open_index > 0 && command.at(open_index - 1) == '$') {
    return false;
  }

  std::size_t cursor = open_index;
  while(cursor > 0 && std::isspace(static_cast<unsigned char>(command.at(cursor - 1))) != 0) {
    --cursor;
  }
  if(cursor == 0) {
    return true;
  }

  switch(command.at(cursor - 1)) {
    case ';':
    case '&':
    case '|':
    case '(':
    case '{':
    case '\n':
    case '\r':
      return true;
    default:
      return false;
  }
}

[[nodiscard]] bool is_probable_group_start(std::string_view command, std::size_t open_index) {
  if(open_index + 1 >= command.size() || std::isspace(static_cast<unsigned char>(command.at(open_index + 1))) == 0) {
    return false;
  }

  std::size_t cursor = open_index;
  while(cursor > 0 && std::isspace(static_cast<unsigned char>(command.at(cursor - 1))) != 0) {
    --cursor;
  }
  if(cursor == 0) {
    return true;
  }

  switch(command.at(cursor - 1)) {
    case ';':
    case '&':
    case '|':
    case '(':
    case '{':
    case '\n':
    case '\r':
      return true;
    default:
      return false;
  }
}

[[nodiscard]] std::vector<std::string> extract_nested_shell_payloads(std::string_view command) {
  std::vector<std::string> payloads;

  auto mode = ShellQuoteMode::None;
  bool escaped = false;

  for(std::size_t index = 0; index < command.size(); ++index) {
    const auto ch = command.at(index);

    if(mode == ShellQuoteMode::Single) {
      if(ch == '\'') {
        mode = ShellQuoteMode::None;
      }
      continue;
    }

    if(mode == ShellQuoteMode::Double) {
      if(escaped) {
        escaped = false;
        continue;
      }
      if(ch == '\\') {
        escaped = true;
        continue;
      }

      if(ch == '`') {
        if(const auto close = find_matching_backtick(command, index); close.has_value()) {
          const auto payload = std::string(ava::core::trim_ascii_view(command.substr(index + 1, *close - index - 1)));
          if(!payload.empty()) {
            payloads.push_back(payload);
          }
          index = *close;
        }
        continue;
      }

      if(ch == '$' && index + 1 < command.size() && command.at(index + 1) == '(') {
        if(const auto close = find_matching_shell_delimiter(command, index + 1, '(', ')'); close.has_value()) {
          const auto payload = std::string(ava::core::trim_ascii_view(command.substr(index + 2, *close - index - 2)));
          if(!payload.empty()) {
            payloads.push_back(payload);
          }
          index = *close;
        }
        continue;
      }

      if(ch == '"') {
        mode = ShellQuoteMode::None;
      }
      continue;
    }

    if(escaped) {
      escaped = false;
      continue;
    }

    if(ch == '\\') {
      escaped = true;
      continue;
    }
    if(ch == '\'') {
      mode = ShellQuoteMode::Single;
      continue;
    }
    if(ch == '"') {
      mode = ShellQuoteMode::Double;
      continue;
    }

    if(ch == '`') {
      if(const auto close = find_matching_backtick(command, index); close.has_value()) {
        const auto payload = std::string(ava::core::trim_ascii_view(command.substr(index + 1, *close - index - 1)));
        if(!payload.empty()) {
          payloads.push_back(payload);
        }
        index = *close;
      }
      continue;
    }

    if(ch == '$' && index + 1 < command.size() && command.at(index + 1) == '(') {
      if(const auto close = find_matching_shell_delimiter(command, index + 1, '(', ')'); close.has_value()) {
        const auto payload = std::string(ava::core::trim_ascii_view(command.substr(index + 2, *close - index - 2)));
        if(!payload.empty()) {
          payloads.push_back(payload);
        }
        index = *close;
      }
      continue;
    }

    if((ch == '<' || ch == '>') && index + 1 < command.size() && command.at(index + 1) == '(') {
      if(const auto close = find_matching_shell_delimiter(command, index + 1, '(', ')'); close.has_value()) {
        const auto payload = std::string(ava::core::trim_ascii_view(command.substr(index + 2, *close - index - 2)));
        if(!payload.empty()) {
          payloads.push_back(payload);
        }
        index = *close;
      }
      continue;
    }

    if(ch == '(' && is_probable_subshell_start(command, index)) {
      if(const auto close = find_matching_shell_delimiter(command, index, '(', ')'); close.has_value()) {
        const auto payload = std::string(ava::core::trim_ascii_view(command.substr(index + 1, *close - index - 1)));
        if(!payload.empty()) {
          payloads.push_back(payload);
        }
        index = *close;
      }
      continue;
    }

    if(ch == '{' && is_probable_group_start(command, index)) {
      if(const auto close = find_matching_shell_delimiter(command, index, '{', '}'); close.has_value()) {
        const auto payload = std::string(ava::core::trim_ascii_view(command.substr(index + 1, *close - index - 1)));
        if(!payload.empty()) {
          payloads.push_back(payload);
        }
        index = *close;
      }
    }
  }

  return payloads;
}

[[nodiscard]] std::optional<std::string> classify_critical_nested_payload(const std::string& command, std::size_t depth) {
  if(depth >= kMaxShellNestingDepth) {
    return std::nullopt;
  }

  const auto tokens = tokenize_shell_command(command);
  std::size_t index = 0;
  while(index < tokens.size()) {
    while(index < tokens.size() && is_command_separator_token(tokens.at(index))) {
      ++index;
    }
    if(index >= tokens.size()) {
      break;
    }

    const auto segment_end = find_command_segment_end(tokens, index);
    if(const auto resolved = resolve_segment_command(tokens, index, segment_end); resolved.has_value()) {
      for(const auto& payload : resolved->recursive_payloads) {
        const auto nested_classification = classify_bash_command_impl(payload, depth + 1);
        if(nested_classification.risk_level == RiskLevel::Critical) {
          return "nested shell payload is critical: " + nested_classification.reason;
        }
      }
    }

    index = segment_end < tokens.size() ? segment_end + 1 : segment_end;
  }

  for(const auto& payload : extract_nested_shell_payloads(command)) {
    const auto nested_classification = classify_bash_command_impl(payload, depth + 1);
    if(nested_classification.risk_level == RiskLevel::Critical) {
      return "nested shell payload is critical: " + nested_classification.reason;
    }
  }

  return std::nullopt;
}

[[nodiscard]] int risk_level_rank(RiskLevel level) {
  switch(level) {
    case RiskLevel::Safe:
      return 0;
    case RiskLevel::Low:
      return 1;
    case RiskLevel::Medium:
      return 2;
    case RiskLevel::High:
      return 3;
    case RiskLevel::Critical:
      return 4;
  }
  return 3;
}

[[nodiscard]] bool risk_level_is_higher(RiskLevel lhs, RiskLevel rhs) {
  return risk_level_rank(lhs) > risk_level_rank(rhs);
}

[[nodiscard]] std::optional<CommandClassification> classify_rm_noncritical_risk_impl(const std::string& lower_command, std::size_t depth) {
  const auto tokens = tokenize_shell_command(lower_command);
  const bool has_control_operator = has_shell_control_operator(lower_command);

  std::optional<CommandClassification> strongest_match;
  const auto update_strongest = [&](RiskLevel risk_level, std::string reason) {
    if(!strongest_match.has_value() || risk_level_is_higher(risk_level, strongest_match->risk_level)) {
      strongest_match = CommandClassification{.risk_level = risk_level, .reason = std::move(reason)};
    }
  };

  std::size_t index = 0;
  while(index < tokens.size()) {
    while(index < tokens.size() && is_command_separator_token(tokens.at(index))) {
      ++index;
    }
    if(index >= tokens.size()) {
      break;
    }

    const auto segment_end = find_command_segment_end(tokens, index);
    if(const auto resolved = resolve_segment_command(tokens, index, segment_end); resolved.has_value()) {
      if(depth < kMaxShellNestingDepth) {
        for(const auto& payload : resolved->recursive_payloads) {
          const auto nested_command = ava::core::lowercase_ascii(payload);
          if(const auto nested_match = classify_rm_noncritical_risk_impl(nested_command, depth + 1); nested_match.has_value()) {
            update_strongest(nested_match->risk_level, nested_match->reason);
          }
        }
      }

      if(resolved->command_index.has_value() && command_token_matches_binary(tokens.at(*resolved->command_index), "rm")) {
        std::vector<std::string_view> rm_tokens;
        rm_tokens.reserve(segment_end - *resolved->command_index);
        for(std::size_t token_index = *resolved->command_index; token_index < segment_end; ++token_index) {
          rm_tokens.push_back(tokens.at(token_index));
        }

        const auto flags = parse_rm_flags(rm_tokens);
        bool touches_critical_path = false;
        for(const auto& candidate : extract_rm_path_candidates(rm_tokens)) {
          if(rm_path_is_critical(candidate)) {
            touches_critical_path = true;
            break;
          }
        }

        if(flags.has_recursive && flags.has_force) {
          update_strongest(RiskLevel::High, "rm -rf can recursively delete files");
        } else if(flags.has_recursive || flags.has_force) {
          if(touches_critical_path || has_control_operator) {
            update_strongest(RiskLevel::High, "rm with recursive or force flags can delete sensitive files");
          } else {
            update_strongest(RiskLevel::Medium, "rm with recursive or force flags can remove files");
          }
        } else if(touches_critical_path) {
          update_strongest(RiskLevel::High, "rm targets a critical path");
        } else {
          update_strongest(RiskLevel::Medium, "rm deletes files or directories");
        }
      }
    }

    index = segment_end < tokens.size() ? segment_end + 1 : segment_end;
  }

  return strongest_match;
}

[[nodiscard]] std::optional<CommandClassification> classify_rm_noncritical_risk(const std::string& lower_command) {
  return classify_rm_noncritical_risk_impl(lower_command, 0);
}

[[nodiscard]] std::string strip_leading_redirection(std::string token) {
  std::size_t index = 0;
  while(index < token.size() && std::isdigit(static_cast<unsigned char>(token.at(index))) != 0) {
    ++index;
  }
  if(index < token.size() && (token.at(index) == '>' || token.at(index) == '<')) {
    while(index < token.size() && (token.at(index) == '>' || token.at(index) == '<')) {
      ++index;
    }
    token.erase(0, index);
  }
  return token;
}

[[nodiscard]] std::string normalize_sensitive_path_token(std::string_view raw_token) {
  auto token = shell_dequote_token(raw_token);
  token = strip_leading_redirection(token);
  token = shell_dequote_token(token);

  while(!token.empty()) {
    const auto first = token.front();
    if(first == '(' || first == ')' || first == '{' || first == '}' || first == '[' || first == ']' || first == ';' || first == ',' || first == '\'' || first == '"') {
      token.erase(token.begin());
      continue;
    }
    break;
  }

  while(!token.empty()) {
    const auto last = token.back();
    if(last == ')' || last == '(' || last == '{' || last == '}' || last == '[' || last == ']' || last == ';' || last == ',' || last == '\'' || last == '"') {
      token.pop_back();
      continue;
    }
    break;
  }

  return token;
}

[[nodiscard]] std::string path_basename(std::string_view path) {
  while(!path.empty() && path.back() == '/') {
    path.remove_suffix(1);
  }
  const auto slash = path.find_last_of('/');
  if(slash == std::string_view::npos) {
    return std::string(path);
  }
  return std::string(path.substr(slash + 1));
}

[[nodiscard]] bool path_matches_exact_or_suffix(const std::string& path, std::string_view suffix) {
  if(std::string_view(path) == suffix) {
    return true;
  }
  if(path.size() <= suffix.size()) {
    return false;
  }
  const auto offset = path.size() - suffix.size();
  return path.compare(offset, suffix.size(), suffix) == 0 && path.at(offset - 1) == '/';
}

[[nodiscard]] bool path_targets_ava_tools_surface(const std::string& path) {
  constexpr std::string_view kRelativePrefix = ".ava/tools/";
  constexpr std::string_view kAbsolutePrefix = "/.ava/tools/";
  constexpr std::string_view kAbsoluteDirectory = "/.ava/tools";
  const auto path_view = std::string_view(path);

  if(path_view == ".ava/tools" || (path_view.size() >= kRelativePrefix.size() && path_view.substr(0, kRelativePrefix.size()) == kRelativePrefix)) {
    return true;
  }

  if(path_view == kAbsoluteDirectory || path_matches_exact_or_suffix(path, ".ava/tools") ||
     path_view.find(kAbsolutePrefix) != std::string_view::npos) {
    return true;
  }

  return false;
}

[[nodiscard]] bool token_targets_sensitive_surface(std::string_view raw_token) {
  const auto token = normalize_sensitive_path_token(raw_token);
  if(token.empty()) {
    return false;
  }
  if(token_contains_shell_metacharacters(token)) {
    return false;
  }
  if(token.find("://") != std::string::npos) {
    return false;
  }

  if(path_matches_exact_or_suffix(token, ".ava/mcp.json")) {
    return true;
  }
  if(path_targets_ava_tools_surface(token)) {
    return true;
  }

  const auto basename = path_basename(token);
  return basename == "credentials.json" || basename == "trusted_projects.json" || basename == "permissions.toml";
}

[[nodiscard]] bool targets_sensitive_surface(const std::string& lower_command) {
  const auto tokens = tokenize_shell_command(lower_command);
  return std::any_of(tokens.begin(), tokens.end(), [](const std::string& token) {
    return token_targets_sensitive_surface(token);
  });
}

[[nodiscard]] bool is_known_low_risk_simple_command(const std::string& value) {
  if(has_shell_control_operator(value)) {
    return false;
  }

  static const std::regex low_command(R"re(^[[:space:]]*(pwd|ls([[:space:]]+\.)?)[[:space:]]*$)re");
  return matches_regex(value, low_command);
}

[[nodiscard]] bool pipes_download_to_shell(const std::string& lower_command) {
  if(lower_command.find("curl") == std::string::npos && lower_command.find("wget") == std::string::npos) {
    return false;
  }
  const auto tokens = tokenize_shell_command(lower_command);
  for(std::size_t index = 0; index < tokens.size(); ++index) {
    if(tokens.at(index) != "|") {
      continue;
    }
    const auto segment_start = index + 1;
    if(segment_start >= tokens.size()) {
      continue;
    }
    const auto segment_end = find_command_segment_end(tokens, segment_start);
    if(const auto resolved = resolve_segment_command(tokens, segment_start, segment_end); resolved.has_value() && resolved->command_index.has_value()) {
      if(is_shell_like_binary(token_binary_name(tokens.at(*resolved->command_index)))) {
        return true;
      }
    }
  }
  return false;
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

[[nodiscard]] CommandClassification classify_bash_command_impl(const std::string& command, std::size_t depth) {
  const auto lower = ava::core::lowercase_ascii(command);

  static const std::regex fork_bomb(R"re(:[[:space:]]*\([[:space:]]*\)[[:space:]]*\{[[:space:]]*:[[:space:]]*\|[[:space:]]*:)re");
  static const std::regex pipe_to_shell(R"re((curl|wget)[^|;]*\|[[:space:]]*([^[:space:]]*/)?(sh|bash|zsh|fish)\b)re");
  static const std::regex reverse_shell(R"re((/dev/tcp/|nc[[:space:]].*(-e|/bin/sh|/bin/bash)|bash[[:space:]]+-i|python[^;&|]*(socket|pty\.spawn)))re");
  const auto rm_critical_path = classify_rm_critical_path(lower);

  if(matches_regex(lower, fork_bomb)) {
    return CommandClassification{.risk_level = RiskLevel::Critical, .reason = "contains a fork bomb pattern"};
  }
  if(matches_regex(lower, pipe_to_shell) || pipes_download_to_shell(lower)) {
    return CommandClassification{.risk_level = RiskLevel::Critical, .reason = "pipes downloaded code into a shell"};
  }
  if(matches_regex(lower, reverse_shell)) {
    return CommandClassification{.risk_level = RiskLevel::Critical, .reason = "contains a reverse-shell pattern"};
  }

  if(rm_critical_path.has_value()) {
    return CommandClassification{.risk_level = RiskLevel::Critical, .reason = *rm_critical_path};
  }

  if(const auto find_semantic_delete = classify_find_semantic_delete(command); find_semantic_delete.has_value()) {
    return CommandClassification{.risk_level = RiskLevel::Critical, .reason = *find_semantic_delete};
  }

  static const std::regex privileged_or_tampering(
      R"re((^|[;&|[:space:]])(sudo|doas|pkexec|mkfs[^[:space:]]*|crontab)([[:space:]]|$)|(^|[;&|[:space:]])dd[[:space:]][^;&|]*(if=|of=)|>[[:space:]]*/dev/|chmod[[:space:]]+0?777|chown[[:space:]]+-r|/etc/sudoers|ssh-key|authorized_keys)re"
  );
  if(matches_regex(lower, privileged_or_tampering)) {
    return CommandClassification{.risk_level = RiskLevel::Critical, .reason = "uses privileged or system-tampering operations"};
  }
  if(targets_sensitive_surface(lower)) {
    return CommandClassification{.risk_level = RiskLevel::Critical, .reason = "modifies AVA trust or credential surfaces"};
  }

  if(const auto nested_critical = classify_critical_nested_payload(command, depth); nested_critical.has_value()) {
    return CommandClassification{.risk_level = RiskLevel::Critical, .reason = *nested_critical};
  }

  if(const auto parser_differential = classify_parser_differential(command); parser_differential.has_value()) {
    return *parser_differential;
  }

  if(const auto rm_noncritical = classify_rm_noncritical_risk(lower); rm_noncritical.has_value()) {
    return *rm_noncritical;
  }

  static const std::regex workspace_code_execution(
      R"re((^|[;&|[:space:]])(cargo[[:space:]]+(test|check|build)|pnpm[[:space:]]+(lint|typecheck|test))([;&|[:space:]]|$))re"
  );
  if(matches_regex(lower, workspace_code_execution)) {
    return CommandClassification{.risk_level = RiskLevel::High, .reason = "runs workspace-controlled build, test, or package scripts"};
  }

  static const std::regex shell_git_read(
      R"re((^|[;&|[:space:]])git[[:space:]]+(status|diff|log)([;&|[:space:]]|$))re"
  );
  if(matches_regex(lower, shell_git_read)) {
    return CommandClassification{.risk_level = RiskLevel::High, .reason = "uses shell git; prefer the read-only git tool"};
  }

  static const std::regex high_risk_command(
      R"re((^|[;&|[:space:]])(env|printenv|npm[[:space:]]+publish|pnpm[[:space:]]+publish|docker[[:space:]]+(rm|rmi)|kubectl[[:space:]]+delete|git[[:space:]]+(push|reset|checkout|clean)|curl|wget|scp|rsync)([[:space:]]|$)|/proc/self/environ|/proc/1/environ|cat[[:space:]]+(~/.|\$home))re"
  );
  if(matches_regex(lower, high_risk_command)) {
    return CommandClassification{.risk_level = RiskLevel::High, .reason = "performs destructive, network, credential, or environment-sensitive work"};
  }

  if(is_known_low_risk_simple_command(lower)) {
    return CommandClassification{.risk_level = RiskLevel::Low, .reason = "known read-only or verification command"};
  }

  return CommandClassification{.risk_level = RiskLevel::High, .reason = "unclassified shell command requires approval"};
}

CommandClassification classify_bash_command(const std::string& command) {
  return classify_bash_command_impl(command, 0);
}

}  // namespace ava::tools
