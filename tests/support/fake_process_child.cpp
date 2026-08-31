#include "sys.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <system_error>
#if !defined(_WIN32)
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#if defined(_WIN32)
int main()
{
  return 77;
}
#else
namespace {

constexpr unsigned int kWatchdogSeconds = 30;

[[noreturn]] void watchdog_exit(int) noexcept
{
  _exit(124);
}

bool write_all(int descriptor, void const* data, std::size_t size) noexcept
{
  auto const* next = static_cast<unsigned char const*>(data);
  std::size_t offset = 0;
  while (offset < size)
  {
    auto const result = ::write(descriptor, next + offset, size - offset);
    if (result > 0)
    {
      offset += static_cast<std::size_t>(result);
      continue;
    }
    if (result < 0 && errno == EINTR)
      continue;
    return false;
  }
  return true;
}

bool write_text(int descriptor, std::string_view text) noexcept
{
  return write_all(descriptor, text.data(), text.size());
}

bool read_control_byte(int descriptor) noexcept
{
  char value = '\0';
  while (true)
  {
    auto const result = ::read(descriptor, &value, 1);
    if (result == 1)
      return true;
    if (result < 0 && errno == EINTR)
      continue;
    return false;
  }
}

int parse_descriptor(std::string_view argument, std::string_view prefix, int fallback) noexcept
{
  if (!argument.starts_with(prefix))
    return fallback;
  int result = -1;
  auto const value = argument.substr(prefix.size());
  auto const parsed = std::from_chars(value.data(), value.data() + value.size(), result);
  return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() && result >= 0 ? result : fallback;
}

int parse_positive(std::string_view argument, int fallback) noexcept
{
  int result = 0;
  auto const parsed = std::from_chars(argument.data(), argument.data() + argument.size(), result);
  return parsed.ec == std::errc{} && parsed.ptr == argument.data() + argument.size() && result > 0 ? result : fallback;
}

void ignore_term() noexcept
{
  struct sigaction ignored{};
  ignored.sa_handler = SIG_IGN;
  static_cast<void>(::sigemptyset(&ignored.sa_mask));
  static_cast<void>(::sigaction(SIGTERM, &ignored, nullptr));
}

[[noreturn]] void wait_forever() noexcept
{
  while (true)
    static_cast<void>(::pause());
}

int run_flood(int stdout_fd, int stderr_fd, int bytes) noexcept
{
  std::array<char, 4096> out{};
  std::array<char, 4096> err{};
  out.fill('O');
  err.fill('E');
  int remaining = bytes;
  while (remaining > 0)
  {
    auto const chunk = static_cast<std::size_t>(std::min<int>(remaining, static_cast<int>(out.size())));
    if (!write_all(stdout_fd, out.data(), chunk) || !write_all(stderr_fd, err.data(), chunk))
      return 3;
    remaining -= static_cast<int>(chunk);
  }
  return 0;
}

int create_exec_marker(char const* path) noexcept
{
  int const descriptor = ::open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
  if (descriptor < 0)
    return 7;
  bool const written = write_text(descriptor, "EXECUTED\n");
  bool const closed = ::close(descriptor) == 0;
  return written && closed ? 0 : 7;
}

}  // namespace

int main(int argc, char** argv)
{
  struct sigaction watchdog{};
  watchdog.sa_handler = watchdog_exit;
  static_cast<void>(::sigemptyset(&watchdog.sa_mask));
  static_cast<void>(::sigaction(SIGALRM, &watchdog, nullptr));
  static_cast<void>(::alarm(kWatchdogSeconds));

  if (argc < 2)
    return 2;
  std::string_view const mode(argv[1]);
  int status_fd = STDOUT_FILENO;
  int control_fd = STDIN_FILENO;
  for (int index = 2; index < argc; ++index)
  {
    std::string_view const argument(argv[index]);
    status_fd = parse_descriptor(argument, "--status-fd=", status_fd);
    control_fd = parse_descriptor(argument, "--control-fd=", control_fd);
  }

  if (mode == "normal")
    return 0;
  if (mode == "nonzero")
    return 23;
  if (mode == "signal-exit")
  {
    static_cast<void>(::raise(SIGUSR2));
    return 125;
  }
  if (mode == "exec-marker")
    return argc >= 3 ? create_exec_marker(argv[2]) : 2;
  if (mode == "ready-gate")
  {
    if (!write_text(status_fd, "READY\n") || !read_control_byte(control_fd) || !write_text(status_fd, "RELEASED\n"))
      return 3;
    return 0;
  }
  if (mode == "input-gate")
    return read_control_byte(control_fd) ? 0 : 3;
  if (mode == "staged-stdio")
  {
    if (!write_text(STDOUT_FILENO, "OUT\n") || !read_control_byte(control_fd) || !write_text(STDERR_FILENO, "ERR\n"))
      return 3;
    return read_control_byte(control_fd) ? 0 : 3;
  }
  if (mode == "buffered-hup")
    return write_text(STDOUT_FILENO, "BUFFERED-BEFORE-HUP\n") ? 0 : 3;
  if (mode == "ignore-term")
  {
    ignore_term();
    if (!write_text(status_fd, "READY\n"))
      return 3;
    wait_forever();
  }
  if (mode == "stopped")
  {
    if (!write_text(status_fd, "READY\n"))
      return 3;
    static_cast<void>(::raise(SIGSTOP));
    wait_forever();
  }
  if (mode == "flood")
  {
    int bytes = 1024 * 1024;
    if (argc >= 3)
      bytes = parse_positive(argv[2], bytes);
    return run_flood(STDOUT_FILENO, STDERR_FILENO, bytes);
  }
  if (mode == "in-group-descendant")
  {
    pid_t const descendant = ::fork();
    if (descendant < 0)
      return 4;
    if (descendant == 0)
    {
      ignore_term();
      if (!write_text(status_fd, "DESCENDANT_READY\n"))
        _exit(5);
      wait_forever();
    }
    if (!write_text(status_fd, "LEADER_READY\n"))
      return 3;
    wait_forever();
  }
  if (mode == "leader-exits-first")
  {
    pid_t const descendant = ::fork();
    if (descendant < 0)
      return 4;
    if (descendant == 0)
    {
      ignore_term();
      if (!write_text(status_fd, "DESCENDANT_READY\n"))
        _exit(5);
      wait_forever();
    }
    if (!write_text(status_fd, "LEADER_EXITING\n"))
      return 3;
    return 0;
  }
  if (mode == "closed-pipes")
  {
    static_cast<void>(::close(STDOUT_FILENO));
    static_cast<void>(::close(STDERR_FILENO));
    return read_control_byte(control_fd) ? 0 : 3;
  }
  if (mode == "check-fd-closed")
  {
    if (argc < 3)
      return 2;
    int const descriptor = parse_positive(argv[2], -1);
    errno = 0;
    int const flags = descriptor < 0 ? 0 : ::fcntl(descriptor, F_GETFD);
    return write_text(status_fd, flags < 0 && errno == EBADF ? "CLOSED\n" : "OPEN\n") ? 0 : 3;
  }
  if (mode == "check-only-standard-fds")
  {
    bool clean = true;
    for (int descriptor = STDERR_FILENO + 1; clean && descriptor < 4096; ++descriptor)
    {
      errno = 0;
      clean = ::fcntl(descriptor, F_GETFD) < 0 && errno == EBADF;
    }
    return write_text(status_fd, clean ? "CLEAN\n" : "OPEN\n") ? (clean ? 0 : 6) : 3;
  }
  if (mode == "environment-clean")
  {
    constexpr std::array<std::string_view, 4> expected{"PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin", "LANG=C.UTF-8", "LC_ALL=C.UTF-8",
                                                       "PWD=/"};
    bool clean = std::getenv("AVA_PROCESS_AMBIENT_CANARY") == nullptr && std::getenv("HOME") == nullptr;
    for (std::size_t index = 0; clean && index < expected.size(); ++index)
      clean = ::environ[index] != nullptr && std::string_view(::environ[index]) == expected[index];
    clean = clean && ::environ[expected.size()] == nullptr;
    return write_text(status_fd, clean ? "CLEAN\n" : "DIRTY\n") ? (clean ? 0 : 6) : 3;
  }
  if (mode == "signal-state")
  {
    sigset_t mask{};
    struct sigaction term{};
    bool const clean = ::sigprocmask(SIG_SETMASK, nullptr, &mask) == 0 && ::sigismember(&mask, SIGUSR1) == 0 && ::sigaction(SIGTERM, nullptr, &term) == 0 &&
                       term.sa_handler == SIG_DFL;
    return write_text(status_fd, clean ? "CLEAN\n" : "DIRTY\n") ? (clean ? 0 : 6) : 3;
  }
  return 2;
}
#endif
