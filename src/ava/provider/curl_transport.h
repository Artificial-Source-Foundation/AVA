#pragma once

#include "ava/provider/provider.h"

namespace ava::provider {

class CurlCliTransport final : public Transport {
 public:
  [[nodiscard]] ava::core::Result<HttpResponse> send(const HttpRequest& request) override;
};

}  // namespace ava::provider
