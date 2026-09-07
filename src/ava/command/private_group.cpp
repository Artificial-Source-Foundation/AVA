#include "sys.h"
#include "ava/command/private_group.h"

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string_view>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ava::command::detail {
namespace {

// The private-primary-group exception is deliberately local-files-only. NSS
// group membership is not exhaustive: gr_mem omits accounts that use a group
// as their primary gid. Dedicated descriptors avoid the process-global passwd
// and group enumeration cursors while fixed buffers/record limits fail closed.
#ifndef __APPLE__
constexpr std::size_t kAccountRecordBytes = 16 * 1024;
constexpr std::size_t kMaxAccountRecords = 16 * 1024;
constexpr std::size_t kMaxGroupMembers = 1024;
#endif
constexpr std::size_t kMaxUsernameBytes = 256;

using FilePtr = std::unique_ptr<FILE, int (*)(FILE*)>;

#ifndef __APPLE__
FilePtr open_trusted_account_file(char const* path) noexcept
{
  int const fd = ::open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
  if (fd < 0)
    return FilePtr(nullptr, &::fclose);

  struct stat status{};
  if (::fstat(fd, &status) != 0 || !S_ISREG(status.st_mode) || status.st_uid != 0 || (status.st_mode & (S_IWGRP | S_IWOTH)) != 0 || status.st_nlink != 1)
  {
    ::close(fd);
    return FilePtr(nullptr, &::fclose);
  }

  FILE* file = ::fdopen(fd, "r");
  if (!file)
  {
    ::close(fd);
    return FilePtr(nullptr, &::fclose);
  }
  return FilePtr(file, &::fclose);
}

bool copy_bounded_username(char const* value, std::array<char, kMaxUsernameBytes>& output) noexcept
{
  if (!value)
    return false;
  auto const size = ::strnlen(value, output.size());
  if (size == 0 || size >= output.size())
    return false;
  std::memcpy(output.data(), value, size);
  output[size] = '\0';
  return true;
}
#endif

bool local_passwd_has_only_current_primary_member(uid_t user_id, gid_t group_id, std::array<char, kMaxUsernameBytes>& username) noexcept
{
#ifdef __APPLE__
  // macOS provides fgetpwent_r(3)/fgetgrent_r(3) neither as API nor as a
  // convention: interactive users live in OpenDirectory rather than
  // /etc/passwd, and there is no user-private-group scheme for which this
  // exception would be meaningful. Fail closed so the exception never applies
  // on Apple platforms (the stricter permission path is taken instead).
  (void)user_id;
  (void)group_id;
  (void)username;
  return false;
#else
  auto file = open_trusted_account_file("/etc/passwd");
  if (!file)
    return false;

  std::array<char, kAccountRecordBytes> storage{};
  std::size_t matching_records = 0;
  for (std::size_t index = 0; index <= kMaxAccountRecords; ++index)
  {
    passwd record{};
    passwd* resolved = nullptr;
    int const result = ::fgetpwent_r(file.get(), &record, storage.data(), storage.size(), &resolved);
    if (!resolved)
    {
      if (result != 0 && result != ENOENT)
        return false;
      return matching_records == 1;
    }
    if (result != 0 || index == kMaxAccountRecords)
      return false;
    if (record.pw_gid != group_id)
      continue;
    ++matching_records;
    if (matching_records != 1 || record.pw_uid != user_id || !copy_bounded_username(record.pw_name, username))
      return false;
  }
  return false;
#endif
}

bool local_group_is_named_private_primary_group(gid_t group_id, std::string_view username) noexcept
{
#ifdef __APPLE__
  // See local_passwd_has_only_current_primary_member: no fgetgrent_r(3) and no
  // user-private-group convention on macOS; fail closed.
  (void)group_id;
  (void)username;
  return false;
#else
  auto file = open_trusted_account_file("/etc/group");
  if (!file)
    return false;

  std::array<char, kAccountRecordBytes> storage{};
  std::size_t matching_records = 0;
  for (std::size_t index = 0; index <= kMaxAccountRecords; ++index)
  {
    group record{};
    group* resolved = nullptr;
    int const result = ::fgetgrent_r(file.get(), &record, storage.data(), storage.size(), &resolved);
    if (!resolved)
    {
      if (result != 0 && result != ENOENT)
        return false;
      return matching_records == 1;
    }
    if (result != 0 || index == kMaxAccountRecords)
      return false;
    if (record.gr_gid != group_id)
      continue;
    ++matching_records;
    if (matching_records != 1 || !record.gr_name || std::string_view(record.gr_name) != username)
      return false;
    if (record.gr_mem)
    {
      for (std::size_t member_index = 0; member_index <= kMaxGroupMembers; ++member_index)
      {
        auto const* member = record.gr_mem[member_index];
        if (!member)
          break;
        if (member_index == kMaxGroupMembers || std::string_view(member) != username)
          return false;
      }
    }
  }
  return false;
#endif
}

}  // namespace

bool is_current_user_private_primary_group_directory(struct stat const& status) noexcept
{
  if (!S_ISDIR(status.st_mode) || status.st_uid == 0 || status.st_uid != ::geteuid() || (status.st_mode & S_IWOTH) != 0)
    return false;

  std::array<char, kMaxUsernameBytes> username{};
  return local_passwd_has_only_current_primary_member(status.st_uid, status.st_gid, username) &&
         local_group_is_named_private_primary_group(status.st_gid, username.data());
}

}  // namespace ava::command::detail
