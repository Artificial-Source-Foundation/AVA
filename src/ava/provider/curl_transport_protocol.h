#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "ava/provider/provider.h"

namespace ava::provider::detail {

inline constexpr std::string_view kCurlStatusMarker = "\nAVA_HTTP_STATUS:";
inline constexpr std::size_t kCurlStatusTailReserve = kCurlStatusMarker.size() + 3;

[[nodiscard]] std::string curl_config_escape(std::string_view value);
[[nodiscard]] std::string build_curl_config(HttpRequest const& request, std::string const& body_path);
[[nodiscard]] ava::core::Result<HttpResponse> parse_curl_output(std::string output, bool include_response_headers);

}  // namespace ava::provider::detail
