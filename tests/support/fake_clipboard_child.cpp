#include "sys.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#if !defined(_WIN32)
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
extern char** environ;
#endif

#if defined(_WIN32)
int main()
{
  return 77;
}
#else
namespace {

using namespace std::chrono_literals;
constexpr std::string_view kScenarioMarker = "--ava-clipboard-test-scenario";
constexpr std::string_view kLogMarker = "--ava-clipboard-test-log";

struct Invocation
{
  std::vector<std::string_view> arguments;
  std::string_view scenario;
  std::string_view log_path;
};

bool write_all(int descriptor, std::string_view value)
{
  std::size_t offset = 0;
  while (offset < value.size())
  {
    auto const count = ::write(descriptor, value.data() + offset, value.size() - offset);
    if (count > 0)
    {
      offset += static_cast<std::size_t>(count);
      continue;
    }
    if (count < 0 && errno == EINTR)
      continue;
    return false;
  }
  return true;
}

std::string tiny_png()
{
  std::string bytes;
  bytes.push_back(static_cast<char>(0x89));
  bytes += "PNG\r\n";
  bytes.push_back(static_cast<char>(0x1A));
  bytes += "\nava-clipboard-image";
  return bytes;
}

bool parse_invocation(int argc, char** argv, Invocation& invocation)
{
  if (argc < 5 || std::string_view(argv[argc - 4]) != kScenarioMarker || std::string_view(argv[argc - 2]) != kLogMarker)
    return false;
  invocation.scenario = argv[argc - 3];
  invocation.log_path = argv[argc - 1];
  for (int index = 0; index < argc - 4; ++index)
    invocation.arguments.emplace_back(argv[index]);
  return !invocation.arguments.empty() && !invocation.scenario.empty() && invocation.log_path.starts_with('/');
}

void log_invocation(Invocation const& invocation)
{
  std::string entry = "BEGIN\n";
  for (auto const argument : invocation.arguments)
    entry += "ARG:" + std::string(argument) + "\n";
  if (invocation.scenario == "environment")
  {
    std::vector<std::string> environment;
    for (char** current = environ; current != nullptr && *current != nullptr; ++current)
      environment.emplace_back(*current);
    std::ranges::sort(environment);
    for (auto const& variable : environment)
      entry += "ENV:" + variable + "\n";
  }
  entry += "END\n";

  int const descriptor = ::open(std::string(invocation.log_path).c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, S_IRUSR | S_IWUSR);
  if (descriptor < 0)
    return;
  static_cast<void>(write_all(descriptor, entry));
  static_cast<void>(::close(descriptor));
}

bool is_wl_list(Invocation const& invocation)
{
  return invocation.arguments.size() == 2 && invocation.arguments[0] == "wl-paste" && invocation.arguments[1] == "--list-types";
}

bool is_wl_data(Invocation const& invocation)
{
  return invocation.arguments.size() == 4 && invocation.arguments[0] == "wl-paste" && invocation.arguments[1] == "--type" &&
         invocation.arguments[3] == "--no-newline";
}

bool is_xclip_targets(Invocation const& invocation)
{
  return invocation.arguments == std::vector<std::string_view>{"xclip", "-selection", "clipboard", "-t", "TARGETS", "-o"};
}

bool is_xclip_data(Invocation const& invocation)
{
  return invocation.arguments.size() == 6 && invocation.arguments[0] == "xclip" && invocation.arguments[1] == "-selection" &&
         invocation.arguments[2] == "clipboard" && invocation.arguments[3] == "-t" && invocation.arguments[4] != "TARGETS" && invocation.arguments[5] == "-o";
}

std::string_view requested_mime(Invocation const& invocation)
{
  if (is_wl_data(invocation))
    return invocation.arguments[2];
  if (is_xclip_data(invocation))
    return invocation.arguments[4];
  return {};
}

[[noreturn]] void idle_forever()
{
  while (true)
    ::pause();
}

[[noreturn]] void write_progress_forever(bool refuse_term)
{
  if (refuse_term)
    static_cast<void>(::signal(SIGTERM, SIG_IGN));
  while (true)
  {
    if (!write_all(STDOUT_FILENO, "p"))
      _exit(0);
    std::this_thread::sleep_for(20ms);
  }
}

void write_large_output(std::size_t byte_count, bool image)
{
  std::string chunk(4096, image ? 'i' : 'l');
  std::size_t written = 0;
  if (image)
  {
    auto const prefix = tiny_png();
    if (!write_all(STDOUT_FILENO, prefix))
      return;
    written = prefix.size();
  }
  while (written < byte_count)
  {
    auto const count = std::min(chunk.size(), byte_count - written);
    if (!write_all(STDOUT_FILENO, std::string_view(chunk).substr(0, count)))
      return;
    written += count;
  }
}

int normal_list(Invocation const& invocation)
{
  if (invocation.scenario == "wayland-order")
    return write_all(STDOUT_FILENO, "image/gif; q=1\nIMAGE/WEBP\n image/jpeg ; q=2\nIMAGE/PNG; charset=binary\n") ? 0 : 70;
  if (invocation.scenario == "xclip-order")
    return write_all(STDOUT_FILENO, "image/webp; charset=binary\nIMAGE/JPEG\nimage/jpeg\n") ? 0 : 70;
  return write_all(STDOUT_FILENO, "image/png\n") ? 0 : 70;
}

int normal_data()
{
  return write_all(STDOUT_FILENO, tiny_png()) ? 0 : 71;
}

}  // namespace

int main(int argc, char** argv)
{
  ::alarm(20);
  Invocation invocation;
  if (!parse_invocation(argc, argv, invocation))
    return 64;
  log_invocation(invocation);

  if (invocation.scenario == "protocol-canary" || invocation.scenario == "read-failure")
    idle_forever();

  if (invocation.scenario == "list-deadline" && is_xclip_targets(invocation))
    write_progress_forever(false);
  if (invocation.scenario == "list-limit" && is_xclip_targets(invocation))
  {
    write_large_output(128U * 1024U, false);
    idle_forever();
  }

  if ((invocation.scenario == "image-deadline" || invocation.scenario == "term-refusal") && is_xclip_data(invocation) &&
      requested_mime(invocation) == "image/png")
  {
    write_progress_forever(invocation.scenario == "term-refusal");
  }
  if (invocation.scenario == "image-limit" && is_xclip_data(invocation))
  {
    write_large_output(20U * 1024U * 1024U + 1U, true);
    idle_forever();
  }

  if (invocation.scenario == "buffered-hup" && is_wl_data(invocation))
  {
    write_large_output(32U * 1024U, true);
    static_cast<void>(::close(STDOUT_FILENO));
    std::this_thread::sleep_for(100ms);
    return 0;
  }

  if (invocation.scenario == "descendant" && is_wl_data(invocation))
  {
    pid_t const child = ::fork();
    if (child == 0)
    {
      static_cast<void>(::signal(SIGTERM, SIG_IGN));
      idle_forever();
    }
    if (child < 0)
      return 72;
    return normal_data();
  }

  if (is_wl_list(invocation) || is_xclip_targets(invocation))
    return normal_list(invocation);

  if (!is_wl_data(invocation) && !is_xclip_data(invocation))
    return 65;

  if (invocation.scenario == "xclip-order")
  {
    if (requested_mime(invocation) == "image/jpeg")
    {
      static_cast<void>(write_all(STDOUT_FILENO, "nonzero-output-canary"));
      return 7;
    }
    if (requested_mime(invocation) == "image/png")
      return 0;
    if (requested_mime(invocation) == "image/webp")
      return normal_data();
  }

  if ((invocation.scenario == "image-deadline" || invocation.scenario == "term-refusal") && requested_mime(invocation) == "image/jpeg")
    return normal_data();

  return normal_data();
}
#endif
