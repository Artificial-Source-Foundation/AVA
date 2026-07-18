#pragma once

#include "ava/core/json.h"

#include <cstddef>
#include <string_view>

namespace ava::provider {

// Provider-private reasoning JSON is retained only for native continuation.
// Keep it below the established 64 KiB opaque-provider payload ceiling, which
// is also comfortably within the session record limit.
inline constexpr std::size_t kMaxOpenAINativeReasoningItemBytes = 64U * 1024U;
inline constexpr std::size_t kMaxOpenAIOpaqueIdBytes = 256U;

[[nodiscard]] inline bool is_valid_openai_opaque_id(std::string_view id) noexcept
{
  if (id.empty() || id.size() > kMaxOpenAIOpaqueIdBytes)
    return false;
  for (char const ch : id)
  {
    auto const byte = static_cast<unsigned char>(ch);
    if (byte < 0x20U || byte == 0x7FU)
      return false;
  }
  return true;
}

// OpenAI may add fields to reasoning items. AVA preserves them only when the
// stable replay identity and summary-array shape are present and bounded.
[[nodiscard]] inline bool is_valid_openai_native_reasoning_item_json(std::string_view native_item_json) noexcept
{
  if (native_item_json.empty() || native_item_json.size() > kMaxOpenAINativeReasoningItemBytes || !ava::core::json::is_valid_object(native_item_json) ||
      ava::core::json::string_field(native_item_json, "type").value_or("") != "reasoning")
  {
    return false;
  }
  auto const id = ava::core::json::string_field(native_item_json, "id");
  auto const summary_start = ava::core::json::field_value_start(native_item_json, "summary");
  return id && is_valid_openai_opaque_id(*id) && summary_start && *summary_start < native_item_json.size() && native_item_json[*summary_start] == '[';
}

}  // namespace ava::provider
