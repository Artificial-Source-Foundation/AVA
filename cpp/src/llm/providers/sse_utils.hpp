#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#if AVA_WITH_CPR
#include <cpr/cpr.h>
#endif

namespace ava::llm::providers {

#if AVA_WITH_CPR
// Parses numeric retry hints into conservative whole seconds.
// Supported headers: retry-after-ms, retry-after (integer seconds), x-ratelimit-reset.
// HTTP-date retry-after parsing is intentionally deferred in the C++ lane.
[[nodiscard]] std::optional<std::uint64_t> parse_retry_after_secs(const cpr::Header& headers);
#endif

[[nodiscard]] std::string normalize_sse_newlines(std::string_view chunk, bool& pending_carriage_return);
[[nodiscard]] std::optional<std::string> extract_sse_data_line(std::string_view line);

class SseEventBuffer {
 public:
  using PayloadHandler = std::function<bool(std::string_view)>;

  [[nodiscard]] bool append(std::string_view chunk, const PayloadHandler& on_payload);
  [[nodiscard]] bool flush(const PayloadHandler& on_payload);

 private:
  [[nodiscard]] bool process_ready_events(const PayloadHandler& on_payload);

  std::string pending_;
  bool pending_carriage_return_{false};
};

}  // namespace ava::llm::providers
