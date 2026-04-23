#include "ava/tools/retry.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>

namespace ava::tools::retry {

namespace {

constexpr std::array<std::string_view, 5> kRetryableTools = {
    "read",
    "glob",
    "grep",
    "git",
    "git_read",
};

constexpr std::array<std::chrono::milliseconds, 2> kBackoffDurations = {
    std::chrono::milliseconds(100),
    std::chrono::milliseconds(200),
};

constexpr std::array<std::string_view, 20> kTransientPatterns = {
    "permission denied",
    "connection refused",
    "connection reset",
    "timed out",
    "timeout",
    "temporarily unavailable",
    "resource busy",
    "too many open files",
    "broken pipe",
    "network unreachable",
    "host unreachable",
    "connection aborted",
    "resource temporarily unavailable",
    "try again",
    "service unavailable",
    "429",
    "502",
    "503",
    "504",
    "eagain",
};

constexpr std::array<std::string_view, 11> kPermanentPatterns = {
    "not found",
    "no such file",
    "no such directory",
    "invalid argument",
    "invalid input",
    "not a directory",
    "is a directory",
    "syntax error",
    "does not exist",
    "unknown tool",
    "missing required",
};

[[nodiscard]] std::string lowercase(std::string_view value) {
  std::string out(value);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return out;
}

}  // namespace

bool is_retryable_tool(std::string_view tool_name) {
  return std::find(kRetryableTools.begin(), kRetryableTools.end(), tool_name) != kRetryableTools.end();
}

bool is_transient_error(std::string_view error_message) {
  const auto lower = lowercase(error_message);

  for(const auto& pattern : kPermanentPatterns) {
    if(lower.find(pattern) != std::string::npos) {
      return false;
    }
  }

  for(const auto& pattern : kTransientPatterns) {
    if(lower.find(pattern) != std::string::npos) {
      return true;
    }
  }
  return false;
}

std::optional<std::chrono::milliseconds> backoff_for_attempt(std::size_t attempt) {
  if(attempt >= kBackoffDurations.size()) {
    return std::nullopt;
  }
  return kBackoffDurations[attempt];
}

}  // namespace ava::tools::retry
