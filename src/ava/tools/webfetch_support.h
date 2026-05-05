#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <string_view>

#include "ava/core/result.h"

namespace ava::tools::detail {

inline constexpr std::size_t kMaxWebFetchBytes = 5 * 1024 * 1024;
inline constexpr int kMaxWebFetchTimeoutMs = 120000;

struct ValidatedWebFetchUrl {
  std::string url;
  std::string host;
  std::string port;
};

[[nodiscard]] std::string lowercase(std::string_view value);
[[nodiscard]] bool starts_with_case_insensitive(std::string_view value, std::string_view prefix);
[[nodiscard]] bool numeric_ipv4_literal_or_alias(std::string_view host);
[[nodiscard]] bool private_or_non_global_ipv4(unsigned long address);
[[nodiscard]] ava::core::Result<ValidatedWebFetchUrl> validate_webfetch_url(std::string_view url);
[[nodiscard]] ava::core::Result<std::string> validate_resolved_webfetch_host(std::string_view host);
[[nodiscard]] std::size_t normalized_webfetch_max_bytes(std::size_t requested_bytes);
[[nodiscard]] int normalized_webfetch_timeout_ms(int requested_timeout_ms);
[[nodiscard]] std::string webfetch_header_value(std::map<std::string, std::string> const& headers,
                                                std::string_view name);
[[nodiscard]] bool webfetch_looks_binary(std::string_view body, std::string_view content_type);

}  // namespace ava::tools::detail
