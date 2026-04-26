#include "sse_utils.hpp"

#include <charconv>
#include <chrono>
#include <stdexcept>
#include <system_error>

#include "ava/core/string_utils.hpp"

namespace ava::llm::providers {
namespace {

constexpr std::size_t kMaxPendingSseBytes = 4 * 1024 * 1024;
constexpr std::size_t kMaxSsePayloadBytes = 2 * 1024 * 1024;

void enforce_sse_size_limit(std::size_t size, std::size_t limit, const char* what) {
  if(size > limit) {
    throw std::runtime_error(std::string{what} + " exceeds maximum supported size");
  }
}

#if AVA_WITH_CPR
[[nodiscard]] std::optional<std::uint64_t> parse_uint64_header_value(std::string_view value) {
  const auto trimmed = ava::core::trim_ascii_view(value);
  if(trimmed.empty()) {
    return std::nullopt;
  }

  std::uint64_t parsed = 0;
  const auto* begin = trimmed.data();
  const auto* end = begin + trimmed.size();
  const auto [ptr, ec] = std::from_chars(begin, end, parsed);
  if(ec == std::errc{} && ptr == end) {
    return parsed;
  }
  return std::nullopt;
}

[[nodiscard]] std::uint64_t ceil_millis_to_secs(std::uint64_t millis) {
  constexpr std::uint64_t kMillisPerSecond = 1000;
  const auto whole_seconds = millis / kMillisPerSecond;
  if(millis % kMillisPerSecond == 0) {
    return whole_seconds;
  }
  return whole_seconds + 1;
}

[[nodiscard]] std::uint64_t seconds_until_unix_timestamp(std::uint64_t unix_timestamp_secs) {
  const auto now_epoch =
      std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch());
  const auto now_secs = now_epoch.count() > 0 ? static_cast<std::uint64_t>(now_epoch.count()) : 0U;
  if(unix_timestamp_secs <= now_secs) {
    return 0;
  }
  return unix_timestamp_secs - now_secs;
}
#endif

}  // namespace

#if AVA_WITH_CPR
std::optional<std::uint64_t> parse_retry_after_secs(const cpr::Header& headers) {
  std::optional<std::uint64_t> retry_after_ms;
  std::optional<std::uint64_t> retry_after_seconds;
  std::optional<std::uint64_t> rate_limit_reset;

  for(const auto& [name, value] : headers) {
    const auto lower_name = ava::core::lowercase_ascii(name);
    if(lower_name == "retry-after-ms") {
      if(!retry_after_ms.has_value()) {
        retry_after_ms = parse_uint64_header_value(value);
      }
      continue;
    }
    if(lower_name == "retry-after") {
      if(!retry_after_seconds.has_value()) {
        retry_after_seconds = parse_uint64_header_value(value);
      }
      continue;
    }
    if(lower_name == "x-ratelimit-reset") {
      if(!rate_limit_reset.has_value()) {
        rate_limit_reset = parse_uint64_header_value(value);
      }
      continue;
    }
  }

  if(retry_after_ms.has_value()) {
    return ceil_millis_to_secs(*retry_after_ms);
  }

  if(retry_after_seconds.has_value()) {
    return retry_after_seconds;
  }

  if(rate_limit_reset.has_value()) {
    return seconds_until_unix_timestamp(*rate_limit_reset);
  }

  return std::nullopt;
}
#endif

std::string normalize_sse_newlines(std::string_view chunk, bool& pending_carriage_return) {
  std::string normalized;
  normalized.reserve(chunk.size() + (pending_carriage_return ? 1U : 0U));

  std::size_t index = 0;
  if(pending_carriage_return) {
    pending_carriage_return = false;
    if(!chunk.empty() && chunk.front() == '\n') {
      normalized.push_back('\n');
      index = 1;
    } else {
      normalized.push_back('\r');
    }
  }

  for(; index < chunk.size(); ++index) {
    const char ch = chunk.at(index);
    if(ch == '\r') {
      if(index + 1 < chunk.size() && chunk.at(index + 1) == '\n') {
        ++index;
        normalized.push_back('\n');
      } else if(index + 1 == chunk.size()) {
        pending_carriage_return = true;
      } else {
        normalized.push_back(ch);
      }
      continue;
    }

    normalized.push_back(ch);
  }

  return normalized;
}

std::optional<std::string> extract_sse_data_line(std::string_view line) {
  if(line.rfind("data:", 0) == 0) {
    auto payload = line.substr(5);
    while(!payload.empty() && payload.front() == ' ') {
      payload.remove_prefix(1);
    }
    return std::string(payload);
  }

  if(line == "data") {
    return std::string{};
  }

  return std::nullopt;
}

bool SseEventBuffer::append(std::string_view chunk, const PayloadHandler& on_payload) {
  pending_ += normalize_sse_newlines(chunk, pending_carriage_return_);
  enforce_sse_size_limit(pending_.size(), kMaxPendingSseBytes, "pending SSE event buffer");
  return process_ready_events(on_payload);
}

bool SseEventBuffer::flush(const PayloadHandler& on_payload) {
  if(pending_carriage_return_) {
    pending_.push_back('\n');
    pending_carriage_return_ = false;
  }
  if(!pending_.empty()) {
    pending_.append("\n\n");
    enforce_sse_size_limit(pending_.size(), kMaxPendingSseBytes, "pending SSE event buffer");
  }
  return process_ready_events(on_payload);
}

bool SseEventBuffer::process_ready_events(const PayloadHandler& on_payload) {
  while(true) {
    const auto event_end = pending_.find("\n\n");
    if(event_end == std::string::npos) {
      break;
    }

    const std::string event_block = pending_.substr(0, event_end);
    pending_.erase(0, event_end + 2);

    bool saw_data = false;
    std::string payload;
    std::size_t line_start = 0;
    while(line_start <= event_block.size()) {
      const auto line_end = event_block.find('\n', line_start);
      const std::string_view line = line_end == std::string::npos
                                        ? std::string_view(event_block).substr(line_start)
                                        : std::string_view(event_block).substr(line_start, line_end - line_start);
      line_start = line_end == std::string::npos ? event_block.size() + 1 : line_end + 1;

      if(const auto data_line = extract_sse_data_line(line); data_line.has_value()) {
        if(saw_data) {
          payload.push_back('\n');
        }
        payload += *data_line;
        enforce_sse_size_limit(payload.size(), kMaxSsePayloadBytes, "SSE event payload");
        saw_data = true;
      }
    }

    if(!saw_data) {
      continue;
    }

    if(on_payload && !on_payload(payload)) {
      return false;
    }
  }

  return true;
}

}  // namespace ava::llm::providers
