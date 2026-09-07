#pragma once

#include "ava/http/transport.h"
#include "ava/process/scope.h"

#include <string>

namespace ava::http {

namespace testing {
class CurlTransportTestAccess;
}  // namespace testing

class CurlCliTransport final : public Transport
{
 public:
  CurlCliTransport() = delete;
  explicit CurlCliTransport(ava::process::ProcessScopeV1 parent_scope);

  [[nodiscard]] ava::core::Result<HttpResponse> send(HttpRequest const& request) override;
  [[nodiscard]] ava::core::Result<HttpResponse> send(HttpRequest const& request, CancelCallback cancel_requested) override;
  [[nodiscard]] bool supports_streaming() const noexcept override;
  [[nodiscard]] ava::core::Result<HttpResponse> send_streaming(HttpRequest const& request, BodyChunkSink on_body_chunk,
                                                               CancelCallback cancel_requested = nullptr) override;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT

 private:
  ava::process::ProcessScopeV1 parent_scope_;
  // Production always leaves this empty and resolves `curl` from the closed
  // ava-curl-v1 PATH. Tests may install one absolute repository-owned helper.
  std::string test_executable_;

  friend class testing::CurlTransportTestAccess;
};

}  // namespace ava::http
