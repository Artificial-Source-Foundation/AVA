#include "ava/app/rpc/handlers.h"

#include <utility>
#include <vector>

#include "ava/app/rpc/protocol.h"
#include "ava/app/rpc/serialization.h"
#include "ava/config/auth.h"
#include "ava/config/openai_oauth.h"
#include "ava/provider/registry.h"

namespace ava::app::rpc {

ava::core::Result<RuntimeRunOptions> ensure_prompt_runtime_options(ava::config::XdgPaths const& paths,
                                                                   std::string_view provider_id,
                                                                   RuntimeRunOptions options,
                                                                   ava::provider::Transport& auth_transport,
                                                                   std::string_view purpose) {
  if (!options.access_token.empty()) return options;

  auto credential = ava::config::provider_credential_for_request(paths, provider_id, auth_transport);
  if (!credential) return std::unexpected(credential.error());
  if (!*credential) {
    auto error = ava::core::Error(
        ava::core::ErrorCategory::InvalidArgument,
        "RPC " + std::string(purpose) + " requires auth for provider `" + std::string(provider_id) + "`");
    error.with_context("auth_file", paths.auth_file.string());
    return std::unexpected(std::move(error));
  }
  options.access_token = (*credential)->access_token;
  options.credential_type = (*credential)->credential_type;
  options.openai_oauth = (*credential)->provider_id == "openai" && (*credential)->credential_type == "oauth";
  options.openai_account_id = (*credential)->account_id;
  if (options.openai_oauth && options.openai_account_id.empty()) {
    options.openai_account_id =
        ava::config::openai_oauth_account_id_from_token((*credential)->access_token).value_or("");
  }
  return options;
}

ava::core::Result<RuntimeSession> create_new_session(RuntimeSession const& current,
                                                     RuntimeOpenOptions const& base_options) {
  RuntimeOpenOptions options = base_options;
  options.workspace_dir = current.workspace_dir;
  options.current_dir = current.current_dir;
  options.mode = current.mode;
  options.paths = current.paths;
  options.requested_session_id = std::nullopt;
  options.continue_last_session = false;
  return open_runtime_session(options);
}

ava::core::Result<RuntimeSession> open_requested_session(RuntimeSession const& current,
                                                         RuntimeOpenOptions const& base_options,
                                                         std::string_view requested_session_id) {
  RuntimeOpenOptions options = base_options;
  options.workspace_dir = current.workspace_dir;
  options.current_dir = current.current_dir;
  options.mode = current.mode;
  options.paths = current.paths;
  options.requested_session_id = std::string(requested_session_id);
  options.continue_last_session = false;
  return open_runtime_session(options);
}

ava::core::Result<ava::config::ModelInfo> resolve_requested_model(RuntimeSession const& session,
                                                                  RpcCommand const& command) {
  if (!command.model || command.model->empty()) return std::unexpected(invalid_rpc("set_model requires model"));
  if (command.provider && !command.provider->empty()) {
    return resolve_runtime_model(session.paths, *command.provider, *command.model);
  }

  auto current_provider_match = resolve_runtime_model(session.paths, session.model.provider_id, *command.model);
  if (current_provider_match) return current_provider_match;
  if (current_provider_match.error().category() != ava::core::ErrorCategory::NotFound) {
    return std::unexpected(std::move(current_provider_match.error()));
  }

  auto registry = ava::config::load_model_registry(session.paths);
  if (!registry) return std::unexpected(std::move(registry.error()));
  auto const providers = ava::provider::builtin_provider_registry();
  std::vector<ava::config::ModelInfo> matches;
  for (auto const& model : effective_models(*registry)) {
    if (model.model_id == *command.model && providers.contains(model.provider_id)) matches.push_back(model);
  }
  if (matches.empty()) {
    auto error = ava::core::Error(ava::core::ErrorCategory::NotFound, "model is not configured");
    error.with_context("model", *command.model);
    return std::unexpected(std::move(error));
  }
  if (matches.size() > 1) {
    auto error = invalid_rpc("model id is ambiguous; provider is required");
    error.with_context("model", *command.model);
    error.with_context("matches", std::to_string(matches.size()));
    return std::unexpected(std::move(error));
  }
  return matches.front();
}

ava::core::Result<ava::config::ModelInfo> next_runtime_model(RuntimeSession const& session) {
  auto registry = ava::config::load_model_registry(session.paths);
  if (!registry) return std::unexpected(std::move(registry.error()));
  auto const providers = ava::provider::builtin_provider_registry();
  std::vector<ava::config::ModelInfo> models;
  for (auto const& model : effective_models(*registry)) {
    if (providers.contains(model.provider_id)) models.push_back(model);
  }
  if (models.empty())
    return std::unexpected(
        ava::core::Error(ava::core::ErrorCategory::NotFound, "no registered provider models are configured"));

  std::size_t next_index = 0;
  for (std::size_t index = 0; index < models.size(); ++index) {
    if (models[index].provider_id == session.model.provider_id && models[index].model_id == session.model.model_id) {
      next_index = (index + 1) % models.size();
      break;
    }
  }
  return models[next_index];
}

ava::core::Result<ProviderHandle> provider_for_session_model(RuntimeSession const& session,
                                                             std::string_view injected_provider_id,
                                                             ava::provider::Provider const& injected_provider) {
  if (session.model.provider_id == injected_provider_id) {
    return ProviderHandle{.provider = &injected_provider, .owned = nullptr};
  }
  auto registry = ava::provider::builtin_provider_registry();
  auto provider = registry.create(session.model.provider_id);
  if (!provider) return std::unexpected(std::move(provider.error()));
  return ProviderHandle{.provider = nullptr, .owned = std::move(*provider)};
}

bool is_plugin_rpc_command(std::string_view type) {
  return type == "list_plugins" || type == "plugin_failures" || type == "inspect_plugin" || type == "enable_plugin" ||
         type == "disable_plugin" || type == "validate_plugin" || type == "list_plugin_prompts" ||
         type == "get_plugin_prompt" || type == "list_plugin_skills" || type == "get_plugin_skill" ||
         type == "run_plugin_command";
}

bool is_mcp_rpc_command(std::string_view type) {
  return type == "list_mcp_servers" || type == "inspect_mcp_server" || type == "list_mcp_tools" ||
         type == "restart_mcp_server";
}

ava::core::Result<std::string> plugin_rpc_slash_command(RpcCommand const& command) {
  if (command.type == "list_plugins") return std::string("/plugins list");
  if (command.type == "plugin_failures") return std::string("/plugins failures");
  if (command.type == "validate_plugin") {
    if (!command.path || command.path->empty()) return std::unexpected(invalid_rpc("validate_plugin requires path"));
    return "/plugins validate " + *command.path;
  }

  if (!command.plugin_id || command.plugin_id->empty()) {
    return std::unexpected(invalid_rpc(command.type + " requires plugin_id"));
  }
  if (command.type == "inspect_plugin") return "/plugins inspect " + *command.plugin_id;
  if (command.type == "enable_plugin") return "/plugins enable " + *command.plugin_id;
  if (command.type == "disable_plugin") return "/plugins disable " + *command.plugin_id;
  if (command.type == "list_plugin_prompts") return "/plugins prompts " + *command.plugin_id;
  if (command.type == "list_plugin_skills") return "/plugins skills " + *command.plugin_id;
  if (command.type == "get_plugin_prompt" || command.type == "get_plugin_skill" ||
      command.type == "run_plugin_command") {
    if (!command.name || command.name->empty()) return std::unexpected(invalid_rpc(command.type + " requires name"));
  }
  if (command.type == "get_plugin_prompt") return "/plugins prompt " + *command.plugin_id + " " + *command.name;
  if (command.type == "get_plugin_skill") return "/plugins skill " + *command.plugin_id + " " + *command.name;
  if (command.type == "run_plugin_command") {
    return "/plugin run " + *command.plugin_id + " " + *command.name + " " + command.arguments.value_or("{}");
  }
  return std::unexpected(invalid_rpc("unsupported plugin RPC command"));
}

ava::core::Result<std::string> mcp_rpc_slash_command(RpcCommand const& command) {
  if (command.type == "list_mcp_servers") return std::string("/mcp list");
  if (!command.server_id || command.server_id->empty()) {
    return std::unexpected(invalid_rpc(command.type + " requires server_id"));
  }
  if (command.type == "inspect_mcp_server") return "/mcp inspect " + *command.server_id;
  if (command.type == "list_mcp_tools") return "/mcp tools " + *command.server_id;
  if (command.type == "restart_mcp_server") return "/mcp restart " + *command.server_id;
  return std::unexpected(invalid_rpc("unsupported MCP RPC command"));
}

}  // namespace ava::app::rpc
