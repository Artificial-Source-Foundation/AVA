#include "ava/mcp/manager.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <unordered_set>
#include <utility>

#include "ava/mcp/transport.hpp"

namespace ava::mcp {
namespace {

[[nodiscard]] bool has_tool_name(const std::vector<McpTool>& tools, const std::string& tool_name) {
  return std::any_of(tools.begin(), tools.end(), [&](const auto& tool) {
    return tool.name == tool_name;
  });
}

[[nodiscard]] std::string available_tool_names(const std::vector<McpTool>& tools) {
  std::string names;
  for(std::size_t index = 0; index < tools.size(); ++index) {
    if(index > 0) {
      names += ", ";
    }
    names += tools[index].name;
  }
  return names;
}

}  // namespace

McpManager::McpManager(TransportFactory transport_factory)
    : transport_factory_(std::move(transport_factory)) {}

McpManager::~McpManager() {
  shutdown();
}

std::vector<McpServerReport> McpManager::initialize(const McpConfig& config) {
  shutdown();
  reports_.clear();

  std::vector<McpServerReport> reports;
  const auto remember_report = [&](const McpServerReport& report) {
    if(report.server_name.empty()) {
      if(!reports_.contains("")) {
        reports_.insert_or_assign("", report);
      }
    } else if(!reports_.contains(report.server_name)) {
      reports_.insert_or_assign(report.server_name, report);
    }
    reports.push_back(report);
  };
  std::unordered_set<std::string> seen_server_names;

  for(const auto& server : config.servers) {
    if(!server.enabled) {
      continue;
    }

    McpServerReport report{
        .server_name = server.name,
        .connected = false,
        .error = std::nullopt,
        .tool_count = 0,
    };

    if(server.name.empty()) {
      report.error = "MCP server name must not be empty";
      remember_report(report);
      continue;
    }

    if(!seen_server_names.insert(server.name).second) {
      report.error = "duplicate MCP server name: " + server.name;
      remember_report(report);
      continue;
    }

    try {
      auto transport = create_transport(server);
      if(!transport) {
        throw std::runtime_error("MCP transport factory returned null transport");
      }

      auto client = std::make_unique<McpClient>(std::move(transport), server.name);
      client->initialize();
      auto tools = client->list_tools();

      report.connected = true;
      report.tool_count = tools.size();

      servers_.insert_or_assign(
          server.name,
          ServerRuntime{
              .client = std::move(client),
              .tools = std::move(tools),
          }
      );
    } catch(const std::exception& e) {
      report.error = e.what();
    } catch(...) {
      report.error = "MCP server initialization failed with a non-standard exception";
    }

    remember_report(report);
  }

  rebuild_tool_index();
  return reports;
}

std::vector<McpDiscoveredTool> McpManager::list_tools() const {
  return tools_;
}

std::size_t McpManager::server_count() const {
  return servers_.size();
}

std::size_t McpManager::tool_count() const {
  return tools_.size();
}

bool McpManager::has_server(const std::string& server_name) const {
  return servers_.contains(server_name);
}

std::optional<McpServerReport> McpManager::server_report(const std::string& server_name) const {
  if(!reports_.contains(server_name)) {
    return std::nullopt;
  }
  return reports_.at(server_name);
}

nlohmann::json McpManager::call_tool(
    const std::string& server_name,
    const std::string& tool_name,
    const nlohmann::json& arguments
) {
  if(server_name.empty()) {
    throw std::runtime_error("MCP server name must not be empty");
  }
  if(tool_name.empty()) {
    throw std::runtime_error("MCP tool name must not be empty");
  }

  if(!servers_.contains(server_name)) {
    throw std::runtime_error("MCP server is not connected: " + server_name);
  }

  auto& server = servers_.at(server_name);
  if(!has_tool_name(server.tools, tool_name)) {
    throw std::runtime_error(
        "MCP tool '" + tool_name + "' is not registered on server '" + server_name +
        "'. Available: " + available_tool_names(server.tools)
    );
  }

  return server.client->call_tool(tool_name, arguments);
}

void McpManager::shutdown() noexcept {
  for(auto& [_, runtime] : servers_) {
    if(!runtime.client) {
      continue;
    }
    try {
      runtime.client->close();
    } catch(...) {
      // Best-effort shutdown only.
    }
  }
  servers_.clear();
  tools_.clear();
  reports_.clear();
}

std::unique_ptr<McpTransport> McpManager::create_transport(const McpServerConfig& config) const {
  if(transport_factory_) {
    return transport_factory_(config);
  }

  if(config.transport_type != TransportType::Stdio) {
    throw std::runtime_error("MCP transport type is not supported in C++ Milestone 25");
  }

  return std::make_unique<StdioTransport>(
      config.stdio.command,
      config.stdio.args,
      config.stdio.env,
      std::chrono::milliseconds(config.stdio.receive_timeout_ms)
  );
}

void McpManager::rebuild_tool_index() {
  tools_.clear();
  for(const auto& [server_name, runtime] : servers_) {
    for(const auto& tool : runtime.tools) {
      tools_.push_back(McpDiscoveredTool{.server_name = server_name, .tool = tool});
    }
  }

  std::sort(tools_.begin(), tools_.end(), [](const auto& left, const auto& right) {
    if(left.server_name == right.server_name) {
      return left.tool.name < right.tool.name;
    }
    return left.server_name < right.server_name;
  });
}

}  // namespace ava::mcp
