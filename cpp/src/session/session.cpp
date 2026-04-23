#include "ava/session/session.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

#include <sqlite3.h>

#include "ava/types/session.hpp"

namespace ava::session {
namespace {

using SqliteDb = std::unique_ptr<sqlite3, decltype(&sqlite3_close)>;
using SqliteStmt = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;

void exec_or_throw(sqlite3* db, const char* sql);

constexpr const char* kSchemaSql = R"SQL(
PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS sessions (
  id TEXT PRIMARY KEY,
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL,
  metadata TEXT NOT NULL,
  branch_head TEXT
);

CREATE TABLE IF NOT EXISTS messages (
  id TEXT PRIMARY KEY,
  session_id TEXT NOT NULL,
  role TEXT NOT NULL,
  content TEXT NOT NULL,
  timestamp TEXT NOT NULL,
  parent_id TEXT,
  FOREIGN KEY(session_id) REFERENCES sessions(id) ON DELETE CASCADE,
  FOREIGN KEY(parent_id) REFERENCES messages(id) ON DELETE SET NULL
);

CREATE INDEX IF NOT EXISTS idx_messages_session_time ON messages(session_id, timestamp, id);
)SQL";

[[nodiscard]] SqliteDb open_db(const std::filesystem::path& path) {
  sqlite3* raw = nullptr;
  const auto rc = sqlite3_open(path.string().c_str(), &raw);
  if(rc != SQLITE_OK) {
    std::string message = raw != nullptr ? sqlite3_errmsg(raw) : "unknown sqlite error";
    if(raw != nullptr) {
      sqlite3_close(raw);
    }
    throw std::runtime_error("Failed to open sqlite DB: " + message);
  }
  exec_or_throw(raw, "PRAGMA foreign_keys = ON;");
  sqlite3_busy_timeout(raw, 5000);
  return SqliteDb(raw, sqlite3_close);
}

void exec_or_throw(sqlite3* db, const char* sql) {
  char* err = nullptr;
  const auto rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
  if(rc != SQLITE_OK) {
    std::string message = err != nullptr ? err : sqlite3_errmsg(db);
    if(err != nullptr) {
      sqlite3_free(err);
    }
    throw std::runtime_error("SQLite exec failed: " + message);
  }
}

[[nodiscard]] SqliteStmt prepare_or_throw(sqlite3* db, const char* sql) {
  sqlite3_stmt* raw = nullptr;
  const auto rc = sqlite3_prepare_v2(db, sql, -1, &raw, nullptr);
  if(rc != SQLITE_OK) {
    throw std::runtime_error("SQLite prepare failed: " + std::string(sqlite3_errmsg(db)));
  }
  return SqliteStmt(raw, sqlite3_finalize);
}

void bind_text_or_throw(sqlite3_stmt* stmt, int index, const std::string& value) {
  if(sqlite3_bind_text(stmt, index, value.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK) {
    throw std::runtime_error("SQLite bind text failed");
  }
}

void bind_optional_text_or_throw(sqlite3_stmt* stmt, int index, const std::optional<std::string>& value) {
  if(value.has_value()) {
    bind_text_or_throw(stmt, index, *value);
  } else if(sqlite3_bind_null(stmt, index) != SQLITE_OK) {
    throw std::runtime_error("SQLite bind null failed");
  }
}

[[nodiscard]] std::string column_text(sqlite3_stmt* stmt, int index) {
  if(const auto* value = sqlite3_column_text(stmt, index); value != nullptr) {
    return reinterpret_cast<const char*>(value);
  }
  return {};
}

[[nodiscard]] std::optional<std::string> column_optional_text(sqlite3_stmt* stmt, int index) {
  if(sqlite3_column_type(stmt, index) == SQLITE_NULL) {
    return std::nullopt;
  }
  return column_text(stmt, index);
}

[[nodiscard]] ava::types::SessionMessage row_to_message(sqlite3_stmt* stmt) {
  return ava::types::SessionMessage{
      .id = column_text(stmt, 0),
      .role = column_text(stmt, 1),
      .content = column_text(stmt, 2),
      .timestamp = column_text(stmt, 3),
      .parent_id = column_optional_text(stmt, 4),
  };
}

void step_done_or_throw(sqlite3_stmt* stmt, sqlite3* db) {
  if(const auto rc = sqlite3_step(stmt); rc != SQLITE_DONE) {
    throw std::runtime_error("SQLite step failed: " + std::string(sqlite3_errmsg(db)));
  }
}

[[nodiscard]] std::string preview_text(const std::string& value) {
  constexpr std::size_t kLimit = 80;
  if(value.size() <= kLimit) {
    return value;
  }
  return value.substr(0, 77) + "...";
}

}  // namespace

SessionManager::SessionManager(std::filesystem::path db_path)
    : db_path_(std::move(db_path)) {
  if(db_path_.has_parent_path()) {
    std::filesystem::create_directories(db_path_.parent_path());
  }
  init_schema();
}

ava::types::SessionRecord SessionManager::create() const {
  const auto now = now_utc_rfc3339();
  return ava::types::SessionRecord{
      .id = generate_id(),
      .created_at = now,
      .updated_at = now,
      .metadata = nlohmann::json::object(),
      .messages = {},
      .branch_head = std::nullopt,
  };
}

void SessionManager::save(const ava::types::SessionRecord& session) {
  auto db = open_db(db_path_);
  exec_or_throw(db.get(), "BEGIN IMMEDIATE TRANSACTION;");
  try {
    auto upsert_session = prepare_or_throw(
        db.get(),
        "INSERT INTO sessions (id, created_at, updated_at, metadata, branch_head) "
        "VALUES (?1, ?2, ?3, ?4, ?5) "
        "ON CONFLICT(id) DO UPDATE SET updated_at = excluded.updated_at, metadata = excluded.metadata, branch_head = excluded.branch_head"
    );
    bind_text_or_throw(upsert_session.get(), 1, session.id);
    bind_text_or_throw(upsert_session.get(), 2, session.created_at);
    bind_text_or_throw(upsert_session.get(), 3, session.updated_at);
    bind_text_or_throw(upsert_session.get(), 4, session.metadata.dump());
    bind_optional_text_or_throw(upsert_session.get(), 5, session.branch_head);
    step_done_or_throw(upsert_session.get(), db.get());

    auto upsert_message = prepare_or_throw(
        db.get(),
        "INSERT INTO messages (id, session_id, role, content, timestamp, parent_id) "
        "VALUES (?1, ?2, ?3, ?4, ?5, ?6) "
        "ON CONFLICT(id) DO UPDATE SET role = excluded.role, content = excluded.content, timestamp = excluded.timestamp, parent_id = excluded.parent_id"
    );

    std::unordered_set<std::string> retain_ids;
    for(const auto& message : session.messages) {
      retain_ids.insert(message.id);
      sqlite3_reset(upsert_message.get());
      sqlite3_clear_bindings(upsert_message.get());
      bind_text_or_throw(upsert_message.get(), 1, message.id);
      bind_text_or_throw(upsert_message.get(), 2, session.id);
      bind_text_or_throw(upsert_message.get(), 3, message.role);
      bind_text_or_throw(upsert_message.get(), 4, message.content);
      bind_text_or_throw(upsert_message.get(), 5, message.timestamp);
      bind_optional_text_or_throw(upsert_message.get(), 6, message.parent_id);
      step_done_or_throw(upsert_message.get(), db.get());
    }

    auto list_message_ids = prepare_or_throw(db.get(), "SELECT id FROM messages WHERE session_id = ?1");
    bind_text_or_throw(list_message_ids.get(), 1, session.id);

    std::vector<std::string> delete_ids;
    while(sqlite3_step(list_message_ids.get()) == SQLITE_ROW) {
      const auto id = column_text(list_message_ids.get(), 0);
      if(!retain_ids.contains(id)) {
        delete_ids.push_back(id);
      }
    }

    auto delete_message = prepare_or_throw(db.get(), "DELETE FROM messages WHERE id = ?1");
    for(const auto& id : delete_ids) {
      sqlite3_reset(delete_message.get());
      sqlite3_clear_bindings(delete_message.get());
      bind_text_or_throw(delete_message.get(), 1, id);
      step_done_or_throw(delete_message.get(), db.get());
    }

    exec_or_throw(db.get(), "COMMIT;");
  } catch(...) {
    exec_or_throw(db.get(), "ROLLBACK;");
    throw;
  }
}

std::optional<ava::types::SessionRecord> SessionManager::get(const std::string& id) const {
  auto db = open_db(db_path_);
  return get_with_db(db.get(), id);
}

std::vector<ava::types::SessionRecord> SessionManager::list_recent(std::size_t limit) const {
  auto db = open_db(db_path_);

  auto stmt = prepare_or_throw(
      db.get(),
      "SELECT id FROM sessions ORDER BY updated_at DESC, id DESC LIMIT ?1"
  );
  if(sqlite3_bind_int64(stmt.get(), 1, static_cast<sqlite3_int64>(limit)) != SQLITE_OK) {
    throw std::runtime_error("SQLite bind limit failed");
  }

  std::vector<ava::types::SessionRecord> sessions;
  while(sqlite3_step(stmt.get()) == SQLITE_ROW) {
    const auto id = column_text(stmt.get(), 0);
    if(auto loaded = get_with_db(db.get(), id); loaded.has_value()) {
      sessions.push_back(std::move(*loaded));
    }
  }
  return sessions;
}

void SessionManager::add_message(const std::string& session_id, const ava::types::SessionMessage& message) {
  auto db = open_db(db_path_);
  if(!session_exists(db.get(), session_id)) {
    throw std::invalid_argument("session not found: " + session_id);
  }
  exec_or_throw(db.get(), "BEGIN IMMEDIATE TRANSACTION;");
  try {
    auto stmt = prepare_or_throw(
        db.get(),
        "INSERT INTO messages (id, session_id, role, content, timestamp, parent_id) "
        "VALUES (?1, ?2, ?3, ?4, ?5, ?6) "
        "ON CONFLICT(id) DO UPDATE SET role = excluded.role, content = excluded.content, timestamp = excluded.timestamp, parent_id = excluded.parent_id"
    );
    bind_text_or_throw(stmt.get(), 1, message.id);
    bind_text_or_throw(stmt.get(), 2, session_id);
    bind_text_or_throw(stmt.get(), 3, message.role);
    bind_text_or_throw(stmt.get(), 4, message.content);
    bind_text_or_throw(stmt.get(), 5, message.timestamp);
    bind_optional_text_or_throw(stmt.get(), 6, message.parent_id);
    step_done_or_throw(stmt.get(), db.get());

    auto touch = prepare_or_throw(db.get(), "UPDATE sessions SET updated_at = ?1 WHERE id = ?2");
    bind_text_or_throw(touch.get(), 1, now_utc_rfc3339());
    bind_text_or_throw(touch.get(), 2, session_id);
    step_done_or_throw(touch.get(), db.get());

    exec_or_throw(db.get(), "COMMIT;");
  } catch(...) {
    exec_or_throw(db.get(), "ROLLBACK;");
    throw;
  }
}

std::vector<ava::types::SessionMessage> SessionManager::get_branch(const std::string& session_id, const std::string& leaf_id) const {
  const auto tree = get_tree(session_id);
  std::vector<ava::types::SessionMessage> branch;

  auto current = std::optional<std::string>{leaf_id};
  while(current.has_value()) {
    const auto it = tree.nodes.find(*current);
    if(it == tree.nodes.end()) {
      throw std::runtime_error("message not found in session branch walk: " + *current);
    }
    branch.push_back(it->second.message);
    current = it->second.message.parent_id;
  }

  std::reverse(branch.begin(), branch.end());
  return branch;
}

ava::types::ConversationTree SessionManager::get_tree(const std::string& session_id) const {
  auto db = open_db(db_path_);

  auto messages_stmt = prepare_or_throw(
      db.get(),
      "SELECT id, role, content, timestamp, parent_id FROM messages WHERE session_id = ?1"
  );
  bind_text_or_throw(messages_stmt.get(), 1, session_id);

  ava::types::ConversationTree tree;
  while(sqlite3_step(messages_stmt.get()) == SQLITE_ROW) {
    const auto message = row_to_message(messages_stmt.get());
    tree.nodes.emplace(message.id, ava::types::TreeNode{.message = message, .children = {}});
  }

  std::size_t root_count = 0;
  for(auto& [id, node] : tree.nodes) {
    (void)id;
    if(node.message.parent_id.has_value()) {
      if(auto it = tree.nodes.find(*node.message.parent_id); it != tree.nodes.end()) {
        it->second.children.push_back(node.message.id);
      }
    } else {
      ++root_count;
      if(!tree.root.has_value()) {
        tree.root = node.message.id;
      }
    }
  }

  if(root_count > 1) {
    throw std::runtime_error("session tree has multiple roots: " + session_id);
  }

  for(auto& [_, node] : tree.nodes) {
    std::sort(node.children.begin(), node.children.end(), [&](const auto& left, const auto& right) {
      const auto left_it = tree.nodes.find(left);
      const auto right_it = tree.nodes.find(right);
      if(left_it == tree.nodes.end() || right_it == tree.nodes.end()) {
        return left < right;
      }
      if(left_it->second.message.timestamp == right_it->second.message.timestamp) {
        return left < right;
      }
      return left_it->second.message.timestamp < right_it->second.message.timestamp;
    });
  }

  auto session_stmt = prepare_or_throw(db.get(), "SELECT branch_head FROM sessions WHERE id = ?1");
  bind_text_or_throw(session_stmt.get(), 1, session_id);
  if(sqlite3_step(session_stmt.get()) == SQLITE_ROW) {
    tree.branch_head = column_optional_text(session_stmt.get(), 0);
  }

  return tree;
}

std::vector<ava::types::BranchLeaf> SessionManager::get_branch_leaves(const std::string& session_id) const {
  const auto tree = get_tree(session_id);
  std::vector<ava::types::BranchLeaf> leaves;

  for(const auto& [id, node] : tree.nodes) {
    if(!node.children.empty()) {
      continue;
    }

    std::size_t depth = 0;
    auto current = std::optional<std::string>{id};
    while(current.has_value()) {
      ++depth;
      const auto it = tree.nodes.find(*current);
      if(it == tree.nodes.end()) {
        break;
      }
      current = it->second.message.parent_id;
    }

    leaves.push_back(ava::types::BranchLeaf{
        .leaf_id = id,
        .preview = preview_text(node.message.content),
        .depth = depth,
        .role = node.message.role,
        .timestamp = node.message.timestamp,
        .is_active = tree.branch_head == id,
    });
  }

  std::sort(leaves.begin(), leaves.end(), [](const auto& left, const auto& right) {
    if(left.timestamp == right.timestamp) {
      return left.leaf_id < right.leaf_id;
    }
    return left.timestamp < right.timestamp;
  });
  return leaves;
}

ava::types::SessionMessage SessionManager::branch_from(
    const std::string& session_id,
    const std::string& branch_point_id,
    const std::string& new_user_message
) {
  const auto message = ava::types::SessionMessage{
      .id = generate_id(),
      .role = "user",
      .content = new_user_message,
      .timestamp = now_utc_rfc3339(),
      .parent_id = branch_point_id,
  };

  auto db = open_db(db_path_);
  if(!session_exists(db.get(), session_id)) {
    throw std::invalid_argument("session not found: " + session_id);
  }
  if(!message_exists(db.get(), session_id, branch_point_id)) {
    throw std::invalid_argument("branch point not found in session: " + branch_point_id);
  }
  exec_or_throw(db.get(), "BEGIN IMMEDIATE TRANSACTION;");
  try {
    auto insert = prepare_or_throw(
        db.get(),
        "INSERT INTO messages (id, session_id, role, content, timestamp, parent_id) VALUES (?1, ?2, ?3, ?4, ?5, ?6)"
    );
    bind_text_or_throw(insert.get(), 1, message.id);
    bind_text_or_throw(insert.get(), 2, session_id);
    bind_text_or_throw(insert.get(), 3, message.role);
    bind_text_or_throw(insert.get(), 4, message.content);
    bind_text_or_throw(insert.get(), 5, message.timestamp);
    bind_optional_text_or_throw(insert.get(), 6, message.parent_id);
    step_done_or_throw(insert.get(), db.get());

    auto update = prepare_or_throw(
        db.get(),
        "UPDATE sessions SET branch_head = ?1, updated_at = ?2 WHERE id = ?3"
    );
    bind_text_or_throw(update.get(), 1, message.id);
    bind_text_or_throw(update.get(), 2, now_utc_rfc3339());
    bind_text_or_throw(update.get(), 3, session_id);
    step_done_or_throw(update.get(), db.get());

    exec_or_throw(db.get(), "COMMIT;");
    return message;
  } catch(...) {
    exec_or_throw(db.get(), "ROLLBACK;");
    throw;
  }
}

void SessionManager::switch_branch(const std::string& session_id, const std::string& leaf_id) {
  auto db = open_db(db_path_);
  if(!session_exists(db.get(), session_id)) {
    throw std::runtime_error("session not found: " + session_id);
  }
  if(!message_exists(db.get(), session_id, leaf_id)) {
    throw std::invalid_argument("branch leaf not found in session: " + leaf_id);
  }
  auto stmt = prepare_or_throw(
      db.get(),
      "UPDATE sessions SET branch_head = ?1, updated_at = ?2 WHERE id = ?3"
  );
  bind_text_or_throw(stmt.get(), 1, leaf_id);
  bind_text_or_throw(stmt.get(), 2, now_utc_rfc3339());
  bind_text_or_throw(stmt.get(), 3, session_id);
  step_done_or_throw(stmt.get(), db.get());
}

void SessionManager::init_schema() {
  auto db = open_db(db_path_);
  exec_or_throw(db.get(), kSchemaSql);
}

std::optional<ava::types::SessionRecord> SessionManager::get_with_db(void* db_raw, const std::string& id) const {
  auto* db = static_cast<sqlite3*>(db_raw);

  auto session_stmt = prepare_or_throw(
      db,
      "SELECT id, created_at, updated_at, metadata, branch_head FROM sessions WHERE id = ?1"
  );
  bind_text_or_throw(session_stmt.get(), 1, id);
  if(sqlite3_step(session_stmt.get()) != SQLITE_ROW) {
    return std::nullopt;
  }

  ava::types::SessionRecord session;
  session.id = column_text(session_stmt.get(), 0);
  session.created_at = column_text(session_stmt.get(), 1);
  session.updated_at = column_text(session_stmt.get(), 2);
  const auto metadata_str = column_text(session_stmt.get(), 3);
  session.metadata = metadata_str.empty() ? nlohmann::json::object() : nlohmann::json::parse(metadata_str);
  session.branch_head = column_optional_text(session_stmt.get(), 4);

  auto messages_stmt = prepare_or_throw(
      db,
      "SELECT id, role, content, timestamp, parent_id FROM messages WHERE session_id = ?1 ORDER BY timestamp ASC, id ASC"
  );
  bind_text_or_throw(messages_stmt.get(), 1, id);
  while(sqlite3_step(messages_stmt.get()) == SQLITE_ROW) {
    session.messages.push_back(row_to_message(messages_stmt.get()));
  }

  return session;
}

bool SessionManager::session_exists(void* db_raw, const std::string& session_id) const {
  auto* db = static_cast<sqlite3*>(db_raw);
  auto stmt = prepare_or_throw(db, "SELECT 1 FROM sessions WHERE id = ?1");
  bind_text_or_throw(stmt.get(), 1, session_id);
  return sqlite3_step(stmt.get()) == SQLITE_ROW;
}

bool SessionManager::message_exists(void* db_raw, const std::string& session_id, const std::string& message_id) const {
  auto* db = static_cast<sqlite3*>(db_raw);
  auto stmt = prepare_or_throw(
      db,
      "SELECT 1 FROM messages WHERE session_id = ?1 AND id = ?2"
  );
  bind_text_or_throw(stmt.get(), 1, session_id);
  bind_text_or_throw(stmt.get(), 2, message_id);
  return sqlite3_step(stmt.get()) == SQLITE_ROW;
}

std::string SessionManager::generate_id() {
  static thread_local std::mt19937_64 rng(std::random_device{}());
  static constexpr char kHex[] = "0123456789abcdef";

  std::string id(36, '0');
  const int dash_positions[] = {8, 13, 18, 23};
  for(int i = 0; i < 36; ++i) {
    const auto is_dash = std::any_of(std::begin(dash_positions), std::end(dash_positions), [&](int pos) {
      return i == pos;
    });
    if(is_dash) {
      id[i] = '-';
      continue;
    }
    id[i] = kHex[rng() % 16];
  }
  id[14] = '4';
  id[19] = kHex[(rng() % 4) + 8];
  return id;
}

std::string SessionManager::now_utc_rfc3339() {
  const auto now = std::chrono::system_clock::now();
  const auto time = std::chrono::system_clock::to_time_t(now);

  std::tm utc{};
#if defined(_WIN32)
  gmtime_s(&utc, &time);
#else
  gmtime_r(&time, &utc);
#endif

  std::ostringstream out;
  out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
  return out.str();
}

}  // namespace ava::session
