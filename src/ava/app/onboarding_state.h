#pragma once

#include "ava/debug/print_members_on.h"
#include "ava/tui/composer.h"
#include "ava/config/xdg_paths.h"
#include "ava/core/result.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace ava::app {
namespace runtime {
class session_ts;
}

// Versioned local-only first-run setup marker under $XDG_STATE_HOME/ava/onboarding.json.
// Shape: {"version":1,"status":"completed"|"skipped"}
//
// Forward-compat policy: unknown extra top-level fields may be ignored only when the
// recognized fields (integer version + enum status) remain present and strict. Unknown
// version, unknown/missing status, wrong types, non-object JSON, oversized content,
// symlink/special targets, and truncated reads never auto-relaunch the wizard and never
// overwrite or delete on read. Finish/Skip may atomically replace a regular owned file
// through the approved writer; symlink/special targets remain rejected.
inline constexpr int kOnboardingWizardVersion = 1;
inline constexpr std::size_t kMaxOnboardingStateBytes = 4 * 1024;
// Automatic open requires at least this many terminal rows. Explicit /setup always attempts.
inline constexpr int kSetupWizardMinAutoHeightRows = 12;

enum class OnboardingStatus : std::uint8_t
{
  Completed = 0,
  Skipped,
};

enum class OnboardingLoadKind : std::uint8_t
{
  // No state file (or empty path). Automatic wizard is eligible on interactive TTY TUI.
  Absent = 0,
  // Current wizard version with status=completed.
  CurrentCompleted,
  // Current wizard version with status=skipped.
  CurrentSkipped,
  // Malformed, unknown version/status/types, non-regular, symlink, oversized, or unreadable.
  // Suppress automatic relaunch; expose a path-free diagnostic. Never mutate on read.
  UnsupportedOrMalformed,
};

struct OnboardingLoadResult
{
  OnboardingLoadKind kind = OnboardingLoadKind::Absent;
  std::optional<OnboardingStatus> status = std::nullopt;
  // Path-free user-facing diagnostic. Never contains raw file contents or absolute paths.
  std::string diagnostic;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

[[nodiscard]] std::filesystem::path onboarding_state_file(ava::config::XdgPaths const& paths);
[[nodiscard]] std::string_view to_string(OnboardingStatus status) noexcept;
[[nodiscard]] std::optional<OnboardingStatus> onboarding_status_from_string(std::string_view value) noexcept;

// Descriptor-anchored component-by-component no-follow regular-file read. Never creates,
// chmods, overwrites, or deletes.
[[nodiscard]] OnboardingLoadResult load_onboarding_state(ava::config::XdgPaths const& paths);

// Finish/Skip only. Retains and revalidates the absolute directory descriptor chain,
// enforces an owner-only final directory (0700), and publishes an exact-0600 temporary
// regular file with renameat/fsync. Rejects symlink/special/wrong-owner targets and
// leaves unrelated sibling files untouched.
[[nodiscard]] ava::core::VoidResult store_onboarding_status(ava::config::XdgPaths const& paths, OnboardingStatus status);

// Automatic eligibility for the interactive TTY TUI path only: current-version state absent.
// Callers still enforce TTY/TUI mode and minimum height before opening.
[[nodiscard]] bool setup_wizard_auto_eligible(OnboardingLoadResult const& loaded) noexcept;

// Path-free boolean readiness from already-local startup credential/config state.
// No credential values/paths, refresh transport, browser, provider call, auth write,
// or plugin/MCP/LSP operation.
[[nodiscard]] ava::tui::SetupReadinessSnapshot build_setup_readiness_snapshot(runtime::session_ts const& session);
[[nodiscard]] ava::tui::SetupReadinessSnapshot build_setup_readiness_snapshot(ava::config::XdgPaths const& paths, std::string_view provider_id);

// Non-TUI surfaces (print/headless/RPC/ACP/line shell) never load, show, block on, or store
// the wizard. Interactive TUI is the sole auto/explicit host.

}  // namespace ava::app
