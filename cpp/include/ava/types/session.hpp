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

struct SessionRecoveryStats {
  std::size_t interrupted_tools_added{0};
  std::size_t empty_assistant_removed{0};
  std::size_t orphan_tool_removed{0};
  std::size_t non_user_after_terminal_removed{0};
  std::size_t consecutive_users_merged{0};
  std::size_t duplicate_messages_removed{0};

  [[nodiscard]] bool changed() const {
    return interrupted_tools_added > 0 || empty_assistant_removed > 0 || orphan_tool_removed > 0
           || non_user_after_terminal_removed > 0 || consecutive_users_merged > 0 || duplicate_messages_removed > 0;
  }
};

SessionRecoveryStats synthesize_interrupted_tool_results(SessionRecord& session);
SessionRecoveryStats repair_session_transcript_structure(SessionRecord& session);
SessionRecoveryStats recover_session_messages(SessionRecord& session);

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
