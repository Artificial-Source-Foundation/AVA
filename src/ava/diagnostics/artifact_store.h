#pragma once

#include "ava/diagnostics/records.h"
#include "ava/config/xdg_paths.h"

#include <filesystem>
#include <string_view>
#include "debug.h"

namespace ava::diagnostics {

enum class ArtifactWriteStatus
{
  Success,
  UnsafeStorage,
  StorageUnavailable,
  IoFailure,
  InvalidRecord,
};

[[nodiscard]] std::string_view to_string(ArtifactWriteStatus status) noexcept;

struct TraceArtifactPreparation
{
  ArtifactWriteStatus status = ArtifactWriteStatus::StorageUnavailable;
  std::filesystem::path path;
  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct SupportArtifactPublication
{
  ArtifactWriteStatus status = ArtifactWriteStatus::StorageUnavailable;
  std::filesystem::path path;
  AVA_DEBUG_PRINT_MEMBERS_ON
};

[[nodiscard]] StoredRecord<LastFailureRecord> read_last_failure_record(ava::config::XdgPaths const& paths) noexcept;
[[nodiscard]] StoredRecord<TraceCounterSnapshot> read_trace_counter_snapshot(ava::config::XdgPaths const& paths) noexcept;

[[nodiscard]] ArtifactWriteStatus write_last_failure_record(ava::config::XdgPaths const& paths, LastFailureRecord const& record) noexcept;
[[nodiscard]] ArtifactWriteStatus write_trace_counter_snapshot(ava::config::XdgPaths const& paths, TraceCounterSnapshot const& snapshot) noexcept;

// Runtime callers use this nonthrowing best-effort boundary. The typed record is
// the only accepted payload, and failures are deliberately not recursively
// recorded.
void write_last_failure_record_best_effort(ava::config::XdgPaths const& paths, LastFailureRecord const& record) noexcept;

// Creates one unique empty owner-only trace artifact beneath the fixed trace
// directory. No caller-selected path is accepted.
[[nodiscard]] TraceArtifactPreparation prepare_trace_artifact(ava::config::XdgPaths const& paths) noexcept;

// Publishes one unique owner-only support artifact under the fixed support
// directory. No caller-selected output path or arbitrary JSON is accepted.
[[nodiscard]] SupportArtifactPublication publish_support_artifact(ava::config::XdgPaths const& paths, SupportArtifact const& artifact) noexcept;

}  // namespace ava::diagnostics
