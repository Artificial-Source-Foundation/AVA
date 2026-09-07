#ifndef AVA_APP_COMMAND_ADVICE_H
#define AVA_APP_COMMAND_ADVICE_H

#include "ava/config/model_config.h"
#include "ava/config/xdg_paths.h"
#include "ava/permissions/permission.h"
#include "ava/provider/provider.h"

#include <memory>
#include <stop_token>

namespace ava::provider {
class ProviderCatalog;
}
namespace ava::app {
// Review result cannot change backend classification or create a reusable grant.
[[nodiscard]] auto command_advice_request(ava::permissions::PermissionPrompt const& prompt, ava::config::ModelInfo const& model)
    -> ava::core::Result<ava::provider::ProviderRequest>;
[[nodiscard]] auto command_advice_text(std::vector<ava::provider::StreamEvent> const& events) -> ava::core::Result<ava::permissions::CommandReview>;
// Catalog is pinned by the caller so asynchronous review uses the same provider endpoint as the session.
[[nodiscard]] auto explain_command(ava::config::XdgPaths const& paths, std::shared_ptr<ava::provider::ProviderCatalog const> const& catalog,
                                   ava::permissions::PermissionPrompt const& prompt, bool offline, std::stop_token stop, ava::http::Transport& transport)
    -> ava::core::Result<ava::permissions::CommandReview>;
} // namespace ava::app
#endif
