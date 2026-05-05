#include "ava/provider/anthropic_request.h"

#include <utility>

#include "ava/provider/anthropic_request_support.h"
#include "ava/provider/anthropic_request_validation.h"

namespace ava::provider {

ava::core::Result<std::string> anthropic_request_body_json(ProviderRequest const& request)
{
  auto const messages = detail::collapse_consecutive_anthropic_roles(request.messages);
  if (auto valid = detail::validate_anthropic_request_options(request); !valid)
    return std::unexpected(std::move(valid.error()));
  if (auto valid = detail::validate_anthropic_content_parts(messages); !valid)
    return std::unexpected(std::move(valid.error()));
  if (auto valid = detail::validate_anthropic_cache_control_order(request, messages); !valid)
    return std::unexpected(std::move(valid.error()));
  return detail::anthropic_request_body_json_unchecked(request, messages);
}

}  // namespace ava::provider
