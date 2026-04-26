#include "ava/tools/todo_tools.hpp"

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ava::tools {
namespace {

constexpr std::size_t kMaxTodoContentBytes = 8 * 1024;

[[nodiscard]] bool is_valid_status(const std::string& status) {
  return status == "pending" || status == "in_progress" || status == "completed" || status == "cancelled";
}

[[nodiscard]] bool is_valid_priority(const std::string& priority) {
  return priority == "high" || priority == "medium" || priority == "low";
}

[[nodiscard]] std::size_t incomplete_count(const std::vector<TodoItem>& items) {
  return static_cast<std::size_t>(std::count_if(items.begin(), items.end(), [](const TodoItem& item) {
    return item.status != "completed" && item.status != "cancelled";
  }));
}

[[nodiscard]] nlohmann::json todo_item_to_json(const TodoItem& item) {
  return nlohmann::json{{"content", item.content}, {"status", item.status}, {"priority", item.priority}};
}

[[nodiscard]] std::string required_string_field(
    const nlohmann::json& value,
    const std::string& path,
    const char* field_name
) {
  if(!value.contains(field_name) || !value.at(field_name).is_string()) {
    throw std::runtime_error(path + ": missing required field: " + field_name);
  }
  return value.at(field_name).get<std::string>();
}

}  // namespace

void TodoListState::set(std::vector<TodoItem> items) {
  std::lock_guard<std::mutex> lock(mutex_);
  items_ = std::move(items);
}

std::vector<TodoItem> TodoListState::get() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return items_;
}

TodoWriteTool::TodoWriteTool(std::shared_ptr<TodoListState> state)
    : state_(std::move(state)) {
  if(!state_) {
    throw std::invalid_argument("TodoWriteTool requires shared state");
  }
}

std::string TodoWriteTool::name() const {
  return "todo_write";
}

std::string TodoWriteTool::description() const {
  return "Create or update the agent's todo/progress list. Each call replaces the entire list.";
}

std::string TodoWriteTool::search_hint() const {
  return "todo checklist progress tasks status";
}

nlohmann::json TodoWriteTool::parameters() const {
  return nlohmann::json{
      {"type", "object"},
      {"required", nlohmann::json::array({"todos"})},
      {"properties",
       {
           {"todos",
            {
                {"type", "array"},
                {"description", "Complete todo list that replaces any existing list"},
                {"items",
                 {
                     {"type", "object"},
                     {"required", nlohmann::json::array({"content", "status", "priority"})},
                     {"properties",
                           {
                           {"content",
                            {{"type", "string"},
                             {"maxLength", kMaxTodoContentBytes},
                             {"description", "Task description"}}},
                          {"status",
                           {{"type", "string"},
                            {"enum", nlohmann::json::array({"pending", "in_progress", "completed", "cancelled"})},
                            {"description", "Task status"}}},
                          {"priority",
                           {{"type", "string"},
                            {"enum", nlohmann::json::array({"high", "medium", "low"})},
                            {"description", "Priority level"}}},
                      }},
                 }},
            }},
       }},
  };
}

ava::types::ToolResult TodoWriteTool::execute(const nlohmann::json& args) const {
  if(!args.contains("todos")) {
    throw std::runtime_error("missing required field: todos");
  }

  const auto& todos_value = args.at("todos");
  if(!todos_value.is_array()) {
    throw std::runtime_error("todos must be an array");
  }

  std::vector<TodoItem> items;
  items.reserve(todos_value.size());
  for(std::size_t index = 0; index < todos_value.size(); ++index) {
    const auto& entry = todos_value.at(index);
    const auto path = "todos[" + std::to_string(index) + "]";

    const auto content = required_string_field(entry, path, "content");
    const auto status = required_string_field(entry, path, "status");
    const auto priority = required_string_field(entry, path, "priority");

    if(content.size() > kMaxTodoContentBytes) {
      throw std::runtime_error(
          path + ": content exceeds maximum length of " + std::to_string(kMaxTodoContentBytes) + " bytes"
      );
    }

    if(!is_valid_status(status)) {
      throw std::runtime_error(
          path + ": invalid status '" + status + "', expected one of: pending, in_progress, completed, cancelled"
      );
    }
    if(!is_valid_priority(priority)) {
      throw std::runtime_error(path + ": invalid priority '" + priority + "', expected one of: high, medium, low");
    }

    items.push_back(TodoItem{.content = content, .status = status, .priority = priority});
  }

  state_->set(items);

  auto rendered = nlohmann::json::array();
  for(const auto& item : items) {
    rendered.push_back(todo_item_to_json(item));
  }

  return ava::types::ToolResult{
      .call_id = "",
      .content = "Updated todo list (" + std::to_string(items.size()) + " total, " +
                 std::to_string(incomplete_count(items)) + " incomplete):\n" + rendered.dump(2),
      .is_error = false,
  };
}

TodoReadTool::TodoReadTool(std::shared_ptr<TodoListState> state)
    : state_(std::move(state)) {
  if(!state_) {
    throw std::invalid_argument("TodoReadTool requires shared state");
  }
}

std::string TodoReadTool::name() const {
  return "todo_read";
}

std::string TodoReadTool::description() const {
  return "Read the current todo/progress list to inspect pending, active, and completed work.";
}

std::string TodoReadTool::search_hint() const {
  return "todo checklist progress read tasks";
}

nlohmann::json TodoReadTool::parameters() const {
  return nlohmann::json{{"type", "object"}, {"properties", nlohmann::json::object()}};
}

ava::types::ToolResult TodoReadTool::execute(const nlohmann::json& args) const {
  (void)args;
  const auto items = state_->get();
  if(items.empty()) {
    return ava::types::ToolResult{
        .call_id = "",
        .content = "No todos. Use todo_write to create a todo list.",
        .is_error = false,
    };
  }

  auto rendered = nlohmann::json::array();
  for(const auto& item : items) {
    rendered.push_back(todo_item_to_json(item));
  }

  return ava::types::ToolResult{
      .call_id = "",
      .content = "Todo list (" + std::to_string(items.size()) + " total, " +
                 std::to_string(incomplete_count(items)) + " incomplete):\n" + rendered.dump(2),
      .is_error = false,
  };
}

}  // namespace ava::tools
