#include "sys.h"
#include "ava/agent/tool_dispatch_bash.h"
#include "ava/agent/tool_dispatch_common.h"
#include "ava/agent/tool_dispatch_file.h"
#include "ava/agent/tool_dispatch_job.h"
#include "ava/agent/tool_dispatch_lsp.h"
#include "ava/agent/tool_dispatch_patch.h"
#include "ava/agent/tool_dispatch_question.h"
#include "ava/agent/tool_dispatch_search.h"
#include "ava/agent/tool_dispatch_services.h"
#include "ava/agent/tool_dispatch_skill.h"
#include "ava/agent/tool_dispatch_task.h"
#include "ava/agent/tool_dispatch_web.h"
#include "ava/agent/tool_dispatcher.h"
#include "ava/agent/tool_registry.h"
#include "ava/agent/tool_result.h"
#include "ava/tools/lsp_tools.h"
#include "ava/tools/mutation_queue.h"
#include "ava/tools/secure_workspace.h"
#include "ava/plugin/tool_broker.h"
#include "ava/mcp/tool_broker.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace ava::agent {
namespace {

constexpr std::size_t kMaxProviderToolCallIdBytes = 256;

using namespace ava::agent::tool_dispatch;

bool is_lsp_diagnostics_metadata(ToolMetadata const& tool)
{
  return tool.name == std::string_view("lsp_diagnostics") || tool.name == std::string_view("lsp_document_symbols") ||
         tool.name == std::string_view("lsp_workspace_symbols") || tool.name == std::string_view("lsp_definition") ||
         tool.name == std::string_view("lsp_references");
}

template <typename Handler>
ToolExecutor ignore_dispatch_services(Handler handler)
{
  return [handler](ava::tools::ToolContext const& context, ToolDispatchServices const&, ProviderToolCall const& call) { return handler(context, call); };
}

ToolExecutor builtin_tool_executor(std::string_view name)
{
  if (name == "read_file")
    return ignore_dispatch_services(read_file_result);
  if (name == "write_file")
    return ignore_dispatch_services(write_file_result);
  if (name == "edit_file")
    return ignore_dispatch_services(edit_file_result);
  if (name == "glob")
    return ignore_dispatch_services(glob_result);
  if (name == "list_directory")
    return ignore_dispatch_services(list_directory_result);
  if (name == "grep")
    return ignore_dispatch_services(grep_result);
  if (name == "bash")
    return ignore_dispatch_services(bash_result);
  if (name == "webfetch")
    return ignore_dispatch_services(webfetch_result);
  if (name == "websearch")
    return ignore_dispatch_services(websearch_result);
  if (name == "skill")
    return ignore_dispatch_services(skill_result);
  if (name == "task")
    return task_result;
  if (name == "job")
    return job_result;
  if (name == "lsp_diagnostics")
    return ignore_dispatch_services(lsp_diagnostics_result);
  if (name == "lsp_document_symbols")
    return ignore_dispatch_services(lsp_document_symbols_result);
  if (name == "lsp_workspace_symbols")
    return ignore_dispatch_services(lsp_workspace_symbols_result);
  if (name == "lsp_definition")
    return ignore_dispatch_services(lsp_definition_result);
  if (name == "lsp_references")
    return ignore_dispatch_services(lsp_references_result);
  if (name == "apply_patch")
    return ignore_dispatch_services(apply_patch_result);
  if (name == "question")
    return question_result;
  return nullptr;
}

RegisteredTool plugin_registered_tool(ava::plugin::PluginBrokeredTool const& descriptor)
{
  auto executor = descriptor.executor;
  return RegisteredTool{.metadata = RegisteredToolMetadata{.name = descriptor.model_tool_name,
                                                           .description = descriptor.description,
                                                           .schema_json = descriptor.schema_json,
                                                           .permission_category = descriptor.permission_category,
                                                           .output_bound_summary = descriptor.output_bound_summary,
                                                           .execution_mode = descriptor.execution_mode,
                                                           .event_rendering_hint = descriptor.event_rendering_hint,
                                                           .description_family = descriptor.description_family},
                        .executor = [executor = std::move(executor)](ava::tools::ToolContext const& tool_context, ToolDispatchServices const&,
                                                                     ProviderToolCall const& call) { return executor(tool_context, call); },
                        .source = ToolSource::Plugin,
                        .source_id = descriptor.source_id,
                        .brokered_external = true,
                        .requires_lsp_diagnostics = false};
}

RegisteredTool mcp_registered_tool(ava::mcp::McpBrokeredTool const& descriptor)
{
  auto executor = descriptor.executor;
  return RegisteredTool{.metadata = RegisteredToolMetadata{.name = descriptor.model_tool_name,
                                                           .description = descriptor.description,
                                                           .schema_json = descriptor.schema_json,
                                                           .permission_category = descriptor.permission_category,
                                                           .output_bound_summary = descriptor.output_bound_summary,
                                                           .execution_mode = descriptor.execution_mode,
                                                           .event_rendering_hint = descriptor.event_rendering_hint,
                                                           .description_family = descriptor.description_family},
                        .executor = [executor = std::move(executor)](ava::tools::ToolContext const& tool_context, ToolDispatchServices const&,
                                                                     ProviderToolCall const& call) { return executor(tool_context, call); },
                        .source = ToolSource::Mcp,
                        .source_id = descriptor.source_id,
                        .brokered_external = true,
                        .requires_lsp_diagnostics = false};
}

ava::core::Error strict_mcp_model_name_collision(ava::mcp::McpBrokeredTool const& descriptor, RegisteredTool const& existing)
{
  auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "strict session MCP tools normalize to a duplicate model tool name");
  error.with_context("tool", descriptor.model_tool_name);
  error.with_context("mcp_server", descriptor.mcp_server);
  error.with_context("mcp_name", descriptor.mcp_name);
  error.with_context("existing_source", std::string(to_string(existing.source)));
  error.with_context("existing_source_id", existing.source_id);
  return error;
}

ava::core::VoidResult register_plugin_brokered_tools(ToolRegistry& registry, ava::tools::ToolContext const& context)
{
  return ava::plugin::visit_enabled_plugin_tools(context, [&registry](ava::plugin::PluginBrokeredTool const& descriptor) -> ava::core::VoidResult {
    if (registry.find(descriptor.model_tool_name) != nullptr)
      return {};
    auto registered = registry.register_tool(plugin_registered_tool(descriptor));
    (void)registered;
    return {};
  });
}

ava::core::VoidResult register_mcp_brokered_tools(ToolRegistry& registry, ava::tools::ToolContext const& context)
{
  bool const exact_composition = context.exact_builtin_tool_names.has_value();
  return ava::mcp::visit_enabled_mcp_tools(context, [&registry, exact_composition](ava::mcp::McpBrokeredTool const& descriptor) -> ava::core::VoidResult {
    if (auto const* existing = registry.find(descriptor.model_tool_name); existing != nullptr)
    {
      if (exact_composition)
        return std::unexpected(strict_mcp_model_name_collision(descriptor, *existing));
      return {};
    }
    auto registered = registry.register_tool(mcp_registered_tool(descriptor));
    if (!registered && exact_composition)
      return std::unexpected(std::move(registered.error()));
    return {};
  });
}

ava::core::Result<ToolRegistry> build_tool_registry_result(ava::tools::ToolContext const& context, ToolVisibilityOptions const& visibility = {})
{
  ToolRegistry registry;
  if (context.exact_builtin_tool_names)
  {
    if (!context.session_mcp_config)
      return std::unexpected(
          ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "exact tool composition requires an immutable session MCP configuration"));
    for (auto const& name : *context.exact_builtin_tool_names)
    {
      auto const* entry = builtin_tool_registry().find(name);
      if (entry == nullptr)
      {
        auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "requested built-in tool is unavailable");
        error.with_context("tool", name);
        return std::unexpected(std::move(error));
      }
      if (auto registered = registry.register_tool(*entry); !registered)
        return std::unexpected(std::move(registered.error()));
    }
    if (context.include_plugin_tools)
      if (auto registered = register_plugin_brokered_tools(registry, context); !registered)
        return std::unexpected(std::move(registered.error()));
    if (auto registered = register_mcp_brokered_tools(registry, context); !registered)
      return std::unexpected(std::move(registered.error()));
    return registry;
  }

  for (auto const& entry : builtin_tool_registry().entries())
  {
    auto registered = registry.register_tool(entry);
    if (!registered)
    {
      std::cerr << "tool registry failed: " << registered.error().format() << '\n';
      std::abort();
    }
  }
  if (context.include_plugin_tools)
    if (auto registered = register_plugin_brokered_tools(registry, context); !registered)
      return std::unexpected(std::move(registered.error()));
  if (auto registered = register_mcp_brokered_tools(registry, context); !registered)
    return std::unexpected(std::move(registered.error()));
  registry.apply_visibility_filter(visibility);
  return registry;
}

ToolRegistry build_tool_registry(ava::tools::ToolContext const& context, ToolVisibilityOptions const& visibility = {})
{
  auto registry = build_tool_registry_result(context, visibility);
  return registry ? std::move(*registry) : ToolRegistry{};
}

}  // namespace

ToolRegistry const& builtin_tool_registry()
{
  static auto const registry = [] {
    ToolRegistry builtins;
    for (auto const& metadata : builtin_tool_metadata())
    {
      auto registered = builtins.register_tool(RegisteredTool{.metadata = own_tool_metadata(metadata),
                                                              .executor = builtin_tool_executor(metadata.name),
                                                              .source = ToolSource::Builtin,
                                                              .source_id = "builtin",
                                                              .requires_lsp_diagnostics = is_lsp_diagnostics_metadata(metadata)});
      if (!registered)
      {
        std::cerr << "builtin tool registry failed: " << registered.error().format() << '\n';
        std::abort();
      }
    }
    return builtins;
  }();
  return registry;
}

ToolDispatcher::ToolDispatcher(ava::tools::ToolContext context, ToolDispatchServices services, ToolVisibilityOptions visibility)
{
  if (!context.mutation_queue)
    context.mutation_queue = std::make_shared<ava::tools::MutationQueue>();
  context_ = std::move(context);
  services_ = std::move(services);
  registry_ = build_tool_registry(context_, visibility);
}

ToolDispatcher::ToolDispatcher(ava::tools::ToolContext context, ToolDispatchServices services, ToolRegistry registry)
    : context_(std::move(context)), services_(std::move(services)), registry_(std::move(registry))
{
  if (!context_.mutation_queue)
    context_.mutation_queue = std::make_shared<ava::tools::MutationQueue>();
}

ava::core::Result<ToolDispatcher> ToolDispatcher::create_strict(ava::tools::ToolContext context, ToolDispatchServices services,
                                                                ToolVisibilityOptions visibility)
{
  if (context.require_descriptor_secure_workspace && !context.secure_workspace)
  {
    auto workspace = ava::tools::SecureWorkspace::open(context.workspace_dir);
    if (!workspace)
      return std::unexpected(std::move(workspace.error()));
    context.secure_workspace = std::move(*workspace);
  }
  auto registry = build_tool_registry_result(context, visibility);
  if (!registry)
    return std::unexpected(std::move(registry.error()));
  return ToolDispatcher(std::move(context), std::move(services), std::move(*registry));
}

ava::core::Result<ToolDispatchResult> ToolDispatcher::dispatch(ProviderToolCall const& call) const
{
  return dispatch_with_context(context_, services_, call);
}

ava::core::Result<ToolDispatchResult> ToolDispatcher::dispatch_with_context(ava::tools::ToolContext context, ToolDispatchServices const& services,
                                                                            ProviderToolCall const& call) const
{
  auto const arguments = call.arguments_json.empty() ? std::string("{}") : call.arguments_json;
  ProviderToolCall const normalized{.id = call.id, .name = call.name, .arguments_json = arguments};
  if (normalized.id.empty())
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "tool call id is required");
    error.with_context("tool", normalized.name);
    return std::unexpected(std::move(error));
  }
  if (normalized.id.size() > kMaxProviderToolCallIdBytes)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "tool call id is too long");
    error.with_context("tool", normalized.name);
    error.with_context("max_bytes", std::to_string(kMaxProviderToolCallIdBytes));
    return std::unexpected(std::move(error));
  }
  if (auto safe_id = reject_control_value(normalized.id, "id", "tool call id contains a forbidden control byte"); !safe_id)
  {
    safe_id.error().with_context("tool", normalized.name);
    return std::unexpected(std::move(safe_id.error()));
  }
  auto const* tool = registry_.find(normalized.name);
  // Resolve before observing so only registry-owned canonical tool metadata can
  // cross the trace boundary. Provider-supplied names and call IDs remain
  // content/product data and are never used as trace correlation.
  auto const trace_call_id = context.observation ? context.observation->next_id("tool") : std::string{};
  auto emit_start = [&](std::string_view canonical_name, bool resolved) {
    if (!context.observation)
      return;
    context.observation->emit(
        ava::observability::TraceEventType::ToolDispatchStart, context.trace_context, [&normalized, &trace_call_id, canonical_name, resolved](auto& event) {
          event.call_id = trace_call_id;
          event.phase = ava::observability::TracePhase::Tool;
          event.outcome = ava::observability::TraceOutcome::Started;
          event.fields = {{.key = "tool_name",
                           .value = resolved ? std::string(canonical_name) : "[omitted]",
                           .provenance = resolved ? ava::observability::FieldProvenance::PublicMetadata : ava::observability::FieldProvenance::Content},
                          {.key = "arguments_bytes", .value = std::to_string(normalized.arguments_json.size())}};
        });
  };
  auto emit_result = [&](ToolDispatchResult const& result) {
    if (!context.observation)
      return;
    context.observation->emit(ava::observability::TraceEventType::ToolDispatchResult, context.trace_context, [&result, &trace_call_id](auto& event) {
      event.call_id = trace_call_id;
      event.phase = ava::observability::TracePhase::Tool;
      event.outcome = result.success ? ava::observability::TraceOutcome::Success
                                     : (result.payload.status == ToolResultStatus::Canceled ? ava::observability::TraceOutcome::Canceled
                                                                                            : ava::observability::TraceOutcome::Error);
      event.fields = {{.key = "result_bytes", .value = std::to_string(result.result_text.size())}};
    });
  };
  if (tool == nullptr)
  {
    emit_start({}, false);
    auto result = with_tool_result_payload(simple_error_result(normalized, ava::core::ErrorCategory::Tool, "unknown tool"));
    emit_result(result);
    return result;
  }

  emit_start(tool->metadata.name, true);
  context.trace_call_id = trace_call_id;
  if (is_canceled(context))
  {
    auto result = with_tool_result_payload(tool_error_result(normalized, canceled_error(normalized)));
    emit_result(result);
    return result;
  }
  context.permission_request_ids = std::make_shared<std::vector<std::string>>();
  if (context.announce_execution_after_permission)
    context.execution_started = std::make_shared<std::atomic_bool>(false);
  if (context.lsp_diagnostics_provider)
    context.lsp_diagnostics_provider->set_permission_request_ids(context.permission_request_ids);
  auto result = tool->executor(context, services, normalized);
  if (context.permission_request_ids && !context.permission_request_ids->empty())
  {
    result.payload.permission_request_ids = *context.permission_request_ids;
  }
  result = with_tool_result_payload(std::move(result));
  emit_result(result);
  return result;
}

std::vector<ToolMetadata> ToolDispatcher::registered_tool_metadata() const
{
  return registry_.metadata();
}

std::vector<std::string> ToolDispatcher::registered_tool_schemas_json() const
{
  return registry_.tool_schemas_json(context_);
}

std::span<ToolMetadata const> ToolDispatcher::tool_metadata()
{
  return builtin_tool_metadata();
}

std::vector<ToolMetadata> ToolDispatcher::tool_metadata(ava::tools::ToolContext const& context, ToolVisibilityOptions const& visibility)
{
  return build_tool_registry(context, visibility).metadata();
}

std::vector<std::string> ToolDispatcher::tool_schemas_json()
{
  return tool_schemas_json(ava::tools::ToolContext{}, ToolVisibilityOptions{});
}

std::vector<std::string> ToolDispatcher::tool_schemas_json(ava::tools::ToolContext const& context, ToolVisibilityOptions const& visibility)
{
  return build_tool_registry(context, visibility).tool_schemas_json(context);
}

}  // namespace ava::agent
