#pragma once

#include "ava/debug/print_members_on.h"
#include "ava/app/clipboard_image.h"

#include <filesystem>
#include <string>

namespace ava::app::testing {

// Narrow test-only injection for one absolute repository-owned helper. The
// production wl-paste/xclip selection and logical argv remain authoritative;
// the scenario and log path are delivered only through fixed fake-child argv.
class ClipboardImageTestAccess final
{
 public:
  [[nodiscard]] static ava::core::Result<std::optional<ava::session::ImageAttachmentRef>> import_with_helper(
      ava::session::SessionStore const& store, std::optional<ava::process::ProcessScopeV1> const& session_process_scope,
      std::filesystem::path const& executable, std::string scenario, std::filesystem::path const& invocation_log);

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

}  // namespace ava::app::testing
