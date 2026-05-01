#pragma once

#include "ava/provider/provider.h"

namespace ava::provider {

class CurlCliTransport final : public Transport {
 public:
  [[nodiscard]] ava::core::Result<HttpResponse> send(const HttpRequest& request) override;
  [[nodiscard]] bool supports_streaming() const noexcept override;
  [[nodiscard]] ava::core::Result<HttpResponse> send_streaming(const HttpRequest& request, BodyChunkSink on_body_chunk,
                                                               CancelCallback cancel_requested = nullptr) override;
};

}  // namespace ava::provider
