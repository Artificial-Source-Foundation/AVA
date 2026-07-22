#include "tests/support/test_harness.h"

#include <array>
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

}  // namespace

void run_test_harness_tests()
{
  test_write_all_transfers_exact_pipe_payload();
  test_write_all_reports_closed_pipe_without_sigpipe_termination();
}
