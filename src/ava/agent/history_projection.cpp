#include "sys.h"
#include "ava/agent/history_projection.h"

namespace ava::agent {

bool HistoricalImagePolicy::supports_mime_type(std::string_view mime_type) const noexcept
{
  return ava::provider::is_supported_image_mime_type(mime_type);
}

bool HistoryReplayTarget::is_complete() const noexcept
{
  return !provider_id.empty() && !model_id.empty() && !api_family.empty();
}

HistoricalImagePolicy HistoryReplayTarget::image_policy() const
{
  HistoricalImagePolicy policy;
  policy.supports_images = supports_images;
  if (!supports_images)
    return policy;

  policy.limits = ava::provider::image_input_policy_for_api_family(api_family);
  return policy;
}

}  // namespace ava::agent
