#pragma once

#include "ava/provider/provider.h"

#include <vector>

namespace ava::tests {

class FakeTransport final : public ava::provider::Transport
{
 public:
  explicit FakeTransport(std::vector<ava::provider::HttpResponse> responses);
  [[nodiscard]] ava::core::Result<ava::provider::HttpResponse> send(ava::provider::HttpRequest const& request) override;
  [[nodiscard]] std::vector<ava::provider::HttpRequest> const& requests() const noexcept;

 private:
  std::vector<ava::provider::HttpResponse> responses_;
  std::vector<ava::provider::HttpRequest> requests_;
};

}  // namespace ava::tests
