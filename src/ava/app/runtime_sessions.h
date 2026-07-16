#pragma once

#include "ava/app/runtime.h"

#include <string_view>

namespace ava::app {

[[nodiscard]] ava::core::Result<runtime::Session> create_runtime_session_like(runtime::Session const& current, runtime::OpenOptions const& base_options);
[[nodiscard]] ava::core::Result<runtime::Session> open_runtime_session_like(runtime::Session const& current, runtime::OpenOptions const& base_options,
                                                                          std::string_view requested_session_id);
[[nodiscard]] ava::core::Result<runtime::Session> create_runtime_session_at(runtime::OpenOptions base_options, std::filesystem::path const& workspace_root,
                                                                          std::filesystem::path const& current_dir);
[[nodiscard]] ava::core::Result<runtime::Session> open_runtime_session_at(runtime::OpenOptions base_options, std::filesystem::path const& workspace_root,
                                                                        std::filesystem::path const& current_dir, std::string_view requested_session_id);

}  // namespace ava::app
