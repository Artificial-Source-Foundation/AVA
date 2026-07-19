#include "sys.h"
#include "ava/command/command.h"
#include "ava/command/discovery.h"
#include "ava/command/environment.h"
#include "ava/command/intent_internal.h"
#include "ava/command/policy.h"

#include <sstream>
#include <utility>

namespace ava::command {
namespace {

void append_path_metadata(detail::Sha256Builder& hash, PathMetadata const& metadata)
{
  hash.append_field(metadata.requested_path.string());
  hash.append_field(metadata.canonical_path.string());
  hash.append_number(metadata.device);
  hash.append_number(metadata.inode);
  hash.append_number(metadata.mode);
  hash.append_number(metadata.size);
  hash.append_number(metadata.owner);
  hash.append_number(metadata.link_count);
  hash.append_field(std::to_string(metadata.changed_seconds));
  hash.append_field(std::to_string(metadata.changed_nanoseconds));
  hash.append_field(metadata.requested_path_is_symlink ? "1" : "0");
  hash.append_number(metadata.requested_device);
  hash.append_number(metadata.requested_inode);
  hash.append_number(metadata.requested_mode);
  hash.append_number(metadata.requested_owner);
  hash.append_number(metadata.requested_link_count);
  hash.append_field(std::to_string(metadata.requested_changed_seconds));
  hash.append_field(std::to_string(metadata.requested_changed_nanoseconds));
  hash.append_number(metadata.ancestor_metadata.size());
  for (auto const& ancestor : metadata.ancestor_metadata)
  {
    hash.append_field(ancestor.path.string());
    hash.append_number(ancestor.device);
    hash.append_number(ancestor.inode);
    hash.append_number(ancestor.mode);
    hash.append_number(ancestor.owner);
    hash.append_number(ancestor.link_count);
    hash.append_field(std::to_string(ancestor.changed_seconds));
    hash.append_field(std::to_string(ancestor.changed_nanoseconds));
    hash.append_field(ancestor.is_symlink ? "1" : "0");
    hash.append_field(ancestor.identity_bound ? "1" : "0");
  }
}

void append_executable_metadata(detail::Sha256Builder& hash, ExecutableMetadata const& metadata)
{
  append_path_metadata(hash, PathMetadata{.requested_path = metadata.requested_path,
                                          .canonical_path = metadata.canonical_path,
                                          .device = metadata.device,
                                          .inode = metadata.inode,
                                          .mode = metadata.mode,
                                          .size = metadata.size,
                                          .owner = metadata.owner,
                                          .link_count = metadata.link_count,
                                          .changed_seconds = metadata.changed_seconds,
                                          .changed_nanoseconds = metadata.changed_nanoseconds,
                                          .requested_path_is_symlink = metadata.requested_path_is_symlink,
                                          .requested_device = metadata.requested_device,
                                          .requested_inode = metadata.requested_inode,
                                          .requested_mode = metadata.requested_mode,
                                          .requested_owner = metadata.requested_owner,
                                          .requested_link_count = metadata.requested_link_count,
                                          .requested_changed_seconds = metadata.requested_changed_seconds,
                                          .requested_changed_nanoseconds = metadata.requested_changed_nanoseconds,
                                          .ancestor_metadata = metadata.ancestor_metadata});
}

void append_synthetic_environment_roots(detail::Sha256Builder& hash, SyntheticEnvironmentRoots const& roots)
{
  for (auto const* root : {&roots.home, &roots.xdg_config_home, &roots.xdg_cache_home, &roots.xdg_data_home, &roots.xdg_state_home, &roots.tmpdir})
    append_path_metadata(hash, *root);
}

std::string compute_fingerprint(CommandPlan const& plan, PathMetadata const& workspace_metadata, PathMetadata const& cwd_metadata,
                                PathMetadata const& trusted_home_metadata, std::vector<PathMetadata> const& ava_authority_root_metadata,
                                SyntheticEnvironmentRoots const& synthetic_environment_roots)
{
  detail::Sha256Builder hash;
  hash.append_field("ava-command-plan-v3");
  hash.append_field(to_string(plan.intent_lane()));
  hash.append_field(to_string(plan.execution_domain()));
  hash.append_field(plan.workspace().string());
  append_path_metadata(hash, workspace_metadata);
  hash.append_field(plan.cwd().string());
  append_path_metadata(hash, cwd_metadata);
  append_path_metadata(hash, trusted_home_metadata);
  hash.append_number(ava_authority_root_metadata.size());
  for (auto const& root : ava_authority_root_metadata) append_path_metadata(hash, root);
  append_synthetic_environment_roots(hash, synthetic_environment_roots);
  hash.append_number(plan.argv().size());
  for (auto const& argument : plan.argv()) hash.append_field(argument);
  hash.append_field(plan.raw_shell_text());
  hash.append_number(plan.path_entries().size());
  for (auto const& entry : plan.path_entries())
  {
    hash.append_field(entry.directory.string());
    hash.append_field(to_string(entry.provenance));
    append_path_metadata(hash, entry.metadata);
  }
  hash.append_field(plan.resolved_executable() ? "resolved" : "unresolved");
  if (plan.resolved_executable())
  {
    append_executable_metadata(hash, plan.resolved_executable()->executable);
    hash.append_field(to_string(plan.resolved_executable()->origin));
    hash.append_field(plan.resolved_executable()->shebang_fully_resolved ? "resolved" : "partial");
    hash.append_number(plan.resolved_executable()->shebang_interpreters.size());
    for (auto const& interpreter : plan.resolved_executable()->shebang_interpreters)
    {
      append_executable_metadata(hash, interpreter.interpreter);
      hash.append_field(interpreter.argument);
    }
  }
  auto const& classification = plan.classification();
  hash.append_field(to_string(classification.level));
  hash.append_field(to_string(classification.family));
  hash.append_field(to_string(classification.max_interactive_scope));
  hash.append_field(classification.ask_candidate ? "1" : "0");
  hash.append_field(classification.capabilities.executes_mutable_project_code ? "1" : "0");
  hash.append_field(classification.capabilities.requires_containment ? "1" : "0");
  hash.append_field(classification.capabilities.network_enabled ? "1" : "0");
  hash.append_field(classification.capabilities.mutates_workspace ? "1" : "0");
  hash.append_field(classification.capabilities.destructive_or_privileged ? "1" : "0");
  hash.append_field(classification.capabilities.interpreter_inline ? "1" : "0");
  hash.append_field(classification.capabilities.unknown_wrapper ? "1" : "0");
  hash.append_field(classification.capabilities.raw_shell ? "1" : "0");
  hash.append_field(classification.recipe ? to_string(classification.recipe->recipe) : "none");
  if (classification.recipe)
  {
    hash.append_number(classification.recipe->canonical_argv.size());
    for (auto const& argument : classification.recipe->canonical_argv) hash.append_field(argument);
    hash.append_number(classification.recipe->path_arguments.size());
    for (auto const& path : classification.recipe->path_arguments) append_path_metadata(hash, path);
  }
  hash.append_field(plan.environment_profile_id());
  hash.append_field(plan.environment_digest());
  return "sha256:ava-command-plan-v3:" + hash.hex();
}

std::string json_escape(std::string_view value)
{
  std::string escaped;
  escaped.reserve(value.size() + 8);
  for (unsigned char const ch : value)
  {
    switch (ch)
    {
      case '"':
        escaped += "\\\"";
        break;
      case '\\':
        escaped += "\\\\";
        break;
      case '\b':
        escaped += "\\b";
        break;
      case '\f':
        escaped += "\\f";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        if (ch < 0x20)
        {
          static constexpr char kHex[] = "0123456789abcdef";
          escaped += "\\u00";
          escaped.push_back(kHex[(ch >> 4U) & 0x0fU]);
          escaped.push_back(kHex[ch & 0x0fU]);
        }
        else
        {
          escaped.push_back(static_cast<char>(ch));
        }
    }
  }
  return escaped;
}

ava::core::Result<bool> fresh(PathMetadata const& metadata)
{
  auto result = detail::path_metadata_is_fresh(metadata);
  if (!result)
    return std::unexpected(std::move(result.error()));
  return *result;
}

ava::core::Result<bool> fresh(ExecutableMetadata const& metadata)
{
  auto result = detail::executable_metadata_is_fresh(metadata);
  if (!result)
    return std::unexpected(std::move(result.error()));
  return *result;
}

bool paths_overlap(std::filesystem::path const& first, std::filesystem::path const& second)
{
  std::error_code error;
  auto first_relative = std::filesystem::relative(first, second, error);
  if (!error && (first_relative.empty() || first_relative == "." || first_relative.begin() == first_relative.end() || *first_relative.begin() != ".."))
    return true;
  error.clear();
  auto second_relative = std::filesystem::relative(second, first, error);
  return !error && (second_relative.empty() || second_relative == "." || second_relative.begin() == second_relative.end() || *second_relative.begin() != "..");
}

}  // namespace

ava::core::Result<CommandPlan> seal_command_plan(CommandIntent const& intent, CommandBuildOptions const& options)
{
  if (auto valid = detail::validate_limits(options.limits); !valid)
    return std::unexpected(std::move(valid.error()));
  if (intent.lane() == CommandIntentLane::RawShell)
  {
    if (auto valid = detail::validate_raw_shell(intent.source_text(), options.limits); !valid)
      return std::unexpected(std::move(valid.error()));
  }
  else if (auto valid = detail::validate_structured_argv(intent.argv(), options.limits); !valid)
  {
    return std::unexpected(std::move(valid.error()));
  }

  auto context = detail::discover_command_context(intent, options);
  if (!context)
    return std::unexpected(std::move(context.error()));
  auto environment = detail::EnvironmentFactory::make(options.environment, context->path_entries, context->synthetic_environment_roots, options.limits,
                                                      CommandEnvironment::make_factory_passkey());
  if (!environment)
    return std::unexpected(std::move(environment.error()));

  CommandPlan plan;
  plan.intent_lane_ = intent.lane();
  plan.workspace_ = context->workspace;
  plan.cwd_ = context->cwd;
  plan.workspace_metadata_ = std::move(context->workspace_metadata);
  plan.cwd_metadata_ = std::move(context->cwd_metadata);
  plan.trusted_home_metadata_ = std::move(context->trusted_home_metadata);
  plan.ava_authority_root_metadata_ = std::move(context->ava_authority_root_metadata);
  plan.synthetic_environment_roots_ = std::move(context->synthetic_environment_roots);
  plan.path_entries_ = std::move(context->path_entries);
  plan.environment_profile_id_ = environment->profile_id();
  plan.environment_digest_ = environment->digest();
  if (intent.lane() == CommandIntentLane::RawShell)
  {
    if (options.shell.empty() || !options.shell.is_absolute())
    {
      return std::unexpected(detail::command_error(ava::core::ErrorCategory::InvalidArgument, "raw-shell plans require an absolute configured shell executable",
                                                   "shell", options.shell.string()));
    }
    plan.execution_domain_ = CommandExecutionDomain::RawShell;
    plan.raw_shell_text_ = std::string(intent.source_text());
    auto shell = detail::resolve_executable({options.shell.string()}, plan.path_entries_, plan.cwd_, plan.workspace_, options.trusted_home, options.limits);
    if (!shell)
      return std::unexpected(std::move(shell.error()));
    plan.resolved_executable_ = std::move(*shell);
    plan.classification_ = detail::classify_raw_shell(*plan.resolved_executable_);
  }
  else
  {
    plan.execution_domain_ = CommandExecutionDomain::DirectArgv;
    plan.argv_ = intent.argv();
    auto resolved = detail::resolve_executable(plan.argv_, plan.path_entries_, plan.cwd_, plan.workspace_, options.trusted_home, options.limits);
    if (!resolved)
      return std::unexpected(std::move(resolved.error()));
    plan.classification_ = detail::classify_command(plan.argv_, *resolved, plan.cwd_, plan.workspace_, options.workspace_script_recipes);
    plan.resolved_executable_ = std::move(*resolved);
  }
  plan.fingerprint_ = compute_fingerprint(plan, plan.workspace_metadata_, plan.cwd_metadata_, plan.trusted_home_metadata_, plan.ava_authority_root_metadata_,
                                          plan.synthetic_environment_roots_);
  return plan;
}

ava::core::Result<CommandPreparation> prepare_command(CommandIntent const& intent, CommandBuildOptions const& options)
{
  auto plan = seal_command_plan(intent, options);
  if (!plan)
    return std::unexpected(std::move(plan.error()));
  auto environment = detail::EnvironmentFactory::make(options.environment, plan->path_entries(), plan->synthetic_environment_roots_, options.limits,
                                                      CommandEnvironment::make_factory_passkey());
  if (!environment)
    return std::unexpected(std::move(environment.error()));
  if (auto valid = detail::validate_environment_matches_plan(*environment, *plan); !valid)
    return std::unexpected(std::move(valid.error()));
  return CommandPreparation(std::move(*plan), std::move(*environment));
}

ava::core::Result<bool> plan_is_fresh(CommandPlan const& plan)
{
  // Resolve executable/interpreter identities first. If an executable was
  // removed or became inaccessible, preserve that inspection error rather than
  // masking it as a stale PATH directory caused by the same unlink.
  if (plan.resolved_executable_)
  {
    auto result = fresh(plan.resolved_executable_->executable);
    if (!result)
      return std::unexpected(std::move(result.error()));
    if (!*result)
      return false;
    for (auto const& interpreter : plan.resolved_executable_->shebang_interpreters)
    {
      result = fresh(interpreter.interpreter);
      if (!result)
        return std::unexpected(std::move(result.error()));
      if (!*result)
        return false;
    }
  }
  for (auto const* metadata : {&plan.workspace_metadata_, &plan.cwd_metadata_, &plan.trusted_home_metadata_})
  {
    auto result = fresh(*metadata);
    if (!result)
      return std::unexpected(std::move(result.error()));
    if (!*result)
      return false;
  }
  for (auto const& root : plan.ava_authority_root_metadata_)
  {
    auto result = fresh(root);
    if (!result)
      return std::unexpected(std::move(result.error()));
    if (!*result)
      return false;
  }
  for (auto const* root :
       {&plan.synthetic_environment_roots_.home, &plan.synthetic_environment_roots_.xdg_config_home, &plan.synthetic_environment_roots_.xdg_cache_home,
        &plan.synthetic_environment_roots_.xdg_data_home, &plan.synthetic_environment_roots_.xdg_state_home, &plan.synthetic_environment_roots_.tmpdir})
  {
    auto result = fresh(*root);
    if (!result)
      return std::unexpected(std::move(result.error()));
    if (!*result)
      return false;
    if (paths_overlap(root->canonical_path, plan.workspace_metadata_.canonical_path) ||
        paths_overlap(root->canonical_path, plan.trusted_home_metadata_.canonical_path))
      return false;
    for (auto const& authority_root : plan.ava_authority_root_metadata_)
    {
      if (paths_overlap(root->canonical_path, authority_root.canonical_path))
        return false;
    }
  }
  for (auto const& entry : plan.path_entries_)
  {
    auto result = fresh(entry.metadata);
    if (!result)
      return std::unexpected(std::move(result.error()));
    if (!*result)
      return false;
  }
  if (plan.classification_.recipe)
  {
    for (auto const& path : plan.classification_.recipe->path_arguments)
    {
      auto result = fresh(path);
      if (!result)
        return std::unexpected(std::move(result.error()));
      if (!*result)
        return false;
    }
  }
  return true;
}

CommandIntentLane CommandPlan::intent_lane() const noexcept
{
  return intent_lane_;
}

CommandExecutionDomain CommandPlan::execution_domain() const noexcept
{
  return execution_domain_;
}

std::filesystem::path const& CommandPlan::workspace() const noexcept
{
  return workspace_;
}

std::filesystem::path const& CommandPlan::cwd() const noexcept
{
  return cwd_;
}

std::vector<std::string> const& CommandPlan::argv() const noexcept
{
  return argv_;
}

std::string const& CommandPlan::raw_shell_text() const noexcept
{
  return raw_shell_text_;
}

std::vector<CommandPathEntry> const& CommandPlan::path_entries() const noexcept
{
  return path_entries_;
}

std::optional<ResolvedExecutable> const& CommandPlan::resolved_executable() const noexcept
{
  return resolved_executable_;
}

CommandClassification const& CommandPlan::classification() const noexcept
{
  return classification_;
}

std::string const& CommandPlan::environment_profile_id() const noexcept
{
  return environment_profile_id_;
}

std::string const& CommandPlan::environment_digest() const noexcept
{
  return environment_digest_;
}

std::string const& CommandPlan::fingerprint() const noexcept
{
  return fingerprint_;
}

std::string CommandPlan::display_json() const
{
  std::ostringstream output;
  output << "{\"fingerprint\":\"" << json_escape(fingerprint_) << "\",\"intent_lane\":\"" << to_string(intent_lane_) << "\",\"execution_domain\":\""
         << to_string(execution_domain_) << "\",\"workspace\":\"" << json_escape(workspace_.string()) << "\",\"cwd\":\"" << json_escape(cwd_.string())
         << "\",\"environment_profile_id\":\"" << json_escape(environment_profile_id_) << "\",\"environment_digest\":\"" << json_escape(environment_digest_)
         << "\",\"level\":\"" << to_string(classification_.level) << "\",\"family\":\"" << to_string(classification_.family) << "\",\"argv\":[";
  for (std::size_t index = 0; index < argv_.size(); ++index)
  {
    if (index != 0)
      output << ',';
    output << '"' << json_escape(argv_[index]) << '"';
  }
  output << ']';
  if (!raw_shell_text_.empty())
    output << ",\"raw_shell\":\"" << json_escape(raw_shell_text_) << '"';
  output << '}';
  return std::move(output).str();
}

std::string CommandPlan::redacted_summary() const
{
  std::ostringstream output;
  output << "{\"fingerprint\":\"" << json_escape(fingerprint_) << "\",\"intent_lane\":\"" << to_string(intent_lane_) << "\",\"execution_domain\":\""
         << to_string(execution_domain_) << "\",\"level\":\"" << to_string(classification_.level) << "\",\"family\":\"" << to_string(classification_.family)
         << "\",\"resolved_executable\":" << (resolved_executable_ ? "true" : "false") << '}';
  return std::move(output).str();
}

CommandPreparation::CommandPreparation(CommandPlan plan, CommandEnvironment environment) : plan_(std::move(plan)), environment_(std::move(environment))
{
}

CommandPlan const& CommandPreparation::plan() const noexcept
{
  return plan_;
}

CommandEnvironment const& CommandPreparation::environment() const noexcept
{
  return environment_;
}

bool command_mode_is_enabled(CommandRuntimeOptions const& options) noexcept
{
  return options.mode == CommandRuntimeMode::Enabled;
}

bool command_mode_is_prompt_only(CommandRuntimeOptions const& options) noexcept
{
  return options.mode == CommandRuntimeMode::PromptOnly;
}

std::string_view to_string(CommandIntentLane value) noexcept
{
  switch (value)
  {
    case CommandIntentLane::Compatibility:
      return "compatibility";
    case CommandIntentLane::StructuredArgv:
      return "structured_argv";
    case CommandIntentLane::RawShell:
      return "raw_shell";
  }
  return "unknown";
}

std::string_view to_string(CommandRuntimeMode value) noexcept
{
  switch (value)
  {
    case CommandRuntimeMode::Legacy:
      return "legacy";
    case CommandRuntimeMode::PromptOnly:
      return "prompt-only";
    case CommandRuntimeMode::Enabled:
      return "enabled";
  }
  return "unknown";
}

std::string_view to_string(CommandExecutionDomain value) noexcept
{
  switch (value)
  {
    case CommandExecutionDomain::DirectArgv:
      return "direct_argv";
    case CommandExecutionDomain::RawShell:
      return "raw_shell";
  }
  return "unknown";
}

std::string_view to_string(PathProvenance value) noexcept
{
  switch (value)
  {
    case PathProvenance::StartupPath:
      return "startup_path";
    case PathProvenance::UserLocal:
      return "user_local";
    case PathProvenance::UserCargo:
      return "user_cargo";
    case PathProvenance::WorkspaceVenv:
      return "workspace_venv";
    case PathProvenance::WorkspaceNodeModules:
      return "workspace_node_modules";
  }
  return "unknown";
}

std::string_view to_string(ExecutableOrigin value) noexcept
{
  switch (value)
  {
    case ExecutableOrigin::System:
      return "system";
    case ExecutableOrigin::User:
      return "user";
    case ExecutableOrigin::Workspace:
      return "workspace";
  }
  return "unknown";
}

std::string_view to_string(CommandLevel value) noexcept
{
  switch (value)
  {
    case CommandLevel::Standard:
      return "standard";
    case CommandLevel::Sensitive:
      return "sensitive";
    case CommandLevel::Critical:
      return "critical";
  }
  return "unknown";
}

std::string_view to_string(CommandFamily value) noexcept
{
  switch (value)
  {
    case CommandFamily::Inspection:
      return "inspection";
    case CommandFamily::CmakeBuild:
      return "cmake_build";
    case CommandFamily::Ctest:
      return "ctest";
    case CommandFamily::Ninja:
      return "ninja";
    case CommandFamily::Make:
      return "make";
    case CommandFamily::Cargo:
      return "cargo";
    case CommandFamily::PackageManagerScript:
      return "package_manager_script";
    case CommandFamily::Pytest:
      return "pytest";
    case CommandFamily::WorkspaceScript:
      return "workspace_script";
    case CommandFamily::InstallOrUpdate:
      return "install_or_update";
    case CommandFamily::RemoteGitMutation:
      return "remote_git_mutation";
    case CommandFamily::PublishOrDeploy:
      return "publish_or_deploy";
    case CommandFamily::Network:
      return "network";
    case CommandFamily::WorkspaceMutation:
      return "workspace_mutation";
    case CommandFamily::InterpreterInline:
      return "interpreter_inline";
    case CommandFamily::DestructiveOrPrivileged:
      return "destructive_or_privileged";
    case CommandFamily::UnknownWrapper:
      return "unknown_wrapper";
    case CommandFamily::RawShell:
      return "raw_shell";
    case CommandFamily::UnverifiedDelegatedExecutor:
      return "unverified_delegated_executor";
  }
  return "unknown";
}

std::string_view to_string(CommandRecipe value) noexcept
{
  switch (value)
  {
    case CommandRecipe::Pwd:
      return "pwd";
    case CommandRecipe::Ls:
      return "ls";
    case CommandRecipe::GitStatus:
      return "git_status";
    case CommandRecipe::GitDiff:
      return "git_diff";
    case CommandRecipe::GitLogOne:
      return "git_log_one";
    case CommandRecipe::CmakeBuild:
      return "cmake_build";
    case CommandRecipe::Ctest:
      return "ctest";
    case CommandRecipe::Ninja:
      return "ninja";
    case CommandRecipe::Make:
      return "make";
    case CommandRecipe::CargoBuild:
      return "cargo_build";
    case CommandRecipe::CargoCheck:
      return "cargo_check";
    case CommandRecipe::CargoTest:
      return "cargo_test";
    case CommandRecipe::PackageManagerRunScript:
      return "package_manager_run_script";
    case CommandRecipe::Pytest:
      return "pytest";
    case CommandRecipe::WorkspaceScript:
      return "workspace_script";
  }
  return "unknown";
}

std::string_view to_string(InteractiveScope value) noexcept
{
  switch (value)
  {
    case InteractiveScope::Once:
      return "once";
    case InteractiveScope::Session:
      return "session";
    case InteractiveScope::Workspace:
      return "workspace";
  }
  return "unknown";
}

}  // namespace ava::command
