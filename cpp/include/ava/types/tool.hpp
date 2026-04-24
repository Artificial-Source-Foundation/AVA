#pragma once

#include <string>

#include <nlohmann/json.hpp>

namespace ava::types {

struct Tool {
  std::string name;
  std::string description;
  nlohmann::json parameters;
};

struct ToolCall {
  std::string id;
  std::string name;
  nlohmann::json arguments;
};

inline bool operator==(const ToolCall& lhs, const ToolCall& rhs) {
  return lhs.id == rhs.id && lhs.name == rhs.name && lhs.arguments == rhs.arguments;
}

struct ToolResult {
  std::string call_id;
  std::string content;
  bool is_error{false};
};

inline bool operator==(const ToolResult& lhs, const ToolResult& rhs) {
  return lhs.call_id == rhs.call_id && lhs.content == rhs.content && lhs.is_error == rhs.is_error;
}

inline void to_json(nlohmann::json& json, const Tool& value) {
  json = nlohmann::json{{"name", value.name}, {"description", value.description}, {"parameters", value.parameters}};
}

inline void from_json(const nlohmann::json& json, Tool& value) {
  json.at("name").get_to(value.name);
  json.at("description").get_to(value.description);
  json.at("parameters").get_to(value.parameters);
}

inline void to_json(nlohmann::json& json, const ToolCall& value) {
  json = nlohmann::json{{"id", value.id}, {"name", value.name}, {"arguments", value.arguments}};
}

inline void from_json(const nlohmann::json& json, ToolCall& value) {
  json.at("id").get_to(value.id);
  json.at("name").get_to(value.name);
  json.at("arguments").get_to(value.arguments);
}

inline void to_json(nlohmann::json& json, const ToolResult& value) {
  json = nlohmann::json{{"call_id", value.call_id}, {"content", value.content}, {"is_error", value.is_error}};
}

inline void from_json(const nlohmann::json& json, ToolResult& value) {
  json.at("call_id").get_to(value.call_id);
  json.at("content").get_to(value.content);
  json.at("is_error").get_to(value.is_error);
}

}  // namespace ava::types
