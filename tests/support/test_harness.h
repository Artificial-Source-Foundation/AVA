#pragma once

#include "ava/tools/file_tools.h"
#include "ava/session/compaction.h"
#include "ava/session/session_metadata.h"
#include "ava/session/session_store.h"
#include "ava/core/error.h"

#include <filesystem>
#include <functional>
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
// Test-only authority adapter. Persistent test fixtures acquire the exact
// lease for the duration of one append; runtime tests instead use owner routes.
ava::core::VoidResult append_session_entry_for_test(ava::session::SessionStore& store, ava::session::SessionEntry const& entry);
std::function<ava::core::VoidResult(ava::session::SessionEntry const&)> append_route_for_test(ava::session::SessionStore const& store);
ava::session::SessionReadAuthority read_authority_for_test(ava::session::SessionStore const& store);
ava::core::Result<ava::session::SessionMetadataView> append_session_metadata_for_test(ava::session::SessionStore& store,
                                                                                      ava::session::SessionMetadataUpdate update);
ava::core::VoidResult append_manual_compaction_for_test(ava::session::SessionStore& store, ava::session::ManualCompactionRequest request);
ava::core::VoidResult append_permission_audit_for_test(ava::session::SessionStore& store, ava::tools::PermissionAuditEvent const& event);
std::vector<ava::session::SessionEntry> permission_entries(std::vector<ava::session::SessionEntry> const& entries);
std::size_t visible_columns(std::string_view text);
