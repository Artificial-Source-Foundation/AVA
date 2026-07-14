#pragma once

#include "ava/app/acp/permission.h"
#include "ava/tools/tool_io.h"

#include <memory>
#include <string>

namespace ava::app::acp {

// These adapters are immutable session routes. Their gateway reference is weak
// so connection teardown cannot be extended by a running tool context.
[[nodiscard]] std::shared_ptr<ava::tools::ExactFileAccess const> make_client_exact_file_access(std::string session_id,
                                                                                               std::weak_ptr<ClientRequestGateway> gateway,
                                                                                               bool read_text_file = true, bool write_text_file = true);
[[nodiscard]] std::shared_ptr<ava::tools::CommandExecutor const> make_client_command_executor(std::string session_id,
                                                                                              std::weak_ptr<ClientRequestGateway> gateway);

}  // namespace ava::app::acp
