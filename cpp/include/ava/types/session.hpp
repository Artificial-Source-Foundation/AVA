#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace ava::types {

struct SessionMessage {
  std::string id;
  std::string role;
  std::string content;
  nlohmann::json tool_calls = nlohmann::json::array();
  nlohmann::json tool_results = nlohmann::json::array();
  std::optional<std::string> tool_call_id;
  nlohmann::json images = nlohmann::json::array();
  std::string timestamp;
  std::optional<std::string> parent_id;
  bool agent_visible{true};
  bool user_visible{true};
  std::optional<std::string> original_content;
  nlohmann::json structured_content = nlohmann::json::array();
  nlohmann::json metadata = nlohmann::json::object();
};

struct SessionRecord {
  std::string id;
  std::string created_at;
  std::string updated_at;
  nlohmann::json metadata;
  nlohmann::json token_usage = nlohmann::json::object();
  std::vector<SessionMessage> messages;
  std::optional<std::string> branch_head;
};

struct TreeNode {
  SessionMessage message;
  std::vector<std::string> children;
};

struct ConversationTree {
  std::optional<std::string> root;
  std::unordered_map<std::string, TreeNode> nodes;
  std::optional<std::string> branch_head;
};

struct BranchLeaf {
  std::string leaf_id;
  std::string preview;
  std::size_t depth{0};
  std::string role;
  std::string timestamp;
  bool is_active{false};
};

}  // namespace ava::types
