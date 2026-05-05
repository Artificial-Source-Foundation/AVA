#pragma once

#include <memory>

#include "ava/provider/provider.h"

namespace ava::provider::detail {

[[nodiscard]] std::unique_ptr<StreamParser> make_default_stream_parser();
[[nodiscard]] ava::core::Result<std::vector<StreamEvent>> parse_default_provider_response(HttpResponse const& response,
                                                                                          bool stream);

}  // namespace ava::provider::detail
