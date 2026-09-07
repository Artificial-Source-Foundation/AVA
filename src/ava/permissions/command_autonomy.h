#ifndef AVA_PERMISSIONS_COMMAND_AUTONOMY_H
#define AVA_PERMISSIONS_COMMAND_AUTONOMY_H

#include "ava/debug/print_members_on.h"
#include "ava/permissions/permission.h"

#include <atomic>
#include <cstdint>
#include <memory>

namespace ava::permissions {

enum class CommandAutonomyMode : std::uint8_t
{
  Manual,
  Safe,
  Reviewed,
  High
};

struct CommandPolicySnapshot
{
  std::string revision;
  std::optional<PermissionResolutionDecision> rule = std::nullopt;
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};
using CommandPolicyReader = std::function<ava::core::Result<CommandPolicySnapshot>(PermissionPrompt const&)>;

// Shared by a frontend and its admitted tool runs. One atomic snapshot binds
// both mode and epoch; switching away and back invalidates pending reviews.
class CommandAutonomyState final
{
 public:
  [[nodiscard]] auto snapshot() const noexcept -> std::uint64_t { return state_.load(); }
  [[nodiscard]] auto mode() const noexcept -> CommandAutonomyMode { return mode_of(snapshot()); }
  void set_mode(CommandAutonomyMode mode) noexcept;
  [[nodiscard]] static auto mode_of(std::uint64_t snapshot) noexcept -> CommandAutonomyMode;
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
 private:
  std::atomic<std::uint64_t> state_{5}; // generation 1, Safe
};

[[nodiscard]] auto to_string(CommandAutonomyMode mode) -> std::string;
[[nodiscard]] auto parse_command_autonomy_mode(std::string_view value) -> std::optional<CommandAutonomyMode>;
[[nodiscard]] auto command_review_mode(CommandAutonomyMode mode) noexcept -> bool;
[[nodiscard]] auto command_contract_digest(PermissionPrompt const& prompt) -> std::string;
[[nodiscard]] auto command_review_input(CommandPermissionMetadata const& metadata) -> std::string;
[[nodiscard]] auto command_reviewer_eligible(PermissionPrompt const& prompt) noexcept -> bool;
[[nodiscard]] auto command_deterministic_auto(PermissionPrompt const& prompt, CommandAutonomyMode mode) noexcept -> bool;

// Internal, per-invocation receipt. Never produced by parsing model fields.
// The backend owns this transaction; the scoped provider worker publishes its
// bounded outcome before it is joined. No command, path or environment values
// occur in the wire input or the durable observation.
struct CommandReviewTransaction
{
  std::string nonce;
  std::string plan_fingerprint;
  std::string contract_digest;
  std::string policy_revision;
  std::uint64_t autonomy_snapshot = 0;
  std::string input;
  std::string input_digest;
  std::function<bool()> admission_check;
  std::string status = "not_requested";
  std::string risk;
  std::string recommendation;
  std::string policy_recheck = "not_applicable";
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

[[nodiscard]] auto command_autonomy_digest(std::string_view value) -> std::string;
[[nodiscard]] auto command_review_audit_json(CommandReviewTransaction const& review, CommandPermissionMetadata const& metadata) -> std::string;

} // namespace ava::permissions
#endif
