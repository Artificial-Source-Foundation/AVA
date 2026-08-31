#include "sys.h"
#include "tests/support/test_harness.h"
#include "ava/process/launch_protocol_posix.h"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <type_traits>
#if !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {

using namespace std::chrono_literals;

#if !defined(_WIN32)

class TestPipe final
{
 public:
  TestPipe()
  {
    std::array<int, 2> descriptors{-1, -1};
#if defined(__linux__)
    valid_ = ::pipe2(descriptors.data(), O_CLOEXEC) == 0;
#else
    valid_ = ::pipe(descriptors.data()) == 0;
#endif
    if (valid_)
    {
      read_ = descriptors[0];
      write_ = descriptors[1];
    }
  }

  TestPipe(TestPipe const&) = delete;
  TestPipe& operator=(TestPipe const&) = delete;
  ~TestPipe()
  {
    close_read();
    close_write();
  }

  [[nodiscard]] bool valid() const noexcept { return valid_; }
  [[nodiscard]] int read_descriptor() const noexcept { return read_; }
  [[nodiscard]] int write_descriptor() const noexcept { return write_; }
  void close_read() noexcept
  {
    if (read_ >= 0)
      static_cast<void>(::close(read_));
    read_ = -1;
  }
  void close_write() noexcept
  {
    if (write_ >= 0)
      static_cast<void>(::close(write_));
    write_ = -1;
  }

 private:
  int read_ = -1;
  int write_ = -1;
  bool valid_ = false;
};

bool write_all(int descriptor, void const* data, std::size_t size) noexcept
{
  auto const* bytes = static_cast<unsigned char const*>(data);
  std::size_t offset = 0;
  while (offset < size)
  {
    auto const written = ::write(descriptor, bytes + offset, size - offset);
    if (written > 0)
    {
      offset += static_cast<std::size_t>(written);
      continue;
    }
    if (written < 0 && errno == EINTR)
      continue;
    return false;
  }
  return true;
}

ava::process::detail::LaunchProtocolOutcomeV1 finish_exec_sequence(TestPipe& channel)
{
  channel.close_write();
  return ava::process::detail::await_launch_exec_confirmation(channel.read_descriptor(), std::chrono::steady_clock::now() + 1s, false);
}

void test_fixed_protocol_success_and_failures()
{
  using namespace ava::process::detail;
  static_assert(std::is_trivially_copyable_v<LaunchFrameV1>);
  static_assert(sizeof(LaunchFrameV1) == 16);
  static_assert(sizeof(LaunchFrameV1) < 512);

  TestPipe success;
  bool const attempt_written = success.valid() && child_write_exec_attempt(success.write_descriptor());
  auto const confirmed = finish_exec_sequence(success);

  TestPipe exec_failure;
  bool const failure_written =
      exec_failure.valid() && child_write_exec_attempt(exec_failure.write_descriptor()) && child_write_exec_failed(exec_failure.write_descriptor(), ENOEXEC);
  auto const failed = finish_exec_sequence(exec_failure);

  TestPipe setup_failure;
  bool const setup_written =
      setup_failure.valid() && child_write_launch_failed(setup_failure.write_descriptor(), LaunchFailureStageV1::WorkingDirectory, EACCES);
  auto const setup = finish_exec_sequence(setup_failure);

  TestPipe no_attempt;
  no_attempt.close_write();
  auto const eof = await_launch_exec_confirmation(no_attempt.read_descriptor(), std::chrono::steady_clock::now() + 1s, false);

  expect(attempt_written && confirmed.disposition == LaunchProtocolDispositionV1::ExecConfirmed,
         "the fixed launch protocol confirms only an explicit exec attempt followed by CLOEXEC EOF");
  expect(failure_written && failed.disposition == LaunchProtocolDispositionV1::ExecFailed && failed.error_number == ENOEXEC,
         "ExecAttempt followed by ExecFailed retains the typed exec-syscall classification");
  expect(setup_written && setup.disposition == LaunchProtocolDispositionV1::LaunchFailed && setup.stage == LaunchFailureStageV1::WorkingDirectory,
         "a typed pre-attempt setup frame remains a launch failure");
  expect(eof.disposition == LaunchProtocolDispositionV1::LaunchFailed && eof.problem == LaunchProtocolProblemV1::EndBeforeAttempt,
         "EOF before an explicit exec attempt is never launch success");
}

void test_malformed_truncated_and_out_of_order_protocol()
{
  using namespace ava::process::detail;

  TestPipe malformed_pipe;
  LaunchFrameV1 malformed{.magic = 0,
                          .version = kLaunchFrameVersionV1,
                          .kind = static_cast<std::uint8_t>(LaunchFrameKindV1::ExecAttempt),
                          .stage = static_cast<std::uint8_t>(LaunchFailureStageV1::None),
                          .error_number = 0,
                          .reserved = 0};
  bool const malformed_written = malformed_pipe.valid() && write_all(malformed_pipe.write_descriptor(), &malformed, sizeof(malformed));
  auto const malformed_result = finish_exec_sequence(malformed_pipe);

  TestPipe truncated_pipe;
  LaunchFrameV1 frame{.magic = kLaunchFrameMagicV1,
                      .version = kLaunchFrameVersionV1,
                      .kind = static_cast<std::uint8_t>(LaunchFrameKindV1::ExecAttempt),
                      .stage = static_cast<std::uint8_t>(LaunchFailureStageV1::None),
                      .error_number = 0,
                      .reserved = 0};
  bool const truncated_written = truncated_pipe.valid() && write_all(truncated_pipe.write_descriptor(), &frame, sizeof(frame) / 2);
  auto const truncated_result = finish_exec_sequence(truncated_pipe);

  TestPipe out_of_order_pipe;
  bool const out_of_order_written = out_of_order_pipe.valid() && child_write_exec_failed(out_of_order_pipe.write_descriptor(), ENOEXEC);
  auto const out_of_order_result = finish_exec_sequence(out_of_order_pipe);

  expect(malformed_written && malformed_result.disposition == LaunchProtocolDispositionV1::LaunchFailed &&
             malformed_result.problem == LaunchProtocolProblemV1::MalformedFrame,
         "a malformed versioned launch frame fails closed before success");
  expect(truncated_written && truncated_result.disposition == LaunchProtocolDispositionV1::LaunchFailed &&
             truncated_result.problem == LaunchProtocolProblemV1::TruncatedFrame,
         "a truncated fixed launch frame fails closed before success");
  expect(out_of_order_written && out_of_order_result.disposition == LaunchProtocolDispositionV1::LaunchFailed &&
             out_of_order_result.problem == LaunchProtocolProblemV1::OutOfOrderFrame,
         "a valid frame in an invalid semantic order is a launch failure rather than an exec failure");
}

void test_containment_checkpoint_sequence()
{
  using namespace ava::process::detail;

  TestPipe status;
  TestPipe continuation;
  bool const frames_written = status.valid() && continuation.valid() && child_write_bash_containment_applied(status.write_descriptor()) &&
                              child_write_exec_attempt(status.write_descriptor());
  status.close_write();
  auto const confirmed = await_launch_exec_confirmation(status.read_descriptor(), std::chrono::steady_clock::now() + 1s, true, continuation.write_descriptor());
  char continued = '\0';
  auto const continuation_read = ::read(continuation.read_descriptor(), &continued, 1);

  TestPipe unexpected;
  bool const unexpected_written = unexpected.valid() && child_write_bash_containment_applied(unexpected.write_descriptor());
  auto const rejected = finish_exec_sequence(unexpected);

  expect(frames_written && confirmed.disposition == LaunchProtocolDispositionV1::ExecConfirmed && continuation_read == 1 && continued == 'C',
         "required Bash containment continues only after its valid checkpoint and still requires exec confirmation");
  expect(
      unexpected_written && rejected.disposition == LaunchProtocolDispositionV1::LaunchFailed && rejected.problem == LaunchProtocolProblemV1::OutOfOrderFrame,
      "a Bash containment checkpoint is a protocol failure when the closed specification selected None");
}

#endif

}  // namespace

void run_process_launch_protocol_posix_tests()
{
#if defined(_WIN32)
  ava::tests::request_skip("process launch framing is compile-time unsupported on Windows");
#else
  test_fixed_protocol_success_and_failures();
  test_malformed_truncated_and_out_of_order_protocol();
  test_containment_checkpoint_sequence();
#endif
}
