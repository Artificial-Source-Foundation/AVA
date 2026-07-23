#include "sys.h"
#include "ava/core/trusted_home.h"
#ifdef CWDEBUG
#include "utils/AtomicFuzzyBool.h"
#endif

#include <cstdlib>
#include <cstring>
#include <string_view>
#include <vector>
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>

namespace ava::core {
namespace {

// The frozen flag exists only in debug builds, as requested: it is the
// one-time gate that makes resolve_trusted_account() assert HOME is not read
// again after startup initialization.
#ifdef CWDEBUG
// Transitions *once* from WasFalse to True: once True it stays True.
utils::AtomicFuzzyBool g_frozen(fuzzy::WasFalse);
#endif

// The resolved account cached for cached_trusted_account(). Populated by
// resolve_trusted_account() and then read-only for the rest of the process, so
// the trusted home is derived once and reused by every later command planner.
std::optional<TrustedAccount> g_cache;

// Read the trusted account directly from the environment and passwd database.
// Mirrors ava::config::home_dir(): prefer HOME, fall back to getpwuid_r only
// when HOME is unset or not absolute. The account name always comes from the
// passwd entry so the home path and USER/LOGNAME describe the same account.
Result<TrustedAccount> read_trusted_account_from_env()
{
  std::filesystem::path home;
  // The account name is always taken from the passwd database; HOME does not
  // carry a trustworthy user name.
  long const suggested = ::sysconf(_SC_GETPW_R_SIZE_MAX);
  std::vector<char> storage(static_cast<std::size_t>(suggested > 0 ? suggested : 16 * 1024));
  passwd record{};
  passwd* resolved = nullptr;
  int const status = ::getpwuid_r(::geteuid(), &record, storage.data(), storage.size(), &resolved);

  char const* const home_env = ::getenv("HOME");
  if (home_env != nullptr && home_env[0] != '\0')
  {
    auto home_path = std::filesystem::path(home_env).lexically_normal();
    if (home_path.is_absolute())
      home = std::move(home_path);
  }
  if (home.empty() && status == 0 && resolved != nullptr && resolved->pw_dir != nullptr && resolved->pw_dir[0] != '\0')
  {
    auto passwd_home = std::filesystem::path(resolved->pw_dir).lexically_normal();
    if (passwd_home.is_absolute())
      home = std::move(passwd_home);
  }

  if (home.empty())
    return std::unexpected(Error(ErrorCategory::Io, "failed to resolve the trusted home directory from HOME or the passwd database"));

  if (status != 0 || resolved == nullptr || !resolved->pw_name || resolved->pw_name[0] == '\0')
  {
    auto error = Error(ErrorCategory::Io, "failed to discover the trusted local account for command planning");
    if (status != 0)
      error.with_context("cause", std::strerror(status));
    return std::unexpected(std::move(error));
  }
  return TrustedAccount{.home = std::move(home), .user = std::string(resolved->pw_name)};
}

}  // namespace

Result<TrustedAccount> resolve_trusted_account()
{
#ifdef CWDEBUG
  // HOME may only be read once, before freeze_trusted_account(). This assertion
  // automatically enforces the one-time read invariant in debug builds.
  ASSERT(g_frozen.load(std::memory_order::relaxed) == utils::fuzzy_true);
#endif
  auto account = read_trusted_account_from_env();
  if (!account)
    return std::unexpected(std::move(account.error()));
  g_cache = *account;
  return *account;
}

std::optional<TrustedAccount> cached_trusted_account()
{
  return g_cache;
}

void freeze_trusted_account()
{
#ifdef CWDEBUG
  g_frozen.store(fuzzy::True, std::memory_order::relaxed);
#endif
}

}  // namespace ava::core
