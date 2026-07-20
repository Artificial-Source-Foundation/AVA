#pragma once

#include "ava/diagnostics/records.h"
#include "ava/config/xdg_paths.h"

#include <string_view>

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

[[nodiscard]] StoredRecord<LastFailureRecord> read_last_failure_record(ava::config::XdgPaths const& paths) noexcept;
[[nodiscard]] StoredRecord<TraceCounterSnapshot> read_trace_counter_snapshot(ava::config::XdgPaths const& paths) noexcept;

[[nodiscard]] ArtifactWriteStatus write_last_failure_record(ava::config::XdgPaths const& paths, LastFailureRecord const& record) noexcept;
[[nodiscard]] ArtifactWriteStatus write_trace_counter_snapshot(ava::config::XdgPaths const& paths, TraceCounterSnapshot const& snapshot) noexcept;

// Runtime callers use this nonthrowing best-effort boundary. The typed record is
// the only accepted payload, and failures are deliberately not recursively
// recorded.
void write_last_failure_record_best_effort(ava::config::XdgPaths const& paths, LastFailureRecord const& record) noexcept;

// Publishes one unique owner-only support artifact under the fixed support
// directory. No caller-selected output path or arbitrary JSON is accepted.
[[nodiscard]] ArtifactWriteStatus publish_support_artifact(ava::config::XdgPaths const& paths, SupportArtifact const& artifact) noexcept;

}  // namespace ava::diagnostics
