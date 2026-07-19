#pragma once

#include "ava/core/openai_wire.h"

namespace ava::provider {

// Compatibility re-exports for provider callers. Session/core code must use
// ava::core directly so it does not depend on a provider implementation header.
inline constexpr auto kMaxOpenAINativeReasoningItemBytes = ava::core::kMaxOpenAINativeReasoningItemBytes;
inline constexpr auto kMaxOpenAIOpaqueIdBytes = ava::core::kMaxOpenAIOpaqueIdBytes;
using ava::core::is_valid_openai_native_reasoning_item_json;
using ava::core::is_valid_openai_opaque_id;

}  // namespace ava::provider
