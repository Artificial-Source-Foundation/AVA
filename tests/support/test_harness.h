#pragma once

#include "ava/tools/file_tools.h"
#include "ava/session/session_store.h"
#include "ava/core/error.h"

#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace ava::test {
int& failure_count();
int failures();
void clear_skip();
void request_skip(std::string message);
bool skip_requested();
std::string skip_message();
}  // namespace ava::test

void expect(bool condition, std::string const& message);

class FailingStreambuf final : public std::streambuf
{
 protected:
  int overflow(int ch) override;
  std::streamsize xsputn(char const* s, std::streamsize count) override;
};

std::filesystem::path temp_root();

class ScopedEnvVar
{
 public:
  ScopedEnvVar(std::string name, std::string value);

  ScopedEnvVar(ScopedEnvVar const&) = delete;
  ScopedEnvVar& operator=(ScopedEnvVar const&) = delete;
  ScopedEnvVar(ScopedEnvVar&&) = delete;
  ScopedEnvVar& operator=(ScopedEnvVar&&) = delete;

  ~ScopedEnvVar();

 private:
  std::string name_;
  std::optional<std::string> previous_ = std::nullopt;
};

std::string strip_sgr(std::string_view text);
bool has_active_sgr_at_text(std::string_view line, std::string_view text, std::string_view sgr);
ava::core::VoidResult append_permission_audit_for_test(ava::session::SessionStore& store, ava::tools::PermissionAuditEvent const& event);
std::vector<ava::session::SessionEntry> permission_entries(std::vector<ava::session::SessionEntry> const& entries);
std::size_t visible_columns(std::string_view text);
