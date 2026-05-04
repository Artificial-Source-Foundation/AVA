#include "tests/support/test_harness.h"

#include <sys/stat.h>
#include <unistd.h>

#include <climits>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>

#include "ava/core/ids.h"

namespace ava::test {
namespace {
int failures_value = 0;
}

int& failure_count()
{
  return failures_value;
}

int failures()
{
  return failures_value;
}
}  // namespace ava::test

namespace {

bool is_zero_width_codepoint(char32_t codepoint)
{
  return (codepoint >= 0x0300 && codepoint <= 0x036F) || (codepoint >= 0x0483 && codepoint <= 0x0489) ||
         (codepoint >= 0x0591 && codepoint <= 0x05BD) || codepoint == 0x05BF ||
         (codepoint >= 0x05C1 && codepoint <= 0x05C2) || (codepoint >= 0x05C4 && codepoint <= 0x05C5) ||
         codepoint == 0x05C7 || (codepoint >= 0x0610 && codepoint <= 0x061A) ||
         (codepoint >= 0x064B && codepoint <= 0x065F) || codepoint == 0x0670 ||
         (codepoint >= 0x06D6 && codepoint <= 0x06DC) || (codepoint >= 0x06DF && codepoint <= 0x06E4) ||
         (codepoint >= 0x06E7 && codepoint <= 0x06E8) || (codepoint >= 0x06EA && codepoint <= 0x06ED) ||
         codepoint == 0x0711 || (codepoint >= 0x0730 && codepoint <= 0x074A) ||
         (codepoint >= 0x07A6 && codepoint <= 0x07B0) || (codepoint >= 0x07EB && codepoint <= 0x07F3) ||
         (codepoint >= 0x0816 && codepoint <= 0x0819) || (codepoint >= 0x081B && codepoint <= 0x0823) ||
         (codepoint >= 0x0825 && codepoint <= 0x0827) || (codepoint >= 0x0829 && codepoint <= 0x082D) ||
         (codepoint >= 0x0859 && codepoint <= 0x085B) || (codepoint >= 0x08D3 && codepoint <= 0x08E1) ||
         (codepoint >= 0x08E3 && codepoint <= 0x0903) || (codepoint >= 0x093A && codepoint <= 0x093C) ||
         (codepoint >= 0x0941 && codepoint <= 0x0948) || codepoint == 0x094D ||
         (codepoint >= 0x0951 && codepoint <= 0x0957) || (codepoint >= 0x0962 && codepoint <= 0x0963) ||
         codepoint == 0x200C || codepoint == 0x200D || (codepoint >= 0x20D0 && codepoint <= 0x20FF) ||
         (codepoint >= 0xFE00 && codepoint <= 0xFE0F) || (codepoint >= 0xFE20 && codepoint <= 0xFE2F) ||
         (codepoint >= 0xE0100 && codepoint <= 0xE01EF);
}

}  // namespace

void expect(bool condition, std::string const& message)
{
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++ava::test::failure_count();
  }
}

int FailingStreambuf::overflow(int ch)
{
  static_cast<void>(ch);
  return traits_type::eof();
}

std::streamsize FailingStreambuf::xsputn(char const* s, std::streamsize count)
{
  static_cast<void>(s);
  static_cast<void>(count);
  return 0;
}

std::filesystem::path temp_root()
{
  auto const build_name = std::filesystem::current_path().filename();
  return std::filesystem::temp_directory_path() / ("ava_core_tests_" + build_name.string());
}

ScopedEnvVar::ScopedEnvVar(std::string name, std::string value) : name_(std::move(name))
{
  if (char const* current = std::getenv(name_.c_str())) {
    previous_ = current;
  }
  setenv(name_.c_str(), value.c_str(), 1);
}

ScopedEnvVar::~ScopedEnvVar()
{
  if (previous_) {
    setenv(name_.c_str(), previous_->c_str(), 1);
  } else {
    unsetenv(name_.c_str());
  }
}

std::string strip_sgr(std::string_view text)
{
  std::string stripped;
  stripped.reserve(text.size());
  for (std::size_t index = 0; index < text.size();) {
    if (text[index] == '\x1b' && index + 1 < text.size() && text[index + 1] == '[') {
      auto end = index + 2;
      while (end < text.size() && text[end] != 'm') {
        ++end;
      }
      if (end < text.size()) {
        index = end + 1;
        continue;
      }
    }
    stripped.push_back(text[index]);
    ++index;
  }
  return stripped;
}

bool has_active_sgr_at_text(std::string_view line, std::string_view text, std::string_view sgr)
{
  auto const text_pos = line.find(text);
  if (text_pos == std::string_view::npos) return false;
  auto const sgr_pos = line.rfind(sgr, text_pos);
  if (sgr_pos == std::string_view::npos) return false;
  auto const reset_pos = line.rfind("\x1b[0m", text_pos);
  return reset_pos == std::string_view::npos || reset_pos < sgr_pos;
}

ava::core::VoidResult append_permission_audit_for_test(ava::session::SessionStore& store,
                                                       ava::tools::PermissionAuditEvent const& event)
{
  return store.append(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                 .parent_id = "",
                                                 .type = ava::session::EntryType::PermissionDecision,
                                                 .timestamp = ava::session::now_timestamp(),
                                                 .data_json = ava::tools::permission_audit_data_json(event)});
}

std::vector<ava::session::SessionEntry> permission_entries(std::vector<ava::session::SessionEntry> const& entries)
{
  std::vector<ava::session::SessionEntry> filtered;
  for (auto const& entry : entries) {
    if (entry.type == ava::session::EntryType::PermissionDecision) filtered.push_back(entry);
  }
  return filtered;
}

std::size_t visible_columns(std::string_view text)
{
  auto const stripped = strip_sgr(text);
  std::size_t columns = 0;
  for (std::size_t index = 0; index < stripped.size();) {
    auto const byte = static_cast<unsigned char>(stripped[index]);
    char32_t codepoint = 0;
    std::size_t length = 1;
    if ((byte & 0x80U) == 0) {
      codepoint = byte;
    } else if (byte >= 0xC2U && byte <= 0xDFU) {
      codepoint = byte & 0x1FU;
      length = 2;
    } else if ((byte & 0xF0U) == 0xE0U) {
      codepoint = byte & 0x0FU;
      length = 3;
    } else if (byte >= 0xF0U && byte <= 0xF4U) {
      codepoint = byte & 0x07U;
      length = 4;
    }
    if (index + length > stripped.size()) {
      ++columns;
      break;
    }
    bool valid = length == 1;
    for (std::size_t offset = 1; offset < length; ++offset) {
      auto const continuation = static_cast<unsigned char>(stripped[index + offset]);
      valid = (continuation & 0xC0U) == 0x80U;
      if (!valid) break;
      codepoint = (codepoint << 6U) | (continuation & 0x3FU);
    }
    if (!valid) {
      ++columns;
      ++index;
      continue;
    }
    if (is_zero_width_codepoint(codepoint)) {
      index += length;
      continue;
    }
    auto const width = codepoint <= static_cast<char32_t>(WCHAR_MAX) ? ::wcwidth(static_cast<wchar_t>(codepoint)) : 1;
    auto const fallback_wide =
        (codepoint >= 0x1100 && codepoint <= 0x115F) || (codepoint >= 0x2329 && codepoint <= 0x232A) ||
        (codepoint >= 0x2E80 && codepoint <= 0xA4CF) || (codepoint >= 0xAC00 && codepoint <= 0xD7A3) ||
        (codepoint >= 0xF900 && codepoint <= 0xFAFF) || (codepoint >= 0xFE10 && codepoint <= 0xFE19) ||
        (codepoint >= 0xFE30 && codepoint <= 0xFE6F) || (codepoint >= 0xFF00 && codepoint <= 0xFF60) ||
        (codepoint >= 0xFFE0 && codepoint <= 0xFFE6) || (codepoint >= 0x1F300 && codepoint <= 0x1FAFF) ||
        (codepoint >= 0x20000 && codepoint <= 0x3FFFD);
    columns += width > 0 ? static_cast<std::size_t>(width) : (fallback_wide ? std::size_t{2} : std::size_t{1});
    index += length;
  }
  return columns;
}
