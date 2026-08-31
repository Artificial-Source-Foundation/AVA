#pragma once

#include "ava/debug/print_members_on.h"
#include "ava/process/types.h"
#include "ava/core/result.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace ava::process {

inline constexpr std::size_t kMaxEnvironmentEntriesV1 = 256;
inline constexpr std::size_t kMaxMcpExplicitEnvironmentEntriesV1 = 64;
inline constexpr std::size_t kMaxEnvironmentNameBytesV1 = 128;
inline constexpr std::size_t kMaxEnvironmentValueBytesV1 = 16 * 1024;
inline constexpr std::size_t kMaxEnvironmentEncodedBytesV1 = 1024 * 1024;

inline constexpr std::string_view kTrustedEnvironmentPathV1 = "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin";
inline constexpr std::string_view kCurlEnvironmentProfileIdV1 = "ava-curl-v1";
inline constexpr std::string_view kBashEnvironmentProfileIdV1 = "ava-local-bash-prompt-v2";
inline constexpr std::string_view kPluginEnvironmentProfileIdV1 = "ava-plugin-minimal-v1";
inline constexpr std::string_view kMcpEnvironmentProfileIdV1 = "ava-mcp-explicit-v1";
inline constexpr std::string_view kLspEnvironmentProfileIdV1 = "ava-lsp-strict-v1";
inline constexpr std::string_view kMermaidEnvironmentProfileIdV1 = "ava-mermaid-v1";
inline constexpr std::string_view kBrowserEnvironmentProfileIdV1 = "ava-browser-desktop-v1";
inline constexpr std::string_view kClipboardEnvironmentProfileIdV1 = "ava-clipboard-desktop-v1";
inline constexpr std::string_view kExternalEditorEnvironmentProfileIdV1 = "ava-external-editor-v1";

enum class EnvironmentProfileV1
{
  Curl,
  Bash,
  PluginMinimal,
  McpExplicit,
  LspStrict,
  Mermaid,
  BrowserDesktop,
  ClipboardDesktop,
  ExternalEditor,
};

enum class HostEnvironmentSelectorV1
{
  Browser,
  Visual,
  Editor,
};

[[nodiscard]] constexpr bool is_valid(EnvironmentProfileV1 value) noexcept
{
  return value >= EnvironmentProfileV1::Curl && value <= EnvironmentProfileV1::ExternalEditor;
}

[[nodiscard]] constexpr bool is_valid(HostEnvironmentSelectorV1 value) noexcept
{
  return value >= HostEnvironmentSelectorV1::Browser && value <= HostEnvironmentSelectorV1::Editor;
}

[[nodiscard]] std::string_view environment_profile_id_v1(EnvironmentProfileV1 value) noexcept;
[[nodiscard]] std::string_view to_string(EnvironmentProfileV1 value) noexcept;

namespace detail {
class EnvironmentAccess;
}  // namespace detail

namespace testing {
class EnvironmentTestAccess;
}  // namespace testing

// Immutable, bounded projection captured from the parent environment. It has no
// public name/value enumeration; only the closed parent-side selectors can be
// queried outside the process policy implementation.
class HostEnvironmentV1 final
{
 public:
  HostEnvironmentV1() noexcept;
  HostEnvironmentV1(HostEnvironmentV1 const&) noexcept;
  HostEnvironmentV1& operator=(HostEnvironmentV1 const&) noexcept;
  HostEnvironmentV1(HostEnvironmentV1&&) noexcept;
  HostEnvironmentV1& operator=(HostEnvironmentV1&&) noexcept;
  ~HostEnvironmentV1();

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] std::optional<std::string_view> selector(HostEnvironmentSelectorV1 selector) const noexcept;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT

 private:
  struct Impl;
  explicit HostEnvironmentV1(std::shared_ptr<Impl const> implementation) noexcept;

  std::shared_ptr<Impl const> implementation_;

  friend class detail::EnvironmentAccess;
  friend class testing::EnvironmentTestAccess;
};

// Opaque, move-only authority to launch one exact role/profile environment.
// PWD-bearing profiles also retain their computed logical-cwd binding. Child
// values and the binding are deliberately unavailable through the public API.
class ExactEnvironmentV1 final
{
 public:
  ExactEnvironmentV1() noexcept;
  ExactEnvironmentV1(ExactEnvironmentV1 const&) = delete;
  ExactEnvironmentV1& operator=(ExactEnvironmentV1 const&) = delete;
  ExactEnvironmentV1(ExactEnvironmentV1&&) noexcept;
  ExactEnvironmentV1& operator=(ExactEnvironmentV1&&) noexcept;
  ~ExactEnvironmentV1();

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] ProcessRoleV1 role() const noexcept;
  [[nodiscard]] EnvironmentProfileV1 profile() const noexcept;
  [[nodiscard]] std::string_view profile_id() const noexcept;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT

 private:
  struct Impl;
  explicit ExactEnvironmentV1(std::unique_ptr<Impl> implementation) noexcept;

  std::unique_ptr<Impl> implementation_;

  friend class detail::EnvironmentAccess;
  friend class testing::EnvironmentTestAccess;
};

[[nodiscard]] ava::core::Result<ExactEnvironmentV1> make_curl_environment_v1(HostEnvironmentV1 const& host);
[[nodiscard]] ava::core::Result<ExactEnvironmentV1> validate_bash_environment_v1(std::string_view profile_id, std::filesystem::path const& logical_cwd,
                                                                                 std::vector<EnvironmentVariableV1> const& imported_variables);
[[nodiscard]] ava::core::Result<ExactEnvironmentV1> make_plugin_environment_v1(std::filesystem::path const& logical_cwd);
[[nodiscard]] ava::core::Result<ExactEnvironmentV1> make_mcp_environment_v1(std::filesystem::path const& logical_cwd,
                                                                            std::vector<EnvironmentVariableV1> const& explicit_variables);
[[nodiscard]] ava::core::Result<ExactEnvironmentV1> make_lsp_environment_v1(HostEnvironmentV1 const& host, std::filesystem::path const& logical_cwd);
[[nodiscard]] ava::core::Result<ExactEnvironmentV1> make_mermaid_environment_v1();
[[nodiscard]] ava::core::Result<ExactEnvironmentV1> make_browser_desktop_environment_v1(HostEnvironmentV1 const& host);
[[nodiscard]] ava::core::Result<ExactEnvironmentV1> make_clipboard_desktop_environment_v1(HostEnvironmentV1 const& host);
[[nodiscard]] ava::core::Result<ExactEnvironmentV1> make_external_editor_environment_v1(HostEnvironmentV1 const& host, std::filesystem::path const& draft_path);

}  // namespace ava::process
