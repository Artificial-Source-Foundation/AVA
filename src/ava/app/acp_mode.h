#pragma once

#include <iosfwd>
#include <memory>

namespace ava::diagnostics {
class RuntimeDiagnostics;
} // namespace ava::diagnostics

namespace ava::app {

[[nodiscard]] int run_acp_mode(std::ostream& error_output, std::shared_ptr<ava::diagnostics::RuntimeDiagnostics> diagnostics = nullptr);

} // namespace ava::app
