#include "sys.h"
#include "ava/diagnostics/artifact_store.h"
#include "ava/app/doctor_support.h"
#include "ava/plugin/diagnostics.h"
#include "ava/mcp/config.h"
#include "ava/config/model_config.h"
#include "ava/config/provider_profiles.h"
#include "ava/permissions/permission_rules.h"
#include "ava/lsp/configured_provider.h"
#include "ava/core/strict_json.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "debug.h"

namespace ava::app {
namespace {

using ava::diagnostics::DoctorCheck;
using ava::diagnostics::DoctorCheckKind;
using ava::diagnostics::DoctorCode;
using ava::diagnostics::DoctorReport;
using ava::diagnostics::DoctorStatus;

constexpr std::size_t kMaxPassiveConfigBytes = 1024 * 1024;

class ScopedFd
{
 public:
  explicit ScopedFd(int fd = -1) noexcept : fd_(fd) { }
  ScopedFd(ScopedFd const&) = delete;
  ScopedFd& operator=(ScopedFd const&) = delete;
  ScopedFd(ScopedFd&& other) noexcept : fd_(std::exchange(other.fd_, -1)) { }
  ScopedFd& operator=(ScopedFd&& other) noexcept
  {
    if (this != &other)
    {
      if (fd_ >= 0)
        static_cast<void>(::close(fd_));
      fd_ = std::exchange(other.fd_, -1);
    }
    return *this;
  }
  ~ScopedFd()
  {
    if (fd_ >= 0)
      static_cast<void>(::close(fd_));
  }
  [[nodiscard]] int get() const noexcept { return fd_; }
  [[nodiscard]] explicit operator bool() const noexcept { return fd_ >= 0; }

 private:
  int fd_ = -1;
};

enum class MetadataState
{
  Ready,
  Missing,
  Unsafe,
  Unavailable,
};

enum class ExpectedType
{
  Directory,
  RegularFile,
};

struct MetadataInspection
{
  MetadataState state = MetadataState::Unavailable;
  struct stat metadata{};
};

bool trusted_ancestor(struct stat const& metadata) noexcept
{
  // Ancestors are opened component-by-component without following symlinks.
  // Their permissions are not AVA-owned (a workspace may intentionally be
  // group-writable); the fixed config/state roots and every final file receive
  // the stricter owner/mode checks below.
  return S_ISDIR(metadata.st_mode) && (metadata.st_uid == 0 || metadata.st_uid == ::geteuid());
}

std::pair<MetadataState, ScopedFd> open_parent_nofollow(std::filesystem::path const& path) noexcept
{
  try
  {
    auto normalized = path.lexically_normal();
    if (!normalized.is_absolute() || normalized.filename().empty())
      return {MetadataState::Unsafe, ScopedFd{}};
    auto const parent = normalized.parent_path();
    ScopedFd current(::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NONBLOCK));
    if (!current)
      return {MetadataState::Unavailable, ScopedFd{}};
    for (auto const& component : parent.relative_path())
    {
      auto const name = component.native();
      if (name.empty() || name == ".")
        continue;
      if (name == "..")
        return {MetadataState::Unsafe, ScopedFd{}};
      ScopedFd next(::openat(current.get(), name.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW));
      if (!next)
      {
        if (errno == ENOENT)
          return {MetadataState::Missing, ScopedFd{}};
        if (errno == ELOOP || errno == ENOTDIR)
          return {MetadataState::Unsafe, ScopedFd{}};
        return {MetadataState::Unavailable, ScopedFd{}};
      }
      struct stat metadata{};
      if (::fstat(next.get(), &metadata) != 0)
        return {MetadataState::Unavailable, ScopedFd{}};
      if (!trusted_ancestor(metadata))
        return {MetadataState::Unsafe, ScopedFd{}};
      current = std::move(next);
    }
    return {MetadataState::Ready, std::move(current)};
  }
  catch (...)
  {
    return {MetadataState::Unavailable, ScopedFd{}};
  }
}

MetadataInspection inspect_metadata(std::filesystem::path const& path, ExpectedType expected, bool exact_private_mode = false)
{
  auto [parent_state, parent] = open_parent_nofollow(path);
  if (parent_state != MetadataState::Ready)
    return {.state = parent_state};
  struct stat metadata{};
  auto const name = path.lexically_normal().filename().native();
  if (::fstatat(parent.get(), name.c_str(), &metadata, AT_SYMLINK_NOFOLLOW) != 0)
  {
    return {.state = errno == ENOENT ? MetadataState::Missing : MetadataState::Unavailable};
  }
  bool const right_type = expected == ExpectedType::Directory ? S_ISDIR(metadata.st_mode) : S_ISREG(metadata.st_mode);
  if (!right_type || metadata.st_uid != ::geteuid() || metadata.st_gid != ::getegid())
    return {.state = MetadataState::Unsafe, .metadata = metadata};
  if (expected == ExpectedType::RegularFile && metadata.st_nlink != 1)
    return {.state = MetadataState::Unsafe, .metadata = metadata};
  auto const permissions = metadata.st_mode & static_cast<mode_t>(07777);
  if (exact_private_mode)
  {
    auto const expected_mode = expected == ExpectedType::Directory ? static_cast<mode_t>(0700) : static_cast<mode_t>(0600);
    if (permissions != expected_mode)
      return {.state = MetadataState::Unsafe, .metadata = metadata};
  }
  else if ((permissions & static_cast<mode_t>(S_IWGRP | S_IWOTH)) != 0)
  {
    return {.state = MetadataState::Unsafe, .metadata = metadata};
  }
  return {.state = MetadataState::Ready, .metadata = metadata};
}

std::optional<std::string> bounded_read(std::filesystem::path const& path, MetadataInspection const& inspected)
{
  if (inspected.state != MetadataState::Ready || inspected.metadata.st_size < 0 ||
      static_cast<std::uint64_t>(inspected.metadata.st_size) > kMaxPassiveConfigBytes)
    return std::nullopt;
  auto [parent_state, parent] = open_parent_nofollow(path);
  if (parent_state != MetadataState::Ready)
    return std::nullopt;
  auto const name = path.lexically_normal().filename().native();
  ScopedFd file(::openat(parent.get(), name.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW));
  if (!file)
    return std::nullopt;
  struct stat opened{};
  if (::fstat(file.get(), &opened) != 0 || !S_ISREG(opened.st_mode) || opened.st_dev != inspected.metadata.st_dev ||
      opened.st_ino != inspected.metadata.st_ino || opened.st_nlink != 1 || opened.st_size < 0 ||
      static_cast<std::uint64_t>(opened.st_size) > kMaxPassiveConfigBytes)
    return std::nullopt;
  std::string body;
  body.reserve(static_cast<std::size_t>(opened.st_size));
  std::array<char, 4096> buffer{};
  while (true)
  {
    auto const count = ::read(file.get(), buffer.data(), buffer.size());
    if (count == 0)
      return body;
    if (count < 0)
    {
      if (errno == EINTR)
        continue;
      return std::nullopt;
    }
    if (body.size() + static_cast<std::size_t>(count) > kMaxPassiveConfigBytes)
      return std::nullopt;
    body.append(buffer.data(), static_cast<std::size_t>(count));
  }
}

DoctorCheck root_check(DoctorCheckKind kind, MetadataInspection const& inspection)
{
  switch (inspection.state)
  {
    case MetadataState::Ready:
      return {.kind = kind, .status = DoctorStatus::Pass, .code = DoctorCode::Ready, .items = 1};
    case MetadataState::Missing:
      return {.kind = kind, .status = DoctorStatus::Warning, .code = DoctorCode::MissingOptional};
    case MetadataState::Unsafe:
    case MetadataState::Unavailable:
      return {.kind = kind, .status = DoctorStatus::Fail, .code = DoctorCode::UnsafeMetadata, .errors = 1};
  }
  return {.kind = kind, .status = DoctorStatus::Fail, .code = DoctorCode::UnsafeMetadata, .errors = 1};
}

bool optional_path_safe(MetadataInspection const& inspection) noexcept
{
  return inspection.state == MetadataState::Ready || inspection.state == MetadataState::Missing;
}

std::int64_t now_seconds() noexcept
{
  auto const value = std::time(nullptr);
  return value < 0 ? 0 : static_cast<std::int64_t>(value);
}

}  // namespace

DoctorReport collect_passive_doctor_report(ava::config::XdgPaths const& paths, std::filesystem::path const& workspace_dir)
{
  DoctorReport report;
  try
  {
    report.checks.reserve(10);
    report.checks.push_back({.kind = DoctorCheckKind::VersionPlatform, .status = DoctorStatus::Pass, .code = DoctorCode::Ready, .items = 1});

    auto const config_root = inspect_metadata(paths.ava_config_dir, ExpectedType::Directory, true);
    auto const state_root = inspect_metadata(paths.ava_state_dir, ExpectedType::Directory, true);
    report.checks.push_back(root_check(DoctorCheckKind::ConfigRoot, config_root));
    report.checks.push_back(root_check(DoctorCheckKind::StateRoot, state_root));

    ava::config::ModelRegistry registry;
    bool registry_ready = false;
    auto const model_metadata = inspect_metadata(paths.models_file, ExpectedType::RegularFile);
    if (model_metadata.state == MetadataState::Missing)
    {
      registry = ava::config::builtin_model_registry();
      registry_ready = true;
      report.checks.push_back({.kind = DoctorCheckKind::ModelRegistry,
                               .status = DoctorStatus::Pass,
                               .code = DoctorCode::BuiltinDefaults,
                               .items = static_cast<std::uint64_t>(registry.models.size())});
    }
    else if (model_metadata.state == MetadataState::Ready)
    {
      auto body = bounded_read(paths.models_file, model_metadata);
      if (body && ava::core::validate_strict_json(*body, 64) == ava::core::StrictJsonStatus::Valid)
      {
        registry = ava::config::parse_model_registry(*body);
        registry_ready = true;
        report.checks.push_back({.kind = DoctorCheckKind::ModelRegistry,
                                 .status = DoctorStatus::Pass,
                                 .code = DoctorCode::Ready,
                                 .items = static_cast<std::uint64_t>(registry.models.size())});
      }
      else
      {
        report.checks.push_back({.kind = DoctorCheckKind::ModelRegistry, .status = DoctorStatus::Fail, .code = DoctorCode::InvalidConfiguration, .errors = 1});
      }
    }
    else
    {
      report.checks.push_back({.kind = DoctorCheckKind::ModelRegistry, .status = DoctorStatus::Fail, .code = DoctorCode::UnsafeMetadata, .errors = 1});
    }

    bool default_ready = false;
    if (registry_ready)
    {
      auto const model = ava::config::find_model(registry, registry.default_provider_id, registry.default_model_id);
      if (model)
      {
        auto const provider = ava::config::provider_profile_for_model(*model);
        default_ready = provider && provider->runtime_selectable;
      }
    }
    report.checks.push_back({.kind = DoctorCheckKind::DefaultModel,
                             .status = default_ready ? DoctorStatus::Pass : DoctorStatus::Fail,
                             .code = default_ready ? DoctorCode::Ready : DoctorCode::DefaultUnavailable,
                             .items = default_ready ? 1U : 0U,
                             .errors = default_ready ? 0U : 1U});

    auto const auth = inspect_metadata(paths.auth_file, ExpectedType::RegularFile, true);
    if (auth.state == MetadataState::Ready)
      report.checks.push_back({.kind = DoctorCheckKind::AuthMetadata, .status = DoctorStatus::Pass, .code = DoctorCode::Present, .items = 1});
    else if (auth.state == MetadataState::Missing)
      report.checks.push_back({.kind = DoctorCheckKind::AuthMetadata, .status = DoctorStatus::Warning, .code = DoctorCode::MissingOptional});
    else
      report.checks.push_back({.kind = DoctorCheckKind::AuthMetadata, .status = DoctorStatus::Warning, .code = DoctorCode::UnsafeMetadata, .errors = 1});

    auto const global_plugins = inspect_metadata(paths.ava_config_dir / "plugins", ExpectedType::Directory);
    auto const project_plugins = workspace_dir.empty() ? MetadataInspection{.state = MetadataState::Missing}
                                                       : inspect_metadata(workspace_dir / ".ava" / "plugins", ExpectedType::Directory);
    auto const enablement = inspect_metadata(paths.ava_state_dir / "plugin-enablement.json", ExpectedType::RegularFile, true);
    std::uint64_t plugin_items = 0;
    std::uint64_t plugin_enabled = 0;
    std::uint64_t plugin_errors = 0;
    if (!optional_path_safe(global_plugins))
      ++plugin_errors;
    if (!optional_path_safe(project_plugins))
      ++plugin_errors;
    if (!optional_path_safe(enablement))
      ++plugin_errors;
    if (plugin_errors == 0)
    {
      auto diagnostics = ava::plugin::collect_plugin_diagnostics(
          {.global_plugins_dir = global_plugins.state == MetadataState::Ready ? paths.ava_config_dir / "plugins" : std::filesystem::path{},
           .project_plugins_dir = project_plugins.state == MetadataState::Ready ? workspace_dir / ".ava" / "plugins" : std::filesystem::path{}},
          enablement.state == MetadataState::Ready ? paths.ava_state_dir / "plugin-enablement.json" : std::filesystem::path{}, workspace_dir);
      plugin_items = diagnostics.plugins.size();
      plugin_errors += diagnostics.failures.size();
      for (auto const& plugin : diagnostics.plugins) plugin_enabled += plugin.enabled ? 1U : 0U;
    }
    report.checks.push_back(
        {.kind = DoctorCheckKind::PluginConfiguration,
         .status = plugin_errors == 0 ? DoctorStatus::Pass : DoctorStatus::Warning,
         .code = plugin_errors == 0 && plugin_items == 0 ? DoctorCode::MissingOptional : (plugin_errors == 0 ? DoctorCode::Ready : DoctorCode::Partial),
         .items = plugin_items,
         .enabled = plugin_enabled,
         .errors = plugin_errors});

    auto const global_mcp = inspect_metadata(paths.ava_config_dir / "mcp.json", ExpectedType::RegularFile);
    auto const project_mcp = workspace_dir.empty() ? MetadataInspection{.state = MetadataState::Missing}
                                                   : inspect_metadata(workspace_dir / ".ava" / "mcp.json", ExpectedType::RegularFile);
    std::uint64_t mcp_items = 0;
    std::uint64_t mcp_enabled = 0;
    std::uint64_t mcp_errors = 0;
    if (!optional_path_safe(global_mcp))
      ++mcp_errors;
    if (!optional_path_safe(project_mcp))
      ++mcp_errors;
    if (mcp_errors == 0)
    {
      auto loaded = ava::mcp::load_mcp_config(
          {.workspace_dir = workspace_dir,
           .global_config_file = global_mcp.state == MetadataState::Ready ? paths.ava_config_dir / "mcp.json" : std::filesystem::path{},
           .project_config_file = project_mcp.state == MetadataState::Ready ? workspace_dir / ".ava" / "mcp.json" : std::filesystem::path{}});
      if (loaded)
      {
        mcp_items = loaded->servers.size();
        for (auto const& server : loaded->servers) mcp_enabled += server.enabled ? 1U : 0U;
      }
      else
      {
        ++mcp_errors;
      }
    }
    report.checks.push_back(
        {.kind = DoctorCheckKind::McpConfiguration,
         .status = mcp_errors == 0 ? DoctorStatus::Pass : DoctorStatus::Warning,
         .code = mcp_errors == 0 && mcp_items == 0 ? DoctorCode::MissingOptional : (mcp_errors == 0 ? DoctorCode::Ready : DoctorCode::Partial),
         .items = mcp_items,
         .enabled = mcp_enabled,
         .errors = mcp_errors});

    auto const global_lsp = inspect_metadata(paths.ava_config_dir / "lsp.json", ExpectedType::RegularFile);
    auto const project_lsp = workspace_dir.empty() ? MetadataInspection{.state = MetadataState::Missing}
                                                   : inspect_metadata(workspace_dir / ".ava" / "lsp.json", ExpectedType::RegularFile);
    std::uint64_t lsp_items = 0;
    std::uint64_t lsp_errors = 0;
    std::vector<ava::lsp::BuiltinServerInspection> lsp_builtins;
    if (!optional_path_safe(global_lsp))
      ++lsp_errors;
    if (!optional_path_safe(project_lsp))
      ++lsp_errors;
    if (lsp_errors == 0)
    {
      auto const inspection = ava::lsp::inspect_configured_lsp_provider(
          {.global_config_file = global_lsp.state == MetadataState::Ready ? paths.ava_config_dir / "lsp.json" : std::filesystem::path{},
           .project_config_file = project_lsp.state == MetadataState::Ready ? workspace_dir / ".ava" / "lsp.json" : std::filesystem::path{},
           .workspace_root = workspace_dir});
      for (auto const& config : inspection.configs) lsp_items += config.server_count;
      lsp_builtins = inspection.builtin_servers;
      lsp_errors += inspection.error_count;
    }
    report.checks.push_back(
        {.kind = DoctorCheckKind::LspConfiguration,
         .status = lsp_errors == 0 ? DoctorStatus::Pass : DoctorStatus::Warning,
         .code = lsp_errors == 0 && lsp_items == 0 ? DoctorCode::MissingOptional : (lsp_errors == 0 ? DoctorCode::Ready : DoctorCode::Partial),
         .items = lsp_items,
         .enabled = lsp_items,
         .errors = lsp_errors});

    if (lsp_builtins.empty())
      lsp_builtins = ava::lsp::inspect_builtin_servers({}, workspace_dir);
    for (auto const& builtin : lsp_builtins)
    {
      DoctorStatus status = DoctorStatus::Pass;
      DoctorCode code = DoctorCode::BuiltinDefaults;
      std::uint64_t enabled = 0;
      std::uint64_t errors = 0;
      if (builtin.status == ava::lsp::BuiltinServerStatus::Available)
      {
        code = DoctorCode::Ready;
        enabled = 1;
      }
      else if (builtin.status == ava::lsp::BuiltinServerStatus::NotFound)
      {
        status = DoctorStatus::Warning;
        code = DoctorCode::MissingOptional;
        enabled = 1;
      }
      else if (builtin.status == ava::lsp::BuiltinServerStatus::Unsafe)
      {
        status = DoctorStatus::Warning;
        code = DoctorCode::UnsafeMetadata;
        enabled = 1;
        errors = 1;
      }
      report.checks.push_back({.kind = DoctorCheckKind::LspBuiltinClangd, .status = status, .code = code, .items = 1, .enabled = enabled, .errors = errors});
    }

    ava::permissions::PermissionRuleStore const permission_store{
        .global_rules_file = paths.ava_config_dir / "permission-rules.json",
        .workspace_rules_file = workspace_dir.empty() ? std::filesystem::path{} : workspace_dir / ".ava" / "permission-rules.json",
        .workspace_dir = workspace_dir};
    auto const global_rules = inspect_metadata(
        ava::permissions::enforceable_permission_rules_file(permission_store, ava::permissions::PermissionRuleScope::Global), ExpectedType::RegularFile);
    auto const workspace_rules =
        workspace_dir.empty()
            ? MetadataInspection{.state = MetadataState::Missing}
            : inspect_metadata(ava::permissions::enforceable_permission_rules_file(permission_store, ava::permissions::PermissionRuleScope::Workspace),
                               ExpectedType::RegularFile);
    std::uint64_t permission_items = 0;
    std::uint64_t permission_errors = 0;
    if (!optional_path_safe(global_rules))
      ++permission_errors;
    if (!optional_path_safe(workspace_rules))
      ++permission_errors;
    if (permission_errors == 0)
    {
      auto loaded = ava::permissions::load_persistent_permission_rules(permission_store);
      if (loaded)
        permission_items = loaded->size();
      else
        ++permission_errors;
    }
    report.checks.push_back({.kind = DoctorCheckKind::PermissionRules,
                             .status = permission_errors == 0 ? DoctorStatus::Pass : DoctorStatus::Fail,
                             .code = permission_errors == 0 && permission_items == 0
                                         ? DoctorCode::MissingOptional
                                         : (permission_errors == 0 ? DoctorCode::Ready : DoctorCode::InvalidConfiguration),
                             .items = permission_items,
                             .enabled = permission_items,
                             .errors = permission_errors});
  }
  catch (...)
  {
    constexpr std::array kinds{DoctorCheckKind::VersionPlatform,     DoctorCheckKind::ConfigRoot,       DoctorCheckKind::StateRoot,
                               DoctorCheckKind::ModelRegistry,       DoctorCheckKind::DefaultModel,     DoctorCheckKind::AuthMetadata,
                               DoctorCheckKind::PluginConfiguration, DoctorCheckKind::McpConfiguration, DoctorCheckKind::LspConfiguration,
                               DoctorCheckKind::PermissionRules};
    for (auto const kind : kinds)
    {
      auto const already_reported = std::ranges::any_of(report.checks, [kind](DoctorCheck const& check) { return check.kind == kind; });
      if (!already_reported)
        report.checks.push_back({.kind = kind, .status = DoctorStatus::Fail, .code = DoctorCode::InvalidConfiguration, .errors = 1});
    }
  }
  return report;
}

int run_doctor(ava::config::XdgPaths const& paths, std::filesystem::path const& workspace_dir, bool json, std::ostream& out, std::ostream& err)
{
  try
  {
    Dout(dc::app, "operation=doctor state=start format=" << (json ? "json" : "human"));
    auto const report = collect_passive_doctor_report(paths, workspace_dir);
    std::uint64_t warning_count = 0;
    std::uint64_t failure_count = 0;
    for (auto const& check : report.checks)
    {
      warning_count += check.status == DoctorStatus::Warning ? 1U : 0U;
      failure_count += check.status == DoctorStatus::Fail ? 1U : 0U;
    }
    Dout(dc::config, "operation=doctor state=result checks=" << report.checks.size() << " warnings=" << warning_count << " failures=" << failure_count);
    out << (json ? ava::diagnostics::serialize_doctor_report_json(report) + '\n' : ava::diagnostics::serialize_doctor_report_human(report));
    return report.has_failures() ? 1 : 0;
  }
  catch (...)
  {
    static_cast<void>(err);
    if (json)
    {
      out << "{\"schema_version\":1,\"checks\":[{\"label\":\"version_platform\",\"status\":\"fail\",\"code\":\"invalid_configuration\","
             "\"items\":0,\"enabled\":0,\"errors\":1}],\"summary\":{\"pass\":0,\"warning\":0,\"fail\":1}}\n";
    }
    else
    {
      out << "AVA doctor\nfail version_platform [invalid_configuration] items=0 enabled=0 errors=1\nsummary pass=0 warning=0 fail=1\n";
    }
    return 1;
  }
}

int run_support_export(ava::config::XdgPaths const& paths, std::filesystem::path const& workspace_dir, std::ostream& out, std::ostream& err)
{
  try
  {
    Dout(dc::app, "operation=support_export state=start");
    auto report = collect_passive_doctor_report(paths, workspace_dir);
    auto trace = ava::diagnostics::read_trace_counter_snapshot(paths);
    auto last_failure = ava::diagnostics::read_last_failure_record(paths);
    ava::diagnostics::SupportArtifact artifact{
        .generated_at = now_seconds(), .doctor = std::move(report), .trace = std::move(trace), .last_failure = std::move(last_failure)};
    auto const publication = ava::diagnostics::publish_support_artifact(paths, artifact);
    Dout(dc::config,
         "operation=support_export state=result status=" << ava::diagnostics::to_string(publication.status) << " checks=" << artifact.doctor.checks.size());
    if (publication.status != ava::diagnostics::ArtifactWriteStatus::Success)
    {
      err << "Support artifact generation failed [" << ava::diagnostics::to_string(publication.status) << "].\n";
      return 1;
    }
    out << "Support artifact created: " << publication.path.string() << '\n';
    return 0;
  }
  catch (...)
  {
    err << "Support artifact generation failed [internal_failure].\n";
    return 1;
  }
}

}  // namespace ava::app
