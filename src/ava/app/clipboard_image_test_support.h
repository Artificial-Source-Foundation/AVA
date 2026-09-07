#pragma once

#include "ava/debug/print_members_on.h"
#include "ava/app/clipboard_image.h"

#include <chrono>
#include <filesystem>
#include <string>

namespace ava::app::testing {

enum class ClipboardHelperStatusDisposition
{
  Accepted,
  Unavailable,
  OutputLimit,
};

// Narrow test-only injection for one absolute repository-owned helper. The
// production wl-paste/xclip selection and logical argv remain authoritative.
// Scenario/log markers use fixed fake-child argv; parent deadline/status fault
// seams are confined to this API and never enter the child environment.
class ClipboardImageTestAccess final
{
 public:
  [[nodiscard]] static ava::core::Result<std::optional<ava::session::ImageAttachmentRef>> import_with_helper(
      ava::session::SessionStore const& store, std::optional<ava::process::ProcessScopeV1> const& session_process_scope,
      std::filesystem::path const& executable, std::string scenario, std::filesystem::path const& invocation_log);
  [[nodiscard]] static ava::core::Result<bool> capture_list_after_preparation_delay(ava::process::ProcessScopeV1 const& session_process_scope,
                                                                                    std::filesystem::path const& executable, std::string scenario,
                                                                                    std::filesystem::path const& invocation_log,
                                                                                    std::chrono::milliseconds preparation_delay);
  [[nodiscard]] static ava::core::Result<ClipboardHelperStatusDisposition> classify_helper_status(ava::process::ExitStatusV1 const& status, bool output_limit);

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

}  // namespace ava::app::testing
