#pragma once

#include <memory>
#include <string>
#include <string_view>
#include "debug.h"

namespace ava::test {

// Routes ava_tests libcwd diagnostics away from its normal process streams.
// A configured AVA_DEBUG_OUTPUT_DIR receives a private per-suite log; otherwise
// initialization and runtime diagnostics are discarded. In libcwd-disabled
// builds this class is an inert compatibility shim.
class LibcwdTestOutput final
{
 public:
  explicit LibcwdTestOutput(std::string_view suite_token);
  ~LibcwdTestOutput();

  LibcwdTestOutput(LibcwdTestOutput const&) = delete;
  LibcwdTestOutput& operator=(LibcwdTestOutput const&) = delete;
  LibcwdTestOutput(LibcwdTestOutput&&) = delete;
  LibcwdTestOutput& operator=(LibcwdTestOutput&&) = delete;

  // Called immediately after ava::app::debug_init(). Disabled and failed
  // configurations balance any on-state established by libcwd initialization.
  void after_libcwd_init();

  [[nodiscard]] bool enabled() const noexcept;
  // False (with setup_error() filled) when AVA_DEBUG_OUTPUT_DIR was set but the
  // directory or per-suite log file could not be opened safely. True otherwise.
  [[nodiscard]] bool setup_succeeded() const noexcept;
  [[nodiscard]] std::string const& setup_error() const noexcept;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace ava::test
