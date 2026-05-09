#include "ava/app/browser_open.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace ava::app {
namespace {

bool is_browser_url(std::string_view url)
{
  return url.starts_with("https://") || url.starts_with("http://");
}

bool contains_ascii_space(std::string_view value)
{
  return std::ranges::any_of(value, [](char ch) { return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r'; });
}

bool executable_exists(std::string_view command)
{
  if (command.empty())
    return false;
  auto const command_text = std::string(command);
  if (command.find('/') != std::string_view::npos)
    return ::access(command_text.c_str(), X_OK) == 0;

  char const* path_env = std::getenv("PATH");
  if (path_env == nullptr)
    return false;
  std::string_view path(path_env);
  std::size_t start = 0;
  while (start <= path.size())
  {
    auto const end = path.find(':', start);
    auto const entry = path.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
    auto candidate = entry.empty() ? std::string(".") : std::string(entry);
    candidate += "/";
    candidate += command_text;
    if (::access(candidate.c_str(), X_OK) == 0)
      return true;
    if (end == std::string_view::npos)
      break;
    start = end + 1;
  }
  return false;
}

void redirect_stdio_to_devnull()
{
  auto const devnull = ::open("/dev/null", O_RDWR);
  if (devnull < 0)
    return;
  static_cast<void>(::dup2(devnull, STDIN_FILENO));
  static_cast<void>(::dup2(devnull, STDOUT_FILENO));
  static_cast<void>(::dup2(devnull, STDERR_FILENO));
  if (devnull > STDERR_FILENO)
    static_cast<void>(::close(devnull));
}

bool spawn_detached(std::vector<std::string> const& arguments)
{
  if (arguments.empty())
    return false;
  auto const launcher = arguments.front();
  if (!executable_exists(launcher))
    return false;

  auto const pid = ::fork();
  if (pid < 0)
    return false;
  if (pid == 0)
  {
    auto const grandchild = ::fork();
    if (grandchild < 0)
      _exit(127);
    if (grandchild > 0)
      _exit(0);
    static_cast<void>(::setsid());
    redirect_stdio_to_devnull();

    std::vector<char*> argv;
    argv.reserve(arguments.size() + 1);
    for (auto const& argument : arguments) argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);
    ::execvp(argv.front(), argv.data());
    _exit(127);
  }

  int status = 0;
  while (::waitpid(pid, &status, 0) < 0)
  {
    if (errno == EINTR)
      continue;
    return false;
  }
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

std::vector<std::vector<std::string>> browser_commands(std::string_view url)
{
  std::vector<std::vector<std::string>> commands;
  if (char const* browser = std::getenv("BROWSER"); browser != nullptr && *browser != '\0')
  {
    std::string_view browser_view(browser);
    if (!contains_ascii_space(browser_view))
      commands.push_back({std::string(browser_view), std::string(url)});
  }
  commands.push_back({"xdg-open", std::string(url)});
  commands.push_back({"gio", "open", std::string(url)});
  commands.push_back({"open", std::string(url)});
  commands.push_back({"wslview", std::string(url)});
  return commands;
}

}  // namespace

bool open_url_in_browser(std::string_view url)
{
  if (!is_browser_url(url))
    return false;
  if (std::getenv("AVA_DISABLE_BROWSER_OPEN") != nullptr)
    return false;

  for (auto const& command : browser_commands(url))
  {
    if (spawn_detached(command))
      return true;
  }
  return false;
}

}  // namespace ava::app
