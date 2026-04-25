#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "ava/types/session.hpp"

namespace ava::session {

struct SqlitePolicySnapshot {
  std::string journal_mode;
  int synchronous{0};
  int foreign_keys{0};
  int busy_timeout{0};
  int cache_size{0};
};

class SessionManager {
 public:
  explicit SessionManager(std::filesystem::path db_path);

  [[nodiscard]] ava::types::SessionRecord create() const;
  void save(const ava::types::SessionRecord& session);
  [[nodiscard]] std::optional<ava::types::SessionRecord> get(const std::string& id) const;
  [[nodiscard]] std::vector<ava::types::SessionRecord> list_recent(std::size_t limit) const;
  void remove(const std::string& id) const;

  void add_message(const std::string& session_id, const ava::types::SessionMessage& message);

  [[nodiscard]] std::vector<ava::types::SessionMessage> get_branch(
      const std::string& session_id,
      const std::string& leaf_id
  ) const;
  [[nodiscard]] ava::types::ConversationTree get_tree(const std::string& session_id) const;
  [[nodiscard]] std::vector<ava::types::BranchLeaf> get_branch_leaves(const std::string& session_id) const;

  [[nodiscard]] ava::types::SessionMessage branch_from(
      const std::string& session_id,
      const std::string& branch_point_id,
      const std::string& new_user_message
  );
  void switch_branch(const std::string& session_id, const std::string& leaf_id);

  [[nodiscard]] const std::filesystem::path& db_path() const { return db_path_; }
  [[nodiscard]] SqlitePolicySnapshot sqlite_policy_snapshot() const;

 private:
  std::filesystem::path db_path_;

  void init_schema();
  [[nodiscard]] std::optional<ava::types::SessionRecord> get_with_db(void* db, const std::string& id) const;
  [[nodiscard]] bool session_exists(void* db, const std::string& session_id) const;
  [[nodiscard]] bool message_exists(void* db, const std::string& session_id, const std::string& message_id) const;

  [[nodiscard]] static std::string generate_id();
  [[nodiscard]] static std::string now_utc_rfc3339();
};

}  // namespace ava::session
