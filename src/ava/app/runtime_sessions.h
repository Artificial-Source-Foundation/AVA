#pragma once

#include "ava/app/runtime.h"

#include <string_view>

namespace ava::app {

[[nodiscard]] ava::core::Result<RuntimeSession> create_runtime_session_like(RuntimeSession const& current, RuntimeOpenOptions const& base_options);
[[nodiscard]] ava::core::Result<RuntimeSession> open_runtime_session_like(RuntimeSession const& current, RuntimeOpenOptions const& base_options,
                                                                          std::string_view requested_session_id);
[[nodiscard]] ava::core::Result<RuntimeSession> create_runtime_session_at(RuntimeOpenOptions base_options, std::filesystem::path const& workspace_root,
                                                                          std::filesystem::path const& current_dir);
[[nodiscard]] ava::core::Result<RuntimeSession> open_runtime_session_at(RuntimeOpenOptions base_options, std::filesystem::path const& workspace_root,
                                                                        std::filesystem::path const& current_dir, std::string_view requested_session_id);

}  // namespace ava::app
