#pragma once

#include "ava/process/scope.h"
#include "ava/session/attachments.h"
#include "ava/core/result.h"

#include <optional>

namespace ava::session {
class SessionStore;
}  // namespace ava::session

namespace ava::app {

[[nodiscard]] ava::core::Result<std::optional<ava::session::ImageAttachmentRef>> import_clipboard_image_attachment(
    ava::session::SessionStore const& store, std::optional<ava::process::ProcessScopeV1> const& session_process_scope);

}  // namespace ava::app
