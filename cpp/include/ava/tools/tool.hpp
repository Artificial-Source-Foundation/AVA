#pragma once

#include <optional>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "ava/types/tool.hpp"

namespace ava::tools {

enum class ToolTier {
  Default,
  Extended,
  Plugin,
  Deferred,
};

enum class ToolSourceKind {
  BuiltIn,
  MCP,
  Custom,
};

struct ToolSource {
  ToolSourceKind kind{ToolSourceKind::BuiltIn};
  std::string detail;

  [[nodiscard]] static ToolSource built_in() { return ToolSource{}; }
  [[nodiscard]] static ToolSource mcp(std::string server) {
    return ToolSource{.kind = ToolSourceKind::MCP, .detail = std::move(server)};
  }
  [[nodiscard]] static ToolSource custom(std::string path) {
    return ToolSource{.kind = ToolSourceKind::Custom, .detail = std::move(path)};
  }

  [[nodiscard]] std::string to_string() const;
  [[nodiscard]] bool operator==(const ToolSource& other) const = default;
};

class Tool {
 public:
  virtual ~Tool() = default;

  [[nodiscard]] virtual std::string name() const = 0;
  [[nodiscard]] virtual std::string description() const = 0;
  [[nodiscard]] virtual nlohmann::json parameters() const = 0;

  virtual ava::types::ToolResult execute(const nlohmann::json& args) const = 0;

  virtual void backfill_input(nlohmann::json& args) const;
  [[nodiscard]] virtual std::string search_hint() const;
};

class Middleware {
 public:
  virtual ~Middleware() = default;
  virtual void before(const ava::types::ToolCall& tool_call) const;
  virtual void before_with_source(const ava::types::ToolCall& tool_call, const ToolSource& source) const;
  [[nodiscard]] virtual ava::types::ToolResult after(
      const ava::types::ToolCall& tool_call,
      const ava::types::ToolResult& result
  ) const;
};

}  // namespace ava::tools
