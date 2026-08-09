#include "sys.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

bool write_all(int fd, std::string_view text)
{
  while (!text.empty())
  {
    auto const written = ::write(fd, text.data(), text.size());
    if (written < 0 && errno == EINTR)
      continue;
    if (written <= 0)
      return false;
    text.remove_prefix(static_cast<std::size_t>(written));
  }
  return true;
}

std::string read_stdin()
{
  std::string input;
  std::array<char, 4096> buffer{};
  for (;;)
  {
    auto const count = ::read(STDIN_FILENO, buffer.data(), buffer.size());
    if (count == 0)
      return input;
    if (count < 0)
    {
      if (errno == EINTR)
        continue;
      std::exit(90);
    }
    input.append(buffer.data(), static_cast<std::size_t>(count));
  }
}

void ignore_signal(int)
{
}

int group_timeout(std::filesystem::path const& pid_file)
{
  struct sigaction parent_action{};
  parent_action.sa_handler = ignore_signal;
  ::sigemptyset(&parent_action.sa_mask);
  if (::sigaction(SIGTERM, &parent_action, nullptr) != 0)
    return 91;

  auto const child = ::fork();
  if (child < 0)
    return 92;
  if (child == 0)
  {
    struct sigaction child_action{};
    child_action.sa_handler = SIG_DFL;
    ::sigemptyset(&child_action.sa_mask);
    static_cast<void>(::sigaction(SIGTERM, &child_action, nullptr));
    for (;;) static_cast<void>(::pause());
  }

  {
    std::ofstream output(pid_file, std::ios::binary | std::ios::trunc);
    output << child << '\n';
    output.flush();
    if (!output)
    {
      static_cast<void>(::kill(child, SIGKILL));
      return 93;
    }
  }
  int status = 0;
  while (::waitpid(child, &status, 0) < 0)
  {
    if (errno != EINTR)
      return 94;
  }
  return 0;
}

bool write_sized_output(std::size_t size)
{
  std::string chunk;
  chunk.reserve(8192);
  while (size > 0)
  {
    auto const line_payload = std::min<std::size_t>(size > 1 ? size - 1 : 0, 8191);
    if (line_payload != 0)
      chunk.append(line_payload, 'x');
    if (chunk.size() < size)
      chunk.push_back('\n');
    if (chunk.empty())
      chunk.push_back('x');
    if (!write_all(STDOUT_FILENO, chunk))
      return false;
    size -= chunk.size();
    chunk.clear();
  }
  return true;
}

}  // namespace

int main(int argc, char** argv)
{
  int index = 1;
  if (argc > 3 && std::string_view(argv[index]) == "--count")
  {
    std::ofstream count(argv[index + 1], std::ios::binary | std::ios::app);
    count << 'x';
    count.flush();
    if (!count)
      return 89;
    index += 2;
  }
  if (index >= argc)
    return 88;
  std::string_view const mode(argv[index++]);
  auto const input = read_stdin();

  if (mode == "echo")
    return write_all(STDOUT_FILENO, input) ? 0 : 87;
  if (mode == "argument")
    return index < argc && write_all(STDOUT_FILENO, argv[index]) ? 0 : 86;
  if (mode == "environment")
  {
    std::vector<std::string> values;
    for (char** entry = ::environ; entry != nullptr && *entry != nullptr; ++entry) values.emplace_back(*entry);
    std::ranges::sort(values);
    for (auto const& value : values)
    {
      if (!write_all(STDOUT_FILENO, value) || !write_all(STDOUT_FILENO, "\n"))
        return 85;
    }
    return 0;
  }
  if (mode == "cwd")
  {
    std::array<char, 4096> cwd{};
    return ::getcwd(cwd.data(), cwd.size()) != nullptr && write_all(STDOUT_FILENO, cwd.data()) ? 0 : 84;
  }
  if (mode == "exit")
    return index < argc ? std::stoi(argv[index]) : 7;
  if (mode == "signal")
  {
    static_cast<void>(::raise(SIGUSR1));
    return 83;
  }
  if (mode == "timeout")
  {
    for (;;) static_cast<void>(::pause());
  }
  if (mode == "group-timeout")
    return index < argc ? group_timeout(argv[index]) : 83;
  if (mode == "output-size")
    return index < argc && write_sized_output(static_cast<std::size_t>(std::stoull(argv[index]))) ? 0 : 82;
  if (mode == "invalid-utf8")
    return write_all(STDOUT_FILENO, std::string_view("bad\x80", 4)) ? 0 : 81;
  if (mode == "nul")
    return write_all(STDOUT_FILENO, std::string_view("bad\0text", 8)) ? 0 : 80;
  if (mode == "tab")
    return write_all(STDOUT_FILENO, "bad\ttext") ? 0 : 79;
  if (mode == "carriage-return")
    return write_all(STDOUT_FILENO, "bad\rtext") ? 0 : 78;
  if (mode == "escape")
    return write_all(STDOUT_FILENO, "bad\x1btext") ? 0 : 77;
  if (mode == "del")
    return write_all(STDOUT_FILENO, std::string_view("bad\x7ftext", 8)) ? 0 : 76;
  if (mode == "c1")
    return write_all(STDOUT_FILENO, "bad\xc2\x80text") ? 0 : 75;
  if (mode == "bidi")
    return write_all(STDOUT_FILENO, "bad\xe2\x80\xaetext") ? 0 : 74;
  if (mode == "long-line")
    return write_all(STDOUT_FILENO, std::string(8193, 'x')) ? 0 : 73;
  if (mode == "too-many-lines")
  {
    for (std::size_t line = 0; line < 4097; ++line)
    {
      if (!write_all(STDOUT_FILENO, "x\n"))
        return 72;
    }
    return 0;
  }
  return 71;
}
