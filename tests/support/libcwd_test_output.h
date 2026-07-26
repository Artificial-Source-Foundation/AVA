#pragma once

#include <memory>
#include <string>
#include <string_view>
#include "debug.h"

namespace ava::test {

// Routes ava_tests libcwd diagnostics to a per-suite log file when
// AVA_DEBUG_OUTPUT_DIR is configured, so parallel ctest invocations do not
// interleave on a shared stream. In libcwd-disabled builds this class is an
// inert compatibility shim.
class LibcwdTestOutput final
{
 public:
  explicit LibcwdTestOutput(std::string_view suite_token);
  ~LibcwdTestOutput();

  LibcwdTestOutput(LibcwdTestOutput const&) = delete;
  LibcwdTestOutput& operator=(LibcwdTestOutput const&) = delete;
  LibcwdTestOutput(LibcwdTestOutput&&) = delete;
  LibcwdTestOutput& operator=(LibcwdTestOutput&&) = delete;

  // False (with setup_error() filled) when AVA_DEBUG_OUTPUT_DIR was set but the
  // directory or per-suite log file could not be opened safely. True otherwise,
  // including when AVA_DEBUG_OUTPUT_DIR is not set at all.
  [[nodiscard]] bool setup_succeeded() const noexcept;
  [[nodiscard]] std::string const& setup_error() const noexcept;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace ava::test
