#include "ava/app/clipboard_image.h"

#include "ava/core/error.h"
#include "ava/session/session_store.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <poll.h>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <vector>

namespace ava::app {
namespace {

constexpr auto kListTimeout = std::chrono::milliseconds(1000);
constexpr auto kReadTimeout = std::chrono::milliseconds(3000);
constexpr std::size_t kMaxTypeListBytes = 64 * 1024;
constexpr std::array<std::string_view, 4> kSupportedImageMimeTypes = {"image/png", "image/jpeg", "image/webp",
                                                                       "image/gif"};

struct CapturedCommand
{
  bool ok = false;
  bool too_large = false;
  std::string stdout_data;
};

class Fd
{
 public:
  explicit Fd(int fd = -1) : fd_(fd) {}
  Fd(Fd const&) = delete;
  Fd& operator=(Fd const&) = delete;
  Fd(Fd&& other) noexcept : fd_(other.release()) {}
  Fd& operator=(Fd&& other) noexcept
  {
    if (this != &other) {
      reset(other.release());
    }
    return *this;
  }
  ~Fd() { reset(); }

  [[nodiscard]] int get() const { return fd_; }
  [[nodiscard]] int release()
  {
    auto const fd = fd_;
    fd_ = -1;
    return fd;
  }
  void reset(int fd = -1)
  {
    if (fd_ >= 0)
      static_cast<void>(::close(fd_));
    fd_ = fd;
  }

 private:
  int fd_ = -1;
};

std::optional<std::string> env_value(char const* name)
{
  auto const* value = std::getenv(name);
  if (value == nullptr || *value == '\0')
    return std::nullopt;
  return std::string(value);
}

ava::core::Error errno_error(std::string message)
{
  auto error = ava::core::Error(ava::core::ErrorCategory::Io, std::move(message));
  error.with_context("cause", std::strerror(errno));
  return error;
}

ava::core::Error clipboard_error(ava::core::ErrorCategory category, std::string message)
{
  return ava::core::Error(category, std::move(message));
}

void wait_for_child(pid_t pid)
{
  int status = 0;
  while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
  }
}

ava::core::Result<CapturedCommand> capture_command_stdout(std::vector<std::string> const& argv,
                                                          std::chrono::milliseconds timeout,
                                                          std::size_t max_bytes)
{
  if (argv.empty())
    return std::unexpected(clipboard_error(ava::core::ErrorCategory::InvalidArgument, "clipboard helper command is empty"));

  int pipe_fds[2] = {-1, -1};
  if (::pipe(pipe_fds) != 0)
    return std::unexpected(errno_error("failed to create clipboard helper pipe"));

  Fd read_fd(pipe_fds[0]);
  Fd write_fd(pipe_fds[1]);
  auto const pid = ::fork();
  if (pid < 0)
    return std::unexpected(errno_error("failed to launch clipboard helper"));

  if (pid == 0) {
    read_fd.reset();
    if (::dup2(write_fd.get(), STDOUT_FILENO) < 0)
      _exit(127);
    auto const dev_null = ::open("/dev/null", O_WRONLY);
    if (dev_null >= 0) {
      static_cast<void>(::dup2(dev_null, STDERR_FILENO));
      static_cast<void>(::close(dev_null));
    }
    write_fd.reset();

    std::vector<char*> exec_argv;
    exec_argv.reserve(argv.size() + 1);
    for (auto const& arg : argv)
      exec_argv.push_back(const_cast<char*>(arg.c_str()));
    exec_argv.push_back(nullptr);
    ::execvp(exec_argv.front(), exec_argv.data());
    _exit(127);
  }

  write_fd.reset();
  auto flags = ::fcntl(read_fd.get(), F_GETFL, 0);
  if (flags >= 0)
    static_cast<void>(::fcntl(read_fd.get(), F_SETFL, flags | O_NONBLOCK));

  CapturedCommand captured;
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  std::array<char, 4096> buffer{};
  bool child_done = false;
  bool pipe_done = false;
  int child_status = 0;

  while (!child_done || !pipe_done) {
    auto const now = std::chrono::steady_clock::now();
    if (now >= deadline && !child_done) {
      static_cast<void>(::kill(pid, SIGKILL));
      read_fd.reset();
      wait_for_child(pid);
      return CapturedCommand{.ok = false};
    }

    if (!pipe_done) {
      auto poll_timeout = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
      if (poll_timeout < 0)
        poll_timeout = 0;
      if (poll_timeout > 50)
        poll_timeout = 50;
      pollfd pfd{.fd = read_fd.get(), .events = POLLIN | POLLHUP, .revents = 0};
      auto const polled = ::poll(&pfd, 1, static_cast<int>(poll_timeout));
      if (polled < 0 && errno != EINTR)
        return std::unexpected(errno_error("failed to read clipboard helper output"));
      if (polled > 0) {
        while (true) {
          auto const count = ::read(read_fd.get(), buffer.data(), buffer.size());
          if (count > 0) {
            if (captured.stdout_data.size() + static_cast<std::size_t>(count) > max_bytes) {
              static_cast<void>(::kill(pid, SIGKILL));
              read_fd.reset();
              wait_for_child(pid);
              captured.stdout_data.clear();
              captured.too_large = true;
              return captured;
            }
            captured.stdout_data.append(buffer.data(), static_cast<std::size_t>(count));
            continue;
          }
          if (count == 0) {
            pipe_done = true;
            break;
          }
          if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            break;
          return std::unexpected(errno_error("failed to read clipboard helper output"));
        }
      }
    }

    auto const waited = ::waitpid(pid, &child_status, WNOHANG);
    if (waited == pid) {
      child_done = true;
    } else if (waited < 0 && errno != EINTR) {
      return std::unexpected(errno_error("failed to wait for clipboard helper"));
    }
    if (child_done && !pipe_done) {
      continue;
    }
  }

  captured.ok = WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0;
  return captured;
}

std::string base_mime_type(std::string_view mime_type)
{
  auto const semicolon = mime_type.find(';');
  auto base = std::string(mime_type.substr(0, semicolon == std::string_view::npos ? mime_type.size() : semicolon));
  while (!base.empty() && std::isspace(static_cast<unsigned char>(base.back())) != 0)
    base.pop_back();
  auto begin = std::size_t{0};
  while (begin < base.size() && std::isspace(static_cast<unsigned char>(base[begin])) != 0)
    ++begin;
  if (begin > 0)
    base.erase(0, begin);
  std::ranges::transform(base, base.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return base;
}

std::vector<std::string> split_lines(std::string_view text)
{
  std::vector<std::string> lines;
  std::size_t start = 0;
  while (start <= text.size()) {
    auto const newline = text.find('\n', start);
    auto const end = newline == std::string_view::npos ? text.size() : newline;
    auto line = base_mime_type(text.substr(start, end - start));
    if (!line.empty())
      lines.push_back(std::move(line));
    if (newline == std::string_view::npos)
      break;
    start = newline + 1;
  }
  return lines;
}

std::optional<std::string> select_preferred_image_mime_type(std::vector<std::string> const& mime_types)
{
  for (auto const preferred : kSupportedImageMimeTypes) {
    auto const found = std::ranges::find(mime_types, preferred);
    if (found != mime_types.end())
      return *found;
  }
  return std::nullopt;
}

ava::core::Result<std::optional<std::string>> read_clipboard_data_command(std::vector<std::string> const& argv)
{
  auto data = capture_command_stdout(argv, kReadTimeout, ava::session::kMaxImageAttachmentBytes + 1);
  if (!data)
    return std::unexpected(std::move(data.error()));
  if (data->too_large)
    return std::unexpected(
        clipboard_error(ava::core::ErrorCategory::InvalidArgument, "clipboard image is too large"));
  if (!data->ok || data->stdout_data.empty())
    return std::optional<std::string>{};
  return std::optional<std::string>{std::move(data->stdout_data)};
}

ava::core::Result<std::optional<std::string>> read_clipboard_image_wl_paste()
{
  auto types = capture_command_stdout({"wl-paste", "--list-types"}, kListTimeout, kMaxTypeListBytes);
  if (!types)
    return std::unexpected(std::move(types.error()));
  if (!types->ok)
    return std::optional<std::string>{};
  auto mime_type = select_preferred_image_mime_type(split_lines(types->stdout_data));
  if (!mime_type)
    return std::optional<std::string>{};
  return read_clipboard_data_command({"wl-paste", "--type", *mime_type, "--no-newline"});
}

ava::core::Result<std::optional<std::string>> read_clipboard_image_xclip()
{
  auto targets = capture_command_stdout({"xclip", "-selection", "clipboard", "-t", "TARGETS", "-o"}, kListTimeout,
                                        kMaxTypeListBytes);
  if (!targets)
    return std::unexpected(std::move(targets.error()));

  std::vector<std::string> candidates;
  if (targets->ok) {
    if (auto preferred = select_preferred_image_mime_type(split_lines(targets->stdout_data)))
      candidates.push_back(std::move(*preferred));
  }
  for (auto const mime_type : kSupportedImageMimeTypes) {
    if (std::ranges::find(candidates, mime_type) == candidates.end())
      candidates.emplace_back(mime_type);
  }

  for (auto const& mime_type : candidates) {
    auto data = read_clipboard_data_command({"xclip", "-selection", "clipboard", "-t", mime_type, "-o"});
    if (!data)
      return std::unexpected(std::move(data.error()));
    if (*data)
      return data;
  }
  return std::optional<std::string>{};
}

ava::core::Result<std::optional<std::string>> read_clipboard_image_bytes()
{
  if (env_value("TERMUX_VERSION"))
    return std::optional<std::string>{};

  if (auto wayland = env_value("WAYLAND_DISPLAY"); wayland) {
    auto image = read_clipboard_image_wl_paste();
    if (!image)
      return std::unexpected(std::move(image.error()));
    if (*image)
      return image;
  }

  auto xclip = read_clipboard_image_xclip();
  if (!xclip)
    return std::unexpected(std::move(xclip.error()));
  if (*xclip)
    return xclip;

  if (!env_value("WAYLAND_DISPLAY")) {
    auto image = read_clipboard_image_wl_paste();
    if (!image)
      return std::unexpected(std::move(image.error()));
    if (*image)
      return image;
  }
  return std::optional<std::string>{};
}

}  // namespace

ava::core::Result<std::optional<ava::session::ImageAttachmentRef>> import_clipboard_image_attachment(
    ava::session::SessionStore const& store)
{
  if (auto override_file = env_value("AVA_CLIPBOARD_IMAGE_FILE")) {
    auto imported = ava::session::import_image_attachment(store, std::filesystem::path(*override_file));
    if (!imported)
      return std::unexpected(std::move(imported.error()));
    return std::optional<ava::session::ImageAttachmentRef>{std::move(*imported)};
  }

  auto bytes = read_clipboard_image_bytes();
  if (!bytes)
    return std::unexpected(std::move(bytes.error()));
  if (!*bytes)
    return std::optional<ava::session::ImageAttachmentRef>{};

  auto imported = ava::session::import_image_attachment_bytes(store, **bytes);
  if (!imported)
    return std::unexpected(std::move(imported.error()));
  return std::optional<ava::session::ImageAttachmentRef>{std::move(*imported)};
}

}  // namespace ava::app
