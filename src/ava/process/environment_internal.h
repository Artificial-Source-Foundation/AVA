#pragma once

#include "ava/process/environment.h"

#include <optional>
#include <string_view>
#include <vector>

namespace ava::process::detail {

// Private bridge used only by the process policy and launch backends. Keeping
// value access out of environment.h prevents callers from treating an exact
// environment capability as a reusable raw environment vector.
class EnvironmentAccess final
{
 public:
  [[nodiscard]] static ava::core::Result<HostEnvironmentV1> capture_host();
  [[nodiscard]] static std::optional<std::string_view> host_value(HostEnvironmentV1 const& host, std::string_view name) noexcept;
  [[nodiscard]] static std::vector<EnvironmentVariableV1> const& host_variables(HostEnvironmentV1 const& host) noexcept;

  [[nodiscard]] static ava::core::Result<ExactEnvironmentV1> mint(EnvironmentProfileV1 profile, ProcessRoleV1 role,
                                                                  std::vector<EnvironmentVariableV1> variables);
  [[nodiscard]] static std::vector<EnvironmentVariableV1> const& variables(ExactEnvironmentV1 const& environment) noexcept;
  [[nodiscard]] static bool revalidate(ExactEnvironmentV1 const& environment) noexcept;
  [[nodiscard]] static bool matches_common_launch(ExactEnvironmentV1 const& environment, ProcessRoleV1 role) noexcept;
  [[nodiscard]] static bool matches_secure_adoption(ExactEnvironmentV1 const& environment, ProcessRoleV1 role) noexcept;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

}  // namespace ava::process::detail
