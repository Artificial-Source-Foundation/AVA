#include "ava/tools/retry.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>

#include "ava/core/string_utils.hpp"

namespace ava::tools::retry {

namespace {

constexpr std::array<std::string_view, 7> kRetryableTools = {
    "read",
    "glob",
    "grep",
    "git",
    "git_read",
    "web_fetch",
    "web_search",
};

constexpr std::array<std::chrono::milliseconds, 2> kBackoffDurations = {
    std::chrono::milliseconds(100),
    std::chrono::milliseconds(200),
};

constexpr std::array<std::string_view, 23> kTransientPatterns = {
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
    "could not resolve host",
    "temporary failure in name resolution",
    "name or service not known",
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

}  // namespace

bool is_retryable_tool(std::string_view tool_name) {
  return std::find(kRetryableTools.begin(), kRetryableTools.end(), tool_name) != kRetryableTools.end();
}

bool is_transient_error(std::string_view error_message) {
  const auto lower = ava::core::lowercase_ascii(error_message);

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
