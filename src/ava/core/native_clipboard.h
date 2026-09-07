#pragma once

#include "ava/core/result.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace ava::core {

inline constexpr std::size_t kMaxNativeClipboardTextBytes = 16 * 1024 * 1024;

// Local macOS sessions use the system pasteboard. SSH sessions and the explicit
// AVA_CLIPBOARD_BACKEND=terminal override keep the client terminal's clipboard.
[[nodiscard]] bool native_clipboard_enabled();
[[nodiscard]] VoidResult write_native_clipboard_text(std::string_view text);
[[nodiscard]] Result<std::optional<std::string>> read_native_clipboard_image(std::size_t max_bytes);

}  // namespace ava::core
