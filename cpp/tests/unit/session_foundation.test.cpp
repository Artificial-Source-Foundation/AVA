#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>

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

  const auto user = ava::types::SessionMessage{
      .id = "m1",
      .role = "user",
      .content = "hello",
      .timestamp = "2026-01-01T00:00:00Z",
      .parent_id = std::nullopt,
  };
  const auto assistant = ava::types::SessionMessage{
      .id = "m2",
      .role = "assistant",
      .content = "hi",
      .timestamp = "2026-01-01T00:00:01Z",
      .parent_id = std::optional<std::string>{"m1"},
  };
  session.messages = {user, assistant};
  session.branch_head = "m2";

  manager.save(session);

  const auto loaded = manager.get(session.id);
  REQUIRE(loaded.has_value());
  REQUIRE(loaded->messages.size() == 2);
  REQUIRE(loaded->branch_head == std::optional<std::string>{"m2"});

  const auto branch = manager.get_branch(session.id, "m2");
  REQUIRE(branch.size() == 2);
  REQUIRE(branch[0].id == "m1");
  REQUIRE(branch[1].id == "m2");

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
