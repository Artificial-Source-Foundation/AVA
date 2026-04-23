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
  std::string timestamp;
  std::optional<std::string> parent_id;
};

struct SessionRecord {
  std::string id;
  std::string created_at;
  std::string updated_at;
  nlohmann::json metadata;
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
