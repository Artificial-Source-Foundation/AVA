#pragma once

#include <memory>
#include <string>
#include <string_view>
#include "debug.h"

namespace ava::test {

// Routes ava_tests libcwd diagnostics away from its normal process streams.
// In libcwd-disabled builds this class is an inert compatibility shim.
class LibcwdTestOutput final
{
 public:
  explicit LibcwdTestOutput(std::string_view suite_token);
  ~LibcwdTestOutput();

  LibcwdTestOutput(LibcwdTestOutput const&) = delete;
  LibcwdTestOutput& operator=(LibcwdTestOutput const&) = delete;
  LibcwdTestOutput(LibcwdTestOutput&&) = delete;
  LibcwdTestOutput& operator=(LibcwdTestOutput&&) = delete;

  // Called immediately after NAMESPACE_DEBUG::init(). Disabled and failed
  // configurations balance the on-state established by libcwd initialization.
  void after_libcwd_init();

  [[nodiscard]] bool enabled() const noexcept;
  [[nodiscard]] bool setup_succeeded() const noexcept;
  [[nodiscard]] std::string const& setup_error() const noexcept;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace ava::test
