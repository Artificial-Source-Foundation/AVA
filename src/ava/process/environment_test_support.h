#pragma once

#include "ava/process/environment.h"

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace ava::process::testing {

// Test-only inspection for exact-value and shared-capture assertions. No
// production target includes this header.
class EnvironmentTestAccess final
{
 public:
  [[nodiscard]] static ava::core::Result<HostEnvironmentV1> capture_host();
  [[nodiscard]] static std::vector<EnvironmentVariableV1> const& variables(ExactEnvironmentV1 const& environment) noexcept;
  [[nodiscard]] static std::vector<EnvironmentVariableV1> const& host_variables(HostEnvironmentV1 const& environment) noexcept;
  [[nodiscard]] static bool shares_capture(HostEnvironmentV1 const& left, HostEnvironmentV1 const& right) noexcept;
  [[nodiscard]] static std::size_t encoded_size(ExactEnvironmentV1 const& environment) noexcept;
  [[nodiscard]] static std::size_t host_encoded_size(HostEnvironmentV1 const& environment) noexcept;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

}  // namespace ava::process::testing
