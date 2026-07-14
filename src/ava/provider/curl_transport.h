#pragma once

#include "ava/provider/provider.h"

namespace ava::provider {

class CurlCliTransport final : public Transport
{
 public:
  [[nodiscard]] ava::core::Result<HttpResponse> send(HttpRequest const& request) override;
  [[nodiscard]] ava::core::Result<HttpResponse> send(HttpRequest const& request, CancelCallback cancel_requested) override;
  [[nodiscard]] bool supports_streaming() const noexcept override;
  [[nodiscard]] ava::core::Result<HttpResponse> send_streaming(HttpRequest const& request, BodyChunkSink on_body_chunk,
                                                               CancelCallback cancel_requested = nullptr) override;

  // No members; use print_on of base class.
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

}  // namespace ava::provider
