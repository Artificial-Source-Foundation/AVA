#pragma once

#include "ava/observability/run_observer.h"
#include "ava/config/xdg_paths.h"
#include "ava/core/error.h"
#include "ava/core/result.h"

#include <filesystem>
#include <memory>
#include <optional>
#include "debug.h"

namespace ava::diagnostics {

enum class RuntimeFailureClass
{
  Provider,
  Session,
  Tool,
  Runtime,
};

// Application-lifetime owner for private production tracing and best-effort
// last-failure persistence. The ordinary constructor is tracing-disabled and
// performs no filesystem access.
class RuntimeDiagnostics final
{
 public:
  explicit RuntimeDiagnostics(ava::config::XdgPaths paths);
  ~RuntimeDiagnostics() noexcept;
  RuntimeDiagnostics(RuntimeDiagnostics const&) = delete;
  RuntimeDiagnostics& operator=(RuntimeDiagnostics const&) = delete;

  [[nodiscard]] static ava::core::Result<std::shared_ptr<RuntimeDiagnostics>> create(ava::config::XdgPaths paths, bool trace_enabled) noexcept;

  [[nodiscard]] bool trace_enabled() const noexcept;
  [[nodiscard]] std::shared_ptr<ava::observability::RunObservation> observation() const noexcept;
  [[nodiscard]] std::optional<std::filesystem::path> trace_path() const noexcept;

  void record_terminal_failure(RuntimeFailureClass failure_class, ava::core::Error const& error) noexcept;
  void close() noexcept;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT

 private:
  struct State;
  [[nodiscard]] ava::core::VoidResult initialize_trace() noexcept;

  ava::config::XdgPaths paths_;
  std::unique_ptr<State> state_;
};

}  // namespace ava::diagnostics
