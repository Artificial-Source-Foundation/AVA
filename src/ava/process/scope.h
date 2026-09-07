#pragma once

#include "ava/debug/print_members_on.h"
#include "ava/process/environment.h"
#include "ava/process/owner.h"
#include "ava/core/result.h"

#include <iosfwd>
#include <memory>

namespace ava::process {

class Supervisor;

// Copyable application-lifetime process authority. Application construction
// captures one bounded host projection; scope derivation shares that capture,
// generates only bounded owner identities, and starts no process or thread.
class ProcessScopeV1 final
{
 public:
  [[nodiscard]] static ava::core::Result<ProcessScopeV1> application(std::shared_ptr<Supervisor> supervisor);

  [[nodiscard]] ava::core::Result<ProcessScopeV1> session() const;
  [[nodiscard]] ava::core::Result<ProcessScopeV1> run() const;
  [[nodiscard]] ava::core::Result<ProcessScopeV1> operation() const;

  // Recover the original application authority without generating a new owner.
  [[nodiscard]] ProcessScopeV1 application_scope() const;

  // Trusted internal callers receive only the launch authority and current
  // generated owner prefix. The shared ownership itself remains encapsulated.
  [[nodiscard]] Supervisor& supervisor() const noexcept;
  [[nodiscard]] OwnerPathV1 const& owner_prefix() const noexcept;
  [[nodiscard]] HostEnvironmentV1 const& host_environment() const noexcept;

  // This type contains process-launch authority and raw generated owner IDs.
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT

 private:
  ProcessScopeV1(std::shared_ptr<Supervisor> supervisor, std::shared_ptr<HostEnvironmentV1 const> host_environment, OwnerPathV1 application_owner,
                 OwnerPathV1 owner_prefix) noexcept;

  [[nodiscard]] ava::core::VoidResult validate() const;
  [[nodiscard]] ava::core::Result<ProcessScopeV1> make_derived(ava::core::Result<OwnerPathV1> owner) const;

  // Shared ownership keeps one composition-root Supervisor alive for every
  // application/session/run/operation scope derived from it.
  std::shared_ptr<Supervisor> supervisor_;
  std::shared_ptr<HostEnvironmentV1 const> host_environment_;
  OwnerPathV1 application_owner_;
  OwnerPathV1 owner_prefix_;
};

#ifdef CWDEBUG
// Containing debug aggregates may emit only this fixed token; generated member
// printing remains disabled so authority and owner identity cannot escape.
std::ostream& operator<<(std::ostream& output, ProcessScopeV1 const& scope);
#endif

}  // namespace ava::process
