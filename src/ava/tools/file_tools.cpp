#include "sys.h"
#include "ava/tools/diff_utils.h"
#include "ava/tools/edit_history.h"
#include "ava/tools/edit_match.h"
#include "ava/tools/file_io.h"
#include "ava/tools/file_tools.h"
#include "ava/tools/mutation_queue.h"
#include "ava/tools/secure_workspace.h"
#include "ava/permissions/permission_rules.h"
#include "ava/core/ids.h"
#include "ava/core/json.h"
#include "ava/core/mode.h"
#include "ava/core/path.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

namespace ava::tools {

namespace {

constexpr std::size_t kMaxPermissionDiffBytes = 32 * 1024;

struct PermissionDiffPreview
{
  std::string text;
  bool truncated = false;
};

struct CommandReviewLease
{
  std::shared_ptr<ava::permissions::CommandReviewTransaction> transaction;
  explicit CommandReviewLease(std::shared_ptr<ava::permissions::CommandReviewTransaction> pending) : transaction(std::move(pending)) { }
  CommandReviewLease(CommandReviewLease const&) = delete;
  auto operator=(CommandReviewLease const&) -> CommandReviewLease& = delete;
  CommandReviewLease(CommandReviewLease&&) = delete;
  auto operator=(CommandReviewLease&&) -> CommandReviewLease& = delete;
  ~CommandReviewLease()
  {
    if (transaction)
    {
      transaction->admission_check = nullptr;
    }
  }
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

using detail::check_canceled;
using detail::is_canceled_error;
using detail::read_all_text;
using detail::read_head_text;

std::string effective_tool_name(ToolContext const& context, ava::permissions::Operation operation, std::string_view tool_name)
{
  if (!tool_name.empty())
    return std::string(tool_name);
  if (!context.permission_tool_name.empty())
    return context.permission_tool_name;
  return ava::permissions::to_string(operation);
}

ava::core::VoidResult record_permission_audit(ToolContext const& context, PermissionAuditEvent const& event)
{
  if (context.permission_request_ids && !event.permission_request_id.empty() &&
      std::ranges::find(*context.permission_request_ids, event.permission_request_id) == context.permission_request_ids->end())
  {
    context.permission_request_ids->push_back(event.permission_request_id);
  }
  if (!context.permission_audit_sink)
    return {};
  return context.permission_audit_sink(event);
}

std::string safe_command_display(std::optional<ava::permissions::CommandPermissionMetadata> const& command_metadata)
{
  if (command_metadata && !command_metadata->recipe_display.empty())
    return command_metadata->recipe_display;
  return "<redacted one-shot command>";
}

PermissionAuditEvent audit_event(ToolContext const& context, std::string permission_request_id, ava::permissions::Operation operation, std::string tool_name,
                                 ava::permissions::PermissionDecision const& decision, std::filesystem::path const& target_path, std::string_view command,
                                 std::optional<ava::permissions::CommandPermissionMetadata> command_metadata)
{
  bool const run_command = operation == ava::permissions::Operation::RunCommand;
  return PermissionAuditEvent{
      .permission_request_id = std::move(permission_request_id),
      .operation = operation,
      .mode = context.mode,
      .tool_name = std::move(tool_name),
      .action = decision.action,
      .reason = run_command ? std::string("command permission decision") : decision.reason,
      .risk = decision.risk,
      .target_path = target_path,
      .command = run_command ? safe_command_display(command_metadata)
                             : (context.redact_permission_audit_arguments && !command.empty() ? std::string("[redacted]") : std::string(command)),
      .resolution = "",
      .resolution_source = "policy",
      .resolution_reason = "",
      .actor = context.permission_actor.empty() ? std::string("agent") : context.permission_actor,
      .rule_id = "",
      .command_arguments_redacted = context.redact_permission_audit_arguments,
      .command_metadata = std::move(command_metadata),
      .command_review_json = {},
      .autonomy_mode = {}};
}

void capture_permission_denial_guidance(ToolContext const& context, std::string_view user_guidance)
{
  if (!context.permission_denial_guidance_capture)
    return;
  // Revalidate at the backend trust boundary. Direct app command contexts leave
  // the capture null and discard guidance because no model continuation exists.
  if (auto validated = ava::permissions::validated_permission_user_guidance(user_guidance))
    context.permission_denial_guidance_capture->provider_user_guidance = std::move(*validated);
  else
    context.permission_denial_guidance_capture->provider_user_guidance.clear();
}

auto command_permission_error(ava::permissions::PermissionDecision const& decision, std::optional<ava::permissions::CommandPermissionMetadata> const& metadata,
                              std::string_view resolution) -> ava::core::Error
{
  bool const approval_required = decision.action == ava::permissions::PermissionAction::Ask && resolution == "no_resolver";
  bool const macos_fallback = metadata && ava::permissions::command_uses_macos_approval_fallback(*metadata);
  std::string message = "Command not executed: permission was denied.";
  std::string status = "command_denied";
  std::string reason = "command permission denied";
  if (approval_required)
  {
    message = macos_fallback ? "Command not executed: macOS native containment is unavailable and this command requires one-shot user approval."
                             : "Command not executed: explicit user approval is required.";
    status = "approval_required";
    reason = "user approval required";
  }
  else if (resolution == "resolver_failed")
  {
    message = "Command not executed: the approval resolver failed.";
    status = "approval_error";
    reason = "approval resolver failed";
  }
  auto error = ava::core::Error(ava::core::ErrorCategory::PermissionDenied, std::move(message));
  error.with_context("command_status", std::move(status));
  error.with_context("executed", "false");
  error.with_context("reason", std::move(reason));
  if (metadata)
  {
    error.with_context("containment", ava::permissions::to_string(metadata->containment_status));
    if (!ava::permissions::command_permission_allows_reusable_grant(*metadata))
    {
      error.with_context("approval_scope", "once");
    }
  }
  if (macos_fallback)
  {
    error.with_context("platform", "macos");
  }
  std::string next_action;
  if (approval_required)
  {
    next_action =
        "No approval prompt is active. Request the user's approval through the interactive TUI or RPC permission flow; wait while a prompt is pending. ";
  }
  next_action += "Do not retry or work around the permission system. Do not infer command output or repository state from this unexecuted command.";
  error.with_context("next_action", std::move(next_action));
  return error;
}

ava::core::Error permission_denied_error(std::string_view error_message, ava::permissions::Operation operation,
                                         ava::permissions::PermissionDecision const& decision, std::filesystem::path const& target_path,
                                         std::string_view command, std::optional<ava::permissions::CommandPermissionMetadata> const& command_metadata,
                                         std::string_view resolution_context, std::string_view resolution_reason = {},
                                         std::string_view permission_request_id = {})
{
  bool const run_command = operation == ava::permissions::Operation::RunCommand;
  auto error = run_command ? command_permission_error(decision, command_metadata, resolution_context)
                           : ava::core::Error(ava::core::ErrorCategory::PermissionDenied, std::string(error_message));
  error.with_context("action", ava::permissions::to_string(decision.action));
  if (!run_command)
  {
    error.with_context("reason", decision.reason);
  }
  error.with_context("risk", ava::permissions::to_string(decision.risk));
  if (!permission_request_id.empty())
  {
    error.with_context("request_id", std::string(permission_request_id));
  }
  if (run_command)
  {
    error.with_context("command", safe_command_display(command_metadata));
  }
  else if (!command.empty())
  {
    error.with_context("command", std::string(command));
  }
  else
  {
    error.with_context("path", target_path.string());
  }
  // User-authored guidance never enters Error context/format: public tool-result details,
  // diagnostics, audits, and events consume this sanitized Error only.
  if (decision.action == ava::permissions::PermissionAction::Ask)
  {
    error.with_context("resolution", std::string(resolution_context));
    if (!run_command && !resolution_reason.empty())
      error.with_context("resolution_reason", std::string(resolution_reason));
    if (resolution_context == "no_resolver")
      error.with_context("headless_hint", "permission ask failed closed because no interactive or RPC resolver was available");
  }
  if (!permission_request_id.empty())
  {
    auto const id = std::string(permission_request_id);
    error.with_context("inspect", "/permissions audit show " + id);
    error.with_context("diagnose", "/permissions diagnose " + id);
  }
  return error;
}

std::shared_ptr<MutationQueue> effective_mutation_queue(ToolContext const& context)
{
  if (context.mutation_queue)
    return context.mutation_queue;
  return default_mutation_queue();
}

using ava::core::normalized_absolute_path;

void append_permission_rule_store_for_config_dir(std::vector<ava::permissions::PermissionRuleStore>& stores, ToolContext const& context,
                                                 std::filesystem::path const& config_dir)
{
  if (context.workspace_dir.empty() || config_dir.empty())
    return;
  stores.push_back(ava::permissions::PermissionRuleStore{.global_rules_file = config_dir / "permission-rules.json",
                                                         .workspace_rules_file = context.workspace_dir / ".ava" / "permission-rules.json",
                                                         .workspace_dir = context.workspace_dir,
                                                         .anchor_set = context.anchor_set});
}

std::vector<ava::permissions::PermissionRuleStore> context_permission_rule_stores(ToolContext const& context)
{
  std::vector<ava::permissions::PermissionRuleStore> stores;
  if (!context.mcp_global_config_file.empty())
  {
    append_permission_rule_store_for_config_dir(stores, context, context.mcp_global_config_file.parent_path());
  }
  if (!context.plugin_global_plugins_dir.empty())
  {
    append_permission_rule_store_for_config_dir(stores, context, context.plugin_global_plugins_dir.parent_path());
  }
  return stores;
}

bool is_legacy_workspace_permission_rules_path(ToolContext const& context, std::filesystem::path const& path)
{
  if (context.workspace_dir.empty())
    return false;
  auto const protected_path = normalized_absolute_path(context.workspace_dir / ".ava" / "permission-rules.json");
  return normalized_absolute_path(path) == protected_path;
}

bool is_enforceable_permission_rules_path(ToolContext const& context, std::filesystem::path const& path)
{
  if (ava::permissions::is_registered_enforceable_permission_rules_file(path))
    return true;
  for (auto const& store : context_permission_rule_stores(context))
  {
    if (ava::permissions::is_enforceable_permission_rules_file(store, path))
      return true;
  }
  return false;
}

ava::core::VoidResult reject_permission_rules_file_mutation(ToolContext const& context, std::filesystem::path const& path)
{
  if (!is_legacy_workspace_permission_rules_path(context, path) && !is_enforceable_permission_rules_path(context, path))
    return {};

  auto error = ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "permission rule files cannot be modified by normal file tools");
  error.with_context("path", normalized_absolute_path(path).string());
  error.with_context("management", "use permission_rule_add or permission_rule_remove RPC commands");
  return std::unexpected(std::move(error));
}

ava::core::Result<bool> write_permission_diff_preview_read_allowed(ToolContext const& context, std::filesystem::path const& path)
{
  auto const decision = ava::permissions::decide(ava::permissions::PermissionRequest{
      .operation = ava::permissions::Operation::ReadFile,
      .mode = context.mode,
      .workspace_dir = context.workspace_dir,
      .target_path = path,
      .command = "",
  });
  if (decision.action != ava::permissions::PermissionAction::Allow)
    return false;
  if (!context.auto_allow_deny_preflight)
    return true;

  auto const permission_request_id = ava::core::make_id("permreq");
  auto const tool_name = effective_tool_name(context, ava::permissions::Operation::ReadFile, {});
  auto preflight = context.auto_allow_deny_preflight(ava::permissions::PermissionPrompt{
      .permission_request_id = permission_request_id,
      .tool_call_id = context.current_call_id,
      .operation = ava::permissions::Operation::ReadFile,
      .mode = context.mode,
      .workspace_dir = context.workspace_dir,
      .target_path = path,
      .command = "",
      .tool_name = tool_name,
      .reason = decision.reason,
      .risk = decision.risk,
  });
  if (preflight && (*preflight == ava::permissions::PermissionResolution::Allow || *preflight == ava::permissions::PermissionResolution::AllowSessionGrant))
  {
    return true;
  }

  auto denied = decision;
  denied.action = ava::permissions::PermissionAction::Deny;
  denied.risk = ava::permissions::PermissionRisk::Critical;
  denied.reason = preflight ? (preflight->reason.empty() ? std::string("operation denied by persistent policy") : preflight->reason)
                            : "persistent deny preflight failed: " + preflight.error().format();
  auto event = audit_event(context, permission_request_id, ava::permissions::Operation::ReadFile, tool_name, denied, path, "", std::nullopt);
  event.resolution = "deny";
  event.resolution_source = preflight && !preflight->resolution_source.empty() ? preflight->resolution_source : "persistent_rule_error";
  event.resolution_reason = denied.reason;
  if (preflight)
    event.rule_id = preflight->rule_id;
  if (auto audited = record_permission_audit(context, event); !audited)
    return std::unexpected(std::move(audited.error()));
  return false;
}

ava::core::Result<std::optional<PermissionDiffPreview>> write_permission_diff_preview(ToolContext const& context, std::filesystem::path const& path,
                                                                                      std::string_view content)
{
  if (auto canceled = check_canceled(context, "write_file_permission_preview", path); !canceled)
  {
    return std::unexpected(std::move(canceled.error()));
  }
  // Permission must precede every installed frontend call. A local preview
  // would also be misleading when the frontend owns the exact written bytes.
  if (context.exact_file_access && context.exact_file_access->supports_write_text_file())
    return std::optional<PermissionDiffPreview>{};
  bool exists = false;
  auto permission_target = path;
  if (context.secure_workspace)
  {
    auto resolved = context.secure_workspace->resolve(path, SecureWorkspaceResolveMode::AllowMissing);
    if (!resolved)
      return std::unexpected(std::move(resolved.error()));
    exists = resolved->exists;
    permission_target = std::move(resolved->absolute);
  }
  else
  {
    std::error_code exists_error;
    exists = std::filesystem::exists(path, exists_error);
    if (exists_error)
      return std::optional<PermissionDiffPreview>{};
  }

  std::string original;
  if (exists)
  {
    auto read_allowed = write_permission_diff_preview_read_allowed(context, permission_target);
    // An overwrite may proceed without a diff preview, but denied old bytes
    // must not be read or returned through that optional preview. This safety
    // check is deliberately preflight-only: preparing a write prompt must not
    // acquire separate frontend read authority.
    if (!read_allowed || !*read_allowed)
      return std::optional<PermissionDiffPreview>{};
    auto current = context.exact_file_access && !context.exact_file_access->supports_write_text_file()
                       ? detail::read_all_text_local_only(context, path, "write_file_permission_preview")
                       : read_all_text(context, path, "write_file_permission_preview");
    if (!current)
    {
      if (is_canceled_error(current.error()))
        return std::unexpected(std::move(current.error()));
      return std::optional<PermissionDiffPreview>{};
    }
    original = std::move(*current);
  }

  auto diff = unified_diff(original, content, path, path, kMaxPermissionDiffBytes);
  return std::optional<PermissionDiffPreview>{PermissionDiffPreview{.text = std::move(diff.text), .truncated = diff.truncated}};
}

ava::core::VoidResult announce_execution_start_impl(ToolContext const& context)
{
  if (!context.announce_execution_after_permission || !context.progress_sink || !context.execution_started)
    return {};
  if (context.execution_started->exchange(true, std::memory_order_acq_rel))
    return {};
  return context.progress_sink(ToolProgressEvent{
      .text = "authorized; execution starting", .call_id = context.current_call_id, .tool_name = context.current_tool_name, .status = "in_progress"});
}

}  // namespace

ava::core::VoidResult announce_tool_execution_start(ToolContext const& context)
{
  return announce_execution_start_impl(context);
}

auto can_read_file_for_edit_snapshot(ToolContext const& context, std::filesystem::path const& path) -> ava::core::Result<bool>
{
  return write_permission_diff_preview_read_allowed(context, path);
}

namespace {

void apply_native_command_policy(ToolContext const& context, auto const& make_command_prompt, ava::permissions::PermissionDecision& decision,
                                 ava::permissions::CommandPolicySnapshot& command_policy,
                                 std::optional<ava::permissions::PermissionResolutionDecision>& native_resolution,
                                 ava::permissions::CommandAutonomyMode autonomy_mode)
{
  using namespace ava::permissions;

    // Absolute Deny precedes every mode, including already-known inspection.
  if (context.command_policy_reader)
  {
    auto policy = context.command_policy_reader(make_command_prompt());
    if (!policy || (policy->rule && policy->rule->resolution == PermissionResolution::Deny))
    {
      decision = {.action = PermissionAction::Deny, .reason = "command denied by current persistent policy", .risk = PermissionRisk::Critical};
      if (policy && policy->rule && policy->rule->resolution == PermissionResolution::Deny)
      {
        // Keep the authoritative rule that caused this deterministic deny so the
        // policy audit event in ensure_permission_impl can attribute it exactly.
        command_policy = std::move(*policy);
      }
    }
    else
    {
      command_policy = std::move(*policy);
    }
  }
  if (decision.action != PermissionAction::Deny && context.auto_allow_deny_preflight)
  {
    auto preflight = context.auto_allow_deny_preflight(make_command_prompt());
    if (!preflight || preflight->resolution != PermissionResolution::Allow)
    {
      decision = {.action = PermissionAction::Deny, .reason = "command deny preflight failed", .risk = PermissionRisk::Critical};
    }
  }
  if (decision.action != PermissionAction::Deny)
  {
      // Keep one policy Ask and a separately sourced resolution in the journal.
    decision.action = PermissionAction::Ask;
    auto const prompt = make_command_prompt();
    if (decision.risk != PermissionRisk::Critical && command_policy.rule && command_prompt_allows_persistent_allow(prompt))
    {
      native_resolution = command_policy.rule;
    }
    else if (command_deterministic_auto(prompt, autonomy_mode))
    {
      native_resolution.emplace(PermissionResolution::Allow, "exact deterministic command effect profile");
      native_resolution->resolution_source = "deterministic_command_auto";
    }
  }
}

auto admit_command_review(ToolContext const& context, ava::permissions::PermissionPrompt& prompt, ava::command::CommandPlan const& sealed_plan,
                          ava::permissions::CommandPolicySnapshot const& command_policy, std::uint64_t autonomy_snapshot,
                          std::optional<ava::permissions::CommandPermissionMetadata> const& command_metadata) -> ava::core::VoidResult
{
  using namespace ava::permissions;

  if (!command_metadata)
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "command metadata is missing before review admission"));
  }

  auto fresh = ava::command::plan_is_fresh(sealed_plan);
  if (!fresh || !*fresh)
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "command is stale before reviewer admission"));
  }
  auto transaction = std::make_shared<CommandReviewTransaction>();
  transaction->nonce = ava::core::make_id("command-review");
  transaction->plan_fingerprint = sealed_plan.fingerprint();
  transaction->contract_digest = command_contract_digest(prompt);
  transaction->policy_revision = command_policy.revision;
  transaction->autonomy_snapshot = autonomy_snapshot;
  transaction->input = command_review_input(*command_metadata);
  transaction->input_digest = command_autonomy_digest(transaction->input);
  transaction->admission_check = [&context, &sealed_plan, admitted_prompt = prompt, revision = command_policy.revision, autonomy_snapshot]() -> bool {
    if ((context.cancel_requested && context.cancel_requested()) || !context.command_autonomy || context.command_autonomy->snapshot() != autonomy_snapshot)
    {
      return false;
    }
    auto current = context.command_policy_reader(admitted_prompt);
    auto current_plan = ava::command::plan_is_fresh(sealed_plan);
    return current && current_plan && *current_plan && current->revision == revision &&
           (!current->rule || current->rule->resolution != PermissionResolution::Deny);
  };
  prompt.command_review = std::move(transaction);

  return {};
}

void recheck_deterministic_command(ToolContext const& context, ava::permissions::PermissionPrompt const& prompt,
                                   ava::permissions::CommandPolicySnapshot const& command_policy,
                                   ava::core::Result<ava::permissions::PermissionResolutionDecision>& resolution)
{
  using namespace ava::permissions;

    // A frontend callback may have waited while policy changed. Auto authority
    // must still be below current Deny at the backend acceptance point.
  auto current = context.command_policy_reader ? context.command_policy_reader(prompt) : ava::core::Result<CommandPolicySnapshot>{command_policy};
  bool denied = !current || (current->rule && current->rule->resolution == PermissionResolution::Deny);
  if (!denied && context.auto_allow_deny_preflight)
  {
    auto preflight = context.auto_allow_deny_preflight(prompt);
    denied = !preflight || preflight->resolution != PermissionResolution::Allow;
  }
  if (denied || (context.cancel_requested && context.cancel_requested()))
  {
    resolution = PermissionResolutionDecision{PermissionResolution::Deny, "automatic command authority denied by current policy"};
  }
}

void recheck_reviewed_command(ToolContext const& context, ava::permissions::PermissionPrompt const& prompt, ava::command::CommandPlan const* sealed_plan,
                              ava::permissions::CommandPolicySnapshot const& command_policy, std::uint64_t autonomy_snapshot,
                              ava::core::Result<ava::permissions::PermissionResolutionDecision>& resolution,
                              std::optional<ava::permissions::CommandPermissionMetadata> const& command_metadata)
{
  using namespace ava::permissions;

    // A frontend's Allow is not reviewer authority. Re-evaluate the native
    // contract and current rules using this backend's exact sealed plan.
  auto receipt = prompt.command_review;
  auto current_policy =
      context.command_policy_reader
          ? context.command_policy_reader(prompt)
          : ava::core::Result<CommandPolicySnapshot>{std::unexpected(ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "no command policy reader"))};
  bool const policy_denied = !current_policy || (current_policy->rule && current_policy->rule->resolution == PermissionResolution::Deny);
  auto fresh = (sealed_plan != nullptr) ? ava::command::plan_is_fresh(*sealed_plan) : ava::core::Result<bool>{false};
  bool const canceled = context.cancel_requested && context.cancel_requested();
  bool const valid = !policy_denied && !canceled && fresh && *fresh && receipt && resolution->resolution == PermissionResolution::Allow &&
                     resolution->command_review == receipt && receipt->status == "validated" && receipt->recommendation == "approve" &&
                     (receipt->risk == "low" || receipt->risk == "medium") && command_reviewer_eligible(prompt) &&
                     receipt->plan_fingerprint == sealed_plan->fingerprint() && receipt->contract_digest == command_contract_digest(prompt) &&
                     context.command_autonomy && context.command_autonomy->snapshot() == receipt->autonomy_snapshot &&
                     command_review_mode(context.command_autonomy->mode()) && receipt->autonomy_snapshot == autonomy_snapshot &&
                     receipt->policy_revision == command_policy.revision && current_policy->revision == command_policy.revision && command_metadata &&
                     receipt->input == command_review_input(*command_metadata) && receipt->input_digest == command_autonomy_digest(receipt->input);
  if (receipt)
  {
    receipt->policy_recheck = valid ? "passed" : "failed";
  }
  if (!valid)
  {
    if (policy_denied || canceled || !fresh || !*fresh)
    {
      resolution = PermissionResolutionDecision{PermissionResolution::Deny, "review authorization precondition failed"};
      resolution->resolution_source = "review_precondition_failed";
    }
    else
    {
        // No automatic retry and no reuse of the stale receipt. Ordinary human
        // fallback is still behind the persistent-rule resolver.
      auto human_prompt = prompt;
      human_prompt.command_review.reset();
      human_prompt.reason = "Command review became stale; a fresh human decision is required";
      resolution = context.permission_resolver ? context.permission_resolver(human_prompt)
                                               : ava::core::Result<PermissionResolutionDecision>{PermissionResolutionDecision{PermissionResolution::Deny}};
      if (resolution && resolution->resolution_source == "qwen_command_review")
      {
        resolution = PermissionResolutionDecision{PermissionResolution::Deny, "review receipt cannot be retried"};
      }
    }
  }
}

auto resolve_and_recheck_command_permission(ToolContext const& context, ava::permissions::PermissionPrompt& prompt,
                                            ava::command::CommandPlan const* sealed_plan, ava::permissions::CommandPolicySnapshot const& command_policy,
                                            std::uint64_t autonomy_snapshot,
                                            std::optional<ava::permissions::PermissionResolutionDecision> const& native_resolution, bool native_command,
                                            std::optional<ava::permissions::CommandPermissionMetadata> const& command_metadata)
    -> ava::core::Result<ava::permissions::PermissionResolutionDecision>
{
  using namespace ava::permissions;
  prompt.deterministic_auto_candidate = native_resolution && native_resolution->resolution_source == "deterministic_command_auto";
  // TUI sessions consult their legal scoped grant before the automatic tier.
  // A frontend without shared autonomy state retains backend-only auto-Allow.
  bool const consult_frontend = prompt.deterministic_auto_candidate && context.command_autonomy && context.permission_resolver;
  auto resolution =
      native_resolution && !consult_frontend ? ava::core::Result<PermissionResolutionDecision>{*native_resolution} : context.permission_resolver(prompt);
  if (resolution && resolution->resolution_source == "deterministic_command_auto" &&
      (!native_command || !command_deterministic_auto(prompt, context.command_autonomy ? context.command_autonomy->mode() : CommandAutonomyMode::Safe)))
  {
    resolution = PermissionResolutionDecision{PermissionResolution::Deny, "deterministic command authority changed"};
  }
  if (resolution && resolution->resolution_source == "deterministic_command_auto")
  {
    recheck_deterministic_command(context, prompt, command_policy, resolution);
  }
  if (resolution && resolution->resolution_source == "qwen_command_review")
  {
    recheck_reviewed_command(context, prompt, sealed_plan, command_policy, autonomy_snapshot, resolution, command_metadata);
  }
  if (resolution && *resolution == ava::permissions::PermissionResolution::AllowSessionGrant && command_metadata &&
      (prompt.risk == PermissionRisk::Critical || !ava::permissions::command_permission_allows_reusable_grant(*command_metadata)))
  {
    ava::permissions::PermissionResolutionDecision denied(ava::permissions::PermissionResolution::Deny,
                                                          "the sealed command backend permits only one-shot approval");
    denied.resolution_source = "review_precondition_failed";
    resolution = std::move(denied);
  }

  return resolution;
}

auto maybe_admit_command_review(ToolContext const& context, ava::permissions::PermissionPrompt& prompt, ava::command::CommandPlan const* sealed_plan,
                                ava::permissions::CommandPolicySnapshot const& command_policy, std::uint64_t autonomy_snapshot,
                                std::optional<ava::permissions::PermissionResolutionDecision> const& native_resolution, bool native_command,
                                std::optional<ava::permissions::CommandPermissionMetadata> const& command_metadata) -> ava::core::VoidResult
{
  using namespace ava::permissions;
  auto const autonomy_mode = CommandAutonomyState::mode_of(autonomy_snapshot);
  if (native_command && !native_resolution && context.command_policy_reader && context.command_autonomy && command_review_mode(autonomy_mode) &&
      command_reviewer_eligible(prompt))
  {
    if (auto admitted = admit_command_review(context, prompt, *sealed_plan, command_policy, autonomy_snapshot, command_metadata); !admitted)
    {
      return admitted;
    }
  }
  return {};
}

auto command_review_audit_prefix(PermissionAuditEvent const& event) -> std::string
{
  std::string data = "{";
  if (!event.autonomy_mode.empty())
  {
    data += R"("autonomy_mode":")" + ava::core::json::escape(event.autonomy_mode) + "\",";
  }
  if (!event.command_review_json.empty())
  {
    data += "\"command_review\":" + event.command_review_json + ',';
  }
  return data;
}

auto record_permission_resolution(ToolContext const& context, ava::permissions::PermissionPrompt const& prompt, PermissionAuditEvent const& policy_event,
                                  ava::core::Result<ava::permissions::PermissionResolutionDecision> const& resolution,
                                  std::optional<ava::permissions::CommandPermissionMetadata> const& command_metadata) -> ava::core::VoidResult
{
  auto outcome_event = policy_event;
  if (prompt.command_review && command_metadata)
  {
    outcome_event.command_review_json = command_review_audit_json(*prompt.command_review, *command_metadata);
    // Drop the backend-scoped callback before any copied receipt can outlive
    // this invocation. The worker has already been joined by the resolver.
    prompt.command_review->admission_check = nullptr;
  }
  if (resolution && !resolution->resolution_source.empty())
  {
    outcome_event.resolution_source = resolution->resolution_source;
  }
  else
  {
    outcome_event.resolution_source = resolution && *resolution == ava::permissions::PermissionResolution::AllowSessionGrant ? "session_grant" : "resolver";
  }
  if (!resolution)
  {
    outcome_event.resolution_source = "resolver_failed";
  }
  outcome_event.resolution = resolution ? ava::permissions::to_string(*resolution) : "deny";
  if (policy_event.operation != ava::permissions::Operation::RunCommand)
  {
    if (resolution)
    {
      outcome_event.resolution_reason = resolution->reason;
    }
    else
    {
      outcome_event.resolution_reason = resolution.error().format();
    }
  }
  if (resolution)
  {
    outcome_event.rule_id = resolution->rule_id;
  }
  if (auto audited = record_permission_audit(context, outcome_event); !audited)
  {
    return std::unexpected(std::move(audited.error()));
  }
  return {};
}

ava::core::VoidResult ensure_permission_impl(ToolContext const& context, ava::permissions::Operation operation, std::filesystem::path const& target_path,
                                             std::string_view command, std::string_view tool_name, std::string_view error_message,
                                             std::string_view diff_preview, bool diff_truncated,
                                             std::optional<ava::permissions::CommandPermissionMetadata> command_metadata, bool omit_policy_allow_audit,
                                             ava::command::CommandPlan const* sealed_plan = nullptr)
{
  auto permission_target = target_path;
  if (context.secure_workspace && (operation == ava::permissions::Operation::ReadFile || operation == ava::permissions::Operation::EditFile ||
                                   operation == ava::permissions::Operation::SearchFiles))
  {
    auto const remote_read = context.exact_file_access && context.exact_file_access->supports_read_text_file();
    auto const mode = operation == ava::permissions::Operation::EditFile || (operation == ava::permissions::Operation::ReadFile && remote_read)
                          ? SecureWorkspaceResolveMode::AllowMissing
                          : SecureWorkspaceResolveMode::Existing;
    auto resolved = context.secure_workspace->resolve(target_path, mode);
    if (!resolved)
      return std::unexpected(std::move(resolved.error()));
    permission_target = std::move(resolved->absolute);
  }

  if (operation == ava::permissions::Operation::EditFile)
  {
    if (auto protected_file = reject_permission_rules_file_mutation(context, permission_target); !protected_file)
    {
      return protected_file;
    }
  }

  auto const request_tool_name = effective_tool_name(context, operation, tool_name);
  auto const permission_request_id = ava::core::make_id("permreq");
  auto decision = ava::permissions::decide(ava::permissions::PermissionRequest{
      .operation = operation,
      .mode = context.mode,
      .workspace_dir = context.workspace_dir,
      .target_path = permission_target,
      .command = std::string(command),
      .command_metadata = command_metadata,
  });
  using namespace ava::permissions;
  auto make_command_prompt = [&] -> PermissionPrompt {
    return PermissionPrompt{.permission_request_id = permission_request_id,
                            .tool_call_id = context.current_call_id,
                            .operation = operation,
                            .mode = context.mode,
                            .workspace_dir = context.workspace_dir,
                            .target_path = permission_target,
                            .command = std::string(command),
                            .tool_name = request_tool_name,
                            .reason = decision.reason,
                            .risk = decision.risk,
                            .command_metadata = command_metadata};
  };
  CommandPolicySnapshot command_policy{.revision = "unconfigured"};
  auto const autonomy_snapshot = context.command_autonomy ? context.command_autonomy->snapshot() : std::uint64_t{5};
  auto const autonomy_mode = CommandAutonomyState::mode_of(autonomy_snapshot);
  bool const native_command = (sealed_plan != nullptr) && operation == Operation::RunCommand && command_metadata;
  std::optional<PermissionResolutionDecision> native_resolution;
  if (native_command && decision.action != PermissionAction::Deny)
  {
    apply_native_command_policy(context, make_command_prompt, decision, command_policy, native_resolution, autonomy_mode);
  }
  bool const backend_policy_allowed = decision.action == ava::permissions::PermissionAction::Allow;
  if (context.require_explicit_file_permissions && decision.action == ava::permissions::PermissionAction::Allow &&
      (operation == ava::permissions::Operation::ReadFile || operation == ava::permissions::Operation::EditFile))
  {
    decision.action = ava::permissions::PermissionAction::Ask;
    decision.reason = "The frontend requires explicit approval for model-initiated file access after backend safety checks";
    decision.risk = operation == ava::permissions::Operation::EditFile ? ava::permissions::PermissionRisk::Medium : ava::permissions::PermissionRisk::Low;
  }

  std::string preflight_source;
  std::string preflight_rule_id;
  std::string preflight_reason;
  auto const needs_auto_allow_deny_preflight = backend_policy_allowed && context.auto_allow_deny_preflight;
  if (needs_auto_allow_deny_preflight)
  {
    auto preflight = context.auto_allow_deny_preflight(ava::permissions::PermissionPrompt{
        .permission_request_id = permission_request_id,
        .tool_call_id = context.current_call_id,
        .operation = operation,
        .mode = context.mode,
        .workspace_dir = context.workspace_dir,
        .target_path = permission_target,
        .command = std::string(command),
        .tool_name = request_tool_name,
        .reason = decision.reason,
        .risk = decision.risk,
        .command_metadata = command_metadata,
    });
    if (!preflight || (*preflight != ava::permissions::PermissionResolution::Allow && *preflight != ava::permissions::PermissionResolution::AllowSessionGrant))
    {
      decision.action = ava::permissions::PermissionAction::Deny;
      decision.risk = ava::permissions::PermissionRisk::Critical;
      decision.reason = preflight ? (preflight->reason.empty() ? std::string("operation denied by persistent policy") : preflight->reason)
                                  : "persistent deny preflight failed: " + preflight.error().format();
      preflight_source = preflight && !preflight->resolution_source.empty() ? preflight->resolution_source : "persistent_rule_error";
      preflight_rule_id = preflight ? preflight->rule_id : std::string{};
      preflight_reason = decision.reason;
    }
  }

  auto policy_event = audit_event(context, permission_request_id, operation, request_tool_name, decision, permission_target, command, command_metadata);
  if (native_command)
  {
    policy_event.autonomy_mode = to_string(autonomy_mode);
  }
  if (!preflight_source.empty())
  {
    policy_event.resolution_source = std::move(preflight_source);
    if (operation != ava::permissions::Operation::RunCommand)
      policy_event.resolution_reason = std::move(preflight_reason);
    policy_event.rule_id = std::move(preflight_rule_id);
  }
  else if (decision.action == ava::permissions::PermissionAction::Deny && command_policy.rule &&
           command_policy.rule->resolution == ava::permissions::PermissionResolution::Deny && !command_policy.rule->resolution_source.empty())
  {
    // The native persistent policy reader denied before any resolver or reviewer
    // ran; attribute the record to the exact rule, as the auto-allow preflight
    // path does, instead of the anonymous built-in "policy" default.
    policy_event.resolution_source = command_policy.rule->resolution_source;
    policy_event.rule_id = command_policy.rule->rule_id;
  }
  if (decision.action == ava::permissions::PermissionAction::Allow || decision.action == ava::permissions::PermissionAction::Deny)
  {
    policy_event.resolution = ava::permissions::to_string(decision.action);
  }
  if (!(omit_policy_allow_audit && decision.action == ava::permissions::PermissionAction::Allow))
  {
    if (auto audited = record_permission_audit(context, policy_event); !audited)
    {
      return std::unexpected(std::move(audited.error()));
    }
  }
  if (decision.action == ava::permissions::PermissionAction::Allow)
  {
    return {};
  }
  if (decision.action == ava::permissions::PermissionAction::Deny)
  {
    return std::unexpected(
        permission_denied_error(error_message, operation, decision, permission_target, command, command_metadata, "policy", "", permission_request_id));
  }

  if (!context.permission_resolver && !native_resolution)
  {
    auto outcome_event = policy_event;
    outcome_event.resolution = "deny";
    outcome_event.resolution_source = "no_resolver";
    if (auto audited = record_permission_audit(context, outcome_event); !audited)
    {
      return std::unexpected(std::move(audited.error()));
    }
    return std::unexpected(
        permission_denied_error(error_message, operation, decision, permission_target, command, command_metadata, "no_resolver", "", permission_request_id));
  }

  auto prompt = ava::permissions::PermissionPrompt{
      .permission_request_id = permission_request_id,
      .tool_call_id = context.current_call_id,
      .operation = operation,
      .mode = context.mode,
      .workspace_dir = context.workspace_dir,
      .target_path = permission_target,
      .command = std::string(command),
      .tool_name = request_tool_name,
      .reason = decision.reason,
      .risk = decision.risk,
      .diff_preview = std::string(diff_preview),
      .diff_truncated = diff_truncated,
      .command_metadata = command_metadata,
  };
  if (auto admitted =
          maybe_admit_command_review(context, prompt, sealed_plan, command_policy, autonomy_snapshot, native_resolution, native_command, command_metadata);
      !admitted)
  {
    return admitted;
  }
  CommandReviewLease review_lease{prompt.command_review};
  auto resolution = resolve_and_recheck_command_permission(context, prompt, sealed_plan, command_policy, autonomy_snapshot, native_resolution, native_command,
                                                           command_metadata);

  if (auto audited = record_permission_resolution(context, prompt, policy_event, resolution, command_metadata); !audited)
  {
    return audited;
  }
  if (resolution && (*resolution == ava::permissions::PermissionResolution::Allow || *resolution == ava::permissions::PermissionResolution::AllowSessionGrant))
  {
    return {};
  }
  if (resolution && *resolution == ava::permissions::PermissionResolution::Cancel)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Unknown, "agent loop canceled");
    error.with_context("permission_request_id", permission_request_id);
    if (operation != ava::permissions::Operation::RunCommand)
      error.with_context("resolution_reason", resolution->reason);
    return std::unexpected(std::move(error));
  }

  auto const resolution_context = resolution ? ava::permissions::to_string(*resolution) : std::string("resolver_failed");
  auto const resolution_reason = resolution ? resolution->reason : resolution.error().format();
  capture_permission_denial_guidance(context, resolution ? std::string_view(resolution->user_guidance) : std::string_view{});
  return std::unexpected(permission_denied_error(error_message, operation, decision, permission_target, command, command_metadata, resolution_context,
                                                 resolution_reason, permission_request_id));
}

}  // namespace

ava::core::VoidResult ensure_permission(ToolContext const& context, ava::permissions::Operation operation, std::filesystem::path const& target_path,
                                        std::string_view command, std::string_view tool_name, std::string_view error_message, std::string_view diff_preview,
                                        bool diff_truncated, std::optional<ava::permissions::CommandPermissionMetadata> command_metadata)
{
  return ensure_permission_impl(context, operation, target_path, command, tool_name, error_message, diff_preview, diff_truncated, std::move(command_metadata),
                                false);
}

ava::core::VoidResult ensure_filtered_read_permission(ToolContext const& context, std::filesystem::path const& target_path, std::string_view tool_name,
                                                      std::string_view error_message)
{
  return ensure_permission_impl(context, ava::permissions::Operation::ReadFile, target_path, "", tool_name, error_message, "", false, std::nullopt, true);
}

ava::core::VoidResult ensure_command_permission(ToolContext const& context, std::string_view command, ava::command::CommandPreparation const& preparation,
                                                bool unverified_delegated_executor, std::string_view tool_name, std::string_view error_message)
{
  auto metadata = ava::permissions::command_permission_metadata(preparation.plan(), unverified_delegated_executor);
  return ensure_permission_impl(context, ava::permissions::Operation::RunCommand, preparation.plan().cwd(), command, tool_name, error_message, {}, false,
                                std::move(metadata), false, &preparation.plan());
}

ava::core::VoidResult ensure_command_permission(ToolContext const& context, std::string_view command, ava::command::CommandPreparation const& preparation,
                                                ava::permissions::CommandContainmentInfo const& containment, bool unverified_delegated_executor,
                                                std::string_view tool_name, std::string_view error_message)
{
  auto metadata = ava::permissions::command_permission_metadata(preparation.plan(), containment, unverified_delegated_executor);
  return ensure_permission_impl(context, ava::permissions::Operation::RunCommand, preparation.plan().cwd(), command, tool_name, error_message, {}, false,
                                std::move(metadata), false, &preparation.plan());
}

std::string permission_audit_data_json(PermissionAuditEvent const& event)
{
  auto data = command_review_audit_prefix(event);
  if (!event.permission_request_id.empty())
  {
    data += "\"permission_request_id\":\"" + ava::core::json::escape(event.permission_request_id) + "\",";
  }
  data += "\"operation\":\"" + ava::core::json::escape(ava::permissions::to_string(event.operation)) + "\",\"mode\":\"" +
          ava::core::json::escape(ava::core::to_string(event.mode)) + "\",\"tool_name\":\"" + ava::core::json::escape(event.tool_name) + "\",\"action\":\"" +
          ava::core::json::escape(ava::permissions::to_string(event.action)) + "\",\"reason\":\"" + ava::core::json::escape(event.reason) + "\",\"risk\":\"" +
          ava::core::json::escape(ava::permissions::to_string(event.risk)) + "\"";
  if (event.operation != ava::permissions::Operation::RunCommand && event.operation != ava::permissions::Operation::NetworkFetch && !event.target_path.empty())
  {
    data += ",\"target_path\":\"" + ava::core::json::escape(event.target_path.string()) + "\"";
  }
  if (event.operation == ava::permissions::Operation::RunCommand || event.command_arguments_redacted || !event.command.empty())
  {
    std::string command;
    if (event.operation == ava::permissions::Operation::RunCommand)
    {
      command = event.command_arguments_redacted ? "[redacted]" : safe_command_display(event.command_metadata);
    }
    else
    {
      command = event.command_arguments_redacted ? "[redacted]" : event.command;
    }
    data += ",\"command\":\"" + ava::core::json::escape(command) + "\"";
  }
  if (!event.resolution.empty())
  {
    data += ",\"resolution\":\"" + ava::core::json::escape(event.resolution) + "\"";
  }
  if (!event.resolution_source.empty())
  {
    data += ",\"resolution_source\":\"" + ava::core::json::escape(event.resolution_source) + "\"";
  }
  if (event.operation != ava::permissions::Operation::RunCommand && !event.resolution_reason.empty())
  {
    data += ",\"resolution_reason\":\"" + ava::core::json::escape(event.resolution_reason) + "\"";
  }
  if (!event.actor.empty())
  {
    data += ",\"actor\":\"" + ava::core::json::escape(event.actor) + "\"";
  }
  if (!event.rule_id.empty())
  {
    data += ",\"rule_id\":\"" + ava::core::json::escape(event.rule_id) + "\"";
  }
  if (event.command_metadata)
  {
    auto const& metadata = *event.command_metadata;
    // This is local audit metadata, not a process environment dump. The
    // profile identifier and digest bind the contract without serializing any
    // HOME/XDG/TMP/PATH or other environment values.
    data += ",\"command_metadata\":{\"level\":\"" + ava::core::json::escape(ava::command::to_string(metadata.level)) + "\",\"family\":\"" +
            ava::core::json::escape(ava::command::to_string(metadata.family)) + "\",\"fingerprint\":\"" + ava::core::json::escape(metadata.fingerprint) +
            "\",\"execution_domain\":\"" + ava::core::json::escape(ava::command::to_string(metadata.execution_domain)) + "\",\"resolved_executable\":\"" +
            ava::core::json::escape(metadata.resolved_executable.string()) + "\",\"origin\":\"" +
            ava::core::json::escape(ava::command::to_string(metadata.executable_origin)) + "\",\"cwd\":\"" + ava::core::json::escape(metadata.cwd.string()) +
            "\",\"containment_available\":" + (metadata.containment_available ? "true" : "false") + ",\"containment_status\":\"" +
            ava::core::json::escape(ava::permissions::to_string(metadata.containment_status)) + "\",\"backend_maximum_scope\":\"" +
            ava::core::json::escape(ava::command::to_string(metadata.backend_maximum_scope)) + "\",\"recipe_payload_version\":\"" +
            ava::core::json::escape(metadata.recipe_payload_version) + "\",\"global_recipe_key\":\"" + ava::core::json::escape(metadata.global_recipe_key) +
            "\",\"workspace_recipe_key\":\"" + ava::core::json::escape(metadata.workspace_recipe_key) + "\"";
    data += R"(,"effect_profile":")" + ava::core::json::escape(metadata.effect_profile) + "\"";
    if (!event.command_arguments_redacted)
    {
      data += ",\"recipe_display\":\"" + ava::core::json::escape(metadata.recipe_display) + "\"";
    }
    data += ",\"effective_allowed_scopes\":[";
    for (std::size_t index = 0; index < metadata.effective_allowed_scopes.size(); ++index)
    {
      if (index > 0)
        data += ',';
      data += "\"" + ava::core::json::escape(ava::command::to_string(metadata.effective_allowed_scopes[index])) + "\"";
    }
    data += "],\"containment_profile_id\":\"" + ava::core::json::escape(metadata.containment_profile_id) +
            "\",\"containment_network_allowed\":" + (metadata.containment_network_allowed ? "true" : "false") + ",\"environment_profile_id\":\"" +
            ava::core::json::escape(metadata.environment_profile_id) + "\",\"environment_digest\":\"" + ava::core::json::escape(metadata.environment_digest) +
            "\",\"executor_identity_verified\":" + (metadata.executor_identity_verified ? "true" : "false") + '}';
  }
  data += '}';
  return data;
}

ava::core::Result<TextOutput> read_file(ToolContext const& context, std::filesystem::path const& path, ReadOptions options)
{
  if (auto canceled = check_canceled(context, "read_file", path); !canceled)
  {
    return std::unexpected(std::move(canceled.error()));
  }
  if (!options.permission_already_checked)
  {
    if (auto permission = ensure_permission(context, ava::permissions::Operation::ReadFile, path, "", "", "tool requires permission"); !permission)
    {
      return std::unexpected(permission.error());
    }
  }
  if (auto canceled = check_canceled(context, "read_file", path); !canceled)
  {
    return std::unexpected(std::move(canceled.error()));
  }
  if (!options.permission_already_checked)
  {
    if (auto started = announce_tool_execution_start(context); !started)
      return std::unexpected(std::move(started.error()));
  }

  return read_head_text(context, path, options);
}

ava::core::Result<FileMutationResult> write_file(ToolContext const& context, std::filesystem::path const& path, std::string_view content, WriteOptions options)
{
  if (auto canceled = check_canceled(context, "write_file", path); !canceled)
  {
    return std::unexpected(std::move(canceled.error()));
  }
  // Reject symlinks before any permission check or file read. This prevents
  // writing through a symlink to a protected target (e.g., a source file in
  // plan mode) without needing canonicalization to detect the target.
  // The check uses symlink_status (no path resolution), consistent with
  // read_file's symlink rejection.
  if (std::error_code link_error; std::filesystem::is_symlink(std::filesystem::symlink_status(path, link_error)))
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "file writes do not follow symlinks");
    error.with_context("operation", "write_file");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }
  if (auto protected_file = reject_permission_rules_file_mutation(context, path); !protected_file)
  {
    return std::unexpected(std::move(protected_file.error()));
  }
  std::optional<PermissionDiffPreview> preview;
  if (!options.permission_already_checked)
  {
    auto preview_result = write_permission_diff_preview(context, path, content);
    if (!preview_result)
      return std::unexpected(std::move(preview_result.error()));
    preview = std::move(*preview_result);
    auto const diff_preview = preview ? std::string_view(preview->text) : std::string_view{};
    if (auto permission = ensure_permission(context, ava::permissions::Operation::EditFile, path, "", "", "tool requires permission", diff_preview,
                                            preview ? preview->truncated : false);
        !permission)
    {
      return std::unexpected(permission.error());
    }
  }
  if (auto canceled = check_canceled(context, "write_file", path); !canceled)
  {
    return std::unexpected(std::move(canceled.error()));
  }
  if (!options.permission_already_checked)
  {
    if (auto started = announce_tool_execution_start(context); !started)
      return std::unexpected(std::move(started.error()));
  }

  auto attach_preview = [&preview](ava::core::Result<FileMutationResult> written) -> ava::core::Result<FileMutationResult> {
    if (written && preview)
    {
      written->diff = std::move(preview->text);
      written->diff_truncated = preview->truncated;
    }
    return written;
  };

  if (options.mutation_already_locked)
    return attach_preview(write_file_recorded(context, path, content));
  [[maybe_unused]] auto mutation_lock = effective_mutation_queue(context)->lock_path(path);
  return attach_preview(write_file_recorded(context, path, content));
}

ava::core::Result<FileMutationResult> edit_file(ToolContext const& context, std::filesystem::path const& path, std::string_view old_text,
                                                std::string_view new_text)
{
  if (old_text.empty())
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "old_text must not be empty");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }
  if (context.exact_file_access && context.exact_file_access->supports_read_text_file() != context.exact_file_access->supports_write_text_file())
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::PermissionDenied,
                                  "edit_file requires coherent read and write ownership; the exact-file capabilities are partial");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }
  if (auto canceled = check_canceled(context, "edit_file", path); !canceled)
  {
    return std::unexpected(std::move(canceled.error()));
  }
  if (auto protected_file = reject_permission_rules_file_mutation(context, path); !protected_file)
  {
    return std::unexpected(std::move(protected_file.error()));
  }
  auto const read_decision = ava::permissions::decide(ava::permissions::PermissionRequest{
      .operation = ava::permissions::Operation::ReadFile,
      .mode = context.mode,
      .workspace_dir = context.workspace_dir,
      .target_path = path,
      .command = "",
  });
  if (read_decision.action == ava::permissions::PermissionAction::Deny)
  {
    if (auto permission = ensure_permission(context, ava::permissions::Operation::ReadFile, path, "", "", "tool requires permission"); !permission)
    {
      return std::unexpected(permission.error());
    }
  }
  auto const edit_decision = ava::permissions::decide(ava::permissions::PermissionRequest{
      .operation = ava::permissions::Operation::EditFile,
      .mode = context.mode,
      .workspace_dir = context.workspace_dir,
      .target_path = path,
      .command = "",
  });
  if (edit_decision.action == ava::permissions::PermissionAction::Deny)
  {
    if (auto permission = ensure_permission(context, ava::permissions::Operation::EditFile, path, "", "", "tool requires permission"); !permission)
    {
      return std::unexpected(permission.error());
    }
  }
  if (auto permission = ensure_permission(context, ava::permissions::Operation::ReadFile, path, "", "", "tool requires permission"); !permission)
  {
    return std::unexpected(permission.error());
  }
  if (auto canceled = check_canceled(context, "edit_file", path); !canceled)
  {
    return std::unexpected(std::move(canceled.error()));
  }

  [[maybe_unused]] auto mutation_lock = effective_mutation_queue(context)->lock_path(path);
  auto content = read_all_text(context, path, "edit_file");
  if (!content)
  {
    return std::unexpected(content.error());
  }
  if (auto canceled = check_canceled(context, "edit_file", path); !canceled)
  {
    return std::unexpected(std::move(canceled.error()));
  }

  auto match = find_unique_text_match(*content, old_text, path, "old_text was not found", "old_text is not unique");
  if (!match)
    return std::unexpected(match.error());

  auto const original = *content;
  content->replace(match->position, match->size, new_text);
  auto diff = unified_diff(original, *content, path, path, kMaxPermissionDiffBytes);
  if (auto permission = ensure_permission(context, ava::permissions::Operation::EditFile, path, "", "", "tool requires permission", diff.text, diff.truncated);
      !permission)
  {
    return std::unexpected(permission.error());
  }
  if (auto canceled = check_canceled(context, "edit_file", path); !canceled)
  {
    return std::unexpected(std::move(canceled.error()));
  }
  if (auto started = announce_tool_execution_start(context); !started)
    return std::unexpected(std::move(started.error()));
  auto written = write_file(context, path, *content, WriteOptions{.permission_already_checked = true, .mutation_already_locked = true});
  if (!written)
    return std::unexpected(written.error());

  written->diff = std::move(diff.text);
  written->diff_truncated = diff.truncated;
  written->line_endings = to_string(match->content_analysis.line_endings);
  written->had_utf8_bom = match->content_analysis.has_utf8_bom;
  return written;
}

}  // namespace ava::tools
