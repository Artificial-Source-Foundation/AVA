#pragma once

#include "ava/debug/print_members_on.h"

#include <string>
#include <utility>
#include <vector>

namespace ava::core {

enum class ErrorCategory
{
  InvalidArgument,
  Io,
  NotFound,
  PermissionDenied,
  Provider,
  Session,
  Tool,
  Unknown,
};

struct ErrorContext
{
  std::string key;
  std::string value;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

class Error
{
 public:
  Error(ErrorCategory category, std::string message);

  [[nodiscard]] ErrorCategory category() const noexcept;
  [[nodiscard]] std::string const& message() const noexcept;
  [[nodiscard]] std::vector<ErrorContext> const& context() const noexcept;
  [[nodiscard]] std::string format() const;

  Error& with_context(std::string key, std::string value);

  AVA_DEBUG_PRINT_MEMBERS_ON

 private:
  ErrorCategory category_;
  std::string message_;
  std::vector<ErrorContext> context_;
};

[[nodiscard]] std::string to_string(ErrorCategory category);

}  // namespace ava::core
