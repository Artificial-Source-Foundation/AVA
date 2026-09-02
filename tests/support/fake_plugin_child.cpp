#include "sys.h"

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <unistd.h>

extern char** environ;

namespace {

using namespace std::chrono_literals;

void initialized()
{
  std::cout << "{\"id\":\"ava_1\",\"type\":\"initialized\",\"api_version\":\"ava.plugin.v1\",\"plugin_version\":\"1.2.3\","
               "\"contributions\":{\"tools\":[]}}\n"
            << std::flush;
}

[[noreturn]] void loop_forever()
{
  while (true)
    std::this_thread::sleep_for(1s);
}

void write_marker(std::filesystem::path const& path, std::string_view value)
{
  if (path.empty())
    return;
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output << value << '\n';
}

}  // namespace

int main(int argc, char** argv)
{
  std::string const scenario = argc > 1 ? argv[1] : "normal";
  std::filesystem::path const marker = argc > 2 ? argv[2] : "";
  std::string initialize_request;

  if (scenario == "startup-hang")
    loop_forever();
  if (!std::getline(std::cin, initialize_request))
    return 2;

  if (scenario == "oversized-initialize")
  {
    std::cout << std::string(8192, 'x') << '\n' << std::flush;
    loop_forever();
  }
  if (scenario == "stderr-large")
  {
    std::cerr << std::string(8192, 'e') << std::flush;
  }
  if (scenario == "environment")
  {
    std::ofstream output(marker, std::ios::binary | std::ios::trunc);
    for (char** current = environ; current != nullptr && *current != nullptr; ++current)
      output << *current << '\n';
  }

  initialized();
  if (scenario == "endpoint-eof")
    return 7;

  std::string request;
  if (!std::getline(std::cin, request))
  {
    if (scenario == "shutdown-term-refusal")
    {
      std::signal(SIGTERM, SIG_IGN);
      loop_forever();
    }
    return 0;
  }

  if (scenario == "request-hang" || scenario == "shutdown-term-refusal")
  {
    write_marker(marker, "request-observed");
    if (scenario == "shutdown-term-refusal")
      std::signal(SIGTERM, SIG_IGN);
    loop_forever();
  }
  if (scenario == "malformed")
  {
    std::cout << "not-json\n" << std::flush;
    loop_forever();
  }
  if (scenario == "descendant")
  {
    auto const child = ::fork();
    if (child < 0)
      return 3;
    if (child == 0)
    {
      std::signal(SIGTERM, SIG_IGN);
      loop_forever();
    }
    write_marker(marker, std::to_string(static_cast<long long>(child)));
    return 0;
  }

  std::cout << "{\"id\":\"ava_tool_compat\",\"type\":\"tool.result\",\"ok\":true,\"content\":\"compatible\",\"metadata\":{}}\n" << std::flush;
  while (std::getline(std::cin, request))
  {
  }
  return 0;
}
