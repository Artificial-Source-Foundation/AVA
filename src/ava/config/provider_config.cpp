#include "sys.h"
#include "ava/config/provider_config.h"
#include "ava/config/provider_profiles.h"
#include "ava/core/json.h"
#include "ava/core/strict_json.h"

#include <array>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>
#include <fcntl.h>
#include <nlohmann/json.hpp>
#include <sys/stat.h>
#include <unistd.h>

namespace ava::config {
namespace {

using Json = nlohmann::json;

constexpr std::size_t kMaxProviderIdBytes = 64;
constexpr std::size_t kMaxDisplayNameBytes = 128;
constexpr std::size_t kMaxBaseUrlBytes = 2048;
constexpr std::size_t kMaxRequestPathBytes = 1024;
constexpr std::size_t kMaxApiKeyEnvBytes = 128;
constexpr std::size_t kMaxStrictJsonDepth = 8;

class ScopedFd
{
 public:
  explicit ScopedFd(int fd) noexcept : fd_(fd) { }
  ScopedFd(ScopedFd const&) = delete;
  ScopedFd& operator=(ScopedFd const&) = delete;
  ScopedFd(ScopedFd&& other) noexcept : fd_(std::exchange(other.fd_, -1)) { }
  ScopedFd& operator=(ScopedFd&& other) noexcept
  {
    if (this != &other)
    {
      close_if_open();
      fd_ = std::exchange(other.fd_, -1);
    }
    return *this;
  }
  ~ScopedFd() { close_if_open(); }

  [[nodiscard]] int get() const noexcept { return fd_; }

 private:
  void close_if_open() noexcept
  {
    if (fd_ >= 0)
      static_cast<void>(::close(fd_));
  }

  int fd_ = -1;
};

ava::core::Error config_error(std::string message, std::string_view field = {})
{
  auto error = ava::core::Error(ava::core::ErrorCategory::Configuration, std::move(message));
  if (!field.empty())
    error.with_context("field", std::string(field));
  return error;
}

ava::core::Error path_error(ava::core::ErrorCategory category, std::string message, std::filesystem::path const& path, std::string_view operation = {})
{
  auto error = ava::core::Error(category, std::move(message));
  error.with_context("path", path.string());
  if (!operation.empty())
    error.with_context("operation", std::string(operation));
  return error;
}

std::string errno_message()
{
  return std::generic_category().message(errno);
}

bool has_control_byte(std::string_view value) noexcept
{
  for (char const ch : value)
  {
    auto const byte = static_cast<unsigned char>(ch);
    if (byte < 0x20 || byte == 0x7f)
      return true;
  }
  return false;
}

bool is_hex_digit(char ch) noexcept
{
  auto const byte = static_cast<unsigned char>(ch);
  return std::isxdigit(byte) != 0;
}

int hex_value(char ch) noexcept
{
  auto const byte = static_cast<unsigned char>(ch);
  if (byte >= '0' && byte <= '9')
    return byte - '0';
  if (byte >= 'a' && byte <= 'f')
    return byte - 'a' + 10;
  if (byte >= 'A' && byte <= 'F')
    return byte - 'A' + 10;
  return -1;
}

// Reject percent-encodings that can smuggle separators or dot-segments after decode.
bool has_encoded_separator_or_dot_ambiguity(std::string_view value) noexcept
{
  for (std::size_t i = 0; i < value.size(); ++i)
  {
    if (value[i] != '%')
      continue;
    if (i + 2 >= value.size() || !is_hex_digit(value[i + 1]) || !is_hex_digit(value[i + 2]))
      return true;
    int const hi = hex_value(value[i + 1]);
    int const lo = hex_value(value[i + 2]);
    if (hi < 0 || lo < 0)
      return true;
    auto const decoded = static_cast<unsigned char>((hi << 4) | lo);
    if (decoded == '.' || decoded == '/' || decoded == '\\' || decoded < 0x20 || decoded == 0x7f)
      return true;
    // Nested/overlong forms such as %25xx that reintroduce separators after a second decode.
    if (decoded == '%')
      return true;
    i += 2;
  }
  return false;
}

bool is_valid_provider_id(std::string_view id) noexcept
{
  if (id.empty() || id.size() > kMaxProviderIdBytes)
    return false;
  for (char const ch : id)
  {
    auto const byte = static_cast<unsigned char>(ch);
    bool const ok = (byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9') || ch == '_' || ch == '-';
    if (!ok)
      return false;
  }
  return true;
}

bool is_valid_shell_env_name(std::string_view name) noexcept
{
  if (name.empty() || name.size() > kMaxApiKeyEnvBytes)
    return false;
  auto const first = static_cast<unsigned char>(name.front());
  if (!((first >= 'A' && first <= 'Z') || first == '_'))
    return false;
  for (std::size_t i = 1; i < name.size(); ++i)
  {
    auto const byte = static_cast<unsigned char>(name[i]);
    bool const ok = (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') || byte == '_';
    if (!ok)
      return false;
  }
  return true;
}

bool is_valid_display_name(std::string_view name) noexcept
{
  return !name.empty() && name.size() <= kMaxDisplayNameBytes && !has_control_byte(name) && ava::core::json::is_valid_utf8(name);
}

bool is_unreserved_hostname_char(char ch) noexcept
{
  auto const byte = static_cast<unsigned char>(ch);
  return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') || ch == '-' || ch == '.' || ch == '_';
}

bool is_valid_hostname(std::string_view host) noexcept
{
  if (host.empty() || host.size() > 253 || has_control_byte(host) || host.front() == '.' || host.back() == '.')
    return false;
  if (host.find("..") != std::string_view::npos)
    return false;
  for (char const ch : host)
  {
    if (!is_unreserved_hostname_char(ch))
      return false;
  }
  return true;
}

bool is_valid_ipv4(std::string_view host) noexcept
{
  int parts = 0;
  std::size_t i = 0;
  while (i < host.size())
  {
    if (parts >= 4)
      return false;
    if (i < host.size() && host[i] == '.')
      return false;
    int value = 0;
    std::size_t digits = 0;
    while (i < host.size() && host[i] >= '0' && host[i] <= '9')
    {
      value = value * 10 + (host[i] - '0');
      if (value > 255)
        return false;
      ++digits;
      ++i;
      if (digits > 3)
        return false;
    }
    if (digits == 0)
      return false;
    ++parts;
    if (i == host.size())
      break;
    if (host[i] != '.')
      return false;
    ++i;
  }
  return parts == 4;
}

bool is_valid_ipv6_literal(std::string_view host) noexcept
{
  // Bracketed form only, e.g. [::1]. Content is hex digits and ':' plus at most one embedded IPv4.
  if (host.size() < 4 || host.front() != '[' || host.back() != ']')
    return false;
  auto const inner = host.substr(1, host.size() - 2);
  if (inner.empty() || inner.size() > 45)
    return false;
  bool saw_double_colon = false;
  std::size_t i = 0;
  while (i < inner.size())
  {
    if (inner[i] == ':')
    {
      if (i + 1 < inner.size() && inner[i + 1] == ':')
      {
        if (saw_double_colon)
          return false;
        saw_double_colon = true;
        i += 2;
        continue;
      }
      if (i == 0)
        return false;
      ++i;
      continue;
    }
    std::size_t hex_digits = 0;
    while (i < inner.size() && is_hex_digit(inner[i]))
    {
      ++hex_digits;
      ++i;
      if (hex_digits > 4)
        return false;
    }
    if (hex_digits == 0)
    {
      // Allow a single trailing/embedded IPv4 after hex groups, e.g. [::ffff:127.0.0.1].
      auto const rest = inner.substr(i);
      auto const dot = rest.find('.');
      if (dot == std::string_view::npos)
        return false;
      return is_valid_ipv4(rest);
    }
    if (i < inner.size() && inner[i] == '.')
    {
      // Back up over the decimal IPv4 that began with digits mistaken for hex.
      std::size_t start = i;
      while (start > 0 && inner[start - 1] != ':') --start;
      return is_valid_ipv4(inner.substr(start));
    }
  }
  return true;
}

bool is_localhost_host(std::string_view host) noexcept
{
  return host == "localhost" || host == "127.0.0.1" || host == "[::1]";
}

bool path_has_dot_or_empty_segment(std::string_view path) noexcept
{
  if (path.empty())
    return false;
  std::size_t begin = 0;
  while (begin <= path.size())
  {
    auto const end = path.find('/', begin);
    auto const segment = path.substr(begin, end == std::string_view::npos ? path.size() - begin : end - begin);
    // Absolute paths start with '/', producing an initial empty segment; that alone is fine.
    if (begin != 0 || !segment.empty())
    {
      if (segment.empty() || segment == "." || segment == "..")
        return true;
    }
    if (end == std::string_view::npos)
      break;
    begin = end + 1;
  }
  return false;
}

ava::core::VoidResult validate_request_path(std::string_view path, std::string_view field)
{
  if (path.empty() || path.size() > kMaxRequestPathBytes)
    return std::unexpected(config_error("provider request_path length is invalid", field));
  if (path.front() != '/')
    return std::unexpected(config_error("provider request_path must be an absolute path", field));
  if (has_control_byte(path) || path.find('\\') != std::string_view::npos || path.find('?') != std::string_view::npos ||
      path.find('#') != std::string_view::npos)
    return std::unexpected(config_error("provider request_path contains forbidden characters", field));
  if (has_encoded_separator_or_dot_ambiguity(path))
    return std::unexpected(config_error("provider request_path contains ambiguous percent-encoding", field));
  if (path_has_dot_or_empty_segment(path))
    return std::unexpected(config_error("provider request_path contains empty or dot segments", field));
  if (!ava::core::json::is_valid_utf8(path))
    return std::unexpected(config_error("provider request_path must be valid UTF-8", field));
  return {};
}

struct ParsedBaseUrl
{
  std::string canonical_base;  // scheme://host[:port][/path...] without trailing slashes
};

ava::core::Result<ParsedBaseUrl> parse_and_validate_base_url(std::string_view raw)
{
  if (raw.empty() || raw.size() > kMaxBaseUrlBytes)
    return std::unexpected(config_error("provider base_url length is invalid", "base_url"));
  if (has_control_byte(raw))
    return std::unexpected(config_error("provider base_url contains control characters", "base_url"));
  if (raw.find('\\') != std::string_view::npos)
    return std::unexpected(config_error("provider base_url must not contain backslashes", "base_url"));
  if (raw.find('?') != std::string_view::npos)
    return std::unexpected(config_error("provider base_url must not contain a query", "base_url"));
  if (raw.find('#') != std::string_view::npos)
    return std::unexpected(config_error("provider base_url must not contain a fragment", "base_url"));
  if (has_encoded_separator_or_dot_ambiguity(raw))
    return std::unexpected(config_error("provider base_url contains ambiguous percent-encoding", "base_url"));

  auto const scheme_end = raw.find("://");
  if (scheme_end == std::string_view::npos || scheme_end == 0)
    return std::unexpected(config_error("provider base_url must include an http or https scheme", "base_url"));
  auto const scheme = raw.substr(0, scheme_end);
  bool const is_https = scheme == "https";
  bool const is_http = scheme == "http";
  if (!is_https && !is_http)
    return std::unexpected(config_error("provider base_url scheme must be http or https", "base_url"));

  auto const rest = raw.substr(scheme_end + 3);
  if (rest.empty())
    return std::unexpected(config_error("provider base_url is missing a host", "base_url"));

  // Reject userinfo: any '@' in the authority section.
  auto const path_start_rel = rest.find('/');
  auto const authority = path_start_rel == std::string_view::npos ? rest : rest.substr(0, path_start_rel);
  auto const base_path = path_start_rel == std::string_view::npos ? std::string_view{} : rest.substr(path_start_rel);
  if (authority.empty())
    return std::unexpected(config_error("provider base_url is missing a host", "base_url"));
  if (authority.find('@') != std::string_view::npos)
    return std::unexpected(config_error("provider base_url must not contain userinfo", "base_url"));

  std::string_view host;
  std::string_view port;
  if (!authority.empty() && authority.front() == '[')
  {
    auto const close = authority.find(']');
    if (close == std::string_view::npos)
      return std::unexpected(config_error("provider base_url has an invalid IPv6 host", "base_url"));
    host = authority.substr(0, close + 1);
    if (close + 1 < authority.size())
    {
      if (authority[close + 1] != ':')
        return std::unexpected(config_error("provider base_url has an invalid host/port", "base_url"));
      port = authority.substr(close + 2);
    }
    if (!is_valid_ipv6_literal(host))
      return std::unexpected(config_error("provider base_url has an invalid IPv6 host", "base_url"));
  }
  else
  {
    auto const colon = authority.rfind(':');
    if (colon != std::string_view::npos)
    {
      host = authority.substr(0, colon);
      port = authority.substr(colon + 1);
    }
    else
    {
      host = authority;
    }
    if (host.empty())
      return std::unexpected(config_error("provider base_url is missing a host", "base_url"));
    // Host must not itself contain percent-encoding (authority identity).
    if (host.find('%') != std::string_view::npos)
      return std::unexpected(config_error("provider base_url host must not be percent-encoded", "base_url"));
    bool const looks_like_ipv4 = host.find_first_not_of("0123456789.") == std::string_view::npos && host.find('.') != std::string_view::npos;
    if (looks_like_ipv4)
    {
      if (!is_valid_ipv4(host))
        return std::unexpected(config_error("provider base_url has an invalid IPv4 host", "base_url"));
    }
    else if (!is_valid_hostname(host))
    {
      return std::unexpected(config_error("provider base_url has an invalid host", "base_url"));
    }
  }

  if (!port.empty())
  {
    if (port.size() > 5)
      return std::unexpected(config_error("provider base_url has an invalid port", "base_url"));
    unsigned long value = 0;
    for (char const ch : port)
    {
      if (ch < '0' || ch > '9')
        return std::unexpected(config_error("provider base_url has an invalid port", "base_url"));
      value = value * 10UL + static_cast<unsigned long>(ch - '0');
      if (value > 65535UL)
        return std::unexpected(config_error("provider base_url has an invalid port", "base_url"));
    }
    if (value == 0UL)
      return std::unexpected(config_error("provider base_url has an invalid port", "base_url"));
  }
  else if (authority.back() == ':')
  {
    return std::unexpected(config_error("provider base_url has an invalid port", "base_url"));
  }

  if (is_http && !is_localhost_host(host))
    return std::unexpected(config_error("provider base_url may use http only for localhost, 127.0.0.1, or [::1]", "base_url"));

  // Trim trailing slashes from any base path before segment checks and joining.
  // A base of scheme://host/ becomes an empty path contribution.
  std::string_view normalized_base_path = base_path;
  while (!normalized_base_path.empty() && normalized_base_path.back() == '/') normalized_base_path.remove_suffix(1);

  if (!normalized_base_path.empty())
  {
    if (auto valid = validate_request_path(normalized_base_path, "base_url"); !valid)
      return std::unexpected(std::move(valid.error()));
  }

  std::string canonical;
  canonical.reserve(raw.size());
  canonical.append(scheme);
  canonical.append("://");
  canonical.append(host);
  if (!port.empty())
  {
    canonical.push_back(':');
    canonical.append(port);
  }
  if (!normalized_base_path.empty())
    canonical.append(normalized_base_path);

  // Final trailing-slash trim of the entire base (defensive; path already trimmed).
  while (!canonical.empty() && canonical.back() == '/') canonical.pop_back();

  // Re-check the scheme://host minimum survived trimming.
  if (canonical.find("://") == std::string::npos)
    return std::unexpected(config_error("provider base_url is invalid after normalization", "base_url"));

  return ParsedBaseUrl{.canonical_base = std::move(canonical)};
}

std::string join_endpoint(std::string_view base_url, std::string_view request_path)
{
  std::string endpoint;
  endpoint.reserve(base_url.size() + request_path.size());
  endpoint.assign(base_url);
  // base_url is already trimmed of trailing slashes; request_path is absolute.
  endpoint.append(request_path);
  return endpoint;
}

ava::core::Result<ProviderProtocol> parse_protocol(Json const& value)
{
  if (!value.is_string())
    return std::unexpected(config_error("provider protocol must be a string", "protocol"));
  auto const text = value.get<std::string>();
  if (text == "openai_chat_completions")
    return ProviderProtocol::OpenAIChatCompletions;
  if (text == "openai_responses")
    return ProviderProtocol::OpenAIResponses;
  if (text == "anthropic_messages")
    return ProviderProtocol::AnthropicMessages;
  return std::unexpected(config_error("provider protocol is not supported", "protocol"));
}

ava::core::Result<ProviderAuthMode> parse_auth_mode(Json const& value)
{
  if (!value.is_string())
    return std::unexpected(config_error("provider auth must be a string", "auth"));
  auto const text = value.get<std::string>();
  if (text == "api_key")
    return ProviderAuthMode::ApiKey;
  if (text == "none")
    return ProviderAuthMode::None;
  return std::unexpected(config_error("provider auth is not supported", "auth"));
}

ava::core::VoidResult reject_unknown_fields(Json const& object, std::set<std::string> const& allowed, std::string_view context)
{
  if (!object.is_object())
    return std::unexpected(config_error(std::string(context) + " must be an object"));
  for (auto const& [key, value] : object.items())
  {
    (void)value;
    if (!allowed.contains(key))
      return std::unexpected(config_error(std::string(context) + " contains an unsupported member", key));
  }
  return {};
}

ava::core::Result<UserProviderDefinition> parse_one_provider(Json const& object)
{
  static std::set<std::string> const allowed{"id", "display_name", "protocol", "base_url", "request_path", "auth", "api_key_env", "compatibility"};
  if (auto unknown = reject_unknown_fields(object, allowed, "provider entry"); !unknown)
    return std::unexpected(std::move(unknown.error()));

  if (!object.contains("id") || !object["id"].is_string())
    return std::unexpected(config_error("provider id is required", "id"));
  auto const id = object["id"].get<std::string>();
  if (!is_valid_provider_id(id))
    return std::unexpected(config_error("provider id is invalid", "id"));

  if (!object.contains("display_name") || !object["display_name"].is_string())
    return std::unexpected(config_error("provider display_name is required", "display_name"));
  auto const display_name = object["display_name"].get<std::string>();
  if (!is_valid_display_name(display_name))
    return std::unexpected(config_error("provider display_name is invalid", "display_name"));

  if (!object.contains("protocol"))
    return std::unexpected(config_error("provider protocol is required", "protocol"));
  auto protocol = parse_protocol(object["protocol"]);
  if (!protocol)
    return std::unexpected(std::move(protocol.error()));

  if (!object.contains("base_url") || !object["base_url"].is_string())
    return std::unexpected(config_error("provider base_url is required", "base_url"));
  auto parsed_base = parse_and_validate_base_url(object["base_url"].get<std::string>());
  if (!parsed_base)
    return std::unexpected(std::move(parsed_base.error()));

  std::string request_path;
  if (object.contains("request_path"))
  {
    if (!object["request_path"].is_string())
      return std::unexpected(config_error("provider request_path must be a string", "request_path"));
    request_path = object["request_path"].get<std::string>();
    if (auto valid = validate_request_path(request_path, "request_path"); !valid)
      return std::unexpected(std::move(valid.error()));
  }
  else
  {
    request_path = std::string(default_request_path_for(*protocol));
  }

  ProviderAuthMode auth = ProviderAuthMode::ApiKey;
  if (object.contains("auth"))
  {
    auto parsed_auth = parse_auth_mode(object["auth"]);
    if (!parsed_auth)
      return std::unexpected(std::move(parsed_auth.error()));
    auth = *parsed_auth;
  }

  std::string api_key_env;
  bool const has_api_key_env = object.contains("api_key_env");
  if (has_api_key_env)
  {
    if (!object["api_key_env"].is_string())
      return std::unexpected(config_error("provider api_key_env must be a string", "api_key_env"));
    api_key_env = object["api_key_env"].get<std::string>();
  }

  if (auth == ProviderAuthMode::None)
  {
    if (has_api_key_env)
      return std::unexpected(config_error("provider auth none rejects api_key_env", "api_key_env"));
  }
  else
  {
    if (!has_api_key_env)
      api_key_env = default_api_key_env_for_provider_id(id);
    if (!is_valid_shell_env_name(api_key_env))
      return std::unexpected(config_error("provider api_key_env is invalid", "api_key_env"));
  }

  UserProviderCompatibility compatibility;
  if (object.contains("compatibility"))
  {
    auto const& compat = object["compatibility"];
    static std::set<std::string> const compat_allowed{"include_stream_usage"};
    if (auto unknown = reject_unknown_fields(compat, compat_allowed, "provider compatibility"); !unknown)
      return std::unexpected(std::move(unknown.error()));
    if (compat.contains("include_stream_usage"))
    {
      if (!compat["include_stream_usage"].is_boolean())
        return std::unexpected(config_error("provider compatibility.include_stream_usage must be a boolean", "include_stream_usage"));
      compatibility.include_stream_usage = compat["include_stream_usage"].get<bool>();
      if (compatibility.include_stream_usage && *protocol != ProviderProtocol::OpenAIChatCompletions)
        return std::unexpected(config_error("include_stream_usage is only valid for openai_chat_completions", "include_stream_usage"));
    }
  }

  UserProviderDefinition definition;
  definition.id = id;
  definition.display_name = display_name;
  definition.protocol = *protocol;
  definition.base_url = std::move(parsed_base->canonical_base);
  definition.request_path = std::move(request_path);
  definition.endpoint = join_endpoint(definition.base_url, definition.request_path);
  definition.auth = auth;
  definition.api_key_env = std::move(api_key_env);
  definition.compatibility = compatibility;
  return definition;
}

// nullopt => path does not exist. empty string => present zero-byte file.
ava::core::Result<std::optional<std::string>> read_providers_file(std::filesystem::path const& path)
{
  int flags = O_RDONLY | O_CLOEXEC | O_NONBLOCK;
#ifdef O_NOFOLLOW
  flags |= O_NOFOLLOW;
#endif
  ScopedFd fd(::open(path.c_str(), flags));
  if (fd.get() < 0)
  {
    if (errno == ENOENT)
      return std::optional<std::string>{};
    auto category = (errno == EACCES || errno == EPERM || errno == ELOOP) ? ava::core::ErrorCategory::PermissionDenied : ava::core::ErrorCategory::Io;
    auto error = path_error(category, "failed to open providers config", path, "open");
    error.with_context("cause", errno_message());
    return std::unexpected(std::move(error));
  }

  struct stat status{};
  if (::fstat(fd.get(), &status) != 0)
  {
    auto error = path_error(ava::core::ErrorCategory::Io, "failed to inspect providers config descriptor", path, "fstat");
    error.with_context("cause", errno_message());
    return std::unexpected(std::move(error));
  }
  if (!S_ISREG(status.st_mode))
    return std::unexpected(path_error(ava::core::ErrorCategory::Configuration, "providers config must be a regular file", path, "validate"));
  if (status.st_uid != ::geteuid())
    return std::unexpected(path_error(ava::core::ErrorCategory::PermissionDenied, "providers config must be owned by the effective user", path, "validate"));
  if (status.st_nlink != 1)
    return std::unexpected(path_error(ava::core::ErrorCategory::PermissionDenied, "providers config must not be hard-linked", path, "validate"));
  if ((status.st_mode & 0022) != 0)
    return std::unexpected(path_error(ava::core::ErrorCategory::PermissionDenied, "providers config must not be group- or world-writable", path, "validate"));
  if (status.st_size < 0 || static_cast<std::uintmax_t>(status.st_size) > kMaxUserProviderConfigBytes)
  {
    auto error = path_error(ava::core::ErrorCategory::Configuration, "providers config is too large", path, "validate");
    error.with_context("max_bytes", std::to_string(kMaxUserProviderConfigBytes));
    return std::unexpected(std::move(error));
  }

  std::string content;
  content.reserve(static_cast<std::size_t>(status.st_size));
  std::array<char, 4096> buffer{};
  for (;;)
  {
    auto const count = ::read(fd.get(), buffer.data(), buffer.size());
    if (count == 0)
      break;
    if (count < 0)
    {
      if (errno == EINTR)
        continue;
      auto error = path_error(ava::core::ErrorCategory::Io, "failed to read providers config", path, "read");
      error.with_context("cause", errno_message());
      return std::unexpected(std::move(error));
    }
    if (static_cast<std::size_t>(count) > kMaxUserProviderConfigBytes - content.size())
    {
      auto error = path_error(ava::core::ErrorCategory::Configuration, "providers config is too large", path, "read");
      error.with_context("max_bytes", std::to_string(kMaxUserProviderConfigBytes));
      return std::unexpected(std::move(error));
    }
    content.append(buffer.data(), static_cast<std::size_t>(count));
  }
  return std::optional<std::string>(std::move(content));
}

}  // namespace

std::string_view to_string(ProviderProtocol protocol) noexcept
{
  switch (protocol)
  {
    case ProviderProtocol::OpenAIChatCompletions:
      return "openai_chat_completions";
    case ProviderProtocol::OpenAIResponses:
      return "openai_responses";
    case ProviderProtocol::AnthropicMessages:
      return "anthropic_messages";
  }
  return "unknown";
}

std::string_view to_string(ProviderAuthMode auth) noexcept
{
  switch (auth)
  {
    case ProviderAuthMode::ApiKey:
      return "api_key";
    case ProviderAuthMode::None:
      return "none";
  }
  return "unknown";
}

std::string_view default_request_path_for(ProviderProtocol protocol) noexcept
{
  switch (protocol)
  {
    case ProviderProtocol::OpenAIChatCompletions:
      return "/v1/chat/completions";
    case ProviderProtocol::OpenAIResponses:
      return "/v1/responses";
    case ProviderProtocol::AnthropicMessages:
      return "/v1/messages";
  }
  return "/v1/chat/completions";
}

std::string default_api_key_env_for_provider_id(std::string_view provider_id)
{
  std::string key;
  key.reserve(provider_id.size() + std::string_view("_API_KEY").size());
  for (char const ch : provider_id)
  {
    auto const uch = static_cast<unsigned char>(ch);
    if (std::isalnum(uch) != 0)
      key.push_back(static_cast<char>(std::toupper(uch)));
    else if (ch == '-' || ch == '_')
      key.push_back('_');
  }
  key += "_API_KEY";
  return key;
}

ava::core::Result<std::vector<UserProviderDefinition>> parse_user_provider_definitions(std::string_view content)
{
  if (content.size() > kMaxUserProviderConfigBytes)
    return std::unexpected(config_error("providers config is too large"));

  auto const strict = ava::core::validate_strict_json(content, kMaxStrictJsonDepth);
  if (strict != ava::core::StrictJsonStatus::Valid)
  {
    if (strict == ava::core::StrictJsonStatus::DuplicateObjectKey)
      return std::unexpected(config_error("providers config contains a duplicate member"));
    if (strict == ava::core::StrictJsonStatus::NestingTooDeep)
      return std::unexpected(config_error("providers config JSON nesting is too deep"));
    return std::unexpected(config_error("providers config is not valid bounded JSON"));
  }

  Json root;
  try
  {
    root = Json::parse(content);
  }
  catch (...)
  {
    return std::unexpected(config_error("providers config is not valid JSON"));
  }

  static std::set<std::string> const root_allowed{"version", "providers"};
  if (auto unknown = reject_unknown_fields(root, root_allowed, "providers config"); !unknown)
    return std::unexpected(std::move(unknown.error()));

  if (!root.contains("version") || !root["version"].is_number_integer() || root["version"].get<long long>() != 1)
    return std::unexpected(config_error("providers config requires version 1", "version"));
  if (!root.contains("providers") || !root["providers"].is_array())
    return std::unexpected(config_error("providers config requires a providers array", "providers"));

  auto const& providers = root["providers"];
  if (providers.size() > kMaxUserProviders)
    return std::unexpected(config_error("providers config exceeds the maximum number of providers", "providers"));

  std::vector<UserProviderDefinition> definitions;
  definitions.reserve(providers.size());
  std::set<std::string> seen_ids;
  for (auto const& entry : providers)
  {
    if (!entry.is_object())
      return std::unexpected(config_error("provider entry must be an object", "providers"));
    auto parsed = parse_one_provider(entry);
    if (!parsed)
      return std::unexpected(std::move(parsed.error()));
    if (!seen_ids.insert(parsed->id).second)
      return std::unexpected(config_error("providers config contains a duplicate provider id", "id").with_context("provider_id", parsed->id));
    definitions.push_back(std::move(*parsed));
  }
  return definitions;
}

ava::core::Result<std::vector<UserProviderDefinition>> load_user_provider_definitions(XdgPaths const& paths)
{
  auto const& path = paths.providers_file;
  if (path.empty())
    return std::unexpected(config_error("providers config path is not configured", "providers_file"));

  auto content = read_providers_file(path);
  if (!content)
    return std::unexpected(std::move(content.error()));
  if (!*content)
    return std::vector<UserProviderDefinition>{};

  auto parsed = parse_user_provider_definitions(**content);
  if (!parsed)
  {
    parsed.error().with_context("path", path.string());
    return std::unexpected(std::move(parsed.error()));
  }
  return parsed;
}

ava::core::VoidResult validate_user_provider_ids_against_reserved(std::span<UserProviderDefinition const> definitions,
                                                                  std::span<std::string_view const> reserved_provider_ids)
{
  for (auto const& definition : definitions)
  {
    for (auto const reserved : reserved_provider_ids)
    {
      if (definition.id == reserved)
      {
        return std::unexpected(config_error("user provider id collides with a reserved provider id", "id").with_context("provider_id", definition.id));
      }
    }
  }
  return {};
}

ava::core::VoidResult validate_user_provider_ids_against_builtins(std::span<UserProviderDefinition const> definitions)
{
  auto const builtins = builtin_provider_profiles();
  std::vector<std::string_view> reserved;
  reserved.reserve(builtins.size());
  for (auto const& profile : builtins) reserved.push_back(profile.provider_id);
  return validate_user_provider_ids_against_reserved(definitions, reserved);
}

}  // namespace ava::config
