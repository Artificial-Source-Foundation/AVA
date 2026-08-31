#include "sys.h"
#include "ava/process/environment_internal.h"
#include "ava/process/environment_test_support.h"
#include "ava/core/error.h"

#include <algorithm>
#include <array>
#include <exception>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <cstdlib>
#else
extern char** environ;
#endif

namespace ava::process {
namespace {

constexpr std::array<std::string_view, 13> kCurlInheritedNames{
    "http_proxy", "https_proxy", "ftp_proxy", "all_proxy",      "no_proxy",      "HTTP_PROXY",   "HTTPS_PROXY",
    "FTP_PROXY",  "ALL_PROXY",   "NO_PROXY",  "CURL_CA_BUNDLE", "SSL_CERT_FILE", "SSL_CERT_DIR",
};

constexpr std::array<std::string_view, 39> kFixedHostNames{
    "PATH",
    "HOME",
    "USER",
    "LOGNAME",
    "SHELL",
    "TMPDIR",
    "TMP",
    "TEMP",
    "LANG",
    "LANGUAGE",
    "LC_ALL",
    "XDG_CONFIG_HOME",
    "XDG_CACHE_HOME",
    "XDG_DATA_HOME",
    "XDG_STATE_HOME",
    "XDG_RUNTIME_DIR",
    "TERM",
    "COLORTERM",
    "DISPLAY",
    "WAYLAND_DISPLAY",
    "XAUTHORITY",
    "DBUS_SESSION_BUS_ADDRESS",
    "DESKTOP_STARTUP_ID",
    "BROWSER",
    "VISUAL",
    "EDITOR",
    "http_proxy",
    "https_proxy",
    "ftp_proxy",
    "all_proxy",
    "no_proxy",
    "HTTP_PROXY",
    "HTTPS_PROXY",
    "FTP_PROXY",
    "ALL_PROXY",
    "NO_PROXY",
    "CURL_CA_BUNDLE",
    "SSL_CERT_FILE",
    "SSL_CERT_DIR",
};

constexpr std::array<std::string_view, 14> kBashNames{
    "LANG",           "LC_ALL",        "LC_CTYPE",       "TZ",     "USER", "LOGNAME", "PWD", "PATH", "HOME", "XDG_CONFIG_HOME",
    "XDG_CACHE_HOME", "XDG_DATA_HOME", "XDG_STATE_HOME", "TMPDIR",
};

ava::core::Error environment_error(std::string message)
{
  return ava::core::Error(ava::core::ErrorCategory::InvalidArgument, std::move(message));
}

ava::core::Error invalid_input_error()
{
  return environment_error("process environment input does not satisfy the closed version-1 policy");
}

ava::core::Error capture_error()
{
  return environment_error("host environment projection does not satisfy the closed version-1 bounds");
}

ava::core::Error allocation_error()
{
  return ava::core::Error(ava::core::ErrorCategory::Io, "failed to retain a bounded process environment capability");
}

bool contains_nul(std::string_view value) noexcept
{
  return value.find('\0') != std::string_view::npos;
}

bool valid_variables(std::vector<EnvironmentVariableV1> const& variables, std::size_t maximum_count) noexcept
{
  if (variables.size() > maximum_count)
    return false;

  std::size_t encoded_size = 0;
  for (std::size_t index = 0; index < variables.size(); ++index)
  {
    auto const& variable = variables[index];
    if (variable.name.empty() || variable.name.size() > kMaxEnvironmentNameBytesV1 || variable.value.size() > kMaxEnvironmentValueBytesV1 ||
        contains_nul(variable.name) || variable.name.find('=') != std::string::npos || contains_nul(variable.value))
    {
      return false;
    }
    for (std::size_t previous = 0; previous < index; ++previous)
    {
      if (variables[previous].name == variable.name)
        return false;
    }
    auto const entry_size = variable.name.size() + variable.value.size() + 2;
    if (entry_size > kMaxEnvironmentEncodedBytesV1 - encoded_size)
      return false;
    encoded_size += entry_size;
  }
  return true;
}

bool profile_matches_role(EnvironmentProfileV1 profile, ProcessRoleV1 role) noexcept
{
  switch (role)
  {
    case ProcessRoleV1::Curl:
      return profile == EnvironmentProfileV1::Curl;
    case ProcessRoleV1::Bash:
      return profile == EnvironmentProfileV1::Bash;
    case ProcessRoleV1::Plugin:
      return profile == EnvironmentProfileV1::PluginMinimal;
    case ProcessRoleV1::Mcp:
      return profile == EnvironmentProfileV1::McpExplicit;
    case ProcessRoleV1::Lsp:
      return profile == EnvironmentProfileV1::LspStrict;
    case ProcessRoleV1::Mermaid:
      return profile == EnvironmentProfileV1::Mermaid;
    case ProcessRoleV1::BrowserOpener:
      return profile == EnvironmentProfileV1::BrowserDesktop;
    case ProcessRoleV1::ClipboardHelper:
      return profile == EnvironmentProfileV1::ClipboardDesktop;
    case ProcessRoleV1::ExternalEditor:
      return profile == EnvironmentProfileV1::ExternalEditor;
  }
  return false;
}

bool profile_binds_logical_cwd(EnvironmentProfileV1 profile) noexcept
{
  switch (profile)
  {
    case EnvironmentProfileV1::Curl:
    case EnvironmentProfileV1::Bash:
    case EnvironmentProfileV1::PluginMinimal:
    case EnvironmentProfileV1::McpExplicit:
    case EnvironmentProfileV1::LspStrict:
    case EnvironmentProfileV1::Mermaid:
      return true;
    case EnvironmentProfileV1::BrowserDesktop:
    case EnvironmentProfileV1::ClipboardDesktop:
    case EnvironmentProfileV1::ExternalEditor:
      return false;
  }
  return false;
}

bool is_common_role(ProcessRoleV1 role) noexcept
{
  switch (role)
  {
    case ProcessRoleV1::Curl:
    case ProcessRoleV1::Plugin:
    case ProcessRoleV1::Mcp:
    case ProcessRoleV1::Lsp:
    case ProcessRoleV1::BrowserOpener:
    case ProcessRoleV1::ClipboardHelper:
    case ProcessRoleV1::ExternalEditor:
      return true;
    case ProcessRoleV1::Bash:
    case ProcessRoleV1::Mermaid:
      return false;
  }
  return false;
}

bool is_secure_role(ProcessRoleV1 role) noexcept
{
  return role == ProcessRoleV1::Bash || role == ProcessRoleV1::Mermaid;
}

bool is_additional_locale_name(std::string_view name) noexcept
{
  return name.size() > 3 && name.starts_with("LC_") && name != "LC_ALL";
}

bool is_sanctioned_host_name(std::string_view name) noexcept
{
  return std::ranges::find(kFixedHostNames, name) != kFixedHostNames.end() || is_additional_locale_name(name);
}

bool bytewise_less(std::string_view left, std::string_view right) noexcept
{
  return std::lexicographical_compare(left.begin(), left.end(), right.begin(), right.end(), [](char left_byte, char right_byte) {
    return static_cast<unsigned char>(left_byte) < static_cast<unsigned char>(right_byte);
  });
}

bool valid_absolute_path_value(std::string_view value) noexcept
{
  return !value.empty() && value.starts_with('/') && !contains_nul(value);
}

bool valid_path_list(std::string_view value) noexcept
{
  if (value.empty() || contains_nul(value))
    return false;
  while (true)
  {
    auto const separator = value.find(':');
    auto const component = value.substr(0, separator);
    if (!valid_absolute_path_value(component))
      return false;
    if (separator == std::string_view::npos)
      return true;
    value.remove_prefix(separator + 1);
  }
}

ava::core::Result<std::string> bounded_absolute_path(std::filesystem::path const& path)
{
  auto value = path.string();
  if (!path.is_absolute() || value.empty() || value.size() > kMaxEnvironmentValueBytesV1 || contains_nul(value))
    return std::unexpected(invalid_input_error());
  return value;
}

template <typename Builder>
ava::core::Result<ExactEnvironmentV1> build_environment(Builder&& builder)
{
  try
  {
    return std::forward<Builder>(builder)();
  }
  catch (...)
  {
    return std::unexpected(allocation_error());
  }
}

void append_if_present(std::vector<EnvironmentVariableV1>& destination, HostEnvironmentV1 const& host, std::string_view name)
{
  auto value = detail::EnvironmentAccess::host_value(host, name);
  if (value)
    destination.push_back({.name = std::string(name), .value = std::string(*value)});
}

template <std::size_t Size>
void append_if_present(std::vector<EnvironmentVariableV1>& destination, HostEnvironmentV1 const& host, std::array<std::string_view, Size> const& names)
{
  for (auto const name : names)
    append_if_present(destination, host, name);
}

void append_additional_locales(std::vector<EnvironmentVariableV1>& destination, HostEnvironmentV1 const& host)
{
  std::vector<EnvironmentVariableV1> locales;
  for (auto const& variable : detail::EnvironmentAccess::host_variables(host))
  {
    if (is_additional_locale_name(variable.name))
      locales.push_back(variable);
  }
  std::ranges::sort(locales, [](auto const& left, auto const& right) { return bytewise_less(left.name, right.name); });
  destination.insert(destination.end(), std::make_move_iterator(locales.begin()), std::make_move_iterator(locales.end()));
}

EnvironmentVariableV1 const* find_variable(std::vector<EnvironmentVariableV1> const& variables, std::string_view name) noexcept
{
  auto const found = std::ranges::find_if(variables, [name](auto const& variable) { return variable.name == name; });
  return found == variables.end() ? nullptr : &*found;
}

bool valid_logical_cwd_binding(EnvironmentProfileV1 profile, std::vector<EnvironmentVariableV1> const& variables,
                               std::optional<std::string> const& logical_cwd) noexcept
{
  auto const* pwd = find_variable(variables, "PWD");
  if (!profile_binds_logical_cwd(profile))
    return !logical_cwd && pwd == nullptr;
  if (!logical_cwd || logical_cwd->size() > kMaxEnvironmentValueBytesV1 || !valid_absolute_path_value(*logical_cwd) || pwd == nullptr ||
      pwd->value != *logical_cwd)
  {
    return false;
  }
  return (profile != EnvironmentProfileV1::Curl && profile != EnvironmentProfileV1::Mermaid) || *logical_cwd == "/";
}

}  // namespace

struct HostEnvironmentV1::Impl
{
  std::vector<EnvironmentVariableV1> variables;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

struct ExactEnvironmentV1::Impl
{
  EnvironmentProfileV1 profile = EnvironmentProfileV1::Curl;
  ProcessRoleV1 role = ProcessRoleV1::Curl;
  std::vector<EnvironmentVariableV1> variables;
  // PWD-bearing profiles retain the exact computed logical cwd out of band so
  // launch validation never has to expose or trust raw child environment data.
  std::optional<std::string> logical_cwd;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

std::string_view environment_profile_id_v1(EnvironmentProfileV1 value) noexcept
{
  switch (value)
  {
    case EnvironmentProfileV1::Curl:
      return kCurlEnvironmentProfileIdV1;
    case EnvironmentProfileV1::Bash:
      return kBashEnvironmentProfileIdV1;
    case EnvironmentProfileV1::PluginMinimal:
      return kPluginEnvironmentProfileIdV1;
    case EnvironmentProfileV1::McpExplicit:
      return kMcpEnvironmentProfileIdV1;
    case EnvironmentProfileV1::LspStrict:
      return kLspEnvironmentProfileIdV1;
    case EnvironmentProfileV1::Mermaid:
      return kMermaidEnvironmentProfileIdV1;
    case EnvironmentProfileV1::BrowserDesktop:
      return kBrowserEnvironmentProfileIdV1;
    case EnvironmentProfileV1::ClipboardDesktop:
      return kClipboardEnvironmentProfileIdV1;
    case EnvironmentProfileV1::ExternalEditor:
      return kExternalEditorEnvironmentProfileIdV1;
  }
  return "invalid";
}

std::string_view to_string(EnvironmentProfileV1 value) noexcept
{
  return environment_profile_id_v1(value);
}

HostEnvironmentV1::HostEnvironmentV1() noexcept = default;
HostEnvironmentV1::HostEnvironmentV1(std::shared_ptr<Impl const> implementation) noexcept : implementation_(std::move(implementation))
{
}
HostEnvironmentV1::HostEnvironmentV1(HostEnvironmentV1 const&) noexcept = default;
HostEnvironmentV1& HostEnvironmentV1::operator=(HostEnvironmentV1 const&) noexcept = default;
HostEnvironmentV1::HostEnvironmentV1(HostEnvironmentV1&&) noexcept = default;
HostEnvironmentV1& HostEnvironmentV1::operator=(HostEnvironmentV1&&) noexcept = default;
HostEnvironmentV1::~HostEnvironmentV1() = default;

bool HostEnvironmentV1::valid() const noexcept
{
  return static_cast<bool>(implementation_);
}

std::optional<std::string_view> HostEnvironmentV1::selector(HostEnvironmentSelectorV1 selector_value) const noexcept
{
  std::string_view name;
  switch (selector_value)
  {
    case HostEnvironmentSelectorV1::Browser:
      name = "BROWSER";
      break;
    case HostEnvironmentSelectorV1::Visual:
      name = "VISUAL";
      break;
    case HostEnvironmentSelectorV1::Editor:
      name = "EDITOR";
      break;
    default:
      return std::nullopt;
  }
  return detail::EnvironmentAccess::host_value(*this, name);
}

ExactEnvironmentV1::ExactEnvironmentV1() noexcept = default;
ExactEnvironmentV1::ExactEnvironmentV1(std::unique_ptr<Impl> implementation) noexcept : implementation_(std::move(implementation))
{
}
ExactEnvironmentV1::ExactEnvironmentV1(ExactEnvironmentV1&&) noexcept = default;
ExactEnvironmentV1& ExactEnvironmentV1::operator=(ExactEnvironmentV1&&) noexcept = default;
ExactEnvironmentV1::~ExactEnvironmentV1() = default;

bool ExactEnvironmentV1::valid() const noexcept
{
  return static_cast<bool>(implementation_);
}

ProcessRoleV1 ExactEnvironmentV1::role() const noexcept
{
  return implementation_ ? implementation_->role : static_cast<ProcessRoleV1>(-1);
}

EnvironmentProfileV1 ExactEnvironmentV1::profile() const noexcept
{
  return implementation_ ? implementation_->profile : static_cast<EnvironmentProfileV1>(-1);
}

std::string_view ExactEnvironmentV1::profile_id() const noexcept
{
  return environment_profile_id_v1(profile());
}

ava::core::Result<ExactEnvironmentV1> make_curl_environment_v1(HostEnvironmentV1 const& host)
{
  return build_environment([&]() -> ava::core::Result<ExactEnvironmentV1> {
    if (!host.valid())
      return std::unexpected(invalid_input_error());
    std::vector<EnvironmentVariableV1> variables{{"PATH", std::string(kTrustedEnvironmentPathV1)}, {"LANG", "C.UTF-8"}, {"LC_ALL", "C.UTF-8"}, {"PWD", "/"}};
    append_if_present(variables, host, kCurlInheritedNames);
    return detail::EnvironmentAccess::mint(EnvironmentProfileV1::Curl, ProcessRoleV1::Curl, std::move(variables), std::string("/"));
  });
}

ava::core::Result<ExactEnvironmentV1> validate_bash_environment_v1(std::string_view profile_id, std::filesystem::path const& logical_cwd,
                                                                   std::vector<EnvironmentVariableV1> const& imported_variables)
{
  return build_environment([&]() -> ava::core::Result<ExactEnvironmentV1> {
    if (profile_id != kBashEnvironmentProfileIdV1 || !valid_variables(imported_variables, kMaxEnvironmentEntriesV1) ||
        imported_variables.size() != kBashNames.size())
    {
      return std::unexpected(invalid_input_error());
    }
    auto cwd = bounded_absolute_path(logical_cwd);
    if (!cwd)
      return std::unexpected(std::move(cwd.error()));

    std::vector<EnvironmentVariableV1> variables;
    variables.reserve(kBashNames.size());
    for (auto const name : kBashNames)
    {
      auto const* variable = find_variable(imported_variables, name);
      if (variable == nullptr)
        return std::unexpected(invalid_input_error());
      variables.push_back(*variable);
    }

    if (variables[0].value != "C.UTF-8" || variables[1].value != "C.UTF-8" || variables[2].value != "C.UTF-8" || variables[3].value != "UTC" ||
        variables[4].value.empty() || variables[5].value.empty() || variables[6].value != *cwd || !valid_path_list(variables[7].value))
    {
      return std::unexpected(invalid_input_error());
    }
    for (std::size_t index = 8; index < variables.size(); ++index)
    {
      if (!valid_absolute_path_value(variables[index].value))
        return std::unexpected(invalid_input_error());
    }
    return detail::EnvironmentAccess::mint(EnvironmentProfileV1::Bash, ProcessRoleV1::Bash, std::move(variables), std::move(*cwd));
  });
}

ava::core::Result<ExactEnvironmentV1> make_plugin_environment_v1(std::filesystem::path const& logical_cwd)
{
  return build_environment([&]() -> ava::core::Result<ExactEnvironmentV1> {
    auto cwd = bounded_absolute_path(logical_cwd);
    if (!cwd)
      return std::unexpected(std::move(cwd.error()));
    std::vector<EnvironmentVariableV1> variables{{"PATH", std::string(kTrustedEnvironmentPathV1)}, {"LANG", "C.UTF-8"}, {"LC_ALL", "C.UTF-8"}, {"PWD", *cwd}};
    return detail::EnvironmentAccess::mint(EnvironmentProfileV1::PluginMinimal, ProcessRoleV1::Plugin, std::move(variables), std::move(*cwd));
  });
}

ava::core::Result<ExactEnvironmentV1> make_mcp_environment_v1(std::filesystem::path const& logical_cwd,
                                                              std::vector<EnvironmentVariableV1> const& explicit_variables)
{
  return build_environment([&]() -> ava::core::Result<ExactEnvironmentV1> {
    if (!valid_variables(explicit_variables, kMaxMcpExplicitEnvironmentEntriesV1) || find_variable(explicit_variables, "PWD") != nullptr)
      return std::unexpected(invalid_input_error());
    auto cwd = bounded_absolute_path(logical_cwd);
    if (!cwd)
      return std::unexpected(std::move(cwd.error()));
    auto const* explicit_path = find_variable(explicit_variables, "PATH");
    if (explicit_path != nullptr && !valid_path_list(explicit_path->value))
      return std::unexpected(invalid_input_error());

    std::vector<EnvironmentVariableV1> variables;
    variables.reserve(explicit_variables.size() + (explicit_path == nullptr ? 2U : 1U));
    variables.push_back({.name = "PWD", .value = *cwd});
    variables.push_back({.name = "PATH", .value = explicit_path == nullptr ? std::string(kTrustedEnvironmentPathV1) : explicit_path->value});
    for (auto const& variable : explicit_variables)
    {
      if (variable.name != "PATH")
        variables.push_back(variable);
    }
    return detail::EnvironmentAccess::mint(EnvironmentProfileV1::McpExplicit, ProcessRoleV1::Mcp, std::move(variables), std::move(*cwd));
  });
}

ava::core::Result<ExactEnvironmentV1> make_lsp_environment_v1(HostEnvironmentV1 const& host, std::filesystem::path const& logical_cwd)
{
  return build_environment([&]() -> ava::core::Result<ExactEnvironmentV1> {
    if (!host.valid())
      return std::unexpected(invalid_input_error());
    auto cwd = bounded_absolute_path(logical_cwd);
    if (!cwd)
      return std::unexpected(std::move(cwd.error()));
    std::vector<EnvironmentVariableV1> variables{{"PATH", std::string(kTrustedEnvironmentPathV1)}, {"PWD", *cwd}};
    append_if_present(variables, host, std::array<std::string_view, 9>{"HOME", "USER", "LOGNAME", "TMPDIR", "TMP", "TEMP", "LANG", "LANGUAGE", "LC_ALL"});
    append_additional_locales(variables, host);
    append_if_present(variables, host,
                      std::array<std::string_view, 6>{"XDG_CONFIG_HOME", "XDG_CACHE_HOME", "XDG_DATA_HOME", "XDG_STATE_HOME", "TERM", "COLORTERM"});
    return detail::EnvironmentAccess::mint(EnvironmentProfileV1::LspStrict, ProcessRoleV1::Lsp, std::move(variables), std::move(*cwd));
  });
}

ava::core::Result<ExactEnvironmentV1> make_mermaid_environment_v1()
{
  return build_environment([]() -> ava::core::Result<ExactEnvironmentV1> {
    std::vector<EnvironmentVariableV1> variables{{"PATH", "/usr/local/bin:/usr/bin:/bin"},
                                                 {"LANG", "C.UTF-8"},
                                                 {"LC_ALL", "C.UTF-8"},
                                                 {"TERM", "dumb"},
                                                 {"NO_COLOR", "1"},
                                                 {"PWD", "/"},
                                                 {"AVA_MERMAID_PROTOCOL", "1"}};
    return detail::EnvironmentAccess::mint(EnvironmentProfileV1::Mermaid, ProcessRoleV1::Mermaid, std::move(variables), std::string("/"));
  });
}

ava::core::Result<ExactEnvironmentV1> make_browser_desktop_environment_v1(HostEnvironmentV1 const& host)
{
  return build_environment([&]() -> ava::core::Result<ExactEnvironmentV1> {
    if (!host.valid())
      return std::unexpected(invalid_input_error());
    auto inherited_path = detail::EnvironmentAccess::host_value(host, "PATH");
    if (!inherited_path || !valid_path_list(*inherited_path))
      return std::unexpected(invalid_input_error());
    std::vector<EnvironmentVariableV1> variables{{"PATH", std::string(*inherited_path)}};
    append_if_present(variables, host, std::array<std::string_view, 9>{"HOME", "USER", "LOGNAME", "TMPDIR", "TMP", "TEMP", "LANG", "LANGUAGE", "LC_ALL"});
    append_additional_locales(variables, host);
    append_if_present(variables, host,
                      std::array<std::string_view, 10>{"XDG_CONFIG_HOME", "XDG_CACHE_HOME", "XDG_DATA_HOME", "XDG_STATE_HOME", "XDG_RUNTIME_DIR", "DISPLAY",
                                                       "WAYLAND_DISPLAY", "XAUTHORITY", "DBUS_SESSION_BUS_ADDRESS", "DESKTOP_STARTUP_ID"});
    return detail::EnvironmentAccess::mint(EnvironmentProfileV1::BrowserDesktop, ProcessRoleV1::BrowserOpener, std::move(variables), std::nullopt);
  });
}

ava::core::Result<ExactEnvironmentV1> make_clipboard_desktop_environment_v1(HostEnvironmentV1 const& host)
{
  return build_environment([&]() -> ava::core::Result<ExactEnvironmentV1> {
    if (!host.valid())
      return std::unexpected(invalid_input_error());
    std::vector<EnvironmentVariableV1> variables{{"PATH", std::string(kTrustedEnvironmentPathV1)}};
    append_if_present(variables, host, std::array<std::string_view, 9>{"HOME", "USER", "LOGNAME", "TMPDIR", "TMP", "TEMP", "LANG", "LANGUAGE", "LC_ALL"});
    append_additional_locales(variables, host);
    append_if_present(variables, host,
                      std::array<std::string_view, 5>{"XDG_RUNTIME_DIR", "DISPLAY", "WAYLAND_DISPLAY", "XAUTHORITY", "DBUS_SESSION_BUS_ADDRESS"});
    return detail::EnvironmentAccess::mint(EnvironmentProfileV1::ClipboardDesktop, ProcessRoleV1::ClipboardHelper, std::move(variables), std::nullopt);
  });
}

ava::core::Result<ExactEnvironmentV1> make_external_editor_environment_v1(HostEnvironmentV1 const& host, std::filesystem::path const& draft_path)
{
  return build_environment([&]() -> ava::core::Result<ExactEnvironmentV1> {
    if (!host.valid())
      return std::unexpected(invalid_input_error());
    auto inherited_path = detail::EnvironmentAccess::host_value(host, "PATH");
    auto draft = bounded_absolute_path(draft_path);
    if (!inherited_path || !valid_path_list(*inherited_path) || !draft)
      return std::unexpected(invalid_input_error());
    std::vector<EnvironmentVariableV1> variables{{"PATH", std::string(*inherited_path)}};
    append_if_present(variables, host,
                      std::array<std::string_view, 10>{"HOME", "USER", "LOGNAME", "SHELL", "TMPDIR", "TMP", "TEMP", "LANG", "LANGUAGE", "LC_ALL"});
    append_additional_locales(variables, host);
    append_if_present(variables, host,
                      std::array<std::string_view, 11>{"TERM", "COLORTERM", "XDG_CONFIG_HOME", "XDG_CACHE_HOME", "XDG_DATA_HOME", "XDG_STATE_HOME",
                                                       "XDG_RUNTIME_DIR", "DISPLAY", "WAYLAND_DISPLAY", "XAUTHORITY", "DBUS_SESSION_BUS_ADDRESS"});
    variables.push_back({.name = "AVA_EXTERNAL_EDITOR_FILE", .value = std::move(*draft)});
    return detail::EnvironmentAccess::mint(EnvironmentProfileV1::ExternalEditor, ProcessRoleV1::ExternalEditor, std::move(variables), std::nullopt);
  });
}

namespace detail {

ava::core::Result<HostEnvironmentV1> EnvironmentAccess::capture_host()
{
  try
  {
    auto implementation = std::make_shared<HostEnvironmentV1::Impl>();
    std::size_t encoded_size = 0;
#if defined(_WIN32)
    auto environment = _environ;
#else
    auto environment = ::environ;
#endif
    for (auto current = environment; current != nullptr && *current != nullptr; ++current)
    {
      char const* raw = *current;
      char const* separator = raw;
      while (*separator != '\0' && *separator != '=')
        ++separator;
      if (*separator != '=')
        continue;
      std::string_view const name(raw, static_cast<std::size_t>(separator - raw));
      if (!is_sanctioned_host_name(name))
        continue;
      if (name.empty() || name.size() > kMaxEnvironmentNameBytesV1)
        return std::unexpected(capture_error());
      for (auto const& existing : implementation->variables)
      {
        if (existing.name == name)
          return std::unexpected(capture_error());
      }

      char const* value_begin = separator + 1;
      std::size_t value_size = 0;
      while (value_size <= kMaxEnvironmentValueBytesV1 && value_begin[value_size] != '\0')
        ++value_size;
      if (value_size > kMaxEnvironmentValueBytesV1)
        return std::unexpected(capture_error());
      auto const entry_size = name.size() + value_size + 2;
      if (implementation->variables.size() >= kMaxEnvironmentEntriesV1 || entry_size > kMaxEnvironmentEncodedBytesV1 - encoded_size)
        return std::unexpected(capture_error());
      encoded_size += entry_size;
      implementation->variables.push_back({.name = std::string(name), .value = std::string(value_begin, value_size)});
    }
    return HostEnvironmentV1(std::move(implementation));
  }
  catch (...)
  {
    return std::unexpected(allocation_error());
  }
}

std::optional<std::string_view> EnvironmentAccess::host_value(HostEnvironmentV1 const& host, std::string_view name) noexcept
{
  if (!host.implementation_)
    return std::nullopt;
  auto const found = std::ranges::find_if(host.implementation_->variables, [name](auto const& variable) { return variable.name == name; });
  if (found == host.implementation_->variables.end())
    return std::nullopt;
  return found->value;
}

std::vector<EnvironmentVariableV1> const& EnvironmentAccess::host_variables(HostEnvironmentV1 const& host) noexcept
{
  static std::vector<EnvironmentVariableV1> const empty;
  return host.implementation_ ? host.implementation_->variables : empty;
}

ava::core::Result<ExactEnvironmentV1> EnvironmentAccess::mint(EnvironmentProfileV1 profile, ProcessRoleV1 role, std::vector<EnvironmentVariableV1> variables,
                                                              std::optional<std::string> logical_cwd)
{
  if (!is_valid(profile) || !is_valid(role) || !profile_matches_role(profile, role) || !valid_variables(variables, kMaxEnvironmentEntriesV1) ||
      !valid_logical_cwd_binding(profile, variables, logical_cwd))
  {
    return std::unexpected(invalid_input_error());
  }
  try
  {
    auto implementation = std::make_unique<ExactEnvironmentV1::Impl>();
    implementation->profile = profile;
    implementation->role = role;
    implementation->variables = std::move(variables);
    implementation->logical_cwd = std::move(logical_cwd);
    return ExactEnvironmentV1(std::move(implementation));
  }
  catch (...)
  {
    return std::unexpected(allocation_error());
  }
}

std::vector<EnvironmentVariableV1> const& EnvironmentAccess::variables(ExactEnvironmentV1 const& environment) noexcept
{
  static std::vector<EnvironmentVariableV1> const empty;
  return environment.implementation_ ? environment.implementation_->variables : empty;
}

bool EnvironmentAccess::revalidate(ExactEnvironmentV1 const& environment) noexcept
{
  return environment.implementation_ && is_valid(environment.implementation_->profile) && is_valid(environment.implementation_->role) &&
         profile_matches_role(environment.implementation_->profile, environment.implementation_->role) &&
         valid_variables(environment.implementation_->variables, kMaxEnvironmentEntriesV1) &&
         valid_logical_cwd_binding(environment.implementation_->profile, environment.implementation_->variables, environment.implementation_->logical_cwd);
}

bool EnvironmentAccess::matches_common_launch(ExactEnvironmentV1 const& environment, ProcessRoleV1 role, std::string_view cwd) noexcept
{
  return is_common_role(role) && revalidate(environment) && environment.implementation_->role == role &&
         profile_matches_role(environment.implementation_->profile, role) &&
         (!environment.implementation_->logical_cwd || *environment.implementation_->logical_cwd == cwd);
}

bool EnvironmentAccess::matches_secure_adoption(ExactEnvironmentV1 const& environment, ProcessRoleV1 role, std::string_view cwd) noexcept
{
  return is_secure_role(role) && revalidate(environment) && environment.implementation_->role == role &&
         profile_matches_role(environment.implementation_->profile, role) && environment.implementation_->logical_cwd &&
         *environment.implementation_->logical_cwd == cwd;
}

}  // namespace detail

namespace testing {

ava::core::Result<HostEnvironmentV1> EnvironmentTestAccess::capture_host()
{
  return detail::EnvironmentAccess::capture_host();
}

ava::core::Result<HostEnvironmentV1> EnvironmentTestAccess::make_host(std::vector<EnvironmentVariableV1> variables)
{
  if (!valid_variables(variables, kMaxEnvironmentEntriesV1) ||
      std::ranges::any_of(variables, [](auto const& variable) { return !is_sanctioned_host_name(variable.name); }))
  {
    return std::unexpected(capture_error());
  }
  try
  {
    auto implementation = std::make_shared<HostEnvironmentV1::Impl>();
    implementation->variables = std::move(variables);
    return HostEnvironmentV1(std::move(implementation));
  }
  catch (...)
  {
    return std::unexpected(allocation_error());
  }
}

std::vector<EnvironmentVariableV1> const& EnvironmentTestAccess::variables(ExactEnvironmentV1 const& environment) noexcept
{
  return detail::EnvironmentAccess::variables(environment);
}

std::vector<EnvironmentVariableV1> const& EnvironmentTestAccess::host_variables(HostEnvironmentV1 const& environment) noexcept
{
  return detail::EnvironmentAccess::host_variables(environment);
}

bool EnvironmentTestAccess::shares_capture(HostEnvironmentV1 const& left, HostEnvironmentV1 const& right) noexcept
{
  return left.implementation_ && left.implementation_ == right.implementation_;
}

std::size_t EnvironmentTestAccess::encoded_size(ExactEnvironmentV1 const& environment) noexcept
{
  std::size_t result = 0;
  for (auto const& variable : detail::EnvironmentAccess::variables(environment))
    result += variable.name.size() + variable.value.size() + 2;
  return result;
}

std::size_t EnvironmentTestAccess::host_encoded_size(HostEnvironmentV1 const& environment) noexcept
{
  std::size_t result = 0;
  for (auto const& variable : detail::EnvironmentAccess::host_variables(environment))
    result += variable.name.size() + variable.value.size() + 2;
  return result;
}

}  // namespace testing

}  // namespace ava::process
