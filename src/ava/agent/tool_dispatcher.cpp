#include "ava/agent/tool_dispatcher.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "ava/agent/question.h"
#include "ava/agent/question_answer_validation.h"
#include "ava/agent/tool_dispatch_support.h"
#include "ava/agent/tool_file_dispatch.h"
#include "ava/agent/tool_lsp_dispatch.h"
#include "ava/agent/tool_network_dispatch.h"
#include "ava/agent/tool_patch_dispatch.h"
#include "ava/agent/tool_process_dispatch.h"
#include "ava/agent/tool_registry.h"
#include "ava/agent/tool_result.h"
#include "ava/agent/tool_search_dispatch.h"
#include "ava/mcp/tool_broker.h"
#include "ava/plugin/tool_broker.h"
#include "ava/tools/mutation_queue.h"

namespace ava::agent {
namespace {

using detail::apply_patch_result;
using detail::bash_result;
using detail::canceled_error;
using detail::check_canceled;
using detail::context_for_provider_tool;
using detail::edit_file_result;
using detail::glob_result;
using detail::grep_result;
using detail::is_canceled;
using detail::is_lsp_diagnostics_metadata;
using detail::lsp_diagnostics_result;
using detail::read_file_result;
using detail::simple_error_result;
using detail::tool_error_result;
using detail::webfetch_result;
using detail::write_file_result;

ToolDispatchResult question_result(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  auto prompt = parse_question_prompt(call.arguments_json, call.name);
  if (!prompt) return tool_error_result(call, prompt.error());
  if (!context.question_resolver) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Tool, "question resolver is unavailable");
    error.with_context("tool", call.name);
    return tool_error_result(call, error);
  }
  auto answer = context.question_resolver(*prompt);
  if (!answer) return tool_error_result(call, answer.error());
  if (auto valid_answer = validate_question_answer(*answer, call.name); !valid_answer) {
    return tool_error_result(call, valid_answer.error());
  }
  return ToolDispatchResult{.call_id = call.id,
                            .name = call.name,
                            .success = true,
                            .result_text = serialize_question_answer_result(*prompt, *answer)};
}

ToolExecutor builtin_tool_executor(std::string_view name)
{
  if (name == "read_file") return read_file_result;
  if (name == "write_file") return write_file_result;
  if (name == "edit_file") return edit_file_result;
  if (name == "glob") return glob_result;
  if (name == "grep") return grep_result;
  if (name == "bash") return bash_result;
  if (name == "webfetch") return webfetch_result;
  if (name == "lsp_diagnostics") return lsp_diagnostics_result;
  if (name == "apply_patch") return apply_patch_result;
  if (name == "question") return question_result;
  return nullptr;
}

ToolRegistry build_tool_registry(ava::tools::ToolContext const& context)
{
  ToolRegistry registry;
  for (auto const& entry : builtin_tool_registry().entries()) {
    auto registered = registry.register_tool(entry);
    if (!registered) {
      std::cerr << "tool registry failed: " << registered.error().format() << '\n';
      std::abort();
    }
  }
  ava::plugin::register_enabled_plugin_tools(registry, context);
  ava::mcp::register_enabled_mcp_tools(registry, context);
  return registry;
}

}  // namespace

ToolRegistry const& builtin_tool_registry()
{
  static auto const registry = [] {
    ToolRegistry builtins;
    for (auto const& metadata : builtin_tool_metadata()) {
      auto registered =
          builtins.register_tool(RegisteredTool{.metadata = own_tool_metadata(metadata),
                                                .executor = builtin_tool_executor(metadata.name),
                                                .source = ToolSource::Builtin,
                                                .source_id = "builtin",
                                                .requires_lsp_diagnostics = is_lsp_diagnostics_metadata(metadata)});
      if (!registered) {
        std::cerr << "builtin tool registry failed: " << registered.error().format() << '\n';
        std::abort();
      }
    }
    return builtins;
  }();
  return registry;
}

ToolDispatcher::ToolDispatcher(ava::tools::ToolContext context)
{
  if (!context.mutation_queue) context.mutation_queue = std::make_shared<ava::tools::MutationQueue>();
  context_ = std::move(context);
  registry_ = build_tool_registry(context_);
}

ava::core::Result<ToolDispatchResult> ToolDispatcher::dispatch(ProviderToolCall const& call) const
{
  ProviderToolCall const normalized = detail::normalize_provider_tool_call(call);
  if (auto valid = detail::validate_provider_tool_call(normalized); !valid) {
    return std::unexpected(std::move(valid.error()));
  }
  auto const* tool = registry_.find(normalized.name);
  if (tool != nullptr) {
    if (is_canceled(context_))
      return with_tool_result_payload(tool_error_result(normalized, canceled_error(normalized)));
    return with_tool_result_payload(tool->executor(context_, normalized));
  }
  return with_tool_result_payload(simple_error_result(normalized, ava::core::ErrorCategory::Tool, "unknown tool"));
}

std::span<ToolMetadata const> ToolDispatcher::tool_metadata()
{
  return builtin_tool_metadata();
}

std::vector<ToolMetadata> ToolDispatcher::tool_metadata(ava::tools::ToolContext const& context)
{
  return build_tool_registry(context).metadata();
}

std::vector<std::string> ToolDispatcher::tool_schemas_json()
{
  return tool_schemas_json(ava::tools::ToolContext{});
}

std::vector<std::string> ToolDispatcher::tool_schemas_json(ava::tools::ToolContext const& context)
{
  return build_tool_registry(context).tool_schemas_json(context);
}

}  // namespace ava::agent
