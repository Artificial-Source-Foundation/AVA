#include "ava/tools/core_tools.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "ava/tools/output_fallback.hpp"
#include "ava/tools/path_guard.hpp"
#include "shell_runner.hpp"

namespace ava::tools {

namespace {

constexpr std::size_t kReadDefaultLimit = 2000;
constexpr std::size_t kReadMaxLineLength = 2000;
constexpr std::size_t kReadOutputBytes = 48 * 1024;
constexpr std::uintmax_t kReadMaxFileBytes = 10 * 1024 * 1024;
constexpr std::size_t kGlobMaxResults = 1000;
constexpr std::size_t kGrepMaxMatches = 500;
constexpr std::size_t kBashOutputBytes = 48 * 1024;
constexpr std::size_t kEditCascadeMaxBytes = 2 * 1024 * 1024;
constexpr std::size_t kEditCascadeMaxOldTextBytes = 64 * 1024;
constexpr std::size_t kEditCascadeMaxLines = 20000;
constexpr std::uintmax_t kEditMaxFileBytes = 8 * 1024 * 1024;
constexpr std::size_t kEditReplaceAllMaxBytes = 8 * 1024 * 1024;
constexpr std::size_t kEditReplaceAllMaxOutputBytes = 4 * 1024 * 1024;
constexpr std::size_t kEditReplaceAllMaxReplacements = 100000;
[[nodiscard]] std::string read_file_text(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if(!file) {
    throw std::runtime_error("Failed to read file: " + path.string());
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

void write_file_text(const std::filesystem::path& path, const std::string& content) {
  if(path.has_parent_path()) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if(ec) {
      throw std::runtime_error("Failed to create parent directories for: " + path.string());
    }
  }

  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if(!out) {
    throw std::runtime_error("Failed to write file: " + path.string());
  }
  out << content;
  out.flush();
  if(!out) {
    throw std::runtime_error("Failed to write content to file: " + path.string());
  }
  out.close();
  if(!out) {
    throw std::runtime_error("Failed to close written file: " + path.string());
  }
}

void ensure_regular_file_size_within_limit(const std::filesystem::path& path, std::uintmax_t limit, const std::string_view operation) {
  std::error_code ec;
  if(!std::filesystem::is_regular_file(path, ec) || ec) {
    throw std::runtime_error(std::string(operation) + " target must be a regular file: " + path.string());
  }
  const auto size = std::filesystem::file_size(path, ec);
  if(ec) {
    throw std::runtime_error(std::string("Failed to stat ") + std::string(operation) + " target: " + path.string());
  }
  if(size > limit) {
    throw std::runtime_error(std::string(operation) + " target is too large: " + path.string());
  }
}

[[nodiscard]] std::vector<std::string> split_lines(const std::string& content) {
  std::vector<std::string> lines;
  std::stringstream ss(content);
  std::string line;
  while(std::getline(ss, line)) {
    if(line.size() > kReadMaxLineLength) {
      line.resize(kReadMaxLineLength);
    }
    lines.push_back(line);
  }
  return lines;
}

[[nodiscard]] std::regex glob_to_regex(const std::string& glob_pattern) {
  std::string regex = "^";
  for(std::size_t i = 0; i < glob_pattern.size(); ++i) {
    const char c = glob_pattern[i];
    if(c == '*') {
      const bool doublestar = (i + 1 < glob_pattern.size() && glob_pattern[i + 1] == '*');
      if(doublestar) {
        const bool slash_after = (i + 2 < glob_pattern.size() && (glob_pattern[i + 2] == '/' || glob_pattern[i + 2] == '\\'));
        if(slash_after) {
          regex += "(?:.*/)?";
          i += 2;
        } else {
          regex += ".*";
          ++i;
        }
      } else {
        regex += "[^/]*";
      }
    } else if(c == '?') {
      regex += ".";
    } else if(c == '.' || c == '+' || c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}' ||
              c == '^' || c == '$' || c == '|') {
      regex.push_back('\\');
      regex.push_back(c);
    } else if(c == '\\') {
      regex += "/";
    } else {
      regex.push_back(c);
    }
  }
  regex += "$";
  return std::regex(regex, std::regex::ECMAScript);
}

[[nodiscard]] std::string lowercase(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

struct SingleEditResult {
  std::string content;
  std::string strategy;
};

[[nodiscard]] std::string_view trim_whitespace(std::string_view value) {
  std::size_t start = 0;
  while(start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
    ++start;
  }

  std::size_t end = value.size();
  while(end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
    --end;
  }

  return value.substr(start, end - start);
}

[[nodiscard]] std::vector<std::string> split_text_lines_for_edit(const std::string& content) {
  std::vector<std::string> lines;
  std::stringstream ss(content);
  std::string line;
  while(std::getline(ss, line)) {
    if(!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    lines.push_back(line);
  }
  return lines;
}

[[nodiscard]] bool had_trailing_newline(const std::string& content) {
  return !content.empty() && content.ends_with('\n');
}

[[nodiscard]] std::string line_ending_for_content(const std::string& content) {
  return content.find("\r\n") != std::string::npos ? "\r\n" : "\n";
}

[[nodiscard]] std::string normalize_line_endings_for_content(const std::string& value, const std::string& content) {
  if(content.find("\r\n") == std::string::npos) {
    return value;
  }

  std::string normalized;
  normalized.reserve(value.size() + 8);
  for(std::size_t index = 0; index < value.size(); ++index) {
    if(value[index] == '\n' && (index == 0 || value[index - 1] != '\r')) {
      normalized += "\r\n";
    } else {
      normalized.push_back(value[index]);
    }
  }
  return normalized;
}

[[nodiscard]] std::string join_lines_for_edit(const std::vector<std::string>& lines, bool trailing_newline, const std::string& newline) {
  std::ostringstream out;
  for(std::size_t index = 0; index < lines.size(); ++index) {
    if(index > 0) {
      out << newline;
    }
    out << lines[index];
  }
  if(trailing_newline) {
    out << newline;
  }
  return out.str();
}

[[nodiscard]] std::string splice_lines_for_edit(
    const std::vector<std::string>& content_lines,
    std::size_t start,
    std::size_t end,
    const std::vector<std::string>& new_lines,
    bool trailing_newline,
    const std::string& newline
) {
  auto updated_lines = std::vector<std::string>();
  updated_lines.reserve(content_lines.size() + new_lines.size());
  updated_lines.insert(updated_lines.end(), content_lines.begin(), content_lines.begin() + static_cast<std::ptrdiff_t>(start));
  updated_lines.insert(updated_lines.end(), new_lines.begin(), new_lines.end());
  updated_lines.insert(updated_lines.end(), content_lines.begin() + static_cast<std::ptrdiff_t>(end), content_lines.end());
  return join_lines_for_edit(updated_lines, trailing_newline, newline);
}

[[nodiscard]] std::optional<SingleEditResult> replace_first_occurrence(
    const std::string& content,
    const std::string& old_text,
    const std::string& new_text,
    std::string strategy
) {
  auto match_text = old_text;
  auto replacement = new_text;
  auto start = content.find(match_text);
  if(start == std::string::npos) {
    match_text = normalize_line_endings_for_content(old_text, content);
    replacement = normalize_line_endings_for_content(new_text, content);
    start = content.find(match_text);
  }
  if(start == std::string::npos) {
    return std::nullopt;
  }

  auto updated = content;
  updated.replace(start, match_text.size(), replacement);
  return SingleEditResult{.content = std::move(updated), .strategy = std::move(strategy)};
}

struct NormalizedQuoteText {
  std::string text;
  std::vector<std::size_t> original_offsets;
};

[[nodiscard]] std::string normalize_quotes_for_match(const std::string& value);

[[nodiscard]] NormalizedQuoteText normalize_quotes_with_offsets(const std::string& value) {
  NormalizedQuoteText result;
  result.text.reserve(value.size());
  result.original_offsets.reserve(value.size());

  for(std::size_t index = 0; index < value.size();) {
    const auto remaining = std::string_view(value).substr(index);
    const auto append_normalized = [&](char ch) {
      result.text.push_back(ch);
      result.original_offsets.push_back(index);
      index += 3;
    };

    if(remaining.starts_with("\xE2\x80\x98") || remaining.starts_with("\xE2\x80\x99")) {
      append_normalized('\'');
      continue;
    }
    if(remaining.starts_with("\xE2\x80\x9C") || remaining.starts_with("\xE2\x80\x9D")) {
      append_normalized('"');
      continue;
    }

    result.text.push_back(value[index]);
    result.original_offsets.push_back(index);
    ++index;
  }

  return result;
}

[[nodiscard]] std::optional<SingleEditResult> quote_normalized_exact_match(
    const std::string& content,
    const std::string& old_text,
    const std::string& new_text
) {
  const auto normalized_content = normalize_quotes_with_offsets(content);
  const auto normalized_old_text = normalize_quotes_for_match(normalize_line_endings_for_content(old_text, content));

  const auto normalized_start = normalized_content.text.find(normalized_old_text);
  if(normalized_start == std::string::npos) {
    return std::nullopt;
  }

  const auto normalized_end = normalized_start + normalized_old_text.size();
  const auto original_start = normalized_content.original_offsets.at(normalized_start);
  const auto original_end = normalized_end < normalized_content.original_offsets.size()
                                ? normalized_content.original_offsets.at(normalized_end)
                                : content.size();
  auto updated = content;
  updated.replace(original_start, original_end - original_start, normalize_line_endings_for_content(new_text, content));
  return SingleEditResult{.content = std::move(updated), .strategy = "quote_normalized_exact_match"};
}

[[nodiscard]] std::string normalize_quotes_for_match(const std::string& value) {
  auto normalized = value;
  const auto replace_all = [&](const std::string_view from, const std::string_view to) {
    std::size_t cursor = 0;
    while((cursor = normalized.find(from, cursor)) != std::string::npos) {
      normalized.replace(cursor, from.size(), to);
      cursor += to.size();
    }
  };

  // UTF-8 curly quote bytes.
  replace_all("\xE2\x80\x98", "'");
  replace_all("\xE2\x80\x99", "'");
  replace_all("\xE2\x80\x9C", "\"");
  replace_all("\xE2\x80\x9D", "\"");

  return normalized;
}

[[nodiscard]] std::optional<SingleEditResult> replace_nth_occurrence(
    const std::string& content,
    const std::string& old_text,
    const std::string& new_text,
    std::size_t occurrence
) {
  const auto match_text = normalize_line_endings_for_content(old_text, content);
  const auto replacement = normalize_line_endings_for_content(new_text, content);
  std::size_t current_match = 0;
  std::size_t cursor = 0;

  while((cursor = content.find(match_text, cursor)) != std::string::npos) {
    ++current_match;
    if(current_match == occurrence) {
      auto updated = content;
      updated.replace(cursor, match_text.size(), replacement);
      return SingleEditResult{.content = std::move(updated), .strategy = "occurrence_match"};
    }
    cursor += match_text.size();
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<SingleEditResult> replace_at_line_number(
    const std::string& content,
    const std::string& old_text,
    const std::string& new_text,
    std::size_t line_number
) {
  const auto lines = split_text_lines_for_edit(content);
  if(line_number == 0 || line_number > lines.size()) {
    return std::nullopt;
  }

  auto updated_lines = lines;
  const auto normalized_old_text = normalize_line_endings_for_content(old_text, content);
  const auto normalized_new_text = normalize_line_endings_for_content(new_text, content);
  auto& line = updated_lines[line_number - 1];
  if(line == normalized_old_text) {
    line = normalized_new_text;
  } else {
    const auto inline_pos = line.find(normalized_old_text);
    if(inline_pos == std::string::npos) {
      return std::nullopt;
    }
    line.replace(inline_pos, normalized_old_text.size(), normalized_new_text);
  }

  return SingleEditResult{
      .content = join_lines_for_edit(updated_lines, had_trailing_newline(content), line_ending_for_content(content)),
      .strategy = "line_number",
  };
}

[[nodiscard]] std::optional<SingleEditResult> replace_between_anchors(
    const std::string& content,
    const std::string& old_text,
    const std::string& new_text,
    const std::string& before_anchor,
    const std::string& after_anchor
) {
  const auto normalized_before_anchor = normalize_line_endings_for_content(before_anchor, content);
  const auto normalized_after_anchor = normalize_line_endings_for_content(after_anchor, content);
  const auto normalized_old_text = normalize_line_endings_for_content(old_text, content);
  const auto normalized_new_text = normalize_line_endings_for_content(new_text, content);

  const auto before_idx = content.find(normalized_before_anchor);
  if(before_idx == std::string::npos) {
    return std::nullopt;
  }

  const auto block_start = before_idx + normalized_before_anchor.size();
  const auto after_idx = content.find(normalized_after_anchor, block_start);
  if(after_idx == std::string::npos) {
    return std::nullopt;
  }

  auto block = content.substr(block_start, after_idx - block_start);
  const auto old_idx = block.find(normalized_old_text);
  if(old_idx == std::string::npos) {
    return std::nullopt;
  }

  block.replace(old_idx, normalized_old_text.size(), normalized_new_text);

  auto updated = std::string();
  updated.reserve(content.size() + new_text.size());
  updated.append(content, 0, block_start);
  updated += block;
  updated.append(content, after_idx, std::string::npos);

  return SingleEditResult{.content = std::move(updated), .strategy = "block_anchor"};
}

[[nodiscard]] std::optional<SingleEditResult> line_trimmed_block_match(
    const std::string& content,
    const std::string& old_text,
    const std::string& new_text
) {
  const auto old_lines = split_text_lines_for_edit(old_text);
  if(old_lines.empty()) {
    return std::nullopt;
  }

  const auto content_lines = split_text_lines_for_edit(content);
  if(content_lines.size() < old_lines.size()) {
    return std::nullopt;
  }

  std::vector<std::size_t> matches;
  for(std::size_t start = 0; start + old_lines.size() <= content_lines.size(); ++start) {
    bool matches_window = true;
    for(std::size_t offset = 0; offset < old_lines.size(); ++offset) {
      if(trim_whitespace(content_lines[start + offset]) != trim_whitespace(old_lines[offset])) {
        matches_window = false;
        break;
      }
    }
    if(matches_window) {
      matches.push_back(start);
    }
  }

  if(matches.size() != 1) {
    return std::nullopt;
  }

  const auto start = matches.front();
  const auto new_lines = split_text_lines_for_edit(new_text);

  return SingleEditResult{
      .content = splice_lines_for_edit(
          content_lines,
          start,
          start + old_lines.size(),
          new_lines,
          had_trailing_newline(content),
          line_ending_for_content(content)
      ),
      .strategy = "line_trimmed",
  };
}

[[nodiscard]] std::optional<SingleEditResult> auto_block_anchor_match(
    const std::string& content,
    const std::string& old_text,
    const std::string& new_text
) {
  const auto old_lines = split_text_lines_for_edit(old_text);
  if(old_lines.empty()) {
    return std::nullopt;
  }

  std::optional<std::string> first_anchor;
  std::optional<std::string> last_anchor;
  for(const auto& line : old_lines) {
    const auto trimmed = trim_whitespace(line);
    if(!trimmed.empty()) {
      first_anchor = std::string(trimmed);
      break;
    }
  }
  for(auto it = old_lines.rbegin(); it != old_lines.rend(); ++it) {
    const auto trimmed = trim_whitespace(*it);
    if(!trimmed.empty()) {
      last_anchor = std::string(trimmed);
      break;
    }
  }

  if(!first_anchor.has_value() || !last_anchor.has_value() || first_anchor.value() == last_anchor.value()) {
    return std::nullopt;
  }

  const auto content_lines = split_text_lines_for_edit(content);
  std::vector<std::pair<std::size_t, std::size_t>> regions;

  for(std::size_t i = 0; i < content_lines.size(); ++i) {
    if(trim_whitespace(content_lines[i]) != first_anchor.value()) {
      continue;
    }

    for(std::size_t j = i + 1; j < content_lines.size(); ++j) {
      if(trim_whitespace(content_lines[j]) == last_anchor.value()) {
        regions.emplace_back(i, j);
        break;
      }
    }
  }

  if(regions.size() != 1) {
    return std::nullopt;
  }

  const auto [start, end] = regions.front();
  const auto new_lines = split_text_lines_for_edit(new_text);

  return SingleEditResult{
      .content = splice_lines_for_edit(
          content_lines,
          start,
          end + 1,
          new_lines,
          had_trailing_newline(content),
          line_ending_for_content(content)
      ),
      .strategy = "auto_block_anchor",
  };
}

[[nodiscard]] bool is_ellipsis_line(std::string_view line) {
  const auto trimmed = trim_whitespace(line);
  if(trimmed == "...") {
    return true;
  }

  static constexpr std::array<std::string_view, 8> kPrefixes = {"//", "#", "--", "/*", "*", "<!--", "%", ";"};
  for(const auto prefix : kPrefixes) {
    if(trimmed.starts_with(prefix) && trim_whitespace(trimmed.substr(prefix.size())) == "...") {
      return true;
    }
  }
  return false;
}

[[nodiscard]] std::vector<std::vector<std::string>> split_old_text_on_ellipsis(const std::string& old_text) {
  const auto lines = split_text_lines_for_edit(old_text);
  std::vector<std::vector<std::string>> fragments;
  std::vector<std::string> current;

  for(const auto& line : lines) {
    if(is_ellipsis_line(line)) {
      if(!current.empty()) {
        fragments.push_back(current);
        current.clear();
      }
      continue;
    }
    current.push_back(line);
  }

  if(!current.empty()) {
    fragments.push_back(std::move(current));
  }

  return fragments;
}

[[nodiscard]] std::optional<std::size_t> find_fragment_start(
    const std::vector<std::string>& content_lines,
    const std::vector<std::string>& fragment,
    std::size_t search_start
) {
  if(fragment.empty() || search_start + fragment.size() > content_lines.size()) {
    return std::nullopt;
  }

  for(std::size_t start = search_start; start + fragment.size() <= content_lines.size(); ++start) {
    bool matches = true;
    for(std::size_t offset = 0; offset < fragment.size(); ++offset) {
      if(trim_whitespace(content_lines[start + offset]) != trim_whitespace(fragment[offset])) {
        matches = false;
        break;
      }
    }
    if(matches) {
      return start;
    }
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<SingleEditResult> ellipsis_fragment_match(
    const std::string& content,
    const std::string& old_text,
    const std::string& new_text
) {
  const auto fragments = split_old_text_on_ellipsis(old_text);
  if(fragments.size() < 2) {
    return std::nullopt;
  }

  const auto content_lines = split_text_lines_for_edit(content);
  std::vector<std::pair<std::size_t, std::size_t>> positions;
  std::size_t search_from = 0;

  for(const auto& fragment : fragments) {
    const auto start = find_fragment_start(content_lines, fragment, search_from);
    if(!start.has_value()) {
      return std::nullopt;
    }

    const auto end = start.value() + fragment.size();
    positions.emplace_back(start.value(), end);
    search_from = end;
  }

  const auto region_start = positions.front().first;
  const auto region_end = positions.back().second;

  const auto new_lines = split_text_lines_for_edit(new_text);

  return SingleEditResult{
      .content = splice_lines_for_edit(
          content_lines,
          region_start,
          region_end,
          new_lines,
          had_trailing_newline(content),
          line_ending_for_content(content)
      ),
      .strategy = "ellipsis",
  };
}

[[nodiscard]] std::string normalize_whitespace_block(const std::string& value) {
  std::istringstream stream(value);
  std::ostringstream normalized;
  std::string token;
  bool first = true;

  while(stream >> token) {
    if(!first) {
      normalized << ' ';
    }
    normalized << lowercase(token);
    first = false;
  }

  return normalized.str();
}

[[nodiscard]] bool edit_cascade_within_limits(const std::string& content, const std::string& old_text) {
  if(content.size() > kEditCascadeMaxBytes || old_text.size() > kEditCascadeMaxOldTextBytes) {
    return false;
  }

  const auto newline_count = static_cast<std::size_t>(std::count(content.begin(), content.end(), '\n'));
  const auto line_count = content.empty() ? 0U : newline_count + (content.ends_with('\n') ? 0U : 1U);
  return line_count <= kEditCascadeMaxLines;
}

[[nodiscard]] std::optional<SingleEditResult> flexible_whitespace_block_match(
    const std::string& content,
    const std::string& old_text,
    const std::string& new_text
) {
  const auto old_lines = split_text_lines_for_edit(old_text);
  if(old_lines.empty()) {
    return std::nullopt;
  }

  const auto content_lines = split_text_lines_for_edit(content);
  if(content_lines.size() < old_lines.size()) {
    return std::nullopt;
  }

  const auto old_normalized = normalize_whitespace_block(old_text);
  std::vector<std::size_t> matches;
  for(std::size_t start = 0; start + old_lines.size() <= content_lines.size(); ++start) {
    std::ostringstream candidate;
    for(std::size_t offset = 0; offset < old_lines.size(); ++offset) {
      if(offset > 0) {
        candidate << "\n";
      }
      candidate << content_lines[start + offset];
    }

    if(normalize_whitespace_block(candidate.str()) != old_normalized) {
      continue;
    }

    matches.push_back(start);
  }

  if(matches.size() != 1) {
    return std::nullopt;
  }

  const auto start = matches.front();
  const auto new_lines = split_text_lines_for_edit(new_text);

  return SingleEditResult{
      .content = splice_lines_for_edit(
          content_lines,
          start,
          start + old_lines.size(),
          new_lines,
          had_trailing_newline(content),
          line_ending_for_content(content)
      ),
      .strategy = "flexible_whitespace",
  };
}

[[nodiscard]] bool is_backup_history_path(
    const std::filesystem::path& workspace_root,
    const std::filesystem::path& candidate
) {
  std::error_code ec;
  const auto normalized = std::filesystem::weakly_canonical(candidate, ec);
  if(ec) {
    return false;
  }
  const auto history_root = std::filesystem::weakly_canonical(
      workspace_root / kAvaDirectoryName / kFileHistoryDirectoryName,
      ec
  );
  if(ec) {
    return false;
  }
  return is_path_within_or_equal(history_root, normalized);
}

void reject_backup_history_access(
    const std::filesystem::path& workspace_root,
    const std::filesystem::path& candidate,
    const std::string& tool_name
) {
  if(is_backup_history_path(workspace_root, candidate)) {
    throw std::runtime_error(tool_name + " cannot access AVA file-history backups");
  }
}

}  // namespace

ReadTool::ReadTool(std::filesystem::path workspace_root)
    : workspace_root_(normalize_workspace_root(workspace_root)) {}

std::string ReadTool::name() const {
  return "read";
}

std::string ReadTool::description() const {
  return "Read file content with line numbers";
}

std::string ReadTool::search_hint() const {
  return "read file contents lines offset limit directory";
}

nlohmann::json ReadTool::parameters() const {
  return nlohmann::json{
      {"type", "object"},
      {"required", nlohmann::json::array({"path"})},
      {"properties",
       {
            {"path", {{"type", "string"}, {"description", "File or directory path to read, relative to the workspace root"}}},
            {"offset", {{"type", "integer"}, {"minimum", 1}, {"description", "1-based line offset for file reads"}}},
            {"limit", {{"type", "integer"}, {"minimum", 1}, {"description", "Maximum number of lines to return"}}},
       }},
  };
}

ava::types::ToolResult ReadTool::execute(const nlohmann::json& args) const {
  if(!args.contains("path")) {
    throw std::runtime_error("missing required field: path");
  }

  const auto path = args.at("path").get<std::string>();
  const auto offset = args.value("offset", static_cast<std::size_t>(1));
  const auto limit = args.value("limit", kReadDefaultLimit);
  const auto full_path = enforce_workspace_path(workspace_root_, path, name());
  reject_backup_history_access(workspace_root_, full_path, name());

  std::error_code ec;
  if(!std::filesystem::exists(full_path, ec) || ec) {
    throw std::runtime_error("Not found: " + path);
  }

  if(std::filesystem::is_directory(full_path, ec) && !ec) {
    std::vector<std::string> entries;
    for(const auto& entry : std::filesystem::directory_iterator(full_path)) {
      if(is_backup_history_path(workspace_root_, entry.path())) {
        continue;
      }
      auto display = entry.path().filename().string();
      if(entry.is_directory()) {
        display += "/";
      }
      entries.push_back(display);
    }
    std::sort(entries.begin(), entries.end());
    std::ostringstream out;
    for(std::size_t idx = 0; idx < entries.size(); ++idx) {
      if(idx > 0) {
        out << "\n";
      }
      out << entries[idx];
    }
    return ava::types::ToolResult{.call_id = "", .content = apply_output_fallback(name(), out.str(), kReadOutputBytes), .is_error = false};
  }

  ensure_regular_file_size_within_limit(full_path, kReadMaxFileBytes, "read");
  const auto content = read_file_text(full_path);
  const auto lines = split_lines(content);

  const std::size_t start = offset > 0 ? offset - 1 : 0;
  std::ostringstream out;
  std::size_t emitted = 0;
  for(std::size_t index = start; index < lines.size() && emitted < limit; ++index, ++emitted) {
    if(emitted > 0) {
      out << "\n";
    }
    out << std::setw(6) << (index + 1) << '\t' << lines[index];
  }

  return ava::types::ToolResult{.call_id = "", .content = apply_output_fallback(name(), out.str(), kReadOutputBytes), .is_error = false};
}

WriteTool::WriteTool(std::filesystem::path workspace_root, std::shared_ptr<FileBackupSession> backup_session)
    : workspace_root_(normalize_workspace_root(workspace_root)),
      backup_session_(std::move(backup_session)) {}

std::string WriteTool::name() const {
  return "write";
}

std::string WriteTool::description() const {
  return "Write content to a file";
}

std::string WriteTool::search_hint() const {
  return "write create overwrite file content";
}

nlohmann::json WriteTool::parameters() const {
  return nlohmann::json{{"type", "object"},
                        {"required", nlohmann::json::array({"path", "content"})},
                        {"properties",
                         {{"path", {{"type", "string"}, {"description", "File path to write, relative to the workspace root"}}},
                          {"content", {{"type", "string"}, {"description", "Complete file content to write"}}}}}};
}

ava::types::ToolResult WriteTool::execute(const nlohmann::json& args) const {
  if(!args.contains("path") || !args.contains("content")) {
    throw std::runtime_error("missing required fields: path/content");
  }

  const auto path = args.at("path").get<std::string>();
  const auto content = args.at("content").get<std::string>();
  const auto full_path = enforce_workspace_path(workspace_root_, path, name());
  reject_backup_history_access(workspace_root_, full_path, name());

  if(backup_session_) {
    backup_session_->backup_file_before_edit(full_path);
  }
  write_file_text(full_path, content);

  return ava::types::ToolResult{
      .call_id = "",
      .content = "Wrote " + std::to_string(content.size()) + " bytes to " + path,
      .is_error = false,
  };
}

EditTool::EditTool(std::filesystem::path workspace_root, std::shared_ptr<FileBackupSession> backup_session)
    : workspace_root_(normalize_workspace_root(workspace_root)),
      backup_session_(std::move(backup_session)) {}

std::string EditTool::name() const {
  return "edit";
}

std::string EditTool::description() const {
  return "Edit existing file content with a scoped multi-strategy cascade";
}

std::string EditTool::search_hint() const {
  return "edit replace old_text new_text occurrence line anchors";
}

nlohmann::json EditTool::parameters() const {
  return nlohmann::json{{"type", "object"},
                        {"required", nlohmann::json::array({"path", "old_text", "new_text"})},
                        {"properties",
                         {{"path", {{"type", "string"}, {"description", "File path to edit, relative to the workspace root"}}},
                          {"old_text", {{"type", "string"}, {"description", "Existing text to replace"}}},
                          {"new_text", {{"type", "string"}, {"description", "Replacement text"}}},
                          {"replace_all", {{"type", "boolean"}, {"description", "Replace every exact occurrence instead of one match"}}},
                          {"occurrence", {{"type", "integer"}, {"minimum", 1}, {"description", "1-based occurrence to replace"}}},
                          {"line_number", {{"type", "integer"}, {"minimum", 1}, {"description", "1-based line locator for the replacement"}}},
                          {"before_anchor", {{"type", "string"}, {"description", "Text immediately before an anchored replacement block"}}},
                          {"after_anchor", {{"type", "string"}, {"description", "Text immediately after an anchored replacement block"}}}}}};
}

ava::types::ToolResult EditTool::execute(const nlohmann::json& args) const {
  if(!args.contains("path") || !args.contains("old_text") || !args.contains("new_text")) {
    throw std::runtime_error("missing required fields: path/old_text/new_text");
  }

  const auto path = args.at("path").get<std::string>();
  const auto old_text = args.at("old_text").get<std::string>();
  const auto new_text = args.at("new_text").get<std::string>();
  const bool replace_all = args.value("replace_all", false);
  if(old_text.empty()) {
    throw std::runtime_error("old_text must not be empty");
  }
  if(!replace_all && old_text.size() > kEditCascadeMaxOldTextBytes) {
    throw std::runtime_error("old_text is too large for edit");
  }

  const auto parse_positive_locator = [](const nlohmann::json& value, const char* field_name) -> std::size_t {
    try {
      const auto parsed = value.get<std::int64_t>();
      if(parsed <= 0) {
        throw std::runtime_error(std::string(field_name) + " must be >= 1");
      }
      return static_cast<std::size_t>(parsed);
    } catch(const nlohmann::json::exception&) {
      throw std::runtime_error(std::string(field_name) + " must be a positive integer");
    }
  };

  std::optional<std::size_t> occurrence;
  if(args.contains("occurrence")) {
    occurrence = parse_positive_locator(args.at("occurrence"), "occurrence");
  }

  std::optional<std::size_t> line_number;
  if(args.contains("line_number")) {
    line_number = parse_positive_locator(args.at("line_number"), "line_number");
  }

  std::optional<std::string> before_anchor;
  if(args.contains("before_anchor")) {
    before_anchor = args.at("before_anchor").get<std::string>();
  }

  std::optional<std::string> after_anchor;
  if(args.contains("after_anchor")) {
    after_anchor = args.at("after_anchor").get<std::string>();
  }

  const bool has_locator_arg = occurrence.has_value() || line_number.has_value() || before_anchor.has_value() || after_anchor.has_value();
  if(replace_all && has_locator_arg) {
    throw std::runtime_error("replace_all cannot be combined with occurrence, line_number, before_anchor, or after_anchor");
  }
  if(before_anchor.has_value() != after_anchor.has_value()) {
    throw std::runtime_error("before_anchor and after_anchor must be provided together");
  }
  if((before_anchor.has_value() && before_anchor->empty()) || (after_anchor.has_value() && after_anchor->empty())) {
    throw std::runtime_error("before_anchor and after_anchor must not be empty");
  }
  const std::size_t locator_family_count = (occurrence.has_value() ? 1U : 0U) + (line_number.has_value() ? 1U : 0U) +
                                          (before_anchor.has_value() && after_anchor.has_value() ? 1U : 0U);
  if(locator_family_count > 1) {
    throw std::runtime_error("only one explicit edit locator may be provided");
  }

  const auto full_path = enforce_workspace_path(workspace_root_, path, name());
  reject_backup_history_access(workspace_root_, full_path, name());
  ensure_regular_file_size_within_limit(full_path, replace_all ? kEditReplaceAllMaxBytes : kEditMaxFileBytes, "edit");
  if(!replace_all && new_text.size() > kEditMaxFileBytes) {
    throw std::runtime_error("edit replacement text is too large");
  }
  auto content = read_file_text(full_path);

  std::size_t replacements = 0;
  std::string strategy;
  if(replace_all) {
    const auto match_text = normalize_line_endings_for_content(old_text, content);
    const auto replacement = normalize_line_endings_for_content(new_text, content);
    if(content.size() > kEditReplaceAllMaxBytes || match_text.size() > kEditReplaceAllMaxBytes ||
       replacement.size() > kEditReplaceAllMaxOutputBytes) {
      throw std::runtime_error("replace_all input is too large");
    }
    std::string updated;
    updated.reserve(std::min(content.size(), kEditReplaceAllMaxOutputBytes));
    std::size_t cursor = 0;
    while(true) {
      const auto found = content.find(match_text, cursor);
      if(found == std::string::npos) {
        break;
      }
      if(replacements >= kEditReplaceAllMaxReplacements) {
        throw std::runtime_error("replace_all replacement count is too large");
      }
      if(updated.size() + (found - cursor) + replacement.size() > kEditReplaceAllMaxOutputBytes) {
        throw std::runtime_error("replace_all output is too large");
      }
      updated.append(content, cursor, found - cursor);
      updated += replacement;
      cursor = found + match_text.size();
      ++replacements;
    }
    if(replacements == 0) {
      throw std::runtime_error("No matching text found for edit");
    }
    if(updated.size() + (content.size() - cursor) > kEditReplaceAllMaxOutputBytes) {
      throw std::runtime_error("replace_all output is too large");
    }
    updated.append(content, cursor, std::string::npos);
    content = std::move(updated);
    strategy = "replace_all";
  } else {
    std::optional<SingleEditResult> match;
    const bool has_explicit_locator =
        occurrence.has_value() || line_number.has_value() || (before_anchor.has_value() && after_anchor.has_value());

    if(!has_explicit_locator) {
      match = replace_first_occurrence(content, old_text, new_text, "exact_match");
      if(!match.has_value() && edit_cascade_within_limits(content, old_text)) {
        match = quote_normalized_exact_match(content, old_text, new_text);
      }
    }

    if(!match.has_value() && occurrence.has_value()) {
      match = replace_nth_occurrence(content, old_text, new_text, occurrence.value());
    }

    if(!match.has_value() && line_number.has_value()) {
      match = replace_at_line_number(content, old_text, new_text, line_number.value());
    }

    if(!match.has_value() && before_anchor.has_value() && after_anchor.has_value()) {
      match = replace_between_anchors(
          content,
          old_text,
          new_text,
          before_anchor.value(),
          after_anchor.value()
      );
    }

    if(has_explicit_locator && !match.has_value()) {
      throw std::runtime_error("No matching text found for edit");
    }

    if(!match.has_value() && !edit_cascade_within_limits(content, old_text)) {
      throw std::runtime_error("No matching text found for edit");
    }

    if(!match.has_value()) {
      match = line_trimmed_block_match(content, old_text, new_text);
    }

    if(!match.has_value()) {
      match = auto_block_anchor_match(content, old_text, new_text);
    }

    if(!match.has_value()) {
      match = ellipsis_fragment_match(content, old_text, new_text);
    }

    if(!match.has_value()) {
      match = flexible_whitespace_block_match(content, old_text, new_text);
    }

    if(!match.has_value()) {
      throw std::runtime_error("No matching text found for edit");
    }

    content = std::move(match->content);
    if(content.size() > kEditMaxFileBytes) {
      throw std::runtime_error("edit output is too large");
    }
    strategy = std::move(match->strategy);
    replacements = 1;
  }

  if(backup_session_) {
    backup_session_->backup_file_before_edit(full_path);
  }
  write_file_text(full_path, content);

  return ava::types::ToolResult{
      .call_id = "",
      .content = std::string("Applied ") + strategy + "; replacements=" + std::to_string(replacements),
      .is_error = false,
  };
}

BashTool::BashTool(std::filesystem::path workspace_root)
    : workspace_root_(normalize_workspace_root(workspace_root)) {}

std::string BashTool::name() const {
  return "bash";
}

std::string BashTool::description() const {
  return "Execute shell command";
}

std::string BashTool::search_hint() const {
  return "run execute shell command terminal bash timeout cwd";
}

nlohmann::json BashTool::parameters() const {
  return nlohmann::json{{"type", "object"},
                        {"required", nlohmann::json::array({"command"})},
                        {"properties",
                         {{"command", {{"type", "string"}, {"description", "Non-interactive shell command to execute"}}},
                          {"timeout_ms", {{"type", "integer"}, {"minimum", 1}, {"description", "Command timeout in milliseconds"}}},
                          {"cwd", {{"type", "string"}, {"description", "Working directory relative to the workspace root"}}}}}};
}

ava::types::ToolResult BashTool::execute(const nlohmann::json& args) const {
  if(!args.contains("command")) {
    throw std::runtime_error("missing required field: command");
  }

  const auto command = args.at("command").get<std::string>();
  const std::uint64_t timeout_ms = args.value("timeout_ms", static_cast<std::uint64_t>(120000));
  const auto cwd_raw = args.value("cwd", std::string("."));
  const auto cwd = enforce_workspace_path(workspace_root_, cwd_raw, name());

  const auto outcome = run_shell_command(command, cwd, timeout_ms);
  auto content = render_shell_result(outcome);
  content = apply_output_fallback(name(), content, kBashOutputBytes);

  return ava::types::ToolResult{.call_id = "", .content = content, .is_error = outcome.exit_code != 0};
}

GlobTool::GlobTool(std::filesystem::path workspace_root)
    : workspace_root_(normalize_workspace_root(workspace_root)) {}

std::string GlobTool::name() const {
  return "glob";
}

std::string GlobTool::description() const {
  return "Find files by glob pattern";
}

std::string GlobTool::search_hint() const {
  return "glob find files pattern path";
}

nlohmann::json GlobTool::parameters() const {
  return nlohmann::json{{"type", "object"},
                        {"required", nlohmann::json::array({"pattern"})},
                        {"properties",
                         {{"pattern", {{"type", "string"}, {"description", "Glob pattern to match"}}},
                          {"path", {{"type", "string"}, {"description", "Directory to search, relative to the workspace root"}}}}}};
}

ava::types::ToolResult GlobTool::execute(const nlohmann::json& args) const {
  if(!args.contains("pattern")) {
    throw std::runtime_error("missing required field: pattern");
  }

  const auto pattern = args.at("pattern").get<std::string>();
  const auto base = enforce_workspace_path(workspace_root_, args.value("path", std::string(".")), name());
  reject_backup_history_access(workspace_root_, base, name());
  const auto pattern_re = glob_to_regex(pattern);

  std::vector<std::string> matches;
  std::error_code ec;
  if(!std::filesystem::exists(base, ec) || ec) {
    throw std::runtime_error("Not found: " + base.string());
  }
  if(std::filesystem::is_regular_file(base, ec) && !ec) {
    const auto rel = std::filesystem::relative(base, base.parent_path());
    if(std::regex_match(rel.generic_string(), pattern_re)) {
      if(!is_backup_history_path(workspace_root_, base)) {
        matches.push_back(base.string());
      }
    }
  } else {
    for(const auto& entry : std::filesystem::recursive_directory_iterator(base, std::filesystem::directory_options::skip_permission_denied)) {
      if(entry.is_symlink(ec) && !ec) {
        auto symlink_target = std::filesystem::read_symlink(entry.path(), ec);
        if(ec) {
          continue;
        }
        if(symlink_target.is_relative()) {
          symlink_target = entry.path().parent_path() / symlink_target;
        }
        const auto resolved_target = std::filesystem::weakly_canonical(symlink_target, ec);
        if(ec || !is_path_within_or_equal(workspace_root_, resolved_target)) {
          continue;
        }
      }
      if(!entry.is_regular_file(ec) || ec) {
        continue;
      }
      if(is_backup_history_path(workspace_root_, entry.path())) {
        continue;
      }
      const auto rel = std::filesystem::relative(entry.path(), base).generic_string();
      if(std::regex_match(rel, pattern_re)) {
        matches.push_back(entry.path().string());
      }
    }
  }

  std::sort(matches.begin(), matches.end());
  const bool truncated = matches.size() > kGlobMaxResults;
  if(truncated) {
    matches.resize(kGlobMaxResults);
  }

  std::ostringstream out;
  for(std::size_t idx = 0; idx < matches.size(); ++idx) {
    if(idx > 0) {
      out << "\n";
    }
    out << matches[idx];
  }
  if(truncated) {
    out << "\n\n(Results are truncated: showing first " << kGlobMaxResults
        << " results. Consider using a more specific path or pattern.)";
  }

  return ava::types::ToolResult{.call_id = "", .content = out.str(), .is_error = false};
}

GrepTool::GrepTool(std::filesystem::path workspace_root)
    : workspace_root_(normalize_workspace_root(workspace_root)) {}

std::string GrepTool::name() const {
  return "grep";
}

std::string GrepTool::description() const {
  return "Search files by regex";
}

std::string GrepTool::search_hint() const {
  return "grep search regex pattern files include path";
}

nlohmann::json GrepTool::parameters() const {
  return nlohmann::json{{"type", "object"},
                        {"required", nlohmann::json::array({"pattern"})},
                        {"properties",
                         {{"pattern", {{"type", "string"}, {"description", "Regular expression to search for"}}},
                          {"path", {{"type", "string"}, {"description", "Directory to search, relative to the workspace root"}}},
                          {"include", {{"type", "string"}, {"description", "Optional glob pattern limiting searched files"}}}}}};
}

ava::types::ToolResult GrepTool::execute(const nlohmann::json& args) const {
  if(!args.contains("pattern")) {
    throw std::runtime_error("missing required field: pattern");
  }

  const auto pattern = args.at("pattern").get<std::string>();
  std::regex matcher;
  try {
    matcher = std::regex(pattern);
  } catch(const std::regex_error& ex) {
    return ava::types::ToolResult{.call_id = "", .content = std::string("Invalid regex: ") + ex.what(), .is_error = true};
  }
  const auto base = enforce_workspace_path(workspace_root_, args.value("path", std::string(".")), name());
  reject_backup_history_access(workspace_root_, base, name());
  std::error_code ec;
  if(!std::filesystem::exists(base, ec) || ec) {
    return ava::types::ToolResult{.call_id = "", .content = "Not found: " + base.string(), .is_error = true};
  }

  std::optional<std::regex> include_filter;
  if(args.contains("include")) {
    include_filter = glob_to_regex(args.at("include").get<std::string>());
  }

  std::vector<std::string> matches;
  for(const auto& entry : std::filesystem::recursive_directory_iterator(base, std::filesystem::directory_options::skip_permission_denied)) {
    if(entry.is_symlink(ec) && !ec) {
      auto symlink_target = std::filesystem::read_symlink(entry.path(), ec);
      if(ec) {
        continue;
      }
      if(symlink_target.is_relative()) {
        symlink_target = entry.path().parent_path() / symlink_target;
      }
      const auto resolved_target = std::filesystem::weakly_canonical(symlink_target, ec);
      if(ec || !is_path_within_or_equal(workspace_root_, resolved_target)) {
        continue;
      }
    }
    if(!entry.is_regular_file(ec) || ec) {
      continue;
    }
    if(is_backup_history_path(workspace_root_, entry.path())) {
      continue;
    }

    const auto relative = std::filesystem::relative(entry.path(), base).generic_string();
    if(include_filter.has_value() && !std::regex_match(relative, include_filter.value())) {
      continue;
    }

    std::ifstream file(entry.path());
    if(!file) {
      continue;
    }

    std::string line;
    std::size_t line_number = 0;
    while(std::getline(file, line)) {
      ++line_number;
      if(line.size() > 8192) {
        line.resize(8192);
      }
      if(std::regex_search(line, matcher)) {
        std::ostringstream hit;
        hit << entry.path().string() << ":" << line_number << ":" << line;
        matches.push_back(hit.str());
        if(matches.size() >= kGrepMaxMatches) {
          break;
        }
      }
    }
    if(matches.size() >= kGrepMaxMatches) {
      break;
    }
  }

  std::sort(matches.begin(), matches.end());
  std::ostringstream out;
  for(std::size_t idx = 0; idx < matches.size(); ++idx) {
    if(idx > 0) {
      out << "\n";
    }
    out << matches[idx];
  }
  if(matches.size() >= kGrepMaxMatches) {
    out << "\n\n(Results truncated: showing first " << kGrepMaxMatches
        << " matches. Consider using a more specific path or pattern.)";
  }

  return ava::types::ToolResult{.call_id = "", .content = out.str(), .is_error = false};
}

DefaultToolRegistration register_default_tools(ToolRegistry& registry, const std::filesystem::path& workspace_root) {
  auto backup_session = std::make_shared<FileBackupSession>(workspace_root);

  registry.register_tool(std::make_unique<ReadTool>(workspace_root));
  registry.register_tool(std::make_unique<WriteTool>(workspace_root, backup_session));
  registry.register_tool(std::make_unique<EditTool>(workspace_root, backup_session));
  registry.register_tool(std::make_unique<BashTool>(workspace_root));
  registry.register_tool(std::make_unique<GlobTool>(workspace_root));
  registry.register_tool(std::make_unique<GrepTool>(workspace_root));
  registry.register_tool(std::make_unique<GitReadTool>(workspace_root));
  registry.register_tool(std::make_unique<GitReadAliasTool>(workspace_root));

  return DefaultToolRegistration{.backup_session = std::move(backup_session)};
}

}  // namespace ava::tools
