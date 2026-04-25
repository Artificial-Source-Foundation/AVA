#include "ava/tools/registry.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <sstream>
#include <stdexcept>
#include <thread>

#include "ava/tools/retry.hpp"

namespace ava::tools {

namespace {

[[nodiscard]] std::string join_names(const std::vector<std::string>& names) {
  std::ostringstream oss;
  for(std::size_t idx = 0; idx < names.size(); ++idx) {
    if(idx > 0) {
      oss << ", ";
    }
    oss << names[idx];
  }
  return oss.str();
}

[[nodiscard]] bool contains_tier(const std::vector<ToolTier>& tiers, ToolTier tier) {
  return std::find(tiers.begin(), tiers.end(), tier) != tiers.end();
}

[[nodiscard]] std::optional<std::string> retryable_tool_failure(
    const std::string& tool_name,
    const std::exception_ptr& execution_error,
    const std::optional<ava::types::ToolResult>& tool_result
) {
  if(!retry::is_retryable_tool(tool_name)) {
    return std::nullopt;
  }

  if(execution_error != nullptr) {
    try {
      std::rethrow_exception(execution_error);
    } catch(const std::exception& ex) {
      const auto msg = std::string(ex.what());
      if(retry::is_transient_error(msg)) {
        return msg;
      }
    }
    return std::nullopt;
  }

  if(tool_result.has_value() && tool_result->is_error && retry::is_transient_error(tool_result->content)) {
    return tool_result->content;
  }
  return std::nullopt;
}

}  // namespace

std::string ToolSource::to_string() const {
  switch(kind) {
    case ToolSourceKind::BuiltIn:
      return "built-in";
    case ToolSourceKind::MCP:
      return "mcp:" + detail;
    case ToolSourceKind::Custom:
      return "custom:" + detail;
  }
  return "unknown";
}

void Tool::backfill_input(nlohmann::json& args) const {
  (void)args;
}

std::string Tool::search_hint() const {
  return "";
}

void Middleware::before(const ava::types::ToolCall& tool_call) const {
  (void)tool_call;
}

void Middleware::before_with_source(const ava::types::ToolCall& tool_call, const ToolSource& source) const {
  (void)source;
  before(tool_call);
}

ava::types::ToolResult Middleware::after(
    const ava::types::ToolCall& tool_call,
    const ava::types::ToolResult& result
) const {
  (void)tool_call;
  return result;
}

void ToolRegistry::register_tool(std::unique_ptr<Tool> tool) {
  register_tool_with_source(std::move(tool), ToolSource::built_in());
}

void ToolRegistry::register_tool_with_tier(std::unique_ptr<Tool> tool, ToolTier tier) {
  if(!tool) {
    throw std::runtime_error("cannot register null tool");
  }
  const auto name = tool->name();
  if(tools_.contains(name)) {
    return;
  }
  tiers_[name] = tier;
  sources_[name] = ToolSource::built_in();
  tools_[name] = std::move(tool);
}

void ToolRegistry::register_tool_with_source(std::unique_ptr<Tool> tool, ToolSource source) {
  if(!tool) {
    throw std::runtime_error("cannot register null tool");
  }

  const auto name = tool->name();
  const auto existing_source = sources_.find(name);
  if(existing_source != sources_.end()) {
    return;
  }

  const auto tier = source.kind == ToolSourceKind::BuiltIn ? ToolTier::Default : ToolTier::Plugin;
  tiers_[name] = tier;
  sources_[name] = std::move(source);
  tools_[name] = std::move(tool);
}

void ToolRegistry::unregister_tool(const std::string& name) {
  tools_.erase(name);
  tiers_.erase(name);
  sources_.erase(name);
}

void ToolRegistry::add_middleware(std::shared_ptr<Middleware> middleware) {
  middleware_.push_back(std::move(middleware));
}

const Tool& ToolRegistry::find_tool_or_throw(const std::string& name) const {
  const auto it = tools_.find(name);
  if(it != tools_.end()) {
    return *it->second;
  }

  auto available = tool_names();
  std::sort(available.begin(), available.end());
  throw std::runtime_error("Tool not found: " + name + ". Available: " + join_names(available));
}

ava::types::ToolResult ToolRegistry::execute(ava::types::ToolCall tool_call) const {
  const auto& tool = find_tool_or_throw(tool_call.name);
  tool.backfill_input(tool_call.arguments);
  const auto source = sources_.contains(tool_call.name) ? sources_.at(tool_call.name) : ToolSource::built_in();

  for(const auto& middleware : middleware_) {
    middleware->before_with_source(tool_call, source);
  }

  std::optional<ava::types::ToolResult> result;
  std::exception_ptr error;

  std::size_t attempt = 0;
  while(true) {
    error = nullptr;
    try {
      result = tool.execute(tool_call.arguments);
    } catch(...) {
      result.reset();
      error = std::current_exception();
    }

    const auto retryable = retryable_tool_failure(tool_call.name, error, result);
    if(!retryable.has_value()) {
      break;
    }

    if(attempt >= retry::MAX_RETRIES) {
      break;
    }

    const auto backoff = retry::backoff_for_attempt(attempt);
    if(!backoff.has_value()) {
      break;
    }

    ++attempt;
    std::this_thread::sleep_for(*backoff);
  }

  if(error != nullptr) {
    std::rethrow_exception(error);
  }

  auto normalized = result.value_or(ava::types::ToolResult{});
  normalized.call_id = tool_call.id;

  for(const auto& middleware : middleware_) {
    normalized = middleware->after(tool_call, normalized);
  }

  return normalized;
}

std::vector<ava::types::Tool> ToolRegistry::list_tools() const {
  std::vector<ava::types::Tool> out;
  out.reserve(tools_.size());

  for(const auto& [_, tool] : tools_) {
    out.push_back(ava::types::Tool{.name = tool->name(), .description = tool->description(), .parameters = tool->parameters()});
  }

  std::sort(out.begin(), out.end(), [](const auto& left, const auto& right) {
    return left.name < right.name;
  });
  return out;
}

std::vector<ava::types::Tool> ToolRegistry::list_tools_for_tiers(const std::vector<ToolTier>& tiers) const {
  std::vector<ava::types::Tool> out;

  for(const auto& [name, tool] : tools_) {
    const auto tier_it = tiers_.find(name);
    const auto tier = tier_it != tiers_.end() ? tier_it->second : ToolTier::Default;
    if(!contains_tier(tiers, tier)) {
      continue;
    }

    out.push_back(ava::types::Tool{.name = tool->name(), .description = tool->description(), .parameters = tool->parameters()});
  }

  std::sort(out.begin(), out.end(), [](const auto& left, const auto& right) {
    return left.name < right.name;
  });
  return out;
}

std::vector<ToolDefinitionWithSource> ToolRegistry::list_tools_with_source() const {
  std::vector<ToolDefinitionWithSource> out;
  out.reserve(tools_.size());

  for(const auto& [name, tool] : tools_) {
    const auto source = sources_.contains(name) ? sources_.at(name) : ToolSource::built_in();
    const auto tier = tiers_.contains(name) ? tiers_.at(name) : ToolTier::Default;
    out.push_back(ToolDefinitionWithSource{
        .definition = ava::types::Tool{.name = tool->name(), .description = tool->description(), .parameters = tool->parameters()},
        .source = source,
        .tier = tier,
    });
  }

  std::sort(out.begin(), out.end(), [](const auto& left, const auto& right) {
    return left.definition.name < right.definition.name;
  });
  return out;
}

std::size_t ToolRegistry::tool_count() const {
  return tools_.size();
}

bool ToolRegistry::has_tool(const std::string& name) const {
  return tools_.contains(name);
}

std::vector<std::string> ToolRegistry::tool_names() const {
  std::vector<std::string> names;
  names.reserve(tools_.size());
  for(const auto& [name, _] : tools_) {
    names.push_back(name);
  }
  return names;
}

std::optional<ToolSource> ToolRegistry::tool_source(const std::string& name) const {
  if(!sources_.contains(name)) {
    return std::nullopt;
  }
  return sources_.at(name);
}

std::optional<nlohmann::json> ToolRegistry::tool_parameters(const std::string& name) const {
  if(!tools_.contains(name)) {
    return std::nullopt;
  }
  return tools_.at(name)->parameters();
}

}  // namespace ava::tools
