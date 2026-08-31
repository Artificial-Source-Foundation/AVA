#include "sys.h"
#include "ava/process/scope.h"
#include "ava/process/supervisor.h"
#include "ava/core/error.h"

#include <exception>
#include <memory>
#include <ostream>
#include <string>
#include <utility>

namespace ava::process {
namespace {

ava::core::Error scope_error(std::string message)
{
  return ava::core::Error(ava::core::ErrorCategory::InvalidArgument, std::move(message));
}

}  // namespace

ProcessScopeV1::ProcessScopeV1(std::shared_ptr<Supervisor> supervisor, OwnerPathV1 application_owner, OwnerPathV1 owner_prefix) noexcept
    : supervisor_(std::move(supervisor)), application_owner_(std::move(application_owner)), owner_prefix_(std::move(owner_prefix))
{
}

ava::core::Result<ProcessScopeV1> ProcessScopeV1::application(std::shared_ptr<Supervisor> supervisor)
{
  if (!supervisor)
    return std::unexpected(scope_error("application process scope requires a non-null supervisor"));

  auto owner = OwnerPathV1::application();
  if (!owner)
    return std::unexpected(std::move(owner.error()));

  try
  {
    auto application_owner = *owner;
    return ProcessScopeV1(std::move(supervisor), std::move(application_owner), std::move(*owner));
  }
  catch (std::exception const& error)
  {
    return std::unexpected(scope_error("failed to retain the generated application process owner").with_context("cause", error.what()));
  }
  catch (...)
  {
    return std::unexpected(scope_error("failed to retain the generated application process owner"));
  }
}

ava::core::VoidResult ProcessScopeV1::validate() const
{
  if (!supervisor_)
    return std::unexpected(scope_error("cannot derive from a process scope without a supervisor"));
  if (!application_owner_.is_valid_prefix() || application_owner_.depth() != 1)
    return std::unexpected(scope_error("cannot derive from a process scope with an invalid application owner"));
  if (!owner_prefix_.is_valid_prefix() || !owner_prefix_.matches_prefix(application_owner_))
    return std::unexpected(scope_error("cannot derive from a process scope with an invalid current owner prefix"));
  return {};
}

ava::core::Result<ProcessScopeV1> ProcessScopeV1::make_derived(ava::core::Result<OwnerPathV1> owner) const
{
  if (!owner)
    return std::unexpected(std::move(owner.error()));
  try
  {
    return ProcessScopeV1(supervisor_, application_owner_, std::move(*owner));
  }
  catch (std::exception const& error)
  {
    return std::unexpected(scope_error("failed to retain a derived process scope").with_context("cause", error.what()));
  }
  catch (...)
  {
    return std::unexpected(scope_error("failed to retain a derived process scope"));
  }
}

ava::core::Result<ProcessScopeV1> ProcessScopeV1::session() const
{
  if (auto valid = validate(); !valid)
    return std::unexpected(std::move(valid.error()));
  return make_derived(owner_prefix_.session());
}

ava::core::Result<ProcessScopeV1> ProcessScopeV1::run() const
{
  if (auto valid = validate(); !valid)
    return std::unexpected(std::move(valid.error()));
  return make_derived(owner_prefix_.run());
}

ava::core::Result<ProcessScopeV1> ProcessScopeV1::operation() const
{
  if (auto valid = validate(); !valid)
    return std::unexpected(std::move(valid.error()));
  return make_derived(owner_prefix_.operation());
}

ProcessScopeV1 ProcessScopeV1::application_scope() const
{
  return ProcessScopeV1(supervisor_, application_owner_, application_owner_);
}

Supervisor& ProcessScopeV1::supervisor() const noexcept
{
  return *supervisor_;
}

OwnerPathV1 const& ProcessScopeV1::owner_prefix() const noexcept
{
  return owner_prefix_;
}

#ifdef CWDEBUG
std::ostream& operator<<(std::ostream& output, ProcessScopeV1 const&)
{
  return output.write("$process_scope$", 15);
}
#endif

}  // namespace ava::process
