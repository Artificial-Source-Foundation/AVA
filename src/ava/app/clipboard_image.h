#pragma once

#include "ava/process/scope.h"
#include "ava/session/attachments.h"
#include "ava/core/result.h"

#include <filesystem>
#include <optional>
#include <string>
#include <variant>

namespace ava::session {
class SessionStore;
}  // namespace ava::session

namespace ava::app {

// Read the operating-system clipboard before acquiring session locks. The path
// alternative preserves the explicit AVA_CLIPBOARD_IMAGE_FILE import behavior.
using ClipboardImageSource = std::variant<std::filesystem::path, std::string>;
[[nodiscard]] ava::core::Result<std::optional<ClipboardImageSource>> read_clipboard_image_source();
[[nodiscard]] ava::core::Result<ava::session::ImageAttachmentRef> import_clipboard_image_source(ava::session::SessionStore const& store,
                                                                                                ClipboardImageSource const& source);

[[nodiscard]] ava::core::Result<std::optional<ava::session::ImageAttachmentRef>> import_clipboard_image_attachment(
    ava::session::SessionStore const& store, std::optional<ava::process::ProcessScopeV1> const& session_process_scope);

}  // namespace ava::app
