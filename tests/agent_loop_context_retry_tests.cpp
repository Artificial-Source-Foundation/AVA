#include <string>
#include <string_view>
#include <vector>

#include "ava/agent/agent_loop_context_retry.h"
#include "ava/core/error.h"
#include "ava/provider/provider.h"
#include "tests/support/test_harness.h"

namespace {

ava::core::Error context_overflow_error()
{
  ava::core::Error error(ava::core::ErrorCategory::Provider, "Maximum context length exceeded");
  error.with_context("provider_error_kind",
                     ava::provider::to_string(ava::provider::ProviderErrorKind::ContextOverflow));
  return error;
}

ava::core::Error ordinary_provider_error()
{
  return ava::core::Error(ava::core::ErrorCategory::Provider, "authentication failed");
}

void test_context_retry_ignores_non_overflow_and_unavailable_compaction()
{
  ava::agent::ContextOverflowRetryState state;
  bool compact_called = false;
  auto ignored = ava::agent::prepare_context_overflow_retry_attempt(
      ordinary_provider_error(), true, state,
      ava::agent::ContextOverflowRetryCallbacks{.compact_context = [&](std::string_view) -> ava::core::Result<bool> {
        compact_called = true;
        return true;
      }});
  expect(ignored && !*ignored && !state.retry_used && !compact_called,
         "context retry helper ignores non-overflow provider errors");

  auto unavailable = ava::agent::prepare_context_overflow_retry_attempt(context_overflow_error(), false, state, {});
  expect(unavailable && !*unavailable && !state.retry_used,
         "context retry helper ignores context overflow when compaction is unavailable");
}

void test_context_retry_success_replays_active_turn_once()
{
  ava::agent::ContextOverflowRetryState state;
  std::vector<std::string> boundaries;
  std::vector<std::string> triggers;
  int replay_count = 0;

  auto retried = ava::agent::prepare_context_overflow_retry_attempt(
      context_overflow_error(), true, state,
      ava::agent::ContextOverflowRetryCallbacks{
          .check_canceled = [&](std::string_view boundary) -> ava::core::VoidResult {
            boundaries.push_back(std::string(boundary));
            return {};
          },
          .compact_context = [&](std::string_view trigger) -> ava::core::Result<bool> {
            triggers.push_back(std::string(trigger));
            return true;
          },
          .replay_active_turn_user_messages = [&]() -> ava::core::VoidResult {
            ++replay_count;
            return {};
          }});
  expect(retried && *retried && state.retry_used && state.skip_next_auto_compaction,
         "context retry helper records successful retry state");
  expect(boundaries == std::vector<std::string>(
                           {"before_context_overflow_compaction", "after_context_overflow_compaction"}) &&
             triggers == std::vector<std::string>({"context_overflow"}) && replay_count == 1,
         "context retry helper runs cancellation, compaction, and replay callbacks in order");

  int second_compact_calls = 0;
  auto second = ava::agent::prepare_context_overflow_retry_attempt(
      context_overflow_error(), true, state,
      ava::agent::ContextOverflowRetryCallbacks{.compact_context = [&](std::string_view) -> ava::core::Result<bool> {
        ++second_compact_calls;
        return true;
      }});
  expect(second && !*second && second_compact_calls == 0,
         "context retry helper bounds context-overflow retries to one attempt");
}

void test_context_retry_without_new_compaction_does_not_replay()
{
  ava::agent::ContextOverflowRetryState state;
  int replay_count = 0;
  auto retried = ava::agent::prepare_context_overflow_retry_attempt(
      context_overflow_error(), true, state,
      ava::agent::ContextOverflowRetryCallbacks{
          .compact_context = [](std::string_view) -> ava::core::Result<bool> { return false; },
          .replay_active_turn_user_messages = [&]() -> ava::core::VoidResult {
            ++replay_count;
            return {};
          }});
  expect(retried && *retried && state.retry_used && !state.skip_next_auto_compaction && replay_count == 0,
         "context retry helper treats no-op compaction as a retry without active-turn replay");
}

void test_context_retry_reports_compaction_failure_with_original_error()
{
  ava::agent::ContextOverflowRetryState state;
  auto retried = ava::agent::prepare_context_overflow_retry_attempt(
      context_overflow_error(), true, state,
      ava::agent::ContextOverflowRetryCallbacks{.compact_context = [](std::string_view) -> ava::core::Result<bool> {
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Provider, "compaction failed"));
      }});
  auto const formatted = retried ? std::string{} : retried.error().format();
  expect(!retried && retried.error().message() == "context overflow compaction failed" &&
             formatted.find("provider_error") != std::string::npos &&
             formatted.find("compaction_error") != std::string::npos,
         "context retry helper reports compaction failure with original provider and compaction context");
}

void test_context_retry_propagates_cancellation_and_replay_errors()
{
  {
    ava::agent::ContextOverflowRetryState state;
    int compact_calls = 0;
    auto retried = ava::agent::prepare_context_overflow_retry_attempt(
        context_overflow_error(), true, state,
        ava::agent::ContextOverflowRetryCallbacks{.check_canceled = [](std::string_view) -> ava::core::VoidResult {
                                                    return std::unexpected(ava::core::Error(
                                                        ava::core::ErrorCategory::Unknown, "agent loop canceled"));
                                                  },
                                                  .compact_context = [&](std::string_view) -> ava::core::Result<bool> {
                                                    ++compact_calls;
                                                    return true;
                                                  }});
    expect(!retried && retried.error().message().find("canceled") != std::string::npos && compact_calls == 0,
           "context retry helper propagates cancellation before compaction");
  }

  {
    ava::agent::ContextOverflowRetryState state;
    auto retried = ava::agent::prepare_context_overflow_retry_attempt(
        context_overflow_error(), true, state,
        ava::agent::ContextOverflowRetryCallbacks{
            .compact_context = [](std::string_view) -> ava::core::Result<bool> { return true; },
            .replay_active_turn_user_messages = []() -> ava::core::VoidResult {
              return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Session, "replay failed"));
            }});
    expect(!retried && retried.error().message() == "replay failed",
           "context retry helper propagates active-turn replay errors");
  }
}

}  // namespace

void run_agent_loop_context_retry_tests()
{
  test_context_retry_ignores_non_overflow_and_unavailable_compaction();
  test_context_retry_success_replays_active_turn_once();
  test_context_retry_without_new_compaction_does_not_replay();
  test_context_retry_reports_compaction_failure_with_original_error();
  test_context_retry_propagates_cancellation_and_replay_errors();
}
