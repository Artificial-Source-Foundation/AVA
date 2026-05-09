#include "ava/provider/openai_provider.h"
#include "ava/provider/openai_response_parser_detail.h"

#include <memory>
#include <utility>
#include <vector>

namespace ava::provider {

OpenAIProvider::OpenAIProvider(std::string base_url) : base_url_(std::move(base_url))
{
}

std::unique_ptr<StreamParser> OpenAIProvider::create_stream_parser() const
{
  return std::make_unique<OpenAIStreamParser>();
}

ava::core::Result<std::vector<StreamEvent>> OpenAIProvider::parse_response(HttpResponse const& response, bool stream) const
{
  if (stream)
    return parse_openai_sse_response(response);
  if (response.status_code < 200 || response.status_code >= 300)
    return parse_openai_sse_response(response);
  return detail::parse_openai_non_stream_response(response.body);
}

}  // namespace ava::provider
