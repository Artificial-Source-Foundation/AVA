#include "ava/orchestration/composition.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>

#include "ava/config/model_registry.hpp"
#include "ava/config/model_spec.hpp"
#include "ava/config/paths.hpp"
#include "ava/config/trust.hpp"
#include "ava/llm/factory.hpp"
#include "ava/mcp/config.hpp"
#include "ava/tools/core_tools.hpp"
#include "ava/tools/mcp_bridge.hpp"
#include "ava/tools/permission_middleware.hpp"

namespace ava::orchestration {
namespace {

constexpr auto* kRuntimeMetadataNamespace = "runtime";
constexpr auto* kLegacyHeadlessMetadataNamespace = "headless";

[[nodiscard]] std::optional<std::reference_wrapper<const nlohmann::json>> metadata_section(
    const ava::types::SessionRecord& session,
    const char* section
) {
  if(!session.metadata.is_object()) {
    return std::nullopt;
  }
  if(!session.metadata.contains(section) || !session.metadata.at(section).is_object()) {
    return std::nullopt;
  }
  return std::cref(session.metadata.at(section));
}

[[nodiscard]] std::optional<std::string> metadata_string(
    const ava::types::SessionRecord& session,
    const char* key
) {
  for(const auto* section : {kRuntimeMetadataNamespace, kLegacyHeadlessMetadataNamespace}) {
    const auto metadata = metadata_section(session, section);
    if(!metadata.has_value()) {
      continue;
    }
    const auto& object = metadata->get();
    if(object.contains(key) && object.at(key).is_string()) {
      return object.at(key).get<std::string>();
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::size_t> metadata_max_turns(const ava::types::SessionRecord& session) {
  for(const auto* section : {kRuntimeMetadataNamespace, kLegacyHeadlessMetadataNamespace}) {
    const auto metadata = metadata_section(session, section);
    if(!metadata.has_value()) {
      continue;
    }
    const auto& object = metadata->get();
    if(!object.contains("max_turns") || !object.at("max_turns").is_number_integer()) {
      continue;
    }

    const auto value = object.at("max_turns").get<std::int64_t>();
    if(value <= 0) {
      continue;
    }
    return static_cast<std::size_t>(value);
  }
  return std::nullopt;
}

[[nodiscard]] std::string default_model_for_provider(const std::string& provider) {
  const auto models = ava::config::registry().models_for_provider(provider);
  if(!models.empty()) {
    return models.front()->id;
  }
  if(provider == "openai") {
    return "gpt-5-mini";
  }
  throw std::runtime_error("no default model known for provider: " + provider);
}

void apply_allowed_tools(ava::tools::ToolRegistry& registry, const std::vector<std::string>& allowed_tools) {
  const auto tool_names = registry.tool_names();
  const std::unordered_set<std::string> known(tool_names.begin(), tool_names.end());
  std::vector<std::string> unknown;
  for(const auto& name : allowed_tools) {
    if(!known.contains(name)) {
      unknown.push_back(name);
    }
  }
  if(!unknown.empty()) {
    std::sort(unknown.begin(), unknown.end());
    std::string message = "Unknown allowed tool name(s): ";
    for(std::size_t index = 0; index < unknown.size(); ++index) {
      if(index > 0) {
        message += ", ";
      }
      message += unknown[index];
    }
    throw std::invalid_argument(message);
  }

  const std::unordered_set<std::string> allowed(allowed_tools.begin(), allowed_tools.end());
  for(const auto& name : tool_names) {
    if(!allowed.contains(name)) {
      registry.unregister_tool(name);
    }
  }
}

[[nodiscard]] std::string lower_ascii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

[[nodiscard]] bool is_headless_auto_approvable_risk(std::string risk_level) {
  risk_level = lower_ascii(std::move(risk_level));
  return risk_level == "safe" || risk_level == "low";
}

void persist_runtime_selection_metadata(ava::types::SessionRecord& session, const ResolvedRuntimeSelection& selection) {
  session.metadata[kRuntimeMetadataNamespace]["provider"] = selection.provider;
  session.metadata[kRuntimeMetadataNamespace]["model"] = selection.model;
  session.metadata[kRuntimeMetadataNamespace]["max_turns"] = selection.max_turns;
}

[[nodiscard]] ava::mcp::McpConfig merge_mcp_config(
    ava::mcp::McpConfig global_config,
    const ava::mcp::McpConfig& project_config
) {
  ava::mcp::McpConfig merged;
  merged.servers.reserve(global_config.servers.size() + project_config.servers.size());

  std::unordered_map<std::string, std::size_t> by_name;
  const auto upsert_server = [&](ava::mcp::McpServerConfig server) {
    if(by_name.contains(server.name)) {
      merged.servers[by_name.at(server.name)] = std::move(server);
      return;
    }

    by_name.insert_or_assign(server.name, merged.servers.size());
    merged.servers.push_back(std::move(server));
  };

  for(auto& server : global_config.servers) {
    upsert_server(std::move(server));
  }
  for(const auto& server : project_config.servers) {
    upsert_server(server);
  }

  return merged;
}

[[nodiscard]] ava::mcp::McpConfig load_runtime_mcp_config(
    const std::filesystem::path& workspace_root,
    bool include_global_config
) {
  auto config = ava::mcp::McpConfig{};
  if(include_global_config) {
    config = ava::mcp::load_mcp_config_file(ava::config::mcp_config_path());
  }

  const auto project_config_path = ava::config::project_mcp_config_path(workspace_root);
  if(!ava::config::is_project_trusted(workspace_root)) {
    if(std::filesystem::exists(project_config_path)) {
      std::cerr << "warning: skipping project-local MCP config at '" << project_config_path.string()
                << "' because workspace is not trusted.\n";
    }
    return config;
  }

  const auto project_config = ava::mcp::load_mcp_config_file(project_config_path);
  return merge_mcp_config(std::move(config), project_config);
}

}  // namespace

ResolvedSessionStartup resolve_startup_session(
    ava::session::SessionManager& manager,
    bool resume_latest,
    const std::optional<std::string>& session_id
) {
  if(resume_latest && session_id.has_value()) {
    throw std::invalid_argument("--continue and --session cannot be combined");
  }

  if(session_id.has_value()) {
    const auto loaded = manager.get(*session_id);
    if(!loaded.has_value()) {
      throw std::runtime_error("session not found: " + *session_id);
    }
    return ResolvedSessionStartup{.session = *loaded, .kind = SessionStartupKind::ContinueById};
  }

  if(resume_latest) {
    const auto recent = manager.list_recent(1);
    if(!recent.empty()) {
      return ResolvedSessionStartup{.session = recent.front(), .kind = SessionStartupKind::ContinueLatest};
    }
  }

  return ResolvedSessionStartup{.session = manager.create(), .kind = SessionStartupKind::New};
}

ResolvedRuntimeSelection resolve_runtime_selection(
    const RuntimeSelectionOptions& options,
    const ava::types::SessionRecord& session
) {
  const auto persisted_provider = metadata_string(session, "provider");
  const auto persisted_model = metadata_string(session, "model");

  std::optional<std::string> provider;
  std::optional<std::string> model;

  if(options.model.has_value()) {
    if(options.provider.has_value()) {
      provider = ava::config::normalize_provider_name(*options.provider);
      model = *options.model;
    } else {
      const auto parsed = ava::config::parse_model_spec(*options.model);
      provider = ava::config::normalize_provider_name(parsed.provider);
      model = parsed.model;
    }
  }

  if(!provider.has_value() && options.provider.has_value()) {
    provider = ava::config::normalize_provider_name(*options.provider);
  }
  if(!model.has_value() && options.model.has_value()) {
    model = *options.model;
  }

  if(!provider.has_value() && persisted_provider.has_value()) {
    provider = ava::config::normalize_provider_name(*persisted_provider);
  }
  if(!model.has_value() && persisted_model.has_value()) {
    model = *persisted_model;
  }

  if(!provider.has_value()) {
    provider = std::string{"openai"};
  }
  if(!model.has_value()) {
    model = default_model_for_provider(*provider);
  }

  std::size_t max_turns = options.max_turns;
  if(!options.max_turns_explicit) {
    if(const auto persisted_turns = metadata_max_turns(session); persisted_turns.has_value()) {
      max_turns = *persisted_turns;
    }
  }

  return ResolvedRuntimeSelection{
      .provider = *provider,
      .model = *model,
      .max_turns = max_turns,
  };
}

void RuntimeComposition::save_session() const {
  sessions->save(session);
}

RuntimeComposition compose_runtime(RuntimeCompositionRequest request) {
  auto sessions = std::make_unique<ava::session::SessionManager>(std::move(request.session_db_path));
  auto startup = resolve_startup_session(*sessions, request.resume_latest, request.session_id);
  const auto selection = resolve_runtime_selection(request.selection, startup.session);
  persist_runtime_selection_metadata(startup.session, selection);

  auto provider = std::move(request.provider_override);
  if(!provider) {
    if(request.provider_factory) {
      provider = request.provider_factory(selection);
      if(!provider) {
        throw std::runtime_error("provider_factory returned null provider");
      }
    }
  }

  if(!provider) {
    const auto credentials = request.credentials_override.has_value()
                                 ? *request.credentials_override
                                 : ava::config::CredentialStore::load(ava::config::credentials_path());
    provider = ava::llm::create_provider(selection.provider, selection.model, credentials);
  }

  auto registry = std::make_shared<ava::tools::ToolRegistry>();
  [[maybe_unused]] const auto registration = ava::tools::register_default_tools(*registry, request.workspace_root);

  auto mcp_manager = std::make_shared<ava::mcp::McpManager>(request.mcp_transport_factory);
  auto mcp_config = ava::mcp::McpConfig{};
  if(request.mcp_config_override.has_value()) {
    mcp_config = *request.mcp_config_override;
  } else {
    try {
      mcp_config = load_runtime_mcp_config(request.workspace_root, request.load_global_mcp_config);
    } catch(const std::exception& e) {
      std::cerr << "warning: disabling MCP runtime because MCP config failed to load: " << e.what() << "\n";
      mcp_config = {};
    } catch(...) {
      std::cerr << "warning: disabling MCP runtime because MCP config failed with a non-standard exception\n";
      mcp_config = {};
    }
  }
  mcp_manager->initialize(mcp_config);
  [[maybe_unused]] const auto mcp_registered = ava::tools::register_mcp_tools(*registry, mcp_manager);

  auto approval_resolver = request.approval_resolver;
  if(!approval_resolver && request.auto_approve) {
    approval_resolver = [](const ava::control_plane::InteractiveRequestHandle&, const ApprovalRequestPayload& payload) {
      if(!is_headless_auto_approvable_risk(payload.inspection.risk_level)) {
        return ApprovalResolution{
            .approval = ava::tools::ToolApproval::rejected("headless auto-approve rejects high-risk tool"),
            .state = ava::control_plane::InteractiveRequestState::Cancelled,
        };
      }
      return ApprovalResolution{
          .approval = ava::tools::ToolApproval{.kind = ava::tools::ToolApprovalKind::Allowed},
          .state = ava::control_plane::InteractiveRequestState::Resolved,
      };
    };
  }

  auto run_controller = std::make_shared<RunController>(startup.session.id);

  auto interactive_bridge = std::make_shared<InteractiveBridge>(
      std::nullopt,
      std::move(approval_resolver),
      std::move(request.question_resolver),
      std::move(request.plan_resolver)
  );

  registry->add_middleware(std::make_shared<ava::tools::PermissionMiddleware>(
      std::make_shared<ava::tools::DefaultHeadlessPermissionInspector>(),
      interactive_bridge
  ));

  if(request.allowed_tools.has_value()) {
    apply_allowed_tools(*registry, *request.allowed_tools);
  }

  ava::agent::AgentConfig agent_config{.max_turns = selection.max_turns};
  if(request.system_prompt_preamble.has_value()) {
    agent_config.system_prompt_preamble = *request.system_prompt_preamble;
  }

  RuntimeComposition composition{
      .sessions = std::move(sessions),
      .session = std::move(startup.session),
      .startup_kind = startup.kind,
      .selection = selection,
      .provider = std::move(provider),
      .run_controller = std::move(run_controller),
      .interactive_bridge = std::move(interactive_bridge),
      .registry = registry,
      .mcp_manager = std::move(mcp_manager),
      .queue = {},
      .runtime = nullptr,
  };

  composition.runtime = std::make_unique<ava::agent::AgentRuntime>(
      *composition.provider,
      *composition.registry,
      std::move(agent_config)
  );

  return composition;
}

}  // namespace ava::orchestration
