#include <filesystem>
#include <mutex>
#include <string>
#include <system_error>

#include "ava/agent/agent_loop_cancellation.h"
#include "ava/core/error.h"
#include "ava/session/session_store.h"
#include "tests/support/test_harness.h"

namespace {

ava::core::Result<ava::session::SessionStore> make_store(std::string const& name)
{
  auto const root = temp_root() / name;
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  return ava::session::SessionStore::create(workspace, root / "sessions");
}

bool has_boundary_context(ava::core::Error const& error, std::string const& boundary)
{
  for (auto const& context : error.context()) {
    if (context.key == "boundary" && context.value == boundary) return true;
  }
  return false;
}

void test_cancel_requested_helper()
{
  expect(!ava::agent::agent_loop_canceled(nullptr), "missing cancel callback is not canceled");

  bool called = false;
  auto canceled = ava::agent::agent_loop_canceled([&called]() {
    called = true;
    return true;
  });
  expect(canceled && called, "cancel callback result is honored");
}

void test_canceled_error_carries_boundary()
{
  auto const error = ava::agent::agent_loop_canceled_error("before_provider_call");
  expect(error.category() == ava::core::ErrorCategory::Unknown, "cancel error uses unknown category");
  expect(error.message() == "agent loop canceled", "cancel error carries stable message");
  expect(has_boundary_context(error, "before_provider_call"), "cancel error includes boundary context");
}

void test_no_cancel_does_not_append_session_entry()
{
  auto store_result = make_store("agent-loop-cancellation-no-cancel");
  expect(store_result.has_value(), "test session store is created for no-cancel path");
  if (!store_result) return;
  auto store = std::move(*store_result);

  auto checked = ava::agent::check_agent_loop_canceled([]() { return false; }, store, nullptr, "before_turn_start");
  expect(checked.has_value(), "non-canceled check succeeds");

  auto entries = store.load();
  expect(entries.has_value(), "session entries load after no-cancel check");
  expect(entries && entries->empty(), "non-canceled check does not append session entries");
}

void test_cancel_appends_session_entry()
{
  auto store_result = make_store("agent-loop-cancellation-cancel");
  expect(store_result.has_value(), "test session store is created for cancel path");
  if (!store_result) return;
  auto store = std::move(*store_result);

  auto checked =
      ava::agent::check_agent_loop_canceled([]() { return true; }, store, nullptr, "during_provider_request");
  expect(!checked.has_value(), "canceled check returns an error");
  expect(!checked && has_boundary_context(checked.error(), "during_provider_request"),
         "canceled check returns boundary context");

  auto entries = store.load();
  expect(entries.has_value(), "session entries load after canceled check");
  expect(entries && entries->size() == 1, "canceled check appends one session entry");
  if (entries && entries->size() == 1) {
    auto const& entry = entries->front();
    expect(entry.type == ava::session::EntryType::Cancel, "canceled check appends a cancel entry");
    expect(entry.data_json.contains("\"reason\":\"cancel_requested\""), "cancel entry records cancel reason");
    expect(entry.data_json.contains("\"boundary\":\"during_provider_request\""), "cancel entry records boundary");
  }
}

void test_cancel_with_mutex_appends_session_entry()
{
  auto store_result = make_store("agent-loop-cancellation-mutex");
  expect(store_result.has_value(), "test session store is created for mutex cancel path");
  if (!store_result) return;
  auto store = std::move(*store_result);
  std::mutex session_mutex;

  auto checked =
      ava::agent::check_agent_loop_canceled([]() { return true; }, store, &session_mutex, "during_provider_stream");
  expect(!checked.has_value(), "mutex-protected canceled check returns an error");

  auto entries = store.load();
  expect(entries && entries->size() == 1 && entries->front().type == ava::session::EntryType::Cancel,
         "mutex-protected canceled check appends a cancel entry");
  expect(entries && entries->front().data_json.contains("\"boundary\":\"during_provider_stream\""),
         "mutex-protected cancel entry records boundary");
}

}  // namespace

void run_agent_loop_cancellation_tests()
{
  test_cancel_requested_helper();
  test_canceled_error_carries_boundary();
  test_no_cancel_does_not_append_session_entry();
  test_cancel_appends_session_entry();
  test_cancel_with_mutex_appends_session_entry();
}
