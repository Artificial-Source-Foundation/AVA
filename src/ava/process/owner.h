#pragma once

#include "ava/debug/print_members_on.h"
#include "ava/core/result.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace ava::process {

class Supervisor;

// A generated-only version-1 owner hierarchy. Callers can derive children but
// cannot inject provider, protocol, session-store, or other external IDs.
class OwnerPathV1
{
 public:
  [[nodiscard]] static ava::core::Result<OwnerPathV1> application();

  [[nodiscard]] ava::core::Result<OwnerPathV1> session() const;
  [[nodiscard]] ava::core::Result<OwnerPathV1> run() const;
  [[nodiscard]] ava::core::Result<OwnerPathV1> operation() const;

  [[nodiscard]] bool is_valid_prefix() const noexcept;
  [[nodiscard]] bool is_launch_owner() const noexcept;
  [[nodiscard]] bool matches_prefix(OwnerPathV1 const& prefix) const noexcept;
  [[nodiscard]] std::size_t depth() const noexcept;
  [[nodiscard]] std::size_t encoded_size() const noexcept;
  [[nodiscard]] std::uint32_t schema_version() const noexcept;

  // Raw generated owner segments are intentionally excluded from debug output.
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT

 private:
  enum class Segment : std::size_t
  {
    Application = 0,
    Session = 1,
    Run = 2,
    Operation = 3,
  };

  explicit OwnerPathV1(std::array<std::optional<std::string>, 4> segments) noexcept;

  [[nodiscard]] ava::core::Result<OwnerPathV1> append(Segment segment) const;
  [[nodiscard]] std::string key() const;

  std::uint32_t schema_version_ = 1;
  std::array<std::optional<std::string>, 4> segments_;

  friend class Supervisor;
};

}  // namespace ava::process
