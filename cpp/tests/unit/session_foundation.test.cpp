#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include "ava/session/session.hpp"

namespace {

std::filesystem::path temp_root_for_test() {
  const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  return std::filesystem::temp_directory_path() / ("ava_cpp_session_test_" + unique);
}

}  // namespace

TEST_CASE("session manager persists and loads sessions/messages", "[ava_session]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  ava::session::SessionManager manager(root / "sessions.db");

  auto session = manager.create();
  session.metadata["title"] = "First session";
  session.metadata["parent_id"] = "root-session";
  session.token_usage = nlohmann::json{{"input_tokens", 11}, {"output_tokens", 7}, {"total_tokens", 18}};

  const auto user = ava::types::SessionMessage{
      .id = "m1",
      .role = "user",
      .content = "hello",
      .tool_calls = nlohmann::json::array({nlohmann::json{{"id", "call_1"}, {"name", "read"}, {"arguments", nlohmann::json{{"path", "README.md"}}}}}),
      .tool_results = nlohmann::json::array({nlohmann::json{{"call_id", "call_1"}, {"content", "ok"}, {"is_error", false}}}),
      .tool_call_id = std::optional<std::string>{"call_1"},
      .images = nlohmann::json::array({nlohmann::json{{"data", "ZmFrZS1pbWFnZS1ieXRlcw=="}, {"media_type", "image/png"}}}),
      .timestamp = "2026-01-01T00:00:00Z",
      .parent_id = std::nullopt,
      .agent_visible = false,
      .user_visible = true,
      .original_content = std::optional<std::string>{"original hello"},
      .structured_content = nlohmann::json::array({nlohmann::json{{"type", "text"}, {"text", "hello"}}}),
      .metadata = nlohmann::json{{"segment", "alpha"}},
  };
  const auto assistant = ava::types::SessionMessage{
      .id = "m2",
      .role = "assistant",
      .content = "hi",
      .tool_calls = nlohmann::json::array({nlohmann::json{{"id", "call-1"}, {"name", "read"}, {"arguments", {}}}}),
      .tool_results = nlohmann::json::array(),
      .tool_call_id = std::nullopt,
      .images = nlohmann::json::array(),
      .timestamp = "2026-01-01T00:00:01Z",
      .parent_id = std::optional<std::string>{"m1"},
      .agent_visible = true,
      .user_visible = true,
      .original_content = std::nullopt,
      .structured_content = nlohmann::json::array(),
      .metadata = nlohmann::json::object(),
  };
  session.messages = {user, assistant};
  session.branch_head = "m2";

  manager.save(session);

  const auto loaded = manager.get(session.id);
  REQUIRE(loaded.has_value());
  REQUIRE(loaded->messages.size() == 2);
  REQUIRE(loaded->metadata["parent_id"] == "root-session");
  REQUIRE(loaded->token_usage == nlohmann::json{{"input_tokens", 11}, {"output_tokens", 7}, {"total_tokens", 18}});
  REQUIRE(loaded->messages.at(1).tool_calls == assistant.tool_calls);
  REQUIRE(loaded->messages.at(1).tool_results == assistant.tool_results);
  REQUIRE(loaded->messages.at(0).tool_call_id == std::optional<std::string>{"call_1"});
  REQUIRE(loaded->messages.at(0).images.is_array());
  REQUIRE_FALSE(loaded->messages.at(0).agent_visible);
  REQUIRE(loaded->messages.at(0).user_visible);
  REQUIRE(loaded->messages.at(0).original_content == std::optional<std::string>{"original hello"});
  REQUIRE(loaded->messages.at(0).structured_content.is_array());
  REQUIRE(loaded->messages.at(0).metadata == nlohmann::json{{"segment", "alpha"}});
  REQUIRE(loaded->branch_head == std::optional<std::string>{"m2"});

  const auto branch = manager.get_branch(session.id, "m2");
  REQUIRE(branch.size() == 2);
  REQUIRE(branch[0].id == "m1");
  REQUIRE(branch[1].id == "m2");

  std::filesystem::remove_all(root);
}

TEST_CASE("session manager migrates legacy sqlite schema idempotently", "[ava_session]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);
  const auto db_path = root / "sessions.db";

  sqlite3* raw = nullptr;
  REQUIRE(sqlite3_open(db_path.string().c_str(), &raw) == SQLITE_OK);
  REQUIRE(raw != nullptr);

  auto exec = [&](const char* sql) {
    char* err = nullptr;
    const int rc = sqlite3_exec(raw, sql, nullptr, nullptr, &err);
    const std::string err_text = err == nullptr ? std::string{} : std::string(err);
    INFO(err_text);
    if(err != nullptr) {
      sqlite3_free(err);
    }
    REQUIRE(rc == SQLITE_OK);
  };

  exec("CREATE TABLE sessions (id TEXT PRIMARY KEY, created_at TEXT NOT NULL, updated_at TEXT NOT NULL, metadata TEXT NOT NULL, branch_head TEXT);");
  exec("CREATE TABLE messages (id TEXT PRIMARY KEY, session_id TEXT NOT NULL, role TEXT NOT NULL, content TEXT NOT NULL, timestamp TEXT NOT NULL, parent_id TEXT);");
  exec("INSERT INTO sessions (id, created_at, updated_at, metadata, branch_head) VALUES ('legacy-session', '2026-01-01T00:00:00Z', '2026-01-01T00:00:00Z', '{\"title\":\"legacy\"}', 'legacy-message');");
  exec("INSERT INTO messages (id, session_id, role, content, timestamp, parent_id) VALUES ('legacy-message', 'legacy-session', 'user', 'from old schema', '2026-01-01T00:00:00Z', NULL);");
  REQUIRE(sqlite3_close(raw) == SQLITE_OK);

  ava::session::SessionManager manager(db_path);
  const auto migrated = manager.get("legacy-session");
  REQUIRE(migrated.has_value());
  REQUIRE(migrated->metadata == nlohmann::json{{"title", "legacy"}});
  REQUIRE(migrated->token_usage == nlohmann::json::object());
  REQUIRE(migrated->messages.size() == 1);
  REQUIRE(migrated->messages.front().tool_calls == nlohmann::json::array());
  REQUIRE(migrated->messages.front().tool_results == nlohmann::json::array());
  REQUIRE(migrated->messages.front().images == nlohmann::json::array());
  REQUIRE(migrated->messages.front().agent_visible);
  REQUIRE(migrated->messages.front().user_visible);
  REQUIRE(migrated->messages.front().metadata == nlohmann::json::object());

  ava::session::SessionManager manager_again(db_path);

  auto session = manager.create();
  session.metadata["parent_id"] = "parent-session";
  session.token_usage = nlohmann::json{{"total_tokens", 3}};
  session.messages = {
      ava::types::SessionMessage{
          .id = "m1",
          .role = "user",
          .content = "legacy-upgrade",
          .tool_calls = nlohmann::json::array(),
          .tool_results = nlohmann::json::array(),
          .tool_call_id = std::nullopt,
          .images = nlohmann::json::array(),
          .timestamp = "2026-01-01T00:00:00Z",
          .parent_id = std::nullopt,
      },
  };
  manager_again.save(session);

  const auto loaded = manager_again.get(session.id);
  REQUIRE(loaded.has_value());
  REQUIRE(loaded->metadata["parent_id"] == "parent-session");
  REQUIRE(loaded->token_usage == nlohmann::json{{"total_tokens", 3}});

  std::filesystem::remove_all(root);
}

TEST_CASE("session manager configures sqlite wal policy", "[ava_session]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);
  const auto db_path = root / "sessions.db";

  ava::session::SessionManager manager(db_path);

  const auto policy = manager.sqlite_policy_snapshot();
  REQUIRE(policy.journal_mode == "wal");
  REQUIRE(policy.synchronous == 1);
  REQUIRE(policy.foreign_keys == 1);
  REQUIRE(policy.busy_timeout == 5000);
  REQUIRE(policy.cache_size == -64000);

  sqlite3* raw = nullptr;
  REQUIRE(sqlite3_open(db_path.string().c_str(), &raw) == SQLITE_OK);
  REQUIRE(raw != nullptr);

  auto scalar_text_pragma = [&](const char* sql) {
    sqlite3_stmt* stmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(raw, sql, -1, &stmt, nullptr) == SQLITE_OK);
    REQUIRE(stmt != nullptr);
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
    const unsigned char* text = sqlite3_column_text(stmt, 0);
    const std::string value = text == nullptr ? std::string{} : std::string(reinterpret_cast<const char*>(text));
    REQUIRE(sqlite3_finalize(stmt) == SQLITE_OK);
    return value;
  };

  REQUIRE(scalar_text_pragma("PRAGMA journal_mode;") == "wal");

  REQUIRE(sqlite3_close(raw) == SQLITE_OK);
  std::filesystem::remove_all(root);
}

TEST_CASE("session manager add_message persists child message and touches session", "[ava_session]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  ava::session::SessionManager manager(root / "sessions.db");
  auto session = manager.create();
  session.messages = {
      ava::types::SessionMessage{
          .id = "m1",
          .role = "user",
          .content = "start",
          .timestamp = "2026-01-01T00:00:00Z",
          .parent_id = std::nullopt,
      },
  };
  session.branch_head = "m1";
  manager.save(session);

  const auto original_updated_at = manager.get(session.id)->updated_at;
  const auto child = ava::types::SessionMessage{
      .id = "m2",
      .role = "assistant",
      .content = "child",
      .tool_calls = nlohmann::json::array({nlohmann::json{{"id", "call-1"}, {"name", "read"}, {"arguments", nlohmann::json::object()}}}),
      .timestamp = "2026-01-01T00:00:01Z",
      .parent_id = std::optional<std::string>{"m1"},
      .metadata = nlohmann::json{{"source", "add_message"}},
  };
  manager.add_message(session.id, child);

  const auto loaded = manager.get(session.id);
  REQUIRE(loaded.has_value());
  REQUIRE(loaded->messages.size() == 2);
  REQUIRE(loaded->updated_at >= original_updated_at);
  REQUIRE(loaded->messages.at(1).id == "m2");
  REQUIRE(loaded->messages.at(1).parent_id == std::optional<std::string>{"m1"});
  REQUIRE(loaded->messages.at(1).tool_calls == child.tool_calls);
  REQUIRE(loaded->messages.at(1).metadata == nlohmann::json{{"source", "add_message"}});
  REQUIRE(loaded->branch_head == std::optional<std::string>{"m2"});

  const auto tree = manager.get_tree(session.id);
  REQUIRE(tree.nodes.at("m1").children == std::vector<std::string>{"m2"});

  std::filesystem::remove_all(root);
}

TEST_CASE("session manager rejects add_message second root", "[ava_session]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  ava::session::SessionManager manager(root / "sessions.db");
  auto session = manager.create();
  session.messages = {
      ava::types::SessionMessage{
          .id = "root",
          .role = "user",
          .content = "root",
          .timestamp = "2026-01-01T00:00:00Z",
          .parent_id = std::nullopt,
      },
  };
  session.branch_head = "root";
  manager.save(session);

  const auto second_root = ava::types::SessionMessage{
      .id = "other-root",
      .role = "assistant",
      .content = "second root",
      .timestamp = "2026-01-01T00:00:01Z",
      .parent_id = std::nullopt,
  };
  REQUIRE_THROWS(manager.add_message(session.id, second_root));

  const auto tree = manager.get_tree(session.id);
  REQUIRE(tree.root == std::optional<std::string>{"root"});
  REQUIRE(tree.nodes.size() == 1);
  REQUIRE(tree.branch_head == std::optional<std::string>{"root"});

  std::filesystem::remove_all(root);
}

TEST_CASE("session manager rejects reparenting child into second root", "[ava_session]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  ava::session::SessionManager manager(root / "sessions.db");
  auto session = manager.create();
  session.messages = {
      ava::types::SessionMessage{
          .id = "root",
          .role = "user",
          .content = "root",
          .timestamp = "2026-01-01T00:00:00Z",
          .parent_id = std::nullopt,
      },
      ava::types::SessionMessage{
          .id = "child",
          .role = "assistant",
          .content = "child",
          .timestamp = "2026-01-01T00:00:01Z",
          .parent_id = std::optional<std::string>{"root"},
      },
  };
  session.branch_head = "child";
  manager.save(session);

  auto child_as_root = session.messages.at(1);
  child_as_root.parent_id = std::nullopt;
  REQUIRE_THROWS(manager.add_message(session.id, child_as_root));

  const auto tree = manager.get_tree(session.id);
  REQUIRE(tree.root == std::optional<std::string>{"root"});
  REQUIRE(tree.nodes.at("root").children == std::vector<std::string>{"child"});
  REQUIRE(tree.branch_head == std::optional<std::string>{"child"});

  std::filesystem::remove_all(root);
}

TEST_CASE("session manager list_recent orders and hydrates sessions", "[ava_session]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  ava::session::SessionManager manager(root / "sessions.db");
  auto first = manager.create();
  first.id = "first";
  first.created_at = "2026-01-01T00:00:00Z";
  first.updated_at = "2026-01-01T00:00:01Z";
  first.metadata = nlohmann::json{{"name", "first"}};
  first.messages = {ava::types::SessionMessage{
      .id = "first-root",
      .role = "user",
      .content = "first",
      .timestamp = "2026-01-01T00:00:00Z",
      .parent_id = std::nullopt,
  }};
  first.branch_head = "first-root";
  manager.save(first);

  auto second = manager.create();
  second.id = "second";
  second.created_at = "2026-01-01T00:00:00Z";
  second.updated_at = "2026-01-01T00:00:02Z";
  second.metadata = nlohmann::json{{"name", "second"}};
  second.messages = {ava::types::SessionMessage{
      .id = "second-root",
      .role = "user",
      .content = "second",
      .timestamp = "2026-01-01T00:00:00Z",
      .parent_id = std::nullopt,
  }};
  second.branch_head = "second-root";
  manager.save(second);

  auto third = manager.create();
  third.id = "third";
  third.created_at = "2026-01-01T00:00:00Z";
  third.updated_at = "2026-01-01T00:00:03Z";
  third.metadata = nlohmann::json{{"name", "third"}};
  third.messages = {ava::types::SessionMessage{
      .id = "third-root",
      .role = "user",
      .content = "third",
      .timestamp = "2026-01-01T00:00:00Z",
      .parent_id = std::nullopt,
  }};
  third.branch_head = "third-root";
  manager.save(third);

  const auto recent = manager.list_recent(2);
  REQUIRE(recent.size() == 2);
  REQUIRE(recent.at(0).id == "third");
  REQUIRE(recent.at(0).metadata == nlohmann::json{{"name", "third"}});
  REQUIRE(recent.at(0).branch_head == std::optional<std::string>{"third-root"});
  REQUIRE(recent.at(0).messages.size() == 1);
  REQUIRE(recent.at(1).id == "second");
  REQUIRE(recent.at(1).metadata == nlohmann::json{{"name", "second"}});

  std::filesystem::remove_all(root);
}

TEST_CASE("session tree branching updates active branch", "[ava_session]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  ava::session::SessionManager manager(root / "sessions.db");

  auto session = manager.create();
  session.messages = {
      ava::types::SessionMessage{
          .id = "m1",
          .role = "user",
          .content = "start",
          .timestamp = "2026-01-01T00:00:00Z",
          .parent_id = std::nullopt,
      },
      ava::types::SessionMessage{
          .id = "m2",
          .role = "assistant",
          .content = "ack",
          .timestamp = "2026-01-01T00:00:01Z",
          .parent_id = std::optional<std::string>{"m1"},
      },
  };
  session.branch_head = "m2";
  manager.save(session);

  const auto fork = manager.branch_from(session.id, "m1", "alternate question");
  REQUIRE(fork.parent_id == std::optional<std::string>{"m1"});

  auto tree = manager.get_tree(session.id);
  REQUIRE(tree.nodes.size() == 3);
  REQUIRE(tree.branch_head == std::optional<std::string>{fork.id});

  manager.switch_branch(session.id, "m2");
  tree = manager.get_tree(session.id);
  REQUIRE(tree.branch_head == std::optional<std::string>{"m2"});

  const auto leaves = manager.get_branch_leaves(session.id);
  REQUIRE(leaves.size() == 2);

  std::filesystem::remove_all(root);
}

TEST_CASE("session manager rejects invalid session and branch references", "[ava_session]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  ava::session::SessionManager manager(root / "sessions.db");
  auto session = manager.create();
  session.messages = {
      ava::types::SessionMessage{
          .id = "m1",
          .role = "user",
          .content = "start",
          .timestamp = "2026-01-01T00:00:00Z",
          .parent_id = std::nullopt,
      },
  };
  session.branch_head = "m1";
  manager.save(session);

  REQUIRE_THROWS(manager.add_message(
      "missing-session",
      ava::types::SessionMessage{
          .id = "m2",
          .role = "assistant",
          .content = "hi",
          .timestamp = "2026-01-01T00:00:01Z",
          .parent_id = std::nullopt,
      }
  ));
  REQUIRE_THROWS(manager.branch_from(session.id, "missing-message", "alt"));
  REQUIRE_THROWS(manager.switch_branch(session.id, "missing-message"));

  std::filesystem::remove_all(root);
}

TEST_CASE("session manager rejects cross-session parent references", "[ava_session]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  ava::session::SessionManager manager(root / "sessions.db");

  auto first = manager.create();
  first.messages = {
      ava::types::SessionMessage{
          .id = "first-root",
          .role = "user",
          .content = "first",
          .timestamp = "2026-01-01T00:00:00Z",
          .parent_id = std::nullopt,
      },
  };
  first.branch_head = "first-root";
  manager.save(first);

  auto second = manager.create();
  second.messages = {
      ava::types::SessionMessage{
          .id = "second-root",
          .role = "user",
          .content = "second",
          .timestamp = "2026-01-01T00:00:00Z",
          .parent_id = std::nullopt,
      },
  };
  second.branch_head = "second-root";
  manager.save(second);

  const auto cross_session_child = ava::types::SessionMessage{
      .id = "cross-child",
      .role = "assistant",
      .content = "bad parent",
      .timestamp = "2026-01-01T00:00:01Z",
      .parent_id = std::optional<std::string>{"first-root"},
  };

  REQUIRE_THROWS(manager.add_message(second.id, cross_session_child));

  second.messages.push_back(cross_session_child);
  second.branch_head = "cross-child";
  REQUIRE_THROWS(manager.save(second));

  std::filesystem::remove_all(root);
}

TEST_CASE("session manager rejects snapshots that retain children without parents", "[ava_session]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  ava::session::SessionManager manager(root / "sessions.db");
  auto session = manager.create();
  session.messages = {
      ava::types::SessionMessage{
          .id = "m1",
          .role = "user",
          .content = "root",
          .timestamp = "2026-01-01T00:00:00Z",
          .parent_id = std::nullopt,
      },
      ava::types::SessionMessage{
          .id = "m2",
          .role = "assistant",
          .content = "child",
          .timestamp = "2026-01-01T00:00:01Z",
          .parent_id = std::optional<std::string>{"m1"},
      },
  };
  session.branch_head = "m2";
  manager.save(session);

  session.messages = {session.messages.at(1)};
  REQUIRE_THROWS(manager.save(session));

  const auto loaded = manager.get(session.id);
  REQUIRE(loaded.has_value());
  REQUIRE(loaded->messages.size() == 2);
  REQUIRE(loaded->messages.at(1).parent_id == std::optional<std::string>{"m1"});

  std::filesystem::remove_all(root);
}

TEST_CASE("session manager rejects invalid branch heads and self parents", "[ava_session]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  ava::session::SessionManager manager(root / "sessions.db");
  auto session = manager.create();
  session.messages = {
      ava::types::SessionMessage{
          .id = "m1",
          .role = "user",
          .content = "root",
          .timestamp = "2026-01-01T00:00:00Z",
          .parent_id = std::nullopt,
      },
  };
  session.branch_head = "missing";
  REQUIRE_THROWS(manager.save(session));

  session.branch_head = "m1";
  session.messages.front().parent_id = "m1";
  REQUIRE_THROWS(manager.save(session));

  std::filesystem::remove_all(root);
}

TEST_CASE("session manager rejects cyclic and rootless snapshots", "[ava_session]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  ava::session::SessionManager manager(root / "sessions.db");

  auto cycle = manager.create();
  cycle.messages = {
      ava::types::SessionMessage{
          .id = "a",
          .role = "user",
          .content = "a",
          .timestamp = "2026-01-01T00:00:00Z",
          .parent_id = std::optional<std::string>{"b"},
      },
      ava::types::SessionMessage{
          .id = "b",
          .role = "assistant",
          .content = "b",
          .timestamp = "2026-01-01T00:00:01Z",
          .parent_id = std::optional<std::string>{"a"},
      },
  };
  cycle.branch_head = "b";
  REQUIRE_THROWS(manager.save(cycle));

  auto rootless = manager.create();
  rootless.messages = {
      ava::types::SessionMessage{
          .id = "child",
          .role = "assistant",
          .content = "child",
          .timestamp = "2026-01-01T00:00:00Z",
          .parent_id = std::optional<std::string>{"missing"},
      },
  };
  rootless.branch_head = "child";
  REQUIRE_THROWS(manager.save(rootless));

  std::filesystem::remove_all(root);
}

TEST_CASE("session manager saves snapshots parent first regardless of input order", "[ava_session]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  ava::session::SessionManager manager(root / "sessions.db");
  auto session = manager.create();
  session.messages = {
      ava::types::SessionMessage{
          .id = "child",
          .role = "assistant",
          .content = "child",
          .timestamp = "2026-01-01T00:00:01Z",
          .parent_id = std::optional<std::string>{"root"},
      },
      ava::types::SessionMessage{
          .id = "root",
          .role = "user",
          .content = "root",
          .timestamp = "2026-01-01T00:00:00Z",
          .parent_id = std::nullopt,
      },
  };
  session.branch_head = "child";

  manager.save(session);

  const auto tree = manager.get_tree(session.id);
  REQUIRE(tree.root == std::optional<std::string>{"root"});
  REQUIRE(tree.branch_head == std::optional<std::string>{"child"});
  REQUIRE(tree.nodes.at("root").children == std::vector<std::string>{"child"});

  std::filesystem::remove_all(root);
}

TEST_CASE("session manager rejects incremental reparenting cycles", "[ava_session]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  ava::session::SessionManager manager(root / "sessions.db");
  auto session = manager.create();
  session.messages = {
      ava::types::SessionMessage{
          .id = "root",
          .role = "user",
          .content = "root",
          .timestamp = "2026-01-01T00:00:00Z",
          .parent_id = std::nullopt,
      },
      ava::types::SessionMessage{
          .id = "child",
          .role = "assistant",
          .content = "child",
          .timestamp = "2026-01-01T00:00:01Z",
          .parent_id = std::optional<std::string>{"root"},
      },
  };
  session.branch_head = "child";
  manager.save(session);

  auto reparent_root = session.messages.front();
  reparent_root.parent_id = "child";
  REQUIRE_THROWS(manager.add_message(session.id, reparent_root));

  const auto tree = manager.get_tree(session.id);
  REQUIRE(tree.root == std::optional<std::string>{"root"});
  REQUIRE(tree.nodes.at("root").children == std::vector<std::string>{"child"});

  std::filesystem::remove_all(root);
}
