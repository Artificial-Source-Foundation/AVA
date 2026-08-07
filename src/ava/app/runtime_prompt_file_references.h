#pragma once

#include "ava/core/result.h"

#include <string>

namespace ava::app::runtime {
class Session;
struct RunOptions;
}  // namespace ava::app::runtime

namespace ava::app {

[[nodiscard]] ava::core::Result<std::string> expand_prompt_file_references(runtime::session_ts& unlocked_session, std::string const& user_message,
                                                                           runtime::RunOptions const& options);

}  // namespace ava::app
