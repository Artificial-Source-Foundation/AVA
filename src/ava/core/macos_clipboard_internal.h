#pragma once

#include "ava/core/native_clipboard.h"

#ifdef __APPLE__
#include <ApplicationServices/ApplicationServices.h>

namespace ava::core::macos_clipboard {

// These operations use the supplied pasteboard authority only. Tests supply a
// unique private pasteboard; they never read or replace the user's clipboard.
[[nodiscard]] VoidResult write_text(PasteboardRef pasteboard, std::string_view text);
[[nodiscard]] Result<std::optional<std::string>> read_image(PasteboardRef pasteboard, std::size_t max_bytes);

}  // namespace ava::core::macos_clipboard
#endif
