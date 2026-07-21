#pragma once

#include <sys/stat.h>

namespace ava::command::detail {

// Returns true only when status names a non-root current-user-owned directory
// whose gid is the account's verified private primary group. Verification uses
// bounded, descriptor-local /etc/passwd and /etc/group enumeration and fails
// closed for NSS-only or ambiguous identities.
[[nodiscard]] bool is_current_user_private_primary_group_directory(struct stat const& status) noexcept;

}  // namespace ava::command::detail
