#pragma once

#include "ava/core/result.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "debug.h"

namespace ava::command {

enum class CommandIntentLane
{
  Compatibility,
  StructuredArgv,
  RawShell,
};

enum class CommandRuntimeMode
{
  Legacy,
  PromptOnly,
  Enabled,
};

enum class CommandExecutionDomain
{
  DirectArgv,
  RawShell,
};

enum class PathProvenance
{
  StartupPath,
  UserLocal,
  UserCargo,
  WorkspaceVenv,
  WorkspaceNodeModules,
};

enum class ExecutableOrigin
{
  System,
  User,
  Workspace,
};

enum class CommandLevel
{
  Standard,
  Sensitive,
  Critical,
};

enum class CommandFamily
{
  Inspection,
  CmakeBuild,
  Ctest,
  Ninja,
  Make,
  Cargo,
  PackageManagerScript,
  Pytest,
  WorkspaceScript,
  InstallOrUpdate,
  RemoteGitMutation,
  PublishOrDeploy,
  Network,
  WorkspaceMutation,
  InterpreterInline,
  DestructiveOrPrivileged,
  UnknownWrapper,
  RawShell,
};

enum class CommandRecipe
{
  Pwd,
  Ls,
  GitStatus,
  GitDiff,
  GitLogOne,
  CmakeBuild,
  Ctest,
  Ninja,
  Make,
  CargoBuild,
  CargoCheck,
  CargoTest,
  PackageManagerRunScript,
  Pytest,
  WorkspaceScript,
};

// Global approval is deliberately not an interactive scope. It remains an
// explicit configuration decision outside command prompts.
enum class InteractiveScope
{
  Once,
  Session,
  Workspace,
};

struct CommandLimits
{
  std::size_t max_request_bytes = 16 * 1024;
  std::size_t max_argument_bytes = 4 * 1024;
  std::size_t max_argv_entries = 128;
  std::size_t max_path_entries = 64;
  std::size_t max_path_bytes = 16 * 1024;
  std::size_t max_shebang_bytes = 512;
  std::size_t max_shebang_depth = 4;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

// These are the paths exposed to the child, not paths used for host-side tool
// discovery. CommandBuildOptions::trusted_home supplies the latter.
struct CommandEnvironmentOptions
{
  std::string profile_id;
  std::string user;
  std::string logname;
  std::filesystem::path home;
  std::filesystem::path xdg_config_home;
  std::filesystem::path xdg_cache_home;
  std::filesystem::path xdg_data_home;
  std::filesystem::path xdg_state_home;
  std::filesystem::path tmpdir;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct WorkspaceScriptRecipe
{
  std::filesystem::path script;
  std::vector<std::string> argv_tail;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct CommandRuntimeOptions
{
  CommandRuntimeMode mode = CommandRuntimeMode::Legacy;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct CommandBuildOptions
{
  std::filesystem::path workspace;
  // The real, trusted host home used only to discover user-local toolchains.
  // It must be distinct from the synthetic child HOME.
  std::filesystem::path trusted_home;
  std::optional<std::string> startup_path;
  // Raw shell plans resolve and bind this exact executable; it is never found
  // by basename lookup. Descriptor-anchored execution remains future work.
  std::filesystem::path shell = "/bin/sh";
  CommandEnvironmentOptions environment;
  std::vector<WorkspaceScriptRecipe> workspace_script_recipes;
  CommandLimits limits = {};

  AVA_DEBUG_PRINT_MEMBERS_ON
};

// Captures both the requested spelling and the canonical target. The requested
// identity records final-component symlink provenance without following a FIFO
// or other blocking special file during metadata inspection.
struct PathMetadata
{
  std::filesystem::path requested_path;
  std::filesystem::path canonical_path;
  std::uintmax_t device = 0;
  std::uintmax_t inode = 0;
  std::uintmax_t mode = 0;
  std::uintmax_t size = 0;
  std::uintmax_t owner = 0;
  std::uintmax_t link_count = 0;
  std::int64_t changed_seconds = 0;
  std::int64_t changed_nanoseconds = 0;
  bool requested_path_is_symlink = false;
  std::uintmax_t requested_device = 0;
  std::uintmax_t requested_inode = 0;
  std::uintmax_t requested_mode = 0;
  std::uintmax_t requested_link_count = 0;
  std::int64_t requested_changed_seconds = 0;
  std::int64_t requested_changed_nanoseconds = 0;

  friend bool operator==(PathMetadata const&, PathMetadata const&) = default;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct CommandPathEntry
{
  std::filesystem::path directory;
  PathProvenance provenance = PathProvenance::StartupPath;
  PathMetadata metadata;

  friend bool operator==(CommandPathEntry const&, CommandPathEntry const&) = default;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct ExecutableMetadata
{
  std::filesystem::path requested_path;
  std::filesystem::path canonical_path;
  std::uintmax_t device = 0;
  std::uintmax_t inode = 0;
  std::uintmax_t mode = 0;
  std::uintmax_t size = 0;
  std::uintmax_t owner = 0;
  std::uintmax_t link_count = 0;
  std::int64_t changed_seconds = 0;
  std::int64_t changed_nanoseconds = 0;
  bool requested_path_is_symlink = false;
  std::uintmax_t requested_device = 0;
  std::uintmax_t requested_inode = 0;
  std::uintmax_t requested_mode = 0;
  std::uintmax_t requested_link_count = 0;
  std::int64_t requested_changed_seconds = 0;
  std::int64_t requested_changed_nanoseconds = 0;

  friend bool operator==(ExecutableMetadata const&, ExecutableMetadata const&) = default;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct ResolvedExecutable
{
  ExecutableMetadata executable;
  ExecutableOrigin origin = ExecutableOrigin::System;
  std::vector<ExecutableMetadata> shebang_interpreters;

  friend bool operator==(ResolvedExecutable const&, ResolvedExecutable const&) = default;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct CommandCapabilities
{
  bool executes_mutable_project_code = false;
  bool requires_containment = false;
  bool network_enabled = false;
  bool mutates_workspace = false;
  bool destructive_or_privileged = false;
  bool interpreter_inline = false;
  bool unknown_wrapper = false;
  bool raw_shell = false;

  friend bool operator==(CommandCapabilities const&, CommandCapabilities const&) = default;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct RecipeDescriptor
{
  CommandRecipe recipe = CommandRecipe::Pwd;
  std::vector<std::string> canonical_argv;
  std::vector<PathMetadata> path_arguments;

  friend bool operator==(RecipeDescriptor const&, RecipeDescriptor const&) = default;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct CommandClassification
{
  CommandLevel level = CommandLevel::Critical;
  CommandFamily family = CommandFamily::UnknownWrapper;
  std::optional<RecipeDescriptor> recipe;
  CommandCapabilities capabilities;
  InteractiveScope max_interactive_scope = InteractiveScope::Once;
  bool ask_candidate = true;

  friend bool operator==(CommandClassification const&, CommandClassification const&) = default;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct EnvironmentVariable
{
  std::string key;
  std::string value;

  friend bool operator==(EnvironmentVariable const&, EnvironmentVariable const&) = default;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

class CommandEnvironment;
class CommandIntent;
class CommandPlan;
class CommandPreparation;

namespace detail {
class EnvironmentFactory;
}

class CommandEnvironment final
{
 public:
  [[nodiscard]] std::string const& profile_id() const noexcept;
  [[nodiscard]] std::string const& digest() const noexcept;
  [[nodiscard]] std::vector<EnvironmentVariable> const& entries() const noexcept;

  AVA_DEBUG_PRINT_MEMBERS_ON

 private:
  friend class detail::EnvironmentFactory;

  CommandEnvironment() = default;

  std::string profile_id_;
  std::string digest_;
  std::vector<EnvironmentVariable> entries_;
};

class CommandIntent final
{
 public:
  [[nodiscard]] static ava::core::Result<CommandIntent> compatibility(std::string command, CommandLimits limits = {});
  [[nodiscard]] static ava::core::Result<CommandIntent> structured(std::vector<std::string> argv, std::optional<std::filesystem::path> cwd = std::nullopt,
                                                                   CommandLimits limits = {});
  [[nodiscard]] static ava::core::Result<CommandIntent> raw_shell(std::string shell_text, CommandLimits limits = {});

  [[nodiscard]] CommandIntentLane lane() const noexcept;
  [[nodiscard]] bool has_argv() const noexcept;
  [[nodiscard]] std::vector<std::string> const& argv() const noexcept;
  [[nodiscard]] std::optional<std::filesystem::path> const& requested_cwd() const noexcept;
  [[nodiscard]] std::string_view source_text() const noexcept;

  AVA_DEBUG_PRINT_MEMBERS_ON

 private:
  CommandIntent(CommandIntentLane lane, std::vector<std::string> argv, std::optional<std::filesystem::path> cwd, std::string source_text);

  CommandIntentLane lane_ = CommandIntentLane::Compatibility;
  std::vector<std::string> argv_;
  std::optional<std::filesystem::path> cwd_;
  std::string source_text_;
};

class CommandPlan final
{
 public:
  [[nodiscard]] CommandIntentLane intent_lane() const noexcept;
  [[nodiscard]] CommandExecutionDomain execution_domain() const noexcept;
  [[nodiscard]] std::filesystem::path const& workspace() const noexcept;
  [[nodiscard]] std::filesystem::path const& cwd() const noexcept;
  [[nodiscard]] std::vector<std::string> const& argv() const noexcept;
  [[nodiscard]] std::string const& raw_shell_text() const noexcept;
  [[nodiscard]] std::vector<CommandPathEntry> const& path_entries() const noexcept;
  [[nodiscard]] std::optional<ResolvedExecutable> const& resolved_executable() const noexcept;
  [[nodiscard]] CommandClassification const& classification() const noexcept;
  [[nodiscard]] std::string const& environment_profile_id() const noexcept;
  [[nodiscard]] std::string const& environment_digest() const noexcept;
  [[nodiscard]] std::string const& fingerprint() const noexcept;
  [[nodiscard]] std::string display_json() const;

  friend bool operator==(CommandPlan const&, CommandPlan const&) = default;

  AVA_DEBUG_PRINT_MEMBERS_ON

 private:
  friend ava::core::Result<CommandPlan> seal_command_plan(CommandIntent const&, CommandBuildOptions const&);
  friend ava::core::Result<bool> plan_is_fresh(CommandPlan const&);

  CommandPlan() = default;

  CommandIntentLane intent_lane_ = CommandIntentLane::Compatibility;
  CommandExecutionDomain execution_domain_ = CommandExecutionDomain::DirectArgv;
  std::filesystem::path workspace_;
  std::filesystem::path cwd_;
  PathMetadata workspace_metadata_;
  PathMetadata cwd_metadata_;
  std::vector<std::string> argv_;
  std::string raw_shell_text_;
  std::vector<CommandPathEntry> path_entries_;
  std::optional<ResolvedExecutable> resolved_executable_;
  CommandClassification classification_;
  std::string environment_profile_id_;
  std::string environment_digest_;
  std::string fingerprint_;
};

class CommandPreparation final
{
 public:
  [[nodiscard]] CommandPlan const& plan() const noexcept;
  [[nodiscard]] CommandEnvironment const& environment() const noexcept;

  AVA_DEBUG_PRINT_MEMBERS_ON

 private:
  friend ava::core::Result<CommandPreparation> prepare_command(CommandIntent const&, CommandBuildOptions const&);

  CommandPreparation(CommandPlan plan, CommandEnvironment environment);

  CommandPlan plan_;
  CommandEnvironment environment_;
};

[[nodiscard]] ava::core::Result<CommandPlan> seal_command_plan(CommandIntent const& intent, CommandBuildOptions const& options);
[[nodiscard]] ava::core::Result<CommandPreparation> prepare_command(CommandIntent const& intent, CommandBuildOptions const& options);
[[nodiscard]] ava::core::Result<bool> plan_is_fresh(CommandPlan const& plan);

[[nodiscard]] bool command_mode_is_enabled(CommandRuntimeOptions const& options) noexcept;
[[nodiscard]] bool command_mode_is_prompt_only(CommandRuntimeOptions const& options) noexcept;

[[nodiscard]] std::string_view to_string(CommandIntentLane value) noexcept;
[[nodiscard]] std::string_view to_string(CommandRuntimeMode value) noexcept;
[[nodiscard]] std::string_view to_string(CommandExecutionDomain value) noexcept;
[[nodiscard]] std::string_view to_string(PathProvenance value) noexcept;
[[nodiscard]] std::string_view to_string(ExecutableOrigin value) noexcept;
[[nodiscard]] std::string_view to_string(CommandLevel value) noexcept;
[[nodiscard]] std::string_view to_string(CommandFamily value) noexcept;
[[nodiscard]] std::string_view to_string(CommandRecipe value) noexcept;
[[nodiscard]] std::string_view to_string(InteractiveScope value) noexcept;

}  // namespace ava::command
