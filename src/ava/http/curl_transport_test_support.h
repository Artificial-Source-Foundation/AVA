#pragma once

#include "ava/http/curl_transport.h"
#include "ava/core/result.h"

#include <filesystem>

namespace ava::http::testing {

// Narrow test-only access to replace the fixed production curl executable with
// one absolute repository-owned fake. The child argv and environment policy
// remain identical to production.
class CurlTransportTestAccess final
{
 public:
  [[nodiscard]] static ava::core::VoidResult set_executable(CurlCliTransport& transport, std::filesystem::path const& executable);

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

}  // namespace ava::http::testing
