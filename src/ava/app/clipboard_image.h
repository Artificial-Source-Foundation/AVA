#pragma once

#include "ava/core/result.h"
#include "ava/session/attachments.h"

#include <optional>

namespace ava::session {
class SessionStore;
} // namespace ava::session

namespace ava::app {

[[nodiscard]] ava::core::Result<std::optional<ava::session::ImageAttachmentRef>> import_clipboard_image_attachment(
    ava::session::SessionStore const& store);

} // namespace ava::app
