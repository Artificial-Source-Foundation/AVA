#pragma once

#include "ava/debug/print_members_on.h"
#include "ava/core/result.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>

namespace ava::core {
class AnchorSet;
}

namespace ava::process {

namespace detail {
class ExecutionCapabilityAccess;
struct ExecutionCapabilityState;
}  // namespace detail

// Ingress-only process-neutral identity supplied by a higher-level authority.
// A factory that receives this value compares every field exactly.
struct ExpectedFileIdentityV1
{
  std::uint64_t uid = 0;
  std::uint64_t gid = 0;
  std::uint64_t mode = 0;
  std::uint64_t nlink = 0;
  std::uint64_t dev = 0;
  std::uint64_t inode = 0;
  std::uint64_t size = 0;
  std::int64_t ctime_sec = 0;
  std::int64_t ctime_nsec = 0;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

// Opaque retained descriptor authority for one exact executable. Native
// handles, logical spelling, and observed identity are deliberately private.
class PreopenedExecutableV1
{
 public:
  PreopenedExecutableV1() noexcept;
  PreopenedExecutableV1(PreopenedExecutableV1 const&) = delete;
  PreopenedExecutableV1& operator=(PreopenedExecutableV1 const&) = delete;
  PreopenedExecutableV1(PreopenedExecutableV1&&) noexcept;
  PreopenedExecutableV1& operator=(PreopenedExecutableV1&&) noexcept;
  ~PreopenedExecutableV1();

  [[nodiscard]] bool valid() const noexcept;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT

 private:
  explicit PreopenedExecutableV1(std::unique_ptr<detail::ExecutionCapabilityState> state) noexcept;

  std::unique_ptr<detail::ExecutionCapabilityState> state_;

  friend class detail::ExecutionCapabilityAccess;
  friend ava::core::Result<PreopenedExecutableV1> mint_preopened_executable(std::shared_ptr<ava::core::AnchorSet const>, std::filesystem::path const&,
                                                                            std::optional<ExpectedFileIdentityV1>);
};

// Opaque retained descriptor authority for one exact working directory.
class AnchoredWorkingDirectoryV1
{
 public:
  AnchoredWorkingDirectoryV1() noexcept;
  AnchoredWorkingDirectoryV1(AnchoredWorkingDirectoryV1 const&) = delete;
  AnchoredWorkingDirectoryV1& operator=(AnchoredWorkingDirectoryV1 const&) = delete;
  AnchoredWorkingDirectoryV1(AnchoredWorkingDirectoryV1&&) noexcept;
  AnchoredWorkingDirectoryV1& operator=(AnchoredWorkingDirectoryV1&&) noexcept;
  ~AnchoredWorkingDirectoryV1();

  [[nodiscard]] bool valid() const noexcept;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT

 private:
  explicit AnchoredWorkingDirectoryV1(std::unique_ptr<detail::ExecutionCapabilityState> state) noexcept;

  std::unique_ptr<detail::ExecutionCapabilityState> state_;

  friend class detail::ExecutionCapabilityAccess;
  friend ava::core::Result<AnchoredWorkingDirectoryV1> mint_anchored_working_directory(std::shared_ptr<ava::core::AnchorSet const>,
                                                                                       std::filesystem::path const&, std::optional<ExpectedFileIdentityV1>);
};

[[nodiscard]] ava::core::Result<PreopenedExecutableV1> mint_preopened_executable(std::shared_ptr<ava::core::AnchorSet const> startup_anchor_set,
                                                                                 std::filesystem::path const& logical_path,
                                                                                 std::optional<ExpectedFileIdentityV1> expected_identity = std::nullopt);

[[nodiscard]] ava::core::Result<AnchoredWorkingDirectoryV1> mint_anchored_working_directory(
    std::shared_ptr<ava::core::AnchorSet const> startup_anchor_set, std::filesystem::path const& logical_path,
    std::optional<ExpectedFileIdentityV1> expected_identity = std::nullopt);

}  // namespace ava::process
