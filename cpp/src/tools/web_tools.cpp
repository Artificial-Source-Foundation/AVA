#include "ava/tools/web_tools.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <iomanip>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ava/core/string_utils.hpp"
#include "ava/tools/output_fallback.hpp"
#include "ava/tools/path_guard.hpp"
#include "shell_runner.hpp"

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#endif

namespace ava::tools {
namespace {

constexpr std::size_t kDefaultFetchMaxLength = 50000;
constexpr std::size_t kMinDownloadBytes = 64 * 1024;
constexpr std::size_t kMaxDownloadBytes = 1024 * 1024;
constexpr std::uint64_t kFetchTimeoutMs = 30000;
constexpr std::uint64_t kSearchTimeoutMs = 20000;
constexpr std::size_t kWebFetchOutputBytes = 30 * 1024;
constexpr std::size_t kWebSearchOutputBytes = 20 * 1024;
constexpr std::size_t kWebSearchMaxResults = 20;
constexpr std::size_t kWebSearchDefaultResults = 5;

constexpr std::string_view kStatusMarker = "__AVA_STATUS__:";
constexpr std::string_view kContentTypeMarker = "__AVA_CONTENT_TYPE__:";

[[nodiscard]] std::string shell_single_quote(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size() + 2);
  escaped.push_back('\'');
  for(const auto ch : value) {
    if(ch == '\'') {
      escaped += "'\\''";
    } else {
      escaped.push_back(ch);
    }
  }
  escaped.push_back('\'');
  return escaped;
}

[[nodiscard]] std::size_t response_byte_limit(std::size_t max_length) {
  const auto calculated = max_length * 4 + 8192;
  return std::max(kMinDownloadBytes, std::min(kMaxDownloadBytes, calculated));
}

[[nodiscard]] bool starts_with_case_insensitive(std::string_view value, std::string_view prefix) {
  if(value.size() < prefix.size()) {
    return false;
  }
  for(std::size_t index = 0; index < prefix.size(); ++index) {
    if(ava::core::lowercase_ascii_char(static_cast<unsigned char>(value[index])) !=
       ava::core::lowercase_ascii_char(static_cast<unsigned char>(prefix[index]))) {
      return false;
    }
  }
  return true;
}

struct ParsedUrl {
  std::string scheme;
  std::string host;
  std::uint16_t port{0};
  std::string path_and_query;
};

[[nodiscard]] std::optional<std::uint16_t> parse_port(const std::string& value) {
  if(value.empty()) {
    return std::nullopt;
  }
  std::uint32_t parsed = 0;
  for(const auto ch : value) {
    if(!std::isdigit(static_cast<unsigned char>(ch))) {
      return std::nullopt;
    }
    parsed = parsed * 10 + static_cast<std::uint32_t>(ch - '0');
    if(parsed > 65535) {
      return std::nullopt;
    }
  }
  if(parsed == 0) {
    return std::nullopt;
  }
  return static_cast<std::uint16_t>(parsed);
}

[[nodiscard]] std::optional<ParsedUrl> parse_url(const std::string& url, std::string* error_out = nullptr) {
  const auto scheme_separator = url.find("://");
  if(scheme_separator == std::string::npos) {
    if(error_out != nullptr) {
      *error_out = "URL must include a scheme";
    }
    return std::nullopt;
  }

  ParsedUrl parsed;
  parsed.scheme = ava::core::lowercase_ascii(url.substr(0, scheme_separator));
  if(parsed.scheme != "http" && parsed.scheme != "https") {
    if(error_out != nullptr) {
      *error_out = "Only http:// and https:// URLs are allowed";
    }
    return std::nullopt;
  }

  const auto authority_start = scheme_separator + 3;
  const auto authority_end = url.find_first_of("/?#", authority_start);
  const auto authority_length = authority_end == std::string::npos ? std::string::npos : authority_end - authority_start;
  const auto authority = url.substr(authority_start, authority_length);
  if(authority.empty()) {
    if(error_out != nullptr) {
      *error_out = "URL has no host";
    }
    return std::nullopt;
  }
  if(authority.find('%') != std::string::npos) {
    if(error_out != nullptr) {
      *error_out = "percent-encoded URL hosts are not supported";
    }
    return std::nullopt;
  }
  if(authority.find('@') != std::string::npos) {
    if(error_out != nullptr) {
      *error_out = "userinfo in URL is not supported";
    }
    return std::nullopt;
  }

  std::string host;
  std::string port_string;
  if(authority.front() == '[') {
    const auto closing = authority.find(']');
    if(closing == std::string::npos) {
      if(error_out != nullptr) {
        *error_out = "invalid IPv6 host syntax";
      }
      return std::nullopt;
    }
    host = authority.substr(1, closing - 1);
    if(closing + 1 < authority.size()) {
      if(authority[closing + 1] != ':') {
        if(error_out != nullptr) {
          *error_out = "invalid host:port syntax";
        }
        return std::nullopt;
      }
      port_string = authority.substr(closing + 2);
    }
  } else {
    const auto colon = authority.rfind(':');
    if(colon != std::string::npos && authority.find(':') == colon) {
      host = authority.substr(0, colon);
      port_string = authority.substr(colon + 1);
    } else {
      host = authority;
    }
  }

  if(host.empty()) {
    if(error_out != nullptr) {
      *error_out = "URL has no host";
    }
    return std::nullopt;
  }

  parsed.host = ava::core::lowercase_ascii(host);
  if(const auto maybe_port = parse_port(port_string); maybe_port.has_value()) {
    parsed.port = *maybe_port;
  } else if(!port_string.empty()) {
    if(error_out != nullptr) {
      *error_out = "invalid URL port";
    }
    return std::nullopt;
  } else {
    parsed.port = parsed.scheme == "https" ? static_cast<std::uint16_t>(443) : static_cast<std::uint16_t>(80);
  }

  if(authority_end == std::string::npos) {
    parsed.path_and_query = "/";
  } else {
    parsed.path_and_query = url.substr(authority_end);
    if(parsed.path_and_query.empty()) {
      parsed.path_and_query = "/";
    }
  }
  return parsed;
}

#if !defined(_WIN32)

[[nodiscard]] bool is_private_ipv4(const in_addr& addr) {
  const auto* octets = reinterpret_cast<const std::uint8_t*>(&addr.s_addr);
  const auto a = octets[0];
  const auto b = octets[1];

  if(a == 127 || a == 10 || a == 0) {
    return true;
  }
  if(a == 169 && b == 254) {
    return true;
  }
  if(a == 192 && b == 168) {
    return true;
  }
  if(a == 172 && b >= 16 && b <= 31) {
    return true;
  }
  return false;
}

#endif

#if !defined(_WIN32)

[[nodiscard]] bool is_private_ipv6(const in6_addr& addr) {
  const auto* bytes = reinterpret_cast<const std::uint8_t*>(&addr.s6_addr);
  // ::1
  bool loopback = true;
  for(std::size_t idx = 0; idx < 15; ++idx) {
    if(bytes[idx] != 0) {
      loopback = false;
      break;
    }
  }
  if(loopback && bytes[15] == 1) {
    return true;
  }

  // ::
  bool unspecified = true;
  for(std::size_t idx = 0; idx < 16; ++idx) {
    if(bytes[idx] != 0) {
      unspecified = false;
      break;
    }
  }
  if(unspecified) {
    return true;
  }

  // fe80::/10 link-local
  if(bytes[0] == 0xFE && (bytes[1] & 0xC0) == 0x80) {
    return true;
  }

  // fc00::/7 unique local
  if((bytes[0] & 0xFE) == 0xFC) {
    return true;
  }

  // ::ffff:x.x.x.x (IPv4 mapped)
  bool mapped = true;
  for(std::size_t idx = 0; idx < 10; ++idx) {
    if(bytes[idx] != 0) {
      mapped = false;
      break;
    }
  }
  if(mapped && bytes[10] == 0xFF && bytes[11] == 0xFF) {
    in_addr mapped_ipv4{};
    std::memcpy(&mapped_ipv4.s_addr, bytes + 12, sizeof(mapped_ipv4.s_addr));
    return is_private_ipv4(mapped_ipv4);
  }

  return false;
}

#endif

[[nodiscard]] bool is_blocked_literal_host(const std::string& host) {
  static const std::array<std::string_view, 9> blocked = {
      "localhost",
      "localhost.",
      "127.0.0.1",
      "::1",
      "0.0.0.0",
      "0:0:0:0:0:0:0:1",
      "[::1]",
      "169.254.169.254",
      "metadata.google.internal",
  };

  if(std::find(blocked.begin(), blocked.end(), host) != blocked.end()) {
    return true;
  }
  if(host.size() > 10 && host.ends_with(".localhost")) {
    return true;
  }
  return false;
}

[[nodiscard]] bool resolves_to_private_address(const ParsedUrl& parsed) {
#if defined(_WIN32)
  (void)parsed;
  return false;
#else
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  hints.ai_flags = AI_ADDRCONFIG;

  addrinfo* result = nullptr;
  const auto port = std::to_string(parsed.port);
  if(getaddrinfo(parsed.host.c_str(), port.c_str(), &hints, &result) != 0 || result == nullptr) {
    return false;
  }

  bool blocked = false;
  for(auto* entry = result; entry != nullptr; entry = entry->ai_next) {
    if(entry->ai_family == AF_INET) {
      auto* address = reinterpret_cast<sockaddr_in*>(entry->ai_addr);
      if(address != nullptr && is_private_ipv4(address->sin_addr)) {
        blocked = true;
        break;
      }
    } else if(entry->ai_family == AF_INET6) {
      auto* address = reinterpret_cast<sockaddr_in6*>(entry->ai_addr);
      if(address != nullptr && is_private_ipv6(address->sin6_addr)) {
        blocked = true;
        break;
      }
    }
  }

  freeaddrinfo(result);
  return blocked;
#endif
}

[[nodiscard]] bool is_private_or_loopback_literal(const std::string& host) {
  const auto lower_host = ava::core::lowercase_ascii(host);
  const auto looks_like_ipv6 = lower_host.find(':') != std::string::npos;
  if(lower_host == "localhost" || lower_host == "localhost." || lower_host == "127.0.0.1" ||
     lower_host.starts_with("127.") || lower_host == "0.0.0.0" || lower_host.starts_with("10.") ||
     lower_host.starts_with("192.168.") || lower_host.starts_with("169.254.") || lower_host == "::1" ||
     lower_host == "0:0:0:0:0:0:0:1" || (looks_like_ipv6 && lower_host.starts_with("fe80:")) ||
     (looks_like_ipv6 && (lower_host.starts_with("fc") || lower_host.starts_with("fd")))) {
    return true;
  }

  if(lower_host.starts_with("172.")) {
    const auto second_start = std::string_view{"172."}.size();
    const auto second_end = lower_host.find('.', second_start);
    if(second_end != std::string::npos) {
      const auto second_octet = lower_host.substr(second_start, second_end - second_start);
      if(std::all_of(second_octet.begin(), second_octet.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; })) {
        int parsed = 0;
        for(const auto ch : second_octet) {
          parsed = (parsed * 10) + static_cast<int>(ch - '0');
          if(parsed > 255) {
            return true;
          }
        }
        if(parsed >= 16 && parsed <= 31) {
          return true;
        }
      }
    }
  }

#if defined(_WIN32)
  return false;
#else
  in_addr ipv4{};
  if(inet_pton(AF_INET, host.c_str(), &ipv4) == 1) {
    return is_private_ipv4(ipv4);
  }

  in6_addr ipv6{};
  if(inet_pton(AF_INET6, host.c_str(), &ipv6) == 1) {
    return is_private_ipv6(ipv6);
  }
  return false;
#endif
}

void validate_web_url_or_throw(const std::string& url) {
  std::string parse_error;
  const auto parsed = parse_url(url, &parse_error);
  if(!parsed.has_value()) {
    throw std::runtime_error(parse_error.empty() ? "invalid URL" : parse_error);
  }

  if(is_blocked_literal_host(parsed->host)) {
    throw std::runtime_error("URL blocked: private/internal network address (" + parsed->host + ")");
  }

  if(is_private_or_loopback_literal(parsed->host)) {
    throw std::runtime_error("URL blocked: private/internal network address (" + parsed->host + ")");
  }

  if(resolves_to_private_address(*parsed)) {
    throw std::runtime_error("URL blocked: host resolves to private/internal network address");
  }
}

[[nodiscard]] std::size_t clamp_positive_arg(
    const nlohmann::json& args,
    const char* key,
    std::size_t fallback,
    std::size_t max_value
) {
  if(!args.contains(key)) {
    return fallback;
  }

  const auto& value = args.at(key);
  if(!value.is_number_integer() && !value.is_number_unsigned()) {
    throw std::runtime_error(std::string(key) + " must be a positive integer");
  }

  const auto parsed = value.get<std::int64_t>();
  if(parsed <= 0) {
    throw std::runtime_error(std::string(key) + " must be a positive integer");
  }
  return static_cast<std::size_t>(std::min<std::int64_t>(parsed, static_cast<std::int64_t>(max_value)));
}

[[nodiscard]] std::string replace_all_copy(
    std::string value,
    std::string_view from,
    std::string_view to
) {
  std::size_t pos = 0;
  while((pos = value.find(from, pos)) != std::string::npos) {
    value.replace(pos, from.size(), to);
    pos += to.size();
  }
  return value;
}

[[nodiscard]] std::string strip_html(const std::string& html) {
  static const std::regex script_re(R"(<script[^>]*>[\s\S]*?</script>)", std::regex::icase);
  static const std::regex style_re(R"(<style[^>]*>[\s\S]*?</style>)", std::regex::icase);
  static const std::regex block_re(R"(<(?:br|/p|/div|/li|/tr|/h[1-6])[^>]*>)", std::regex::icase);
  static const std::regex tags_re(R"(<[^>]+>)", std::regex::icase);
  static const std::regex blank_lines_re(R"(\n{3,})");

  auto text = std::regex_replace(html, script_re, "");
  text = std::regex_replace(text, style_re, "");
  text = std::regex_replace(text, block_re, "\n");
  text = std::regex_replace(text, tags_re, "");

  text = replace_all_copy(std::move(text), "&amp;", "&");
  text = replace_all_copy(std::move(text), "&lt;", "<");
  text = replace_all_copy(std::move(text), "&gt;", ">");
  text = replace_all_copy(std::move(text), "&quot;", "\"");
  text = replace_all_copy(std::move(text), "&#39;", "'");
  text = replace_all_copy(std::move(text), "&apos;", "'");
  text = replace_all_copy(std::move(text), "&nbsp;", " ");
  text = std::regex_replace(text, blank_lines_re, "\n\n");
  return ava::core::trim_copy(std::move(text));
}

[[nodiscard]] std::string decode_html_entities_basic(std::string value) {
  value = replace_all_copy(std::move(value), "&amp;", "&");
  value = replace_all_copy(std::move(value), "&lt;", "<");
  value = replace_all_copy(std::move(value), "&gt;", ">");
  value = replace_all_copy(std::move(value), "&quot;", "\"");
  value = replace_all_copy(std::move(value), "&#39;", "'");
  value = replace_all_copy(std::move(value), "&apos;", "'");
  value = replace_all_copy(std::move(value), "&nbsp;", " ");
  return value;
}

[[nodiscard]] std::pair<std::string, bool> truncate_utf8_bytes(const std::string& input, std::size_t max_bytes) {
  if(input.size() <= max_bytes) {
    return {input, false};
  }

  std::size_t cut = max_bytes;
  while(cut > 0 && (static_cast<unsigned char>(input[cut]) & 0xC0) == 0x80) {
    --cut;
  }
  if(cut == 0) {
    cut = max_bytes;
  }
  return {input.substr(0, cut) + "[truncated]", true};
}

[[nodiscard]] std::string url_encode_query_component(std::string_view input) {
  std::ostringstream out;
  for(const auto ch_raw : input) {
    const auto ch = static_cast<unsigned char>(ch_raw);
    if(std::isalnum(ch) != 0 || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
      out << static_cast<char>(ch);
    } else if(ch == ' ') {
      out << '+';
    } else {
      out << '%' << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(ch)
          << std::nouppercase << std::dec;
    }
  }
  return out.str();
}

[[nodiscard]] std::string percent_decode(std::string_view input) {
  std::string output;
  output.reserve(input.size());
  for(std::size_t index = 0; index < input.size(); ++index) {
    const auto ch = input[index];
    if(ch == '+' ) {
      output.push_back(' ');
      continue;
    }
    if(ch == '%' && index + 2 < input.size()) {
      const auto hi = input[index + 1];
      const auto lo = input[index + 2];
      if(std::isxdigit(static_cast<unsigned char>(hi)) != 0 && std::isxdigit(static_cast<unsigned char>(lo)) != 0) {
        const auto hex = std::string{hi, lo};
        const auto decoded = static_cast<char>(std::strtol(hex.c_str(), nullptr, 16));
        output.push_back(decoded);
        index += 2;
        continue;
      }
    }
    output.push_back(ch);
  }
  return output;
}

struct CurlFetchResult {
  int exit_code{1};
  std::string body;
  int status_code{0};
  std::string content_type;
  std::string raw_output;
};

[[nodiscard]] CurlFetchResult fetch_url_via_curl(
    const std::string& url,
    std::uint64_t timeout_ms,
    std::size_t byte_limit,
    const std::filesystem::path& cwd
) {
  const auto timeout_secs = std::max<std::uint64_t>(1, (timeout_ms + 999) / 1000);
  const auto write_out = std::string{"\n"} + std::string(kStatusMarker) + "%{http_code}\n" +
                         std::string(kContentTypeMarker) + "%{content_type}\n";

  const auto command =
      "if ! command -v curl >/dev/null 2>&1; then printf '%s' 'curl is not available in PATH'; exit 127; fi; "
      "curl --silent --show-error --max-time " +
      std::to_string(timeout_secs) + " --max-filesize " + std::to_string(byte_limit) +
      " --proto '=http,https' --proto-redir '=http,https' --user-agent 'ava-cpp-web/1.0' --url " +
      shell_single_quote(url) + " --write-out " + shell_single_quote(write_out);

  const auto outcome = run_shell_command(command, cwd, timeout_ms + 2000);
  CurlFetchResult result{
      .exit_code = outcome.exit_code,
      .body = outcome.output,
      .status_code = 0,
      .content_type = "",
      .raw_output = outcome.output,
  };

  const auto status_pos = outcome.output.rfind(kStatusMarker);
  const auto content_type_pos = outcome.output.rfind(kContentTypeMarker);
  if(status_pos == std::string::npos || content_type_pos == std::string::npos || content_type_pos < status_pos) {
    return result;
  }

  const auto status_start = status_pos + kStatusMarker.size();
  const auto status_end = outcome.output.find('\n', status_start);
  if(status_end == std::string::npos) {
    return result;
  }

  const auto content_type_start = content_type_pos + kContentTypeMarker.size();
  const auto content_type_end = outcome.output.find('\n', content_type_start);
  if(content_type_end == std::string::npos) {
    return result;
  }

  const auto status_text = ava::core::trim_copy(outcome.output.substr(status_start, status_end - status_start));
  try {
    result.status_code = std::stoi(status_text);
  } catch(...) {
    result.status_code = 0;
  }

  result.content_type = ava::core::trim_copy(outcome.output.substr(content_type_start, content_type_end - content_type_start));
  result.body = outcome.output.substr(0, status_pos);
  if(!result.body.empty() && result.body.back() == '\n') {
    result.body.pop_back();
  }
  return result;
}

[[nodiscard]] std::string classify_curl_failure(const CurlFetchResult& response, const std::string& url, std::uint64_t timeout_ms) {
  if(response.exit_code == 124 || response.exit_code == 137 || response.exit_code == 28) {
    return "Request timed out after " + std::to_string(timeout_ms / 1000) + " seconds: " + url;
  }
  if(response.exit_code == 127) {
    return "web_fetch requires curl to be installed in PATH";
  }
  if(response.exit_code == 63) {
    return "Response exceeded download size limit";
  }
  const auto details = ava::core::trim_copy(response.raw_output);
  if(details.empty()) {
    return "Request failed with exit code " + std::to_string(response.exit_code);
  }
  return "Request failed: " + details;
}

[[nodiscard]] std::string build_duckduckgo_url(const std::string& query) {
  return "https://duckduckgo.com/html/?q=" + url_encode_query_component(query);
}

[[nodiscard]] std::optional<std::string> extract_query_parameter(const std::string& path_and_query, std::string_view key) {
  const auto query_pos = path_and_query.find('?');
  if(query_pos == std::string::npos || query_pos + 1 >= path_and_query.size()) {
    return std::nullopt;
  }

  const auto query = path_and_query.substr(query_pos + 1);
  std::size_t start = 0;
  while(start < query.size()) {
    const auto end = query.find('&', start);
    const auto pair = query.substr(start, end == std::string::npos ? std::string::npos : end - start);
    const auto equals = pair.find('=');
    const auto name = equals == std::string::npos ? pair : pair.substr(0, equals);
    const auto value = equals == std::string::npos ? std::string{} : pair.substr(equals + 1);
    if(name == key) {
      return percent_decode(value);
    }
    if(end == std::string::npos) {
      break;
    }
    start = end + 1;
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::string> normalize_duckduckgo_href(const std::string& href) {
  auto candidate = ava::core::trim_copy(href);
  if(candidate.empty()) {
    return std::nullopt;
  }
  if(starts_with_case_insensitive(candidate, "//")) {
    candidate = "https:" + candidate;
  }

  const auto parsed = parse_url(candidate, nullptr);
  if(!parsed.has_value()) {
    return std::nullopt;
  }

  if(parsed->host == "duckduckgo.com" && starts_with_case_insensitive(parsed->path_and_query, "/l/")) {
    if(const auto uddg = extract_query_parameter(parsed->path_and_query, "uddg"); uddg.has_value()) {
      candidate = *uddg;
    }
  }

  const auto normalized = parse_url(candidate, nullptr);
  if(!normalized.has_value()) {
    return std::nullopt;
  }
  if(normalized->scheme != "http" && normalized->scheme != "https") {
    return std::nullopt;
  }
  return candidate;
}

struct WebSearchResult {
  std::string title;
  std::string url;
};

[[nodiscard]] std::vector<WebSearchResult> parse_duckduckgo_results(const std::string& html, std::size_t max_results) {
  static const std::regex result_re(
      R"(<a[^>]*class=["'][^"']*result__a[^"']*["'][^>]*href=["']([^"']+)["'][^>]*>([\s\S]*?)</a>)",
      std::regex::icase
  );
  static const std::regex tag_re(R"(<[^>]+>)", std::regex::icase);

  std::vector<WebSearchResult> out;
  std::unordered_set<std::string> seen_urls;
  for(std::sregex_iterator it(html.begin(), html.end(), result_re), end; it != end; ++it) {
    const auto href = (*it)[1].str();
    auto title = std::regex_replace((*it)[2].str(), tag_re, "");
    title = ava::core::trim_copy(decode_html_entities_basic(std::move(title)));

    const auto normalized = normalize_duckduckgo_href(href);
    if(!normalized.has_value() || title.empty()) {
      continue;
    }

    try {
      validate_web_url_or_throw(*normalized);
    } catch(...) {
      continue;
    }

    if(!seen_urls.insert(*normalized).second) {
      continue;
    }

    out.push_back(WebSearchResult{.title = title, .url = *normalized});
    if(out.size() >= max_results) {
      break;
    }
  }
  return out;
}

}  // namespace

WebFetchTool::WebFetchTool(std::filesystem::path workspace_root)
    : workspace_root_(normalize_workspace_root(std::move(workspace_root))) {}

std::string WebFetchTool::name() const {
  return "web_fetch";
}

std::string WebFetchTool::description() const {
  return "Fetch a URL and return extracted content (text or pretty-printed JSON)";
}

std::string WebFetchTool::search_hint() const {
  return "fetch download webpage URL HTTP";
}

nlohmann::json WebFetchTool::parameters() const {
  return nlohmann::json{
      {"type", "object"},
      {"required", nlohmann::json::array({"url"})},
      {"properties",
       {
           {"url", {{"type", "string"}, {"description", "The URL to fetch"}}},
           {"max_length",
            {{"type", "integer"},
             {"minimum", 1},
             {"description", "Maximum UTF-8 bytes of processed text to return (default: 50000)"}}},
       }},
  };
}

ava::types::ToolResult WebFetchTool::execute(const nlohmann::json& args) const {
  if(!args.contains("url") || !args.at("url").is_string()) {
    throw std::runtime_error("missing required field: url");
  }

  const auto url = ava::core::trim_copy(args.at("url").get<std::string>());
  if(url.empty()) {
    throw std::runtime_error("url must not be empty");
  }

  validate_web_url_or_throw(url);

  const auto max_length = clamp_positive_arg(args, "max_length", kDefaultFetchMaxLength, kMaxDownloadBytes);
  const auto byte_limit = response_byte_limit(max_length);
  const auto response = fetch_url_via_curl(url, kFetchTimeoutMs, byte_limit, workspace_root_);

  if(response.exit_code != 0) {
    return ava::types::ToolResult{
        .call_id = "",
        .content = classify_curl_failure(response, url, kFetchTimeoutMs),
        .is_error = true,
    };
  }

  std::string processed = response.body;
  const auto content_type = response.content_type.empty() ? std::string{"unknown"}
                                                           : ava::core::lowercase_ascii(response.content_type);
  if(content_type.find("application/json") != std::string::npos) {
    try {
      processed = nlohmann::json::parse(processed).dump(2);
    } catch(...) {
      // Keep raw text when JSON parsing fails.
    }
  } else if(content_type.find("text/html") != std::string::npos) {
    processed = strip_html(processed);
  }

  const auto [truncated_content, was_truncated] = truncate_utf8_bytes(processed, max_length);
  std::string meta = "URL: " + url + "\nStatus: " + std::to_string(response.status_code) + "\nContent-Type: " +
                     (response.content_type.empty() ? "unknown" : response.content_type);
  if(was_truncated) {
    meta += "\nNote: Response truncated to " + std::to_string(max_length) + " UTF-8 bytes";
  }
  if(response.status_code >= 400) {
    meta += "\nWarning: Non-success status code " + std::to_string(response.status_code);
  }

  const auto content = apply_output_fallback(name(), meta + "\n\n" + truncated_content, kWebFetchOutputBytes);
  return ava::types::ToolResult{
      .call_id = "",
      .content = content,
      .is_error = response.status_code >= 400,
  };
}

WebSearchTool::WebSearchTool(std::filesystem::path workspace_root)
    : workspace_root_(normalize_workspace_root(std::move(workspace_root))) {}

std::string WebSearchTool::name() const {
  return "web_search";
}

std::string WebSearchTool::description() const {
  return "Search the web using DuckDuckGo HTML results and return parsed snippets";
}

std::string WebSearchTool::search_hint() const {
  return "search web internet query";
}

nlohmann::json WebSearchTool::parameters() const {
  return nlohmann::json{
      {"type", "object"},
      {"required", nlohmann::json::array({"query"})},
      {"properties",
       {
           {"query", {{"type", "string"}, {"description", "Search query"}}},
           {"provider",
            {{"type", "string"},
             {"enum", nlohmann::json::array({"duckduckgo"})},
             {"description", "Search provider (default: duckduckgo)"}}},
           {"max_results",
            {{"type", "integer"},
             {"minimum", 1},
             {"maximum", static_cast<int>(kWebSearchMaxResults)},
             {"description", "Maximum number of results to return (default: 5)"}}},
       }},
  };
}

ava::types::ToolResult WebSearchTool::execute(const nlohmann::json& args) const {
  if(!args.contains("query") || !args.at("query").is_string()) {
    throw std::runtime_error("missing required field: query");
  }

  const auto query = ava::core::trim_copy(args.at("query").get<std::string>());
  if(query.empty()) {
    throw std::runtime_error("query must not be empty");
  }

  const auto provider = ava::core::lowercase_ascii(ava::core::trim_copy(args.value("provider", std::string{"duckduckgo"})));
  if(provider != "duckduckgo") {
    throw std::runtime_error("unsupported web_search provider: " + provider);
  }

  const auto max_results = clamp_positive_arg(args, "max_results", kWebSearchDefaultResults, kWebSearchMaxResults);
  const auto search_url = build_duckduckgo_url(query);
  validate_web_url_or_throw(search_url);

  const auto response = fetch_url_via_curl(search_url, kSearchTimeoutMs, 512 * 1024, workspace_root_);
  if(response.exit_code != 0) {
    return ava::types::ToolResult{
        .call_id = "",
        .content = classify_curl_failure(response, search_url, kSearchTimeoutMs),
        .is_error = true,
    };
  }

  if(response.status_code >= 400) {
    return ava::types::ToolResult{
        .call_id = "",
        .content = "web_search provider returned non-success status " + std::to_string(response.status_code) +
                   " for query: " + query,
        .is_error = true,
    };
  }

  const auto parsed = parse_duckduckgo_results(response.body, max_results);
  if(parsed.empty()) {
    return ava::types::ToolResult{
        .call_id = "",
        .content = "web_search parsed 0 results; provider format may have changed",
        .is_error = true,
    };
  }

  auto results = nlohmann::json::array();
  for(const auto& item : parsed) {
    results.push_back(nlohmann::json{{"title", item.title}, {"url", item.url}});
  }

  const auto content = apply_output_fallback(
      name(),
      nlohmann::json{{"provider", provider}, {"query", query}, {"results", results}}.dump(2),
      kWebSearchOutputBytes
  );
  return ava::types::ToolResult{
      .call_id = "",
      .content = content,
      .is_error = false,
  };
}

}  // namespace ava::tools
