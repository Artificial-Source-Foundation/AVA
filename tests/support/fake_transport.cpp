#include "tests/support/fake_transport.h"
#include "ava/http/transport.h"

#include <utility>

namespace ava::tests {

FakeTransport::FakeTransport(std::vector<ava::http::HttpResponse> responses) : responses_(std::move(responses))
{
}

ava::core::Result<ava::http::HttpResponse> FakeTransport::send(ava::http::HttpRequest const& request)
{
  requests_.push_back(request);
  if (responses_.empty())
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Provider, "fake transport has no response"));
  }
  auto response = responses_.front();
  responses_.erase(responses_.begin());
  return response;
}

std::vector<ava::http::HttpRequest> const& FakeTransport::requests() const noexcept
{
  return requests_;
}

}  // namespace ava::tests
