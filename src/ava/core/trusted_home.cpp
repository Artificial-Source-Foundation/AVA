#include "sys.h"
#include "ava/core/trusted_home.h"
#include "utils/AtomicFuzzyBool.h"

#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string_view>
#include <vector>
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>
#include "debug.h"      // ASSERT

namespace ava::core {
namespace {

// The frozen flag exists only in debug builds, as requested: it is the
// one-time gate that makes resolve_trusted_account() assert HOME is not read
// again after startup initialization.
//
// Transitions *once* from WasFalse to True: once True it stays True.
utils::AtomicFuzzyBool g_account_frozen(fuzzy::WasFalse);

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
  DoutEntering(dc::core|continued_cf, "resolve_trusted_account() -> ");

  auto account = read_trusted_account_from_env();
  if (!account)
  {
    Dout(dc::finish, account.error());
    return std::unexpected(std::move(account.error()));
  }
  g_cache = *account;

  // The environment variable HOME should *only* be read by `read_trusted_account_from_env`
  // and that function may only be called *once*. Therefore this function may only be
  // called once (which is currently done from `construct_runtime_session`?!)
  //
  // Afterwards `ava::core::freeze_trusted_account` is called, signifying that all early
  // one-time initialization have been completed.
  //
  // Hence, we get here - the boolean g_account_frozen must still be (or have been) false at
  // the moment of testing *after* calling read_trusted_account_from_env.
  ASSERT(g_account_frozen.is_momentary_false(std::memory_order::relaxed));

  Dout(dc::finish, *account);
  return *account;
}

TrustedAccount const& cached_trusted_account()
{
  // This function should only be called after `resolve_trusted_account` and, subsequently,
  // `ava::core::freeze_trusted_account` were already called.
  ASSERT(g_account_frozen.is_true(std::memory_order::acquire));
  // If g_account_frozen is true with memory order acquire then this must be true as well.
  ASSERT(g_cache.has_value());

  return g_cache.value();
}

void freeze_trusted_account()
{
  DoutEntering(dc::core, "freeze_trusted_account()");

  // Transition g_account_frozen from WasFalse -> True.
  g_account_frozen.store(fuzzy::True, std::memory_order::release);
}

bool was_not_frozen()
{
  // If the fuzzy bool is still transitory false then a moment ago freeze_trusted_account
  // wasn't called yet and therefore it is very likely that resolve_trusted_account wasn't
  // called yet.
  return g_account_frozen.is_transitory_false();
}

ava::core::VoidResult load_account_once_and_freeze()
{
  // No-op if already frozen.
  if (ava::core::was_not_frozen())
  {
    // Transitory false means that g_account_frozen might have become True in the meantime.
    // In other words, another thread might have entered here before us. But only one thread
    // is allowed to call ava::core::resolve_trusted_account.
    static std::mutex m;        // This works because this is the ONLY place where ava::core::resolve_trusted_account is called.
    std::lock_guard lock(m);
    // If now g_account_frozen is still false then we are truly the first thread because
    // inside this critial area the WasFalse can not transition to True due to another thread.
    if (ava::core::was_not_frozen())
    {
      auto resolved = ava::core::resolve_trusted_account();
      if (!resolved)
        return std::unexpected(std::move(resolved.error()));
      ava::core::freeze_trusted_account();
    }
  }
  return {};
}

}  // namespace ava::core
