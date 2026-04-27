#include "ava/tui/terminal.h"

#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <csignal>
#include <string_view>
#include <utility>

#include "ava/core/error.h"

namespace ava::tui {
namespace {

termios g_original_termios{};
int g_original_fd = -1;
volatile sig_atomic_t g_restore_terminal = 0;
struct sigaction g_previous_sigint {};
struct sigaction g_previous_sigterm {};

void restore_terminal_for_signal(int signal_number) {
  if (g_restore_terminal != 0 && g_original_fd >= 0) {
    static_cast<void>(tcsetattr(g_original_fd, TCSAFLUSH, &g_original_termios));
  }
  signal(signal_number, SIG_DFL);
  raise(signal_number);
}

void install_signal_restore(int input_fd, const termios& original) {
  g_original_termios = original;
  g_original_fd = input_fd;
  g_restore_terminal = 1;

  struct sigaction action {};
  action.sa_handler = restore_terminal_for_signal;
  sigemptyset(&action.sa_mask);
  action.sa_flags = 0;
  static_cast<void>(sigaction(SIGINT, &action, &g_previous_sigint));
  static_cast<void>(sigaction(SIGTERM, &action, &g_previous_sigterm));
}

void uninstall_signal_restore() {
  if (g_restore_terminal == 0) return;
  static_cast<void>(sigaction(SIGINT, &g_previous_sigint, nullptr));
  static_cast<void>(sigaction(SIGTERM, &g_previous_sigterm, nullptr));
  g_restore_terminal = 0;
  g_original_fd = -1;
}

}  // namespace

RawTerminalGuard::RawTerminalGuard(int input_fd, termios original) : input_fd_(input_fd), original_(original), active_(true) {}

RawTerminalGuard::RawTerminalGuard(RawTerminalGuard&& other) noexcept
    : input_fd_(std::exchange(other.input_fd_, -1)),
      original_(other.original_),
      active_(std::exchange(other.active_, false)) {}

RawTerminalGuard& RawTerminalGuard::operator=(RawTerminalGuard&& other) noexcept {
  if (this == &other) return *this;
  if (active_) {
    static_cast<void>(tcsetattr(input_fd_, TCSAFLUSH, &original_));
    uninstall_signal_restore();
  }
  input_fd_ = std::exchange(other.input_fd_, -1);
  original_ = other.original_;
  active_ = std::exchange(other.active_, false);
  return *this;
}

RawTerminalGuard::~RawTerminalGuard() {
  if (active_) {
    static_cast<void>(tcsetattr(input_fd_, TCSAFLUSH, &original_));
    uninstall_signal_restore();
  }
}

ava::core::Result<RawTerminalGuard> RawTerminalGuard::enable(int input_fd) {
  termios original{};
  if (tcgetattr(input_fd, &original) != 0) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to read terminal mode");
    error.with_context("errno", std::strerror(errno));
    return std::unexpected(std::move(error));
  }

  termios raw = original;
  raw.c_lflag &= static_cast<tcflag_t>(~(ECHO | ICANON | IEXTEN | ISIG));
  raw.c_iflag &= static_cast<tcflag_t>(~(IXON | ICRNL));
  raw.c_cc[VMIN] = 1;
  raw.c_cc[VTIME] = 0;
  if (tcsetattr(input_fd, TCSAFLUSH, &raw) != 0) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to enable terminal raw mode");
    error.with_context("errno", std::strerror(errno));
    return std::unexpected(std::move(error));
  }
  install_signal_restore(input_fd, original);
  return RawTerminalGuard(input_fd, original);
}

InputEvent parse_input_byte(unsigned char byte) {
  if (byte == '\r' || byte == '\n') return InputEvent{.key = Key::Enter};
  if (byte == '\t') return InputEvent{.key = Key::Tab};
  if (byte == 0x1B) return InputEvent{.key = Key::Escape};
  if (byte == 0x03) return InputEvent{.key = Key::CtrlC};
  if (byte == 0x04) return InputEvent{.key = Key::CtrlD};
  if (byte == 0x7F || byte == 0x08) return InputEvent{.key = Key::Backspace};
  if (byte >= 0x20) return InputEvent{.key = Key::Character, .character = static_cast<char>(byte)};
  return InputEvent{.key = Key::Unknown};
}

void erase_last_utf8_codepoint(std::string& text) {
  if (text.empty()) return;
  if ((static_cast<unsigned char>(text.back()) & 0xC0U) != 0x80U) {
    text.pop_back();
    return;
  }
  while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0U) == 0x80U) {
    text.pop_back();
  }
  if (!text.empty()) {
    text.pop_back();
  }
}

bool terminal_is_tty() { return isatty(STDIN_FILENO) != 0 && isatty(STDOUT_FILENO) != 0; }

ava::core::VoidResult write_terminal(std::string_view text) {
  while (!text.empty()) {
    const auto written = write(STDOUT_FILENO, text.data(), text.size());
    if (written < 0 && errno == EINTR) continue;
    if (written <= 0) {
      auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to write terminal output");
      if (written < 0) error.with_context("errno", std::strerror(errno));
      return std::unexpected(std::move(error));
    }
    text.remove_prefix(static_cast<std::size_t>(written));
  }
  return {};
}

}  // namespace ava::tui
