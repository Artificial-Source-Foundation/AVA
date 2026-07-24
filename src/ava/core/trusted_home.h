#pragma once

#include "ava/core/result.h"
#include "ava/debug/print_members_on.h"

#include <filesystem>
#include <optional>
#include <string>

namespace ava::core {

// The trusted local account: a home directory and account name resolved once
// at startup. The home directory is read from the HOME environment variable
// (falling back to the passwd database only when HOME is unset or not an
// absolute path), using the same precedence as ava::config::home_dir(). The
// account name always comes from the passwd database (getpwuid_r), because the
// trusted-home path and the command USER/LOGNAME must describe one real local
// account and the USER environment variable is not authoritative for that.
struct TrustedAccount
{
  std::filesystem::path home;
  std::string user;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

// Resolve the trusted account by reading HOME (passwd fallback).
//
// This is the single function that reads the HOME environment variable. It may
// only be called before freeze_trusted_account(): in debug builds it asserts
// that the one-time freeze has not yet happened, so the sensitive HOME value is
// never re-read after startup initialization. The resolved account is also
// stored for cached_trusted_account(); after the freeze, callers must use that
// cache instead of calling this function again.
[[nodiscard]] Result<TrustedAccount> resolve_trusted_account();

// Return the account resolved by a prior resolve_trusted_account() call, or
// nullopt when no resolution has happened yet.
//
// Always safe to call: it never reads HOME and never trips the freeze
// assertion, so it is the correct accessor for every post-startup caller
// (including the bash command planner).
[[nodiscard]] TrustedAccount const& cached_trusted_account();

// Freeze trusted-home resolution.
//
// Debug builds only: after this, any call to resolve_trusted_account() trips an
// assertion, automatically enforcing that HOME is read at most once. Called once
// after the runtime session has performed the one-time startup initialization.
void freeze_trusted_account();

// Return the state of `g_account_frozen`, a fuzzy boolean that goes from WasFalse -> True
// exactly once (when freeze_trusted_account is called the first time). This returns
// true iff `g_account_frozen` WasFalse (a few microseconds ago).
bool was_not_frozen();

// Called for every session from construct_runtime_session.
ava::core::VoidResult load_account_once_and_freeze();

}  // namespace ava::core
