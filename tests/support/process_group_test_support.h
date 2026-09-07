#pragma once

#include <cerrno>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <signal.h>
#include <sys/types.h>

namespace ava::test {

inline bool process_group_exists(pid_t pgid)
{
  errno = 0;
  if (::kill(-pgid, 0) == 0)
    return true;
  return errno != ESRCH;
}

// kill(-pgid, 0) also reports orphaned zombies that only an external reaper can
// collect. Teardown guarantees that no descendant can execute, so inspect that
// property directly when procfs is available.
inline std::optional<bool> process_group_has_non_zombie_member(pid_t pgid)
{
  std::error_code error;
  std::filesystem::directory_iterator entry("/proc", error);
  if (error)
    return std::nullopt;

  for (std::filesystem::directory_iterator end; entry != end; entry.increment(error))
  {
    if (error)
      return std::nullopt;
    auto const name = entry->path().filename().string();
    if (name.empty() || name.find_first_not_of("0123456789") != std::string::npos)
      continue;

    std::ifstream stat_file(entry->path() / "stat", std::ios::binary);
    std::string stat;
    std::getline(stat_file, stat);
    auto const command_end = stat.rfind(") ");
    if (!stat_file || command_end == std::string::npos)
      continue;

    std::istringstream fields(stat.substr(command_end + 2));
    char state = '\0';
    long long parent_pid = 0;
    long long process_group = 0;
    fields >> state >> parent_pid >> process_group;
    if (fields && process_group == pgid && state != 'Z' && state != 'X')
      return true;
  }
  return error ? std::nullopt : std::optional<bool>{false};
}

inline bool process_group_has_live_member(pid_t pgid)
{
  if (auto const has_non_zombie = process_group_has_non_zombie_member(pgid))
    return *has_non_zombie;
  return process_group_exists(pgid);
}

inline bool wait_for_process_group_exit(pid_t pgid)
{
  for (int index = 0; index < 100; ++index)
  {
    if (!process_group_has_live_member(pgid))
      return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return !process_group_has_live_member(pgid);
}

}  // namespace ava::test
