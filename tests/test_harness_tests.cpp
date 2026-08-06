#include "tests/support/test_harness.h"
#include "tests/support/test_timeout.h"

#include <array>
#include <chrono>
#include <string_view>
#include <unistd.h>

namespace {

void test_write_all_transfers_exact_pipe_payload()
{
  std::array<int, 2> descriptors{-1, -1};
  bool const pipe_created = ::pipe(descriptors.data()) == 0;
  if (!pipe_created)
  {
    expect(false, "test harness exact-transfer pipe is created");
    return;
  }

  constexpr std::string_view payload = "exact pipe transfer";
  bool const written = write_all_to_descriptor_for_test(descriptors[1], payload.data(), payload.size());
  static_cast<void>(::close(descriptors[1]));

  std::array<char, payload.size()> observed{};
  bool const read = read_exact_from_descriptor_for_test(descriptors[0], observed.data(), observed.size());
  static_cast<void>(::close(descriptors[0]));

  expect(written && read && std::string_view(observed.data(), observed.size()) == payload, "test harness write helper transfers an exact pipe payload");
}

void test_write_all_reports_closed_pipe_without_sigpipe_termination()
{
  std::array<int, 2> descriptors{-1, -1};
  bool const pipe_created = ::pipe(descriptors.data()) == 0;
  if (!pipe_created)
  {
    expect(false, "test harness closed-reader pipe is created");
    return;
  }

  static_cast<void>(::close(descriptors[0]));
  constexpr char payload = 'x';
  bool const written = write_all_to_descriptor_for_test(descriptors[1], &payload, sizeof(payload));
  static_cast<void>(::close(descriptors[1]));

  expect(!written, "test harness write helper reports a closed pipe without terminating the test process");
}

void test_debug_timeout_deadlines_honor_runtime_environment()
{
  ScopedEnvVar const enabled("AVA_DEBUG_NO_TIMEOUT", "1");
  ScopedEnvVar const seconds("AVA_DEBUG_NO_TIMEOUT_SECONDS", "17");

  auto const before_integer = std::chrono::steady_clock::now();
  auto const integer_deadline = ava::tests::now_plus_seconds(2);
  auto const integer_delay = integer_deadline - before_integer;
  expect(integer_delay >= std::chrono::seconds(16) && integer_delay <= std::chrono::seconds(18),
         "test timeout helper applies AVA_DEBUG_NO_TIMEOUT_SECONDS to integer-second deadlines");

  auto const before_duration = std::chrono::steady_clock::now();
  auto const duration_deadline = ava::tests::now_plus_seconds(std::chrono::milliseconds(250));
  auto const duration_delay = duration_deadline - before_duration;
  expect(duration_delay >= std::chrono::seconds(16) && duration_delay <= std::chrono::seconds(18),
         "test timeout helper applies AVA_DEBUG_NO_TIMEOUT_SECONDS to duration-valued deadlines");
}

}  // namespace

void run_test_harness_tests()
{
  test_write_all_transfers_exact_pipe_payload();
  test_write_all_reports_closed_pipe_without_sigpipe_termination();
  test_debug_timeout_deadlines_honor_runtime_environment();
}
