#include "ava/session/session.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
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
  parent_id TEXT,
  token_usage TEXT NOT NULL DEFAULT '{}',
  branch_head TEXT
);

CREATE TABLE IF NOT EXISTS messages (
  id TEXT PRIMARY KEY,
  session_id TEXT NOT NULL,
  role TEXT NOT NULL,
  content TEXT NOT NULL,
  tool_calls TEXT NOT NULL DEFAULT '[]',
  tool_results TEXT NOT NULL DEFAULT '[]',
  tool_call_id TEXT,
  images TEXT NOT NULL DEFAULT '[]',
  timestamp TEXT NOT NULL,
  parent_id TEXT,
  agent_visible INTEGER NOT NULL DEFAULT 1,
  user_visible INTEGER NOT NULL DEFAULT 1,
  original_content TEXT,
  structured_content TEXT NOT NULL DEFAULT '[]',
  metadata TEXT NOT NULL DEFAULT '{}',
  FOREIGN KEY(session_id) REFERENCES sessions(id) ON DELETE CASCADE,
  FOREIGN KEY(parent_id) REFERENCES messages(id) ON DELETE SET NULL
);

CREATE INDEX IF NOT EXISTS idx_messages_session_time ON messages(session_id, timestamp, id);
CREATE UNIQUE INDEX IF NOT EXISTS idx_messages_session_id ON messages(session_id, id);

CREATE TRIGGER IF NOT EXISTS messages_parent_same_session_insert
BEFORE INSERT ON messages
WHEN NEW.parent_id IS NOT NULL
  AND NOT EXISTS (SELECT 1 FROM messages WHERE session_id = NEW.session_id AND id = NEW.parent_id)
BEGIN
  SELECT RAISE(ABORT, 'message parent must belong to same session');
END;

CREATE TRIGGER IF NOT EXISTS messages_parent_same_session_update
BEFORE UPDATE OF session_id, parent_id ON messages
WHEN NEW.parent_id IS NOT NULL
  AND NOT EXISTS (SELECT 1 FROM messages WHERE session_id = NEW.session_id AND id = NEW.parent_id)
BEGIN
  SELECT RAISE(ABORT, 'message parent must belong to same session');
END;
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
  sqlite3_busy_timeout(raw, 5000);
  exec_or_throw(raw, "PRAGMA journal_mode = WAL;");
  exec_or_throw(raw, "PRAGMA synchronous = NORMAL;");
  exec_or_throw(raw, "PRAGMA foreign_keys = ON;");
  exec_or_throw(raw, "PRAGMA cache_size = -64000;");
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

void rollback_best_effort(sqlite3* db) noexcept {
  try {
    exec_or_throw(db, "ROLLBACK;");
  } catch(...) {
    // Preserve the original exception; closing the connection will clean up.
  }
}

void exec_or_ignore_duplicate_column(sqlite3* db, const char* sql) {
  char* err = nullptr;
  const auto rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
  if(rc == SQLITE_OK) {
    return;
  }

  std::string message = err != nullptr ? err : sqlite3_errmsg(db);
  if(err != nullptr) {
    sqlite3_free(err);
  }
  if(message.find("duplicate column name") != std::string::npos) {
    return;
  }
  throw std::runtime_error("SQLite exec failed: " + message);
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

[[nodiscard]] nlohmann::json column_json_array(sqlite3_stmt* stmt, int index) {
  try {
    const auto parsed = nlohmann::json::parse(column_text(stmt, index));
    if(parsed.is_array()) {
      return parsed;
    }
  } catch(const std::exception&) {
    // Keep older or partially-written milestone databases loadable.
  }
  return nlohmann::json::array();
}

[[nodiscard]] nlohmann::json column_json_value(sqlite3_stmt* stmt, int index, nlohmann::json fallback) {
  try {
    return nlohmann::json::parse(column_text(stmt, index));
  } catch(const std::exception&) {
    return fallback;
  }
}

[[nodiscard]] nlohmann::json column_json_object(sqlite3_stmt* stmt, int index) {
  try {
    const auto parsed = nlohmann::json::parse(column_text(stmt, index));
    if(parsed.is_object()) {
      return parsed;
    }
  } catch(const std::exception&) {
    // Keep older or partially-written milestone databases loadable.
  }
  return nlohmann::json::object();
}

[[nodiscard]] bool column_bool(sqlite3_stmt* stmt, int index, bool fallback = true) {
  if(sqlite3_column_type(stmt, index) == SQLITE_NULL) {
    return fallback;
  }
  return sqlite3_column_int(stmt, index) != 0;
}

[[nodiscard]] ava::types::SessionMessage row_to_message(sqlite3_stmt* stmt) {
  return ava::types::SessionMessage{
      .id = column_text(stmt, 0),
      .role = column_text(stmt, 1),
      .content = column_text(stmt, 2),
      .tool_calls = column_json_array(stmt, 3),
      .tool_results = column_json_array(stmt, 4),
      .tool_call_id = column_optional_text(stmt, 5),
      .images = column_json_array(stmt, 6),
      .timestamp = column_text(stmt, 7),
      .parent_id = column_optional_text(stmt, 8),
      .agent_visible = column_bool(stmt, 9, true),
      .user_visible = column_bool(stmt, 10, true),
      .original_content = column_optional_text(stmt, 11),
      .structured_content = column_json_array(stmt, 12),
      .metadata = column_json_object(stmt, 13),
  };
}

[[nodiscard]] std::optional<std::string> session_parent_id_from_metadata(const nlohmann::json& metadata) {
  if(!metadata.is_object()) {
    return std::nullopt;
  }
  const auto it = metadata.find("parent_id");
  if(it == metadata.end() || !it->is_string()) {
    return std::nullopt;
  }
  return it->get<std::string>();
}

[[nodiscard]] std::string json_array_text(const nlohmann::json& value) {
  return value.is_array() ? value.dump() : "[]";
}

[[nodiscard]] std::string json_object_text(const nlohmann::json& value) {
  return value.is_object() ? value.dump() : "{}";
}

[[nodiscard]] std::optional<std::string> message_session_id(sqlite3* db, const std::string& message_id) {
  auto stmt = prepare_or_throw(db, "SELECT session_id FROM messages WHERE id = ?1");
  bind_text_or_throw(stmt.get(), 1, message_id);
  if(sqlite3_step(stmt.get()) == SQLITE_ROW) {
    return column_text(stmt.get(), 0);
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::string> message_parent_id(sqlite3* db, const std::string& session_id, const std::string& message_id) {
  auto stmt = prepare_or_throw(db, "SELECT parent_id FROM messages WHERE session_id = ?1 AND id = ?2");
  bind_text_or_throw(stmt.get(), 1, session_id);
  bind_text_or_throw(stmt.get(), 2, message_id);
  if(sqlite3_step(stmt.get()) == SQLITE_ROW) {
    return column_optional_text(stmt.get(), 0);
  }
  return std::nullopt;
}

[[nodiscard]] bool would_create_parent_cycle(
    sqlite3* db,
    const std::string& session_id,
    const std::string& message_id,
    const std::string& parent_id
) {
  auto current = std::optional<std::string>{parent_id};
  std::unordered_set<std::string> visited;
  while(current.has_value()) {
    if(*current == message_id) {
      return true;
    }
    if(!visited.insert(*current).second) {
      return true;
    }
    current = message_parent_id(db, session_id, *current);
  }
  return false;
}

void validate_message_owner(sqlite3* db, const std::string& session_id, const std::string& message_id) {
  const auto owner = message_session_id(db, message_id);
  if(owner.has_value() && *owner != session_id) {
    throw std::invalid_argument("message belongs to another session: " + message_id);
  }
}

void validate_session_snapshot(sqlite3* db, const ava::types::SessionRecord& session) {
  std::unordered_set<std::string> incoming_ids;
  std::unordered_map<std::string, const ava::types::SessionMessage*> by_id;
  for(const auto& message : session.messages) {
    if(!incoming_ids.insert(message.id).second) {
      throw std::invalid_argument("duplicate message id in session snapshot: " + message.id);
    }
    by_id.emplace(message.id, &message);
    validate_message_owner(db, session.id, message.id);
  }

  std::size_t root_count = 0;
  for(const auto& message : session.messages) {
    if(!message.parent_id.has_value()) {
      ++root_count;
      continue;
    }
    if(*message.parent_id == message.id) {
      throw std::invalid_argument("message cannot be its own parent: " + message.id);
    }
    if(!incoming_ids.contains(*message.parent_id)) {
      throw std::invalid_argument("message parent is not retained in session snapshot: " + *message.parent_id);
    }
  }

  if(!session.messages.empty() && root_count != 1) {
    throw std::invalid_argument("session snapshot must contain exactly one root message");
  }

  std::unordered_set<std::string> visiting;
  std::unordered_set<std::string> visited;
  auto visit = [&](const auto& self, const std::string& id) -> void {
    if(visited.contains(id)) {
      return;
    }
    if(!visiting.insert(id).second) {
      throw std::invalid_argument("cycle detected in session snapshot: " + id);
    }
    const auto it = by_id.find(id);
    if(it == by_id.end()) {
      throw std::invalid_argument("message not found in session snapshot: " + id);
    }
    if(it->second->parent_id.has_value()) {
      self(self, *it->second->parent_id);
    }
    visiting.erase(id);
    visited.insert(id);
  };
  for(const auto& message : session.messages) {
    visit(visit, message.id);
  }

  if(session.branch_head.has_value() && !incoming_ids.contains(*session.branch_head)) {
    throw std::invalid_argument("branch_head is not retained in session snapshot: " + *session.branch_head);
  }
}

[[nodiscard]] std::vector<const ava::types::SessionMessage*> messages_parent_first(const ava::types::SessionRecord& session) {
  std::vector<const ava::types::SessionMessage*> ordered;
  ordered.reserve(session.messages.size());
  std::unordered_set<std::string> inserted;

  while(ordered.size() < session.messages.size()) {
    bool made_progress = false;
    for(const auto& message : session.messages) {
      if(inserted.contains(message.id)) {
        continue;
      }
      if(!message.parent_id.has_value() || inserted.contains(*message.parent_id)) {
        ordered.push_back(&message);
        inserted.insert(message.id);
        made_progress = true;
      }
    }
    if(!made_progress) {
      throw std::invalid_argument("session messages are not parent-orderable");
    }
  }

  return ordered;
}

void step_done_or_throw(sqlite3_stmt* stmt, sqlite3* db) {
  if(const auto rc = sqlite3_step(stmt); rc != SQLITE_DONE) {
    throw std::runtime_error("SQLite step failed: " + std::string(sqlite3_errmsg(db)));
  }
}

[[nodiscard]] std::string scalar_text(sqlite3* db, const char* sql) {
  auto stmt = prepare_or_throw(db, sql);
  if(sqlite3_step(stmt.get()) != SQLITE_ROW) {
    throw std::runtime_error("SQLite scalar query returned no row: " + std::string(sql));
  }
  return column_text(stmt.get(), 0);
}

[[nodiscard]] int scalar_int(sqlite3* db, const char* sql) {
  auto stmt = prepare_or_throw(db, sql);
  if(sqlite3_step(stmt.get()) != SQLITE_ROW) {
    throw std::runtime_error("SQLite scalar query returned no row: " + std::string(sql));
  }
  return sqlite3_column_int(stmt.get(), 0);
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
      .token_usage = nlohmann::json::object(),
      .messages = {},
      .branch_head = std::nullopt,
  };
}

void SessionManager::save(const ava::types::SessionRecord& session) {
  auto db = open_db(db_path_);
  exec_or_throw(db.get(), "BEGIN IMMEDIATE TRANSACTION;");
  try {
    validate_session_snapshot(db.get(), session);

    auto upsert_session = prepare_or_throw(
        db.get(),
        "INSERT INTO sessions (id, created_at, updated_at, metadata, parent_id, token_usage, branch_head) "
        "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7) "
        "ON CONFLICT(id) DO UPDATE SET updated_at = excluded.updated_at, metadata = excluded.metadata, parent_id = excluded.parent_id, token_usage = excluded.token_usage, branch_head = excluded.branch_head"
    );
    bind_text_or_throw(upsert_session.get(), 1, session.id);
    bind_text_or_throw(upsert_session.get(), 2, session.created_at);
    bind_text_or_throw(upsert_session.get(), 3, session.updated_at);
    bind_text_or_throw(upsert_session.get(), 4, json_object_text(session.metadata));
    bind_optional_text_or_throw(upsert_session.get(), 5, session_parent_id_from_metadata(session.metadata));
    bind_text_or_throw(upsert_session.get(), 6, json_object_text(session.token_usage));
    bind_optional_text_or_throw(upsert_session.get(), 7, session.branch_head);
    step_done_or_throw(upsert_session.get(), db.get());

    auto upsert_message = prepare_or_throw(
        db.get(),
        "INSERT INTO messages (id, session_id, role, content, tool_calls, tool_results, tool_call_id, images, timestamp, parent_id, agent_visible, user_visible, original_content, structured_content, metadata) "
        "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15) "
        "ON CONFLICT(id) DO UPDATE SET session_id = excluded.session_id, role = excluded.role, content = excluded.content, tool_calls = excluded.tool_calls, tool_results = excluded.tool_results, tool_call_id = excluded.tool_call_id, images = excluded.images, timestamp = excluded.timestamp, parent_id = excluded.parent_id, agent_visible = excluded.agent_visible, user_visible = excluded.user_visible, original_content = excluded.original_content, structured_content = excluded.structured_content, metadata = excluded.metadata"
    );

    const auto ordered_messages = messages_parent_first(session);
    std::unordered_set<std::string> retain_ids;
    for(const auto* message_ptr : ordered_messages) {
      const auto& message = *message_ptr;
      retain_ids.insert(message.id);
      sqlite3_reset(upsert_message.get());
      sqlite3_clear_bindings(upsert_message.get());
      bind_text_or_throw(upsert_message.get(), 1, message.id);
      bind_text_or_throw(upsert_message.get(), 2, session.id);
      bind_text_or_throw(upsert_message.get(), 3, message.role);
      bind_text_or_throw(upsert_message.get(), 4, message.content);
      bind_text_or_throw(upsert_message.get(), 5, json_array_text(message.tool_calls));
      bind_text_or_throw(upsert_message.get(), 6, json_array_text(message.tool_results));
      bind_optional_text_or_throw(upsert_message.get(), 7, message.tool_call_id);
      bind_text_or_throw(upsert_message.get(), 8, json_array_text(message.images));
      bind_text_or_throw(upsert_message.get(), 9, message.timestamp);
      bind_optional_text_or_throw(upsert_message.get(), 10, message.parent_id);
      if(sqlite3_bind_int(upsert_message.get(), 11, message.agent_visible ? 1 : 0) != SQLITE_OK) {
        throw std::runtime_error("SQLite bind bool failed");
      }
      if(sqlite3_bind_int(upsert_message.get(), 12, message.user_visible ? 1 : 0) != SQLITE_OK) {
        throw std::runtime_error("SQLite bind bool failed");
      }
      bind_optional_text_or_throw(upsert_message.get(), 13, message.original_content);
      bind_text_or_throw(
          upsert_message.get(),
          14,
          json_array_text(message.structured_content)
      );
      bind_text_or_throw(upsert_message.get(), 15, json_object_text(message.metadata));
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
    rollback_best_effort(db.get());
    throw;
  }
}

std::optional<ava::types::SessionRecord> SessionManager::get(const std::string& id) const {
  auto db = open_db(db_path_);
  return get_with_db(db.get(), id);
}

SqlitePolicySnapshot SessionManager::sqlite_policy_snapshot() const {
  auto db = open_db(db_path_);
  return SqlitePolicySnapshot{
      .journal_mode = scalar_text(db.get(), "PRAGMA journal_mode;"),
      .synchronous = scalar_int(db.get(), "PRAGMA synchronous;"),
      .foreign_keys = scalar_int(db.get(), "PRAGMA foreign_keys;"),
      .busy_timeout = scalar_int(db.get(), "PRAGMA busy_timeout;"),
      .cache_size = scalar_int(db.get(), "PRAGMA cache_size;"),
  };
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
  exec_or_throw(db.get(), "BEGIN IMMEDIATE TRANSACTION;");
  try {
    if(!session_exists(db.get(), session_id)) {
      throw std::invalid_argument("session not found: " + session_id);
    }
    validate_message_owner(db.get(), session_id, message.id);
    if(message.parent_id.has_value() && !message_exists(db.get(), session_id, *message.parent_id)) {
      throw std::invalid_argument("message parent not found in session: " + *message.parent_id);
    }
    if(message.parent_id.has_value() && *message.parent_id == message.id) {
      throw std::invalid_argument("message cannot be its own parent: " + message.id);
    }
    if(message.parent_id.has_value() && would_create_parent_cycle(db.get(), session_id, message.id, *message.parent_id)) {
      throw std::invalid_argument("message parent would create a cycle: " + message.id);
    }
    if(!message.parent_id.has_value()) {
      auto root_stmt = prepare_or_throw(db.get(), "SELECT id FROM messages WHERE session_id = ?1 AND parent_id IS NULL");
      bind_text_or_throw(root_stmt.get(), 1, session_id);
      while(sqlite3_step(root_stmt.get()) == SQLITE_ROW) {
        if(column_text(root_stmt.get(), 0) != message.id) {
          throw std::invalid_argument("session already has a root message; new message must specify a parent");
        }
      }
    }

    auto stmt = prepare_or_throw(
        db.get(),
        "INSERT INTO messages (id, session_id, role, content, tool_calls, tool_results, tool_call_id, images, timestamp, parent_id, agent_visible, user_visible, original_content, structured_content, metadata) "
        "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15) "
        "ON CONFLICT(id) DO UPDATE SET session_id = excluded.session_id, role = excluded.role, content = excluded.content, tool_calls = excluded.tool_calls, tool_results = excluded.tool_results, tool_call_id = excluded.tool_call_id, images = excluded.images, timestamp = excluded.timestamp, parent_id = excluded.parent_id, agent_visible = excluded.agent_visible, user_visible = excluded.user_visible, original_content = excluded.original_content, structured_content = excluded.structured_content, metadata = excluded.metadata"
    );
    bind_text_or_throw(stmt.get(), 1, message.id);
    bind_text_or_throw(stmt.get(), 2, session_id);
    bind_text_or_throw(stmt.get(), 3, message.role);
    bind_text_or_throw(stmt.get(), 4, message.content);
    bind_text_or_throw(stmt.get(), 5, json_array_text(message.tool_calls));
    bind_text_or_throw(stmt.get(), 6, json_array_text(message.tool_results));
    bind_optional_text_or_throw(stmt.get(), 7, message.tool_call_id);
    bind_text_or_throw(stmt.get(), 8, json_array_text(message.images));
    bind_text_or_throw(stmt.get(), 9, message.timestamp);
    bind_optional_text_or_throw(stmt.get(), 10, message.parent_id);
    if(sqlite3_bind_int(stmt.get(), 11, message.agent_visible ? 1 : 0) != SQLITE_OK) {
      throw std::runtime_error("SQLite bind bool failed");
    }
    if(sqlite3_bind_int(stmt.get(), 12, message.user_visible ? 1 : 0) != SQLITE_OK) {
      throw std::runtime_error("SQLite bind bool failed");
    }
    bind_optional_text_or_throw(stmt.get(), 13, message.original_content);
    bind_text_or_throw(stmt.get(), 14, json_array_text(message.structured_content));
    bind_text_or_throw(stmt.get(), 15, json_object_text(message.metadata));
    step_done_or_throw(stmt.get(), db.get());

    auto touch = prepare_or_throw(db.get(), "UPDATE sessions SET branch_head = ?1, updated_at = ?2 WHERE id = ?3");
    bind_text_or_throw(touch.get(), 1, message.id);
    bind_text_or_throw(touch.get(), 2, now_utc_rfc3339());
    bind_text_or_throw(touch.get(), 3, session_id);
    step_done_or_throw(touch.get(), db.get());

    exec_or_throw(db.get(), "COMMIT;");
  } catch(...) {
    rollback_best_effort(db.get());
    throw;
  }
}

std::vector<ava::types::SessionMessage> SessionManager::get_branch(const std::string& session_id, const std::string& leaf_id) const {
  const auto tree = get_tree(session_id);
  std::vector<ava::types::SessionMessage> branch;

  auto current = std::optional<std::string>{leaf_id};
  std::unordered_set<std::string> visited;
  while(current.has_value()) {
    if(!visited.insert(*current).second) {
      throw std::runtime_error("cycle detected in session branch walk: " + *current);
    }
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
      "SELECT id, role, content, tool_calls, tool_results, tool_call_id, images, timestamp, parent_id, agent_visible, user_visible, original_content, structured_content, metadata FROM messages WHERE session_id = ?1"
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
  if(tree.branch_head.has_value() && !tree.nodes.contains(*tree.branch_head)) {
    tree.branch_head = std::nullopt;
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
    std::unordered_set<std::string> visited;
    while(current.has_value()) {
      if(!visited.insert(*current).second) {
        throw std::runtime_error("cycle detected in session branch leaf walk: " + *current);
      }
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
  exec_or_throw(db.get(), "BEGIN IMMEDIATE TRANSACTION;");
  try {
    if(!session_exists(db.get(), session_id)) {
      throw std::invalid_argument("session not found: " + session_id);
    }
    if(!message_exists(db.get(), session_id, branch_point_id)) {
      throw std::invalid_argument("branch point not found in session: " + branch_point_id);
    }

    auto insert = prepare_or_throw(
        db.get(),
        "INSERT INTO messages (id, session_id, role, content, tool_calls, tool_results, tool_call_id, images, timestamp, parent_id, agent_visible, user_visible, original_content, structured_content, metadata) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15)"
    );
    bind_text_or_throw(insert.get(), 1, message.id);
    bind_text_or_throw(insert.get(), 2, session_id);
    bind_text_or_throw(insert.get(), 3, message.role);
    bind_text_or_throw(insert.get(), 4, message.content);
    bind_text_or_throw(insert.get(), 5, json_array_text(message.tool_calls));
    bind_text_or_throw(insert.get(), 6, json_array_text(message.tool_results));
    bind_optional_text_or_throw(insert.get(), 7, message.tool_call_id);
    bind_text_or_throw(insert.get(), 8, json_array_text(message.images));
    bind_text_or_throw(insert.get(), 9, message.timestamp);
    bind_optional_text_or_throw(insert.get(), 10, message.parent_id);
    if(sqlite3_bind_int(insert.get(), 11, message.agent_visible ? 1 : 0) != SQLITE_OK) {
      throw std::runtime_error("SQLite bind bool failed");
    }
    if(sqlite3_bind_int(insert.get(), 12, message.user_visible ? 1 : 0) != SQLITE_OK) {
      throw std::runtime_error("SQLite bind bool failed");
    }
    bind_optional_text_or_throw(insert.get(), 13, message.original_content);
    bind_text_or_throw(insert.get(), 14, json_array_text(message.structured_content));
    bind_text_or_throw(insert.get(), 15, json_object_text(message.metadata));
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
    rollback_best_effort(db.get());
    throw;
  }
}

void SessionManager::switch_branch(const std::string& session_id, const std::string& leaf_id) {
  auto db = open_db(db_path_);
  exec_or_throw(db.get(), "BEGIN IMMEDIATE TRANSACTION;");
  try {
    if(!session_exists(db.get(), session_id)) {
      throw std::invalid_argument("session not found: " + session_id);
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

    exec_or_throw(db.get(), "COMMIT;");
  } catch(...) {
    rollback_best_effort(db.get());
    throw;
  }
}

void SessionManager::init_schema() {
  auto db = open_db(db_path_);
  exec_or_throw(db.get(), kSchemaSql);
  exec_or_ignore_duplicate_column(db.get(), "ALTER TABLE sessions ADD COLUMN parent_id TEXT;");
  exec_or_ignore_duplicate_column(db.get(), "ALTER TABLE sessions ADD COLUMN token_usage TEXT NOT NULL DEFAULT '{}';");
  exec_or_ignore_duplicate_column(db.get(), "ALTER TABLE messages ADD COLUMN tool_calls TEXT NOT NULL DEFAULT '[]';");
  exec_or_ignore_duplicate_column(db.get(), "ALTER TABLE messages ADD COLUMN tool_results TEXT NOT NULL DEFAULT '[]';");
  exec_or_ignore_duplicate_column(db.get(), "ALTER TABLE messages ADD COLUMN tool_call_id TEXT;");
  exec_or_ignore_duplicate_column(db.get(), "ALTER TABLE messages ADD COLUMN images TEXT NOT NULL DEFAULT '[]';");
  exec_or_ignore_duplicate_column(db.get(), "ALTER TABLE messages ADD COLUMN agent_visible INTEGER NOT NULL DEFAULT 1;");
  exec_or_ignore_duplicate_column(db.get(), "ALTER TABLE messages ADD COLUMN user_visible INTEGER NOT NULL DEFAULT 1;");
  exec_or_ignore_duplicate_column(db.get(), "ALTER TABLE messages ADD COLUMN original_content TEXT;");
  exec_or_ignore_duplicate_column(db.get(), "ALTER TABLE messages ADD COLUMN structured_content TEXT NOT NULL DEFAULT '[]';");
  exec_or_ignore_duplicate_column(db.get(), "ALTER TABLE messages ADD COLUMN metadata TEXT NOT NULL DEFAULT '{}';");
}

std::optional<ava::types::SessionRecord> SessionManager::get_with_db(void* db_raw, const std::string& id) const {
  auto* db = static_cast<sqlite3*>(db_raw);

  auto session_stmt = prepare_or_throw(
      db,
      "SELECT id, created_at, updated_at, metadata, parent_id, token_usage, branch_head FROM sessions WHERE id = ?1"
  );
  bind_text_or_throw(session_stmt.get(), 1, id);
  if(sqlite3_step(session_stmt.get()) != SQLITE_ROW) {
    return std::nullopt;
  }

  ava::types::SessionRecord session;
  session.id = column_text(session_stmt.get(), 0);
  session.created_at = column_text(session_stmt.get(), 1);
  session.updated_at = column_text(session_stmt.get(), 2);
  session.metadata = column_json_object(session_stmt.get(), 3);
  if(const auto parent_id = column_optional_text(session_stmt.get(), 4); parent_id.has_value()) {
    session.metadata["parent_id"] = *parent_id;
  }
  session.token_usage = column_json_object(session_stmt.get(), 5);
  session.branch_head = column_optional_text(session_stmt.get(), 6);

  auto messages_stmt = prepare_or_throw(
      db,
      "SELECT id, role, content, tool_calls, tool_results, tool_call_id, images, timestamp, parent_id, agent_visible, user_visible, original_content, structured_content, metadata FROM messages WHERE session_id = ?1 ORDER BY timestamp ASC, id ASC"
  );
  bind_text_or_throw(messages_stmt.get(), 1, id);
  while(sqlite3_step(messages_stmt.get()) == SQLITE_ROW) {
    session.messages.push_back(row_to_message(messages_stmt.get()));
  }

  if(session.branch_head.has_value()) {
    const auto branch_head = *session.branch_head;
    const auto has_branch_head = std::any_of(session.messages.begin(), session.messages.end(), [&](const auto& message) {
      return message.id == branch_head;
    });
    if(!has_branch_head) {
      session.branch_head = std::nullopt;
    }
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
