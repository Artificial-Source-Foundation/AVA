#include "sys.h"
#include "ava/command/environment.h"
#include "ava/permissions/command_autonomy.h"
#include "ava/core/json.h"

namespace ava::permissions {

void CommandAutonomyState::set_mode(CommandAutonomyMode mode) noexcept
{
  auto previous = state_.load();
  while (!state_.compare_exchange_weak(previous, (((previous >> 2) + 1) * 4) + static_cast<std::uint64_t>(mode)))
  {
  }
}
auto CommandAutonomyState::mode_of(std::uint64_t snapshot) noexcept -> CommandAutonomyMode
{
  return static_cast<CommandAutonomyMode>(snapshot & 3);
}
auto to_string(CommandAutonomyMode mode) -> std::string
{
  switch (mode)
  {
    case CommandAutonomyMode::Manual:
      return "manual";
    case CommandAutonomyMode::Safe:
      return "safe";
    case CommandAutonomyMode::Reviewed:
      return "reviewed";
    case CommandAutonomyMode::High:
      return "high";
  }
  return "manual";
}
auto parse_command_autonomy_mode(std::string_view value) -> std::optional<CommandAutonomyMode>
{
  for (auto mode : {CommandAutonomyMode::Manual, CommandAutonomyMode::Safe, CommandAutonomyMode::Reviewed, CommandAutonomyMode::High})
  {
    if (value == to_string(mode))
    {
      return mode;
    }
  }
  return std::nullopt;
}
auto command_review_mode(CommandAutonomyMode mode) noexcept -> bool
{
  return mode == CommandAutonomyMode::Reviewed || mode == CommandAutonomyMode::High;
}
auto command_autonomy_digest(std::string_view value) -> std::string
{
  ava::command::detail::Sha256Builder digest;
  digest.append_field("ava-command-autonomy-v1");
  digest.append_field(value);
  return "sha256:" + digest.hex();
}
auto command_contract_digest(PermissionPrompt const& prompt) -> std::string
{
  ava::command::detail::Sha256Builder digest;
  digest.append_field(prompt.command);
  digest.append_field(prompt.workspace_dir.generic_string());
  digest.append_field(prompt.target_path.generic_string());
  digest.append_field(to_string(prompt.risk));
  digest.append_field(ava::core::to_string(prompt.mode));
  digest.append_field(prompt.permission_request_id);
  digest.append_field(prompt.tool_call_id);
  if (prompt.command_metadata)
  {
    auto const& metadata = *prompt.command_metadata;
    digest.append_field(metadata.fingerprint);
    digest.append_field(metadata.cwd.generic_string());
    digest.append_field(metadata.resolved_executable.generic_string());
    digest.append_field(ava::command::to_string(metadata.level));
    digest.append_field(ava::command::to_string(metadata.family));
    digest.append_field(ava::command::to_string(metadata.execution_domain));
    digest.append_field(ava::command::to_string(metadata.executable_origin));
    digest.append_field(to_string(metadata.containment_status));
    digest.append_field(metadata.containment_profile_id);
    digest.append_field(metadata.environment_digest);
    digest.append_field(metadata.environment_profile_id);
    digest.append_field(metadata.global_recipe_key);
    digest.append_field(metadata.workspace_recipe_key);
    digest.append_field(metadata.recipe_payload_version);
    digest.append_field(metadata.recipe_display);
    digest.append_field(ava::command::to_string(metadata.backend_maximum_scope));
    for (auto scope : metadata.effective_allowed_scopes)
    {
      digest.append_field(ava::command::to_string(scope));
    }
    digest.append_field(metadata.effect_profile);
    digest.append_field(metadata.review_presentation);
    for (bool flag :
         {metadata.executor_identity_verified, metadata.containment_available, metadata.executes_mutable_project_code, metadata.containment_network_allowed,
          metadata.scope_verified, metadata.disclosure_safe, metadata.capabilities.requires_containment, metadata.capabilities.network_enabled,
          metadata.capabilities.mutates_workspace, metadata.capabilities.executes_mutable_project_code, metadata.capabilities.destructive_or_privileged,
          metadata.capabilities.interpreter_inline, metadata.capabilities.unknown_wrapper, metadata.capabilities.raw_shell})
    {
      digest.append_number(static_cast<std::uintmax_t>(flag));
    }
  }
  return "sha256:" + digest.hex();
}

auto command_reviewer_eligible(PermissionPrompt const& prompt) noexcept -> bool
{
  if (prompt.operation != Operation::RunCommand || prompt.risk == PermissionRisk::Critical || !prompt.command_metadata)
  {
    return false;
  }
  auto const& metadata = *prompt.command_metadata;
  auto const& capabilities = metadata.capabilities;
  if (metadata.level != ava::command::CommandLevel::Standard || metadata.execution_domain != ava::command::CommandExecutionDomain::DirectArgv ||
      !metadata.executor_identity_verified || !metadata.scope_verified || !metadata.disclosure_safe || metadata.fingerprint.empty() ||
      metadata.review_presentation.empty() || capabilities.network_enabled || metadata.containment_network_allowed || capabilities.destructive_or_privileged ||
      capabilities.interpreter_inline || capabilities.unknown_wrapper || capabilities.raw_shell)
  {
    return false;
  }
  if (metadata.containment_status != CommandContainmentStatus::NotRequired && metadata.containment_status != CommandContainmentStatus::Available)
  {
    return false;
  }
  if (metadata.containment_status == CommandContainmentStatus::Available && !metadata.containment_available)
  {
    return false;
  }
  if ((capabilities.requires_containment || capabilities.executes_mutable_project_code || metadata.executes_mutable_project_code) &&
      (metadata.containment_status != CommandContainmentStatus::Available || !metadata.containment_available))
  {
    return false;
  }
  return metadata.effect_profile == "minimal_inspection" || metadata.effect_profile == "workspace_inspection" || metadata.effect_profile == "vcs_read_only" ||
         metadata.effect_profile == "executes_project_code";
}

auto command_deterministic_auto(PermissionPrompt const& prompt, CommandAutonomyMode mode) noexcept -> bool
{
  if (!prompt.command_metadata || !command_reviewer_eligible(prompt))
  {
    return false;
  }
  auto const& metadata = *prompt.command_metadata;
  if (metadata.effect_profile == "minimal_inspection")
  {
    return true;
  }
  if (mode == CommandAutonomyMode::Manual)
  {
    return false;
  }
  if (metadata.effect_profile == "workspace_inspection" && !metadata.executes_mutable_project_code)
  {
    return true;
  }
  return mode == CommandAutonomyMode::High;
}

auto command_review_input(CommandPermissionMetadata const& metadata) -> std::string
{
  using ava::core::json::escape;
  return R"({"command_presentation":")" + escape(metadata.review_presentation) + R"(","effect_profile":")" + escape(metadata.effect_profile) +
         R"(","family":")" + escape(ava::command::to_string(metadata.family)) +
         "\",\"cwd_scope\":\"workspace\",\"execution\":\"sealed_direct_argv\","
         "\"mutable_project_code\":" +
         (metadata.executes_mutable_project_code ? "true" : "false") + R"(,"network_allowed":false,"containment_status":")" +
         escape(to_string(metadata.containment_status)) +
         R"(","approval_reason":"Exact recognized effect profile; one-shot review within verified workspace and execution constraints"})";
}

auto command_review_audit_json(CommandReviewTransaction const& review, CommandPermissionMetadata const& metadata) -> std::string
{
  using ava::core::json::escape;
  return R"({"version":1,"reviewer_enabled":true,"autonomy_mode":")" + to_string(CommandAutonomyState::mode_of(review.autonomy_snapshot)) +
         "\",\"provider\":\"airouter\",\"model\":\"Qwen3.8\",\"prompt_version\":\"command-review-v2\",\"schema_version\":1,"
         "\"plan_fingerprint\":\"" +
         escape(review.plan_fingerprint) + R"(","request_nonce":")" + escape(review.nonce) + R"(","effect_profile":")" + escape(metadata.effect_profile) +
         "\",\"eligibility\":\"passed\","
         "\"gates\":[\"direct_argv\",\"noncritical\",\"verified_executor\",\"workspace_scope\",\"containment\",\"effect_allowlist\",\"disclosure\",\"no_deny\"]"
         ","
         "\"input_digest\":\"" +
         escape(review.input_digest) + R"(","status":")" + escape(review.status) + R"(","risk":")" + escape(review.risk) + R"(","recommendation":")" +
         escape(review.recommendation) + R"(","scope":"once","policy_recheck":")" + escape(review.policy_recheck) + "\"}";
}
} // namespace ava::permissions
