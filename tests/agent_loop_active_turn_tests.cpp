#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#include "ava/agent/agent_loop_active_turn.h"
#include "ava/session/session_store.h"
#include "tests/support/test_harness.h"

namespace {

ava::session::SessionStore make_store(std::string const& name)
{
  auto const root = temp_root() / name;
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  return ava::session::SessionStore(
      ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = name});
}

std::size_t count_user_messages(std::vector<ava::session::SessionEntry> const& entries)
{
  std::size_t count = 0;
  for (auto const& entry : entries) {
    if (entry.type == ava::session::EntryType::UserMessage) ++count;
  }
  return count;
}

std::size_t count_replayed_user_messages(std::vector<ava::session::SessionEntry> const& entries)
{
  std::size_t count = 0;
  for (auto const& entry : entries) {
    if (ava::session::is_internal_replay_user_message(entry)) ++count;
  }
  return count;
}

void test_active_turn_tracks_user_messages_and_replay_texts()
{
  auto store = make_store("agent-active-turn-track");
  ava::agent::ActiveTurnMessages active_turn;

  auto first = active_turn.append_user_message(store, nullptr, "initial prompt");
  auto second = active_turn.append_user_message(store, nullptr, "mid-turn steering");
  auto texts = active_turn.replayable_texts();

  expect(first.has_value() && second.has_value(), "active turn appends user messages");
  expect(active_turn.messages().size() == 2 && !active_turn.messages()[0].id.empty() &&
             active_turn.messages()[0].text == "initial prompt" &&
             active_turn.messages()[1].text == "mid-turn steering",
         "active turn records original entry ids and text");
  expect(texts == std::vector<std::string>({"initial prompt", "mid-turn steering"}),
         "active turn exposes replayable text in original order");
}

void test_active_turn_replays_messages_with_original_ids()
{
  auto store = make_store("agent-active-turn-replay");
  ava::agent::ActiveTurnMessages active_turn;

  auto first = active_turn.append_user_message(store, nullptr, "alpha");
  auto second = active_turn.append_user_message(store, nullptr, "beta");
  auto replayed = active_turn.replay_user_messages(store, nullptr);
  auto entries = store.load();

  expect(first.has_value() && second.has_value() && replayed.has_value(), "active turn replays appended messages");
  expect(entries && count_user_messages(*entries) == 4 && count_replayed_user_messages(*entries) == 2,
         "active turn replay writes internal replay user entries");
  if (entries && active_turn.messages().size() == 2) {
    auto const& replay_alpha = (*entries)[2].data_json;
    auto const& replay_beta = (*entries)[3].data_json;
    expect(replay_alpha.find("\"text\":\"alpha\"") != std::string::npos &&
               replay_alpha.find("\"replay_of\":\"" + active_turn.messages()[0].id + "\"") != std::string::npos &&
               replay_beta.find("\"text\":\"beta\"") != std::string::npos &&
               replay_beta.find("\"replay_of\":\"" + active_turn.messages()[1].id + "\"") != std::string::npos,
           "active turn replay preserves original text and replay correlation ids");
  }
}

void test_active_turn_uses_session_mutex_for_append_and_replay()
{
  auto store = make_store("agent-active-turn-mutex");
  ava::agent::ActiveTurnMessages active_turn;
  std::mutex session_mutex;

  auto appended = active_turn.append_user_message(store, &session_mutex, "locked prompt");
  auto replayed = active_turn.replay_user_messages(store, &session_mutex);
  auto entries = store.load();

  expect(appended.has_value() && replayed.has_value(), "active turn append and replay work with session mutex");
  expect(entries && count_user_messages(*entries) == 2 && count_replayed_user_messages(*entries) == 1,
         "active turn mutex path records original and replay entries");
}

}  // namespace

void run_agent_loop_active_turn_tests()
{
  test_active_turn_tracks_user_messages_and_replay_texts();
  test_active_turn_replays_messages_with_original_ids();
  test_active_turn_uses_session_mutex_for_append_and_replay();
}
