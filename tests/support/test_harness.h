#pragma once

#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "ava/core/error.h"
#include "ava/session/session_store.h"
#include "ava/tools/file_tools.h"

namespace ava::test {
int& failure_count();
int failures();
}  // namespace ava::test

void expect(bool condition, const std::string& message);

class FailingStreambuf final : public std::streambuf {
 protected:
  int overflow(int ch) override;
  std::streamsize xsputn(const char* s, std::streamsize count) override;
};

std::filesystem::path temp_root();

class ScopedEnvVar {
 public:
  ScopedEnvVar(std::string name, std::string value);

  ScopedEnvVar(const ScopedEnvVar&) = delete;
  ScopedEnvVar& operator=(const ScopedEnvVar&) = delete;
  ScopedEnvVar(ScopedEnvVar&&) = delete;
  ScopedEnvVar& operator=(ScopedEnvVar&&) = delete;

  ~ScopedEnvVar();

 private:
  std::string name_;
  std::optional<std::string> previous_ = std::nullopt;
};

std::string strip_sgr(std::string_view text);
bool has_active_sgr_at_text(std::string_view line, std::string_view text, std::string_view sgr);
ava::core::VoidResult append_permission_audit_for_test(ava::session::SessionStore& store,
                                                       const ava::tools::PermissionAuditEvent& event);
std::vector<ava::session::SessionEntry> permission_entries(const std::vector<ava::session::SessionEntry>& entries);
std::size_t visible_columns(std::string_view text);
