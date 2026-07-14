#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef AVA_EXE_PATH
#define AVA_EXE_PATH ""
#endif

namespace {

std::optional<std::size_t> fd_count()
{
  std::error_code error;
  std::size_t count = 0;
  for (auto const& entry : std::filesystem::directory_iterator("/proc/self/fd", error))
  {
    static_cast<void>(entry);
    ++count;
  }
  if (error)
    return std::nullopt;
  return count;
}

void close_fd(int& fd)
{
  if (fd >= 0)
  {
    close(fd);
    fd = -1;
  }
}

bool write_all(int fd, std::string const& text)
{
  std::size_t offset = 0;
  while (offset < text.size())
  {
    auto const written = write(fd, text.data() + offset, text.size() - offset);
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

std::string read_all(int fd)
{
  std::string result;
  char buffer[4096];
  while (true)
  {
    auto const bytes = read(fd, buffer, sizeof(buffer));
    if (bytes > 0)
    {
      result.append(buffer, static_cast<std::size_t>(bytes));
      continue;
    }
    if (bytes < 0 && errno == EINTR)
      continue;
    break;
  }
  return result;
}

int fail(std::string const& message)
{
  std::cerr << "FAIL: " << message << '\n';
  return 1;
}

}  // namespace

int main()
{
  auto const before_fds = fd_count();
  auto root = std::filesystem::temp_directory_path() / ("ava-rpc-broken-pipe-" + std::to_string(static_cast<long long>(getpid())));
  std::error_code error;
  std::filesystem::remove_all(root, error);
  auto const workspace = root / "workspace";
  auto const home = root / "home";
  auto const config = root / "config";
  auto const state = root / "state";
  auto const data = root / "data";
  std::filesystem::create_directories(workspace, error);
  std::filesystem::create_directories(home, error);
  std::filesystem::create_directories(config, error);
  std::filesystem::create_directories(state, error);
  std::filesystem::create_directories(data, error);
  if (error)
    return fail("could not create isolated RPC test directories: " + error.message());

  int stdin_pipe[2] = {-1, -1};
  int stdout_pipe[2] = {-1, -1};
  int stderr_pipe[2] = {-1, -1};
  if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0)
    return fail("could not create subprocess pipes: " + std::string(std::strerror(errno)));

  pid_t const child = fork();
  if (child < 0)
    return fail("could not fork ava: " + std::string(std::strerror(errno)));
  if (child == 0)
  {
    struct sigaction default_sigpipe{};
    default_sigpipe.sa_handler = SIG_DFL;
    sigemptyset(&default_sigpipe.sa_mask);
    static_cast<void>(sigaction(SIGPIPE, &default_sigpipe, nullptr));

    static_cast<void>(dup2(stdin_pipe[0], STDIN_FILENO));
    static_cast<void>(dup2(stdout_pipe[1], STDOUT_FILENO));
    static_cast<void>(dup2(stderr_pipe[1], STDERR_FILENO));
    close_fd(stdin_pipe[0]);
    close_fd(stdin_pipe[1]);
    close_fd(stdout_pipe[0]);
    close_fd(stdout_pipe[1]);
    close_fd(stderr_pipe[0]);
    close_fd(stderr_pipe[1]);

    static_cast<void>(setenv("HOME", home.c_str(), 1));
    static_cast<void>(setenv("XDG_CONFIG_HOME", config.c_str(), 1));
    static_cast<void>(setenv("XDG_STATE_HOME", state.c_str(), 1));
    static_cast<void>(setenv("XDG_DATA_HOME", data.c_str(), 1));
    static_cast<void>(setenv("NO_COLOR", "1", 1));
    if (chdir(workspace.c_str()) != 0)
      _exit(126);
    execl(AVA_EXE_PATH, AVA_EXE_PATH, "--rpc", "--offline", static_cast<char*>(nullptr));
    _exit(127);
  }

  close_fd(stdin_pipe[0]);
  close_fd(stdout_pipe[1]);
  close_fd(stderr_pipe[1]);
  close_fd(stdout_pipe[0]);

  std::string const request = "{\"id\":\"broken-pipe\",\"type\":\"prompt\",\"message\":\"trigger worker response\"}\n";
  bool const request_written = write_all(stdin_pipe[1], request);

  int status = 0;
  bool exited = false;
  auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < deadline)
  {
    auto const waited = waitpid(child, &status, WNOHANG);
    if (waited == child)
    {
      exited = true;
      break;
    }
    if (waited < 0 && errno != EINTR)
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  if (!exited)
  {
    static_cast<void>(kill(child, SIGKILL));
    static_cast<void>(waitpid(child, &status, 0));
  }

  close_fd(stdin_pipe[1]);
  auto const stderr_text = read_all(stderr_pipe[0]);
  close_fd(stderr_pipe[0]);
  std::filesystem::remove_all(root, error);
  auto const after_fds = fd_count();

  if (!request_written)
    return fail("could not write the worker-triggering request while stdin remained open");
  if (!exited)
    return fail("ava did not terminate within five seconds after its stdout reader closed");
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 1)
  {
    if (WIFSIGNALED(status))
      return fail("ava terminated from signal " + std::to_string(WTERMSIG(status)) + "; stderr: " + stderr_text);
    return fail("ava did not exit 1; status=" + std::to_string(status) + "; stderr: " + stderr_text);
  }
  if (stderr_text.find("failed to write RPC JSONL record") == std::string::npos)
    return fail("ava exit 1 did not include the RPC output I/O diagnostic; stderr: " + stderr_text);
  if (before_fds && after_fds && *before_fds != *after_fds)
    return fail("subprocess regression leaked parent file descriptors: before=" + std::to_string(*before_fds) + " after=" + std::to_string(*after_fds));

  std::cout << "PASS: ava ignored SIGPIPE, woke blocked stdin, joined its worker, reported RPC output I/O failure, and exited 1 without fd leaks\n";
  return 0;
}
