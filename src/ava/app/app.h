#pragma once

namespace ava::process {
class ProcessScopeV1;
}  // namespace ava::process

namespace ava::app {

[[nodiscard]] int run(int argc, char** argv, ava::process::ProcessScopeV1 const& application_process_scope);

}  // namespace ava::app
