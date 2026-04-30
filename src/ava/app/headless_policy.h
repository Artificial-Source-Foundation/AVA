#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "ava/core/result.h"
#include "ava/permissions/permission.h"

namespace ava::app {

struct HeadlessPermissionPolicyOptions {
  bool allow_read_only = false;
  std::vector<std::string> allowed_tools;
};

[[nodiscard]] ava::core::VoidResult add_headless_allow_policy(HeadlessPermissionPolicyOptions& options,
                                                              std::string_view value);
[[nodiscard]] ava::core::VoidResult add_headless_allowed_tools(HeadlessPermissionPolicyOptions& options,
                                                               std::string_view value);
[[nodiscard]] ava::permissions::PermissionResolver build_headless_permission_resolver(
    HeadlessPermissionPolicyOptions options);

}  // namespace ava::app
