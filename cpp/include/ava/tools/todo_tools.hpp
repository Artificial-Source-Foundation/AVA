#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "ava/tools/tool.hpp"

namespace ava::tools {

struct TodoItem {
  std::string content;
  std::string status;
  std::string priority;
};

class TodoListState {
 public:
  void set(std::vector<TodoItem> items);
  [[nodiscard]] std::vector<TodoItem> get() const;

 private:
  mutable std::mutex mutex_;
  std::vector<TodoItem> items_;
};

class TodoWriteTool final : public Tool {
 public:
  explicit TodoWriteTool(std::shared_ptr<TodoListState> state);

  [[nodiscard]] std::string name() const override;
  [[nodiscard]] std::string description() const override;
  [[nodiscard]] std::string search_hint() const override;
  [[nodiscard]] nlohmann::json parameters() const override;
  [[nodiscard]] ava::types::ToolResult execute(const nlohmann::json& args) const override;

 private:
  std::shared_ptr<TodoListState> state_;
};

class TodoReadTool final : public Tool {
 public:
  explicit TodoReadTool(std::shared_ptr<TodoListState> state);

  [[nodiscard]] std::string name() const override;
  [[nodiscard]] std::string description() const override;
  [[nodiscard]] std::string search_hint() const override;
  [[nodiscard]] nlohmann::json parameters() const override;
  [[nodiscard]] ava::types::ToolResult execute(const nlohmann::json& args) const override;

 private:
  std::shared_ptr<TodoListState> state_;
};

}  // namespace ava::tools
