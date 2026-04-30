#pragma once

#include <vector>

#include "ava/provider/provider.h"

namespace ava::tests {

class FakeTransport final : public ava::provider::Transport {
 public:
  explicit FakeTransport(std::vector<ava::provider::HttpResponse> responses);
  [[nodiscard]] ava::core::Result<ava::provider::HttpResponse> send(const ava::provider::HttpRequest& request) override;
  [[nodiscard]] const std::vector<ava::provider::HttpRequest>& requests() const noexcept;

 private:
  std::vector<ava::provider::HttpResponse> responses_;
  std::vector<ava::provider::HttpRequest> requests_;
};

}  // namespace ava::tests
