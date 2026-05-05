#pragma once

#include <cstddef>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ava/app/connect_openai.h"
#include "ava/config/xdg_paths.h"
#include "ava/core/result.h"

namespace ava::app::detail {

enum class ConnectCliCredentialSource {
  None,
  Stdin,
  Env,
  Prompt,
};

struct ConnectCliInvocation {
  std::optional<std::string> provider_id;
  ConnectCliCredentialSource credential_source = ConnectCliCredentialSource::None;
  std::optional<ava::app::ConnectCredentialType> credential_type;
  std::optional<std::string> env_var;
};

[[nodiscard]] ava::core::Result<ConnectCliInvocation> parse_connect_cli_invocation(
    std::vector<std::string_view> const& args, std::size_t first_arg_index);

[[nodiscard]] int run_connect_cli_invocation(ava::config::XdgPaths const& paths, ConnectCliInvocation const& invocation,
                                             bool preserve_openai_browser_default, bool stdin_is_tty, std::istream& in,
                                             std::ostream& out, std::ostream& err);

}  // namespace ava::app::detail
