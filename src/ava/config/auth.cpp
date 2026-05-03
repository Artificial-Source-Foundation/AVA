#include "ava/config/auth.h"

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "ava/core/json.h"
#include "ava/config/openai_oauth.h"

namespace ava::config {
namespace {

constexpr std::size_t max_auth_file_bytes = 1024 * 1024;
constexpr long long kOAuthRefreshSkewSeconds = 300;

std::string env_key_from_provider_id(std::string_view provider_id) {
  std::string key;
  key.reserve(provider_id.size() + std::string_view("_API_KEY").size());
  for (const char ch : provider_id) {
    const auto uch = static_cast<unsigned char>(ch);
    if (std::isalnum(uch) != 0) {
      key.push_back(static_cast<char>(std::toupper(uch)));
    } else if (ch == '-' || ch == '_') {
      key.push_back('_');
    }
  }
  key += "_API_KEY";
  return key;
}

std::vector<std::string> provider_env_keys(std::string_view provider_id) {
  std::vector<std::string> keys;
  if (provider_id == "openai") keys.push_back("OPENAI_API_KEY");
  if (provider_id == "anthropic") {
    keys.push_back("ANTHROPIC_OAUTH_TOKEN");
    keys.push_back("ANTHROPIC_API_KEY");
  }
  const auto generic = env_key_from_provider_id(provider_id);
  if (!generic.empty() && std::find(keys.begin(), keys.end(), generic) == keys.end()) keys.push_back(generic);
  return keys;
}

std::optional<ProviderCredential> provider_credential_from_env(std::string_view provider_id) {
  for (const auto& key : provider_env_keys(provider_id)) {
    const char* value = std::getenv(key.c_str());
    if (value == nullptr || std::string_view(value).empty()) continue;
    return ProviderCredential{.provider_id = std::string(provider_id),
                               .access_token = value,
                               .credential_type = key.ends_with("_OAUTH_TOKEN") ? "oauth" : "api_key",
                               .account_id = "",
                               .source = "env:" + key};
  }
  return std::nullopt;
}

class ScopedFd {
 public:
  explicit ScopedFd(int fd) : fd_(fd) {}
  ScopedFd(const ScopedFd&) = delete;
  ScopedFd& operator=(const ScopedFd&) = delete;
  ScopedFd(ScopedFd&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
  ScopedFd& operator=(ScopedFd&& other) noexcept {
    if (this != &other) {
      close_if_open();
      fd_ = std::exchange(other.fd_, -1);
    }
    return *this;
  }
  ~ScopedFd() { close_if_open(); }

  [[nodiscard]] int get() const noexcept { return fd_; }

 private:
  void close_if_open() noexcept {
    if (fd_ >= 0) static_cast<void>(::close(fd_));
  }

  int fd_ = -1;
};

struct CandidateRead {
  std::optional<std::string> content;
};

ava::core::Result<CandidateRead> read_text_if_exists(const std::filesystem::path& path,
                                                     bool explicit_ava_auth_file,
                                                     bool allow_broad_permissions = false);

struct AuthMember {
  std::string key;
  std::string raw_value;
};

class TempPathCleanup {
 public:
  explicit TempPathCleanup(std::filesystem::path path) : path_(std::move(path)) {}
  TempPathCleanup(const TempPathCleanup&) = delete;
  TempPathCleanup& operator=(const TempPathCleanup&) = delete;
  TempPathCleanup(TempPathCleanup&& other) noexcept
      : path_(std::move(other.path_)), active_(std::exchange(other.active_, false)) {}
  TempPathCleanup& operator=(TempPathCleanup&& other) noexcept {
    if (this != &other) {
      cleanup();
      path_ = std::move(other.path_);
      active_ = std::exchange(other.active_, false);
    }
    return *this;
  }
  ~TempPathCleanup() { cleanup(); }

  void dismiss() noexcept { active_ = false; }

 private:
  void cleanup() noexcept {
    if (!active_) return;
    std::error_code remove_error;
    std::filesystem::remove(path_, remove_error);
    active_ = false;
  }

  std::filesystem::path path_;
  bool active_ = true;
};

std::string errno_message() { return std::strerror(errno); }

bool is_symlink_mode(mode_t mode) { return S_ISLNK(mode); }

ava::core::Error auth_file_error(ava::core::ErrorCategory category,
                                 std::string message,
                                 const std::filesystem::path& path) {
  auto error = ava::core::Error(category, std::move(message));
  error.with_context("path", path.string());
  return error;
}

bool is_json_ws(char ch) { return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r'; }

void skip_json_ws(std::string_view text, std::size_t& index) {
  while (index < text.size() && is_json_ws(text[index])) ++index;
}

bool is_hex_digit(char ch) {
  return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
}

ava::core::Result<std::string> parse_auth_member_key(std::string_view text,
                                                      std::size_t& index,
                                                      const std::filesystem::path& path) {
  if (index >= text.size() || text[index] != '"') {
    return std::unexpected(auth_file_error(ava::core::ErrorCategory::InvalidArgument,
                                           "auth file member key must be a JSON string", path));
  }
  ++index;
  std::string key;
  bool escaped = false;
  for (; index < text.size(); ++index) {
    const char ch = text[index];
    if (escaped) {
      switch (ch) {
        case '"':
        case '\\':
        case '/':
          key.push_back(ch);
          break;
        case 'b':
          key.push_back('\b');
          break;
        case 'f':
          key.push_back('\f');
          break;
        case 'n':
          key.push_back('\n');
          break;
        case 'r':
          key.push_back('\r');
          break;
        case 't':
          key.push_back('\t');
          break;
        case 'u':
          if (index + 4 >= text.size() || !is_hex_digit(text[index + 1]) || !is_hex_digit(text[index + 2]) ||
              !is_hex_digit(text[index + 3]) || !is_hex_digit(text[index + 4])) {
            return std::unexpected(auth_file_error(ava::core::ErrorCategory::InvalidArgument,
                                                   "auth file member key has an invalid unicode escape", path));
          }
          key.push_back('?');
          index += 4;
          break;
        default:
          return std::unexpected(auth_file_error(ava::core::ErrorCategory::InvalidArgument,
                                                 "auth file member key has an invalid escape", path));
      }
      escaped = false;
      continue;
    }
    if (ch == '\\') {
      escaped = true;
      continue;
    }
    if (ch == '"') {
      ++index;
      return key;
    }
    if (static_cast<unsigned char>(ch) < 0x20) {
      return std::unexpected(auth_file_error(ava::core::ErrorCategory::InvalidArgument,
                                             "auth file member key contains a control character", path));
    }
    key.push_back(ch);
  }
  return std::unexpected(auth_file_error(ava::core::ErrorCategory::InvalidArgument,
                                         "auth file member key is unterminated", path));
}

std::optional<std::size_t> raw_json_value_end(std::string_view text, std::size_t start) {
  if (start >= text.size()) return std::nullopt;
  const char first = text[start];
  const bool valid_start = first == '"' || first == '{' || first == '[' || first == '-' || first == 't' ||
                           first == 'f' || first == 'n' || (first >= '0' && first <= '9');
  if (!valid_start) return std::nullopt;
  bool in_string = false;
  bool escaped = false;
  int object_depth = 0;
  int array_depth = 0;
  std::size_t last_non_ws = start;
  for (std::size_t index = start; index < text.size(); ++index) {
    const char ch = text[index];
    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (ch == '\\') {
        escaped = true;
      } else if (ch == '"') {
        in_string = false;
      }
      last_non_ws = index + 1;
      continue;
    }

    if (ch == '"') {
      in_string = true;
      last_non_ws = index + 1;
      continue;
    }
    if (ch == '{') {
      ++object_depth;
      last_non_ws = index + 1;
      continue;
    }
    if (ch == '[') {
      ++array_depth;
      last_non_ws = index + 1;
      continue;
    }
    if (ch == '}') {
      if (object_depth == 0 && array_depth == 0) return last_non_ws;
      --object_depth;
      last_non_ws = index + 1;
      if (object_depth == 0 && array_depth == 0) return index + 1;
      continue;
    }
    if (ch == ']') {
      if (array_depth == 0) return std::nullopt;
      --array_depth;
      last_non_ws = index + 1;
      if (object_depth == 0 && array_depth == 0) return index + 1;
      continue;
    }
    if (ch == ',' && object_depth == 0 && array_depth == 0) return last_non_ws;
    if (!is_json_ws(ch)) last_non_ws = index + 1;
  }
  if (in_string || object_depth != 0 || array_depth != 0 || last_non_ws == start) return std::nullopt;
  return last_non_ws;
}

ava::core::Result<std::vector<AuthMember>> parse_auth_members(std::string_view text,
                                                              const std::filesystem::path& path) {
  std::vector<AuthMember> members;
  std::size_t index = 0;
  skip_json_ws(text, index);
  if (index >= text.size()) return members;
  if (text[index] != '{') {
    return std::unexpected(auth_file_error(ava::core::ErrorCategory::InvalidArgument,
                                           "auth file must be a JSON object", path));
  }
  ++index;
  skip_json_ws(text, index);
  if (index < text.size() && text[index] == '}') {
    ++index;
    skip_json_ws(text, index);
    if (index == text.size()) return members;
    return std::unexpected(auth_file_error(ava::core::ErrorCategory::InvalidArgument,
                                           "auth file has trailing content after JSON object", path));
  }
  while (index < text.size()) {
    auto key = parse_auth_member_key(text, index, path);
    if (!key) return std::unexpected(std::move(key.error()));
    skip_json_ws(text, index);
    if (index >= text.size() || text[index] != ':') {
      return std::unexpected(auth_file_error(ava::core::ErrorCategory::InvalidArgument,
                                             "auth file member is missing a colon", path));
    }
    ++index;
    skip_json_ws(text, index);
    const auto value_start = index;
    const auto value_end = raw_json_value_end(text, value_start);
    if (!value_end || *value_end <= value_start) {
      return std::unexpected(auth_file_error(ava::core::ErrorCategory::InvalidArgument,
                                             "auth file member has an invalid value", path));
    }
    members.push_back(AuthMember{.key = std::move(*key),
                                 .raw_value = std::string(text.substr(value_start, *value_end - value_start))});
    index = *value_end;
    skip_json_ws(text, index);
    if (index >= text.size()) break;
    if (text[index] == ',') {
      ++index;
      skip_json_ws(text, index);
      continue;
    }
    if (text[index] == '}') {
      ++index;
      skip_json_ws(text, index);
      if (index == text.size()) return members;
      return std::unexpected(auth_file_error(ava::core::ErrorCategory::InvalidArgument,
                                             "auth file has trailing content after JSON object", path));
    }
    return std::unexpected(auth_file_error(ava::core::ErrorCategory::InvalidArgument,
                                           "auth file object has an invalid separator", path));
  }
  return std::unexpected(auth_file_error(ava::core::ErrorCategory::InvalidArgument,
                                         "auth file JSON object is unterminated", path));
}

ava::core::VoidResult ensure_auth_directory(const XdgPaths& paths) {
  std::error_code mkdir_error;
  std::filesystem::create_directories(paths.auth_file.parent_path(), mkdir_error);
  if (mkdir_error) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to create auth directory");
    error.with_context("path", paths.auth_file.parent_path().string());
    error.with_context("cause", mkdir_error.message());
    return std::unexpected(std::move(error));
  }
  if (::chmod(paths.auth_file.parent_path().c_str(), S_IRWXU) != 0) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to set auth directory permissions");
    error.with_context("path", paths.auth_file.parent_path().string());
    error.with_context("cause", errno_message());
    return std::unexpected(std::move(error));
  }
  return {};
}

ava::core::VoidResult reject_unsafe_auth_replace_target(const std::filesystem::path& path) {
  std::error_code status_error;
  const auto status = std::filesystem::symlink_status(path, status_error);
  if (status_error) {
    if (status_error.default_error_condition() == std::errc::no_such_file_or_directory) return {};
    auto error = auth_file_error(ava::core::ErrorCategory::Io, "failed to inspect auth file before writing", path);
    error.with_context("cause", status_error.message());
    return std::unexpected(std::move(error));
  }
  if (!std::filesystem::exists(status)) return {};
  if (std::filesystem::is_symlink(status)) {
    return std::unexpected(auth_file_error(ava::core::ErrorCategory::PermissionDenied,
                                           "auth file is a symbolic link", path));
  }
  if (!std::filesystem::is_regular_file(status)) {
    return std::unexpected(auth_file_error(ava::core::ErrorCategory::PermissionDenied,
                                           "auth file is not a regular file", path));
  }
  return {};
}

ava::core::VoidResult write_all_to_fd(int fd, std::string_view body, const std::filesystem::path& path) {
  std::size_t offset = 0;
  while (offset < body.size()) {
    const auto written = ::write(fd, body.data() + offset, body.size() - offset);
    if (written < 0) {
      auto error = auth_file_error(ava::core::ErrorCategory::Io, "failed to write auth file", path);
      error.with_context("cause", errno_message());
      return std::unexpected(std::move(error));
    }
    if (written == 0) {
      return std::unexpected(auth_file_error(ava::core::ErrorCategory::Io, "auth file write made no progress", path));
    }
    offset += static_cast<std::size_t>(written);
  }
  return {};
}

ava::core::VoidResult fsync_fd(int fd, const std::filesystem::path& path, std::string_view message) {
  if (::fsync(fd) == 0) return {};
  auto error = auth_file_error(ava::core::ErrorCategory::Io, std::string(message), path);
  error.with_context("cause", errno_message());
  return std::unexpected(std::move(error));
}

ava::core::VoidResult fsync_parent_dir(const std::filesystem::path& path) {
  const auto parent = path.parent_path();
  const ScopedFd dir_fd(::open(parent.c_str(), O_RDONLY | O_CLOEXEC));
  if (dir_fd.get() < 0) {
    auto error = auth_file_error(ava::core::ErrorCategory::Io, "failed to open auth directory for sync", parent);
    error.with_context("cause", errno_message());
    return std::unexpected(std::move(error));
  }
  return fsync_fd(dir_fd.get(), parent, "failed to sync auth directory");
}

ava::core::Result<ScopedFd> acquire_auth_lock(const XdgPaths& paths) {
  const auto lock_path = paths.auth_file.parent_path() / (paths.auth_file.filename().string() + ".lock");
  int flags = O_RDWR | O_CREAT | O_CLOEXEC;
#ifdef O_NOFOLLOW
  flags |= O_NOFOLLOW;
#endif
  ScopedFd fd(::open(lock_path.c_str(), flags, S_IRUSR | S_IWUSR));
  if (fd.get() < 0) {
    auto error = auth_file_error(ava::core::ErrorCategory::Io, "failed to open auth lock file", lock_path);
    error.with_context("cause", errno_message());
    return std::unexpected(std::move(error));
  }

  struct stat opened_st {};
  if (::fstat(fd.get(), &opened_st) != 0) {
    auto error = auth_file_error(ava::core::ErrorCategory::Io, "failed to inspect auth lock file", lock_path);
    error.with_context("cause", errno_message());
    return std::unexpected(std::move(error));
  }
  if (!S_ISREG(opened_st.st_mode)) {
    return std::unexpected(auth_file_error(ava::core::ErrorCategory::PermissionDenied,
                                           "auth lock file is not regular", lock_path));
  }
  if (::fchmod(fd.get(), S_IRUSR | S_IWUSR) != 0) {
    auto error = auth_file_error(ava::core::ErrorCategory::Io, "failed to set auth lock permissions", lock_path);
    error.with_context("cause", errno_message());
    return std::unexpected(std::move(error));
  }
  while (::flock(fd.get(), LOCK_EX) != 0) {
    if (errno == EINTR) continue;
    auto error = auth_file_error(ava::core::ErrorCategory::Io, "failed to lock auth file", lock_path);
    error.with_context("cause", errno_message());
    return std::unexpected(std::move(error));
  }
  return fd;
}

ava::core::VoidResult write_auth_file_atomic(const std::filesystem::path& path, std::string_view body) {
  auto replace_check = reject_unsafe_auth_replace_target(path);
  if (!replace_check) return std::unexpected(replace_check.error());

  const auto parent = path.parent_path();
  const auto basename = path.filename().string();
  for (int attempt = 0; attempt < 100; ++attempt) {
    const auto temp_path = parent / (basename + ".tmp." + std::to_string(::getpid()) + "." + std::to_string(attempt));
    const ScopedFd fd(::open(temp_path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, S_IRUSR | S_IWUSR));
    if (fd.get() < 0) {
      if (errno == EEXIST) continue;
      auto error = auth_file_error(ava::core::ErrorCategory::Io, "failed to create temporary auth file", temp_path);
      error.with_context("cause", errno_message());
      return std::unexpected(std::move(error));
    }
    TempPathCleanup cleanup(temp_path);
    struct stat opened_st {};
    if (::fstat(fd.get(), &opened_st) != 0) {
      auto error = auth_file_error(ava::core::ErrorCategory::Io, "failed to inspect temporary auth file", temp_path);
      error.with_context("cause", errno_message());
      return std::unexpected(std::move(error));
    }
    if (!S_ISREG(opened_st.st_mode)) {
      return std::unexpected(auth_file_error(ava::core::ErrorCategory::PermissionDenied,
                                             "temporary auth file is not regular", temp_path));
    }
    if (::fchmod(fd.get(), S_IRUSR | S_IWUSR) != 0) {
      auto error = auth_file_error(ava::core::ErrorCategory::Io, "failed to set auth file permissions", temp_path);
      error.with_context("cause", errno_message());
      return std::unexpected(std::move(error));
    }
    if (auto written = write_all_to_fd(fd.get(), body, temp_path); !written) return written;
    if (auto synced = fsync_fd(fd.get(), temp_path, "failed to sync auth file"); !synced) return synced;
    if (::rename(temp_path.c_str(), path.c_str()) != 0) {
      auto error = auth_file_error(ava::core::ErrorCategory::Io, "failed to replace auth file", path);
      error.with_context("cause", errno_message());
      return std::unexpected(std::move(error));
    }
    cleanup.dismiss();
    static_cast<void>(fsync_parent_dir(path));
    return {};
  }
  return std::unexpected(auth_file_error(ava::core::ErrorCategory::Io,
                                        "failed to create unique temporary auth file", path));
}

bool is_valid_provider_id(std::string_view provider_id) {
  if (provider_id.empty() || provider_id.size() > 128) return false;
  for (const char ch : provider_id) {
    const auto uch = static_cast<unsigned char>(ch);
    if (std::isalnum(uch) != 0 || ch == '-' || ch == '_') continue;
    return false;
  }
  return true;
}

ava::core::Result<std::string> provider_credential_object_json(const ProviderCredential& credential) {
  if (!is_valid_provider_id(credential.provider_id)) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "provider id is invalid");
    error.with_context("provider", credential.provider_id);
    return std::unexpected(std::move(error));
  }
  if (credential.access_token.empty()) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "provider credential token is empty");
    error.with_context("provider", credential.provider_id);
    return std::unexpected(std::move(error));
  }
  if (credential.credential_type == "api_key") {
    return "{\n    \"type\": \"api_key\",\n    \"api_key\": \"" +
           ava::core::json::escape(credential.access_token) + "\"\n  }";
  }
  if (credential.credential_type == "oauth") {
    std::string body = "{\n    \"type\": \"oauth\",\n    \"access_token\": \"" +
                       ava::core::json::escape(credential.access_token) + "\"";
    if (!credential.account_id.empty()) {
      body += ",\n    \"account_id\": \"" + ava::core::json::escape(credential.account_id) + "\"";
    }
    body += "\n  }";
    return body;
  }
  auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "provider credential type is unsupported");
  error.with_context("provider", credential.provider_id);
  error.with_context("credential_type", credential.credential_type);
  return std::unexpected(std::move(error));
}

ava::core::VoidResult store_provider_object(const XdgPaths& paths,
                                            std::string_view provider_id,
                                            std::string raw_object) {
  if (auto ensured = ensure_auth_directory(paths); !ensured) return ensured;
  auto lock = acquire_auth_lock(paths);
  if (!lock) return std::unexpected(std::move(lock.error()));

  auto content = read_text_if_exists(paths.auth_file, true);
  if (!content && content.error().category() == ava::core::ErrorCategory::PermissionDenied &&
      content.error().message().find("permissions are too broad") != std::string::npos) {
    content = CandidateRead{};
  }
  if (!content) return std::unexpected(std::move(content.error()));
  auto members = content->content ? parse_auth_members(*content->content, paths.auth_file)
                                  : ava::core::Result<std::vector<AuthMember>>(std::vector<AuthMember>{});
  if (!members) return std::unexpected(std::move(members.error()));

  std::vector<AuthMember> merged;
  merged.reserve(members->size() + 1);
  bool replaced = false;
  for (auto& member : *members) {
    if (member.key == provider_id) {
      if (!replaced) {
        merged.push_back(AuthMember{.key = std::string(provider_id), .raw_value = raw_object});
        replaced = true;
      }
      continue;
    }
    merged.push_back(std::move(member));
  }
  if (!replaced) merged.push_back(AuthMember{.key = std::string(provider_id), .raw_value = std::move(raw_object)});

  std::string body = "{\n";
  for (std::size_t index = 0; index < merged.size(); ++index) {
    body += "  \"" + ava::core::json::escape(merged[index].key) + "\": " + merged[index].raw_value;
    body += index + 1 == merged.size() ? "\n" : ",\n";
  }
  body += "}\n";
  return write_auth_file_atomic(paths.auth_file, body);
}

ava::core::Result<CandidateRead> missing_or_ignored_auth_file(const std::filesystem::path& path,
                                                              bool explicit_ava_auth_file,
                                                              std::string_view message,
                                                              std::string_view cause = {}) {
  if (!explicit_ava_auth_file) return CandidateRead{};
  auto error = auth_file_error(ava::core::ErrorCategory::Io, std::string(message), path);
  if (!cause.empty()) error.with_context("cause", std::string(cause));
  return std::unexpected(std::move(error));
}

ava::core::Result<CandidateRead> read_text_if_exists(const std::filesystem::path& path,
                                                     bool explicit_ava_auth_file,
                                                     bool allow_broad_permissions) {
  std::error_code status_error;
  const auto status = std::filesystem::symlink_status(path, status_error);
  if (status_error) {
    if (status_error.default_error_condition() == std::errc::no_such_file_or_directory) return CandidateRead{};
    return missing_or_ignored_auth_file(path, explicit_ava_auth_file, "failed to inspect auth file",
                                        status_error.message());
  }
  if (!std::filesystem::exists(status)) return CandidateRead{};
  if (std::filesystem::is_symlink(status)) {
    if (!explicit_ava_auth_file) return CandidateRead{};
    auto error = auth_file_error(ava::core::ErrorCategory::PermissionDenied, "auth file is a symbolic link", path);
    return std::unexpected(std::move(error));
  }
  if (!std::filesystem::is_regular_file(status)) {
    if (!explicit_ava_auth_file) return CandidateRead{};
    auto error = auth_file_error(ava::core::ErrorCategory::PermissionDenied, "auth file is not a regular file", path);
    return std::unexpected(std::move(error));
  }

  int flags = O_RDONLY | O_CLOEXEC;
#ifdef O_NOFOLLOW
  flags |= O_NOFOLLOW;
#endif
  const ScopedFd fd(::open(path.c_str(), flags));
  if (fd.get() < 0) {
    return missing_or_ignored_auth_file(path, explicit_ava_auth_file, "failed to open auth file", errno_message());
  }

  struct stat st {};
  if (::fstat(fd.get(), &st) != 0) {
    return missing_or_ignored_auth_file(path, explicit_ava_auth_file, "failed to inspect opened auth file",
                                        errno_message());
  }
  if (!S_ISREG(st.st_mode) || is_symlink_mode(st.st_mode)) {
    if (!explicit_ava_auth_file) return CandidateRead{};
    auto error = auth_file_error(ava::core::ErrorCategory::PermissionDenied, "opened auth file is not regular", path);
    return std::unexpected(std::move(error));
  }
  if (st.st_uid != ::geteuid()) {
    if (!explicit_ava_auth_file) return CandidateRead{};
    auto error = auth_file_error(ava::core::ErrorCategory::PermissionDenied,
                                 "auth file is not owned by the current user", path);
    return std::unexpected(std::move(error));
  }
  if (!allow_broad_permissions && (st.st_mode & (S_IRWXG | S_IRWXO)) != 0) {
    if (!explicit_ava_auth_file) return CandidateRead{};
    auto error = auth_file_error(ava::core::ErrorCategory::PermissionDenied,
                                 "auth file permissions are too broad; run `chmod 600` on the auth file", path);
    error.with_context("expected_permissions", "0600");
    return std::unexpected(std::move(error));
  }
  if (st.st_size < 0 || static_cast<std::uintmax_t>(st.st_size) > max_auth_file_bytes) {
    if (!explicit_ava_auth_file) return CandidateRead{};
    auto error = auth_file_error(ava::core::ErrorCategory::Io, "auth file is too large", path);
    error.with_context("max_bytes", std::to_string(max_auth_file_bytes));
    return std::unexpected(std::move(error));
  }

#ifndef O_NOFOLLOW
  struct stat path_st {};
  if (::lstat(path.c_str(), &path_st) != 0) {
    return missing_or_ignored_auth_file(path, explicit_ava_auth_file, "failed to inspect auth file",
                                        errno_message());
  }
  if (is_symlink_mode(path_st.st_mode) || path_st.st_dev != st.st_dev || path_st.st_ino != st.st_ino) {
    if (!explicit_ava_auth_file) return CandidateRead{};
    auto error = auth_file_error(ava::core::ErrorCategory::PermissionDenied, "auth file changed during open", path);
    return std::unexpected(std::move(error));
  }
#endif

  std::string content;
  std::array<char, 4096> buffer{};
  while (true) {
    const auto bytes_read = ::read(fd.get(), buffer.data(), buffer.size());
    if (bytes_read == 0) break;
    if (bytes_read < 0) {
      auto error = auth_file_error(ava::core::ErrorCategory::Io, "failed while reading auth file", path);
      error.with_context("cause", errno_message());
      return std::unexpected(std::move(error));
    }
    content.append(buffer.data(), static_cast<std::size_t>(bytes_read));
    if (content.size() > max_auth_file_bytes) {
      auto error = auth_file_error(ava::core::ErrorCategory::Io, "auth file is too large", path);
      error.with_context("max_bytes", std::to_string(max_auth_file_bytes));
      return std::unexpected(std::move(error));
    }
  }
  return CandidateRead{.content = content};
}

std::optional<std::string> oauth_token_from(std::string_view scope) {
  auto access = ava::core::json::string_field(scope, "access_token");
  if (!access) access = ava::core::json::string_field(scope, "access");
  if (!access) access = ava::core::json::string_field(scope, "token");
  return access;
}

std::optional<std::string> api_key_from(std::string_view scope) {
  auto key = ava::core::json::string_field(scope, "api_key");
  if (!key) key = ava::core::json::string_field(scope, "key");
  if (!key) key = ava::core::json::string_field(scope, "OPENAI_API_KEY");
  if (!key) key = ava::core::json::string_field(scope, "openai_api_key");
  return key;
}

std::optional<std::string> generic_api_key_from(std::string_view scope) {
  auto key = ava::core::json::string_field(scope, "api_key");
  if (!key) key = ava::core::json::string_field(scope, "key");
  return key;
}

std::optional<ProviderCredential> parse_provider_credential(std::string_view content,
                                                            std::string_view provider_id,
                                                            const std::filesystem::path& source_path) {
  const auto provider = ava::core::json::object_field(content, provider_id);
  if (!provider) return std::nullopt;
  const std::string_view scope(*provider);
  const auto type = ava::core::json::string_field(scope, "type");
  auto account_id = ava::core::json::string_field(scope, "account_id");

  if (type && *type == "oauth") {
    auto token = oauth_token_from(scope);
    if (!token) return std::nullopt;
    return ProviderCredential{.provider_id = std::string(provider_id),
                              .access_token = *token,
                              .credential_type = "oauth",
                              .account_id = account_id.value_or(""),
                              .source = source_path.string()};
  }

  if (type && *type != "api" && *type != "api_key") return std::nullopt;
  auto key = generic_api_key_from(scope);
  if (!key) return std::nullopt;
  return ProviderCredential{.provider_id = std::string(provider_id),
                            .access_token = *key,
                            .credential_type = "api_key",
                            .account_id = account_id.value_or(""),
                            .source = source_path.string()};
}

ava::core::Result<std::optional<ProviderCredential>> load_provider_credential_from_auth_file(
    const XdgPaths& paths, std::string_view provider_id) {
  auto content = read_text_if_exists(paths.auth_file, true);
  if (!content) return std::unexpected(std::move(content.error()));
  if (!content->content) return std::optional<ProviderCredential>{};
  return parse_provider_credential(*content->content, provider_id, paths.auth_file);
}

std::optional<OpenAICredential> parse_oauth_credential(std::string_view scope,
                                                       const std::filesystem::path& source_path) {
  auto access = oauth_token_from(scope);
  if (!access) return std::nullopt;
  auto refresh = ava::core::json::string_field(scope, "refresh_token");
  if (!refresh) refresh = ava::core::json::string_field(scope, "refresh");
  auto expires = ava::core::json::integer_field(scope, "expires_at");
  if (!expires) expires = ava::core::json::integer_field(scope, "expires");
  auto account_id = ava::core::json::string_field(scope, "account_id");
  if (!account_id) account_id = ava::core::json::string_field(scope, "accountId");

  return OpenAICredential{
      .type = OpenAICredentialType::OAuth,
      .access_token = *access,
      .refresh_token = refresh.value_or(""),
      .expires_at = expires.value_or(0),
      .account_id = account_id.value_or(""),
      .source_path = source_path,
  };
}

std::optional<OpenAICredential> parse_api_key_credential(std::string_view scope,
                                                         const std::filesystem::path& source_path) {
  auto key = api_key_from(scope);
  if (!key) return std::nullopt;
  return OpenAICredential{
      .type = OpenAICredentialType::ApiKey,
      .access_token = *key,
      .refresh_token = "",
      .expires_at = 0,
      .account_id = "",
      .source_path = source_path,
  };
}

long long unix_time_seconds() {
  return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

bool should_refresh_openai_credential(const OpenAICredential& credential, long long now_seconds) {
  return credential.type == OpenAICredentialType::OAuth && credential.expires_at > 0 &&
         credential.expires_at <= now_seconds + kOAuthRefreshSkewSeconds;
}

}  // namespace

std::optional<OpenAICredential> parse_openai_credential(std::string_view content,
                                                        const std::filesystem::path& source_path) {
  const auto openai = ava::core::json::object_field(content, "openai");
  const std::string_view scope = openai ? std::string_view(*openai) : content;
  if (const auto type = ava::core::json::string_field(scope, "type")) {
    if (*type == "oauth") return parse_oauth_credential(scope, source_path);
    if (*type == "api" || *type == "api_key") return parse_api_key_credential(scope, source_path);
    return std::nullopt;
  }

  if (auto oauth = parse_oauth_credential(scope, source_path)) return oauth;
  if (auto api_key = parse_api_key_credential(scope, source_path)) return api_key;

  return std::nullopt;
}

ava::core::Result<std::optional<OpenAICredential>> load_openai_credential(const XdgPaths& paths) {
  auto explicit_content = read_text_if_exists(paths.auth_file, true);
  if (!explicit_content) return std::unexpected(explicit_content.error());
  if (explicit_content->content) {
    if (auto credential = parse_openai_credential(*explicit_content->content, paths.auth_file)) return credential;
  }

  const std::array candidates{legacy_ava_credentials_path(), opencode_auth_path()};
  std::optional<OpenAICredential> first_api_key;
  for (const auto& path : candidates) {
    auto content = read_text_if_exists(path, false);
    if (!content) return std::unexpected(content.error());
    if (!content->content) continue;
    auto credential = parse_openai_credential(*content->content, path);
    if (!credential) continue;
    if (credential->type == OpenAICredentialType::OAuth) return credential;
    if (!first_api_key) first_api_key = std::move(credential);
  }
  if (first_api_key) return first_api_key;
  return std::optional<OpenAICredential>{};
}

ava::core::VoidResult store_openai_credential(const XdgPaths& paths, const OpenAICredential& credential) {
  std::string body;
  if (credential.type == OpenAICredentialType::ApiKey) {
    body = "{\n    \"type\": \"api_key\",\n    \"api_key\": \"" +
           ava::core::json::escape(credential.access_token) + "\"\n  }";
  } else {
    body = "{\n    \"type\": \"oauth\",\n    \"access_token\": \"" +
           ava::core::json::escape(credential.access_token) + "\",\n    \"refresh_token\": \"" +
           ava::core::json::escape(credential.refresh_token) +
           "\",\n    \"expires_at\": " + std::to_string(credential.expires_at);
    if (!credential.account_id.empty()) {
      body += ",\n    \"account_id\": \"" + ava::core::json::escape(credential.account_id) + "\"";
    }
    body += "\n  }";
  }

  return store_provider_object(paths, "openai", std::move(body));
}

bool is_openai_credential_expired(const OpenAICredential& credential, long long now_seconds) {
  return credential.type == OpenAICredentialType::OAuth && credential.expires_at > 0 &&
         credential.expires_at <= now_seconds;
}

ava::core::Result<std::string> openai_access_token_for_request(const OpenAICredential& credential,
                                                               long long now_seconds) {
  if (credential.access_token.empty()) {
    return std::unexpected(
        ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "OpenAI credential token is missing"));
  }
  if (is_openai_credential_expired(credential, now_seconds)) {
    auto error = ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "OpenAI OAuth credential is expired");
    error.with_context("expires_at", std::to_string(credential.expires_at));
    if (!credential.source_path.empty()) error.with_context("source", credential.source_path.string());
    return std::unexpected(std::move(error));
  }
  return credential.access_token;
}

ava::core::Result<std::string> openai_access_token_for_request(const OpenAICredential& credential) {
  return openai_access_token_for_request(credential, unix_time_seconds());
}

ava::core::Result<OpenAICredential> openai_credential_for_request(const XdgPaths& paths,
                                                                  const OpenAICredential& credential,
                                                                  ava::provider::Transport& transport,
                                                                  long long now_seconds) {
  if (credential.type == OpenAICredentialType::ApiKey) {
    auto token = openai_access_token_for_request(credential, now_seconds);
    if (!token) return std::unexpected(token.error());
    return credential;
  }

  if (!should_refresh_openai_credential(credential, now_seconds)) {
    auto token = openai_access_token_for_request(credential, now_seconds);
    if (!token) return std::unexpected(token.error());
    return credential;
  }

  if (credential.refresh_token.empty()) {
    auto error = ava::core::Error(ava::core::ErrorCategory::PermissionDenied,
                                  "OpenAI OAuth credential needs refresh but has no refresh token; run `ava connect "
                                  "openai` to re-authenticate");
    error.with_context("expires_at", std::to_string(credential.expires_at));
    if (!credential.source_path.empty()) error.with_context("source", credential.source_path.string());
    return std::unexpected(std::move(error));
  }

  auto refreshed = refresh_openai_oauth_credential(credential, transport, now_seconds);
  if (!refreshed) {
    auto error = ava::core::Error(ava::core::ErrorCategory::PermissionDenied,
                                  "failed to refresh OpenAI OAuth credential; run `ava connect openai` to "
                                  "re-authenticate");
    error.with_context("cause", refreshed.error().format());
    if (!credential.source_path.empty()) error.with_context("source", credential.source_path.string());
    return std::unexpected(std::move(error));
  }

  refreshed->source_path = paths.auth_file;
  auto stored = store_openai_credential(paths, *refreshed);
  if (!stored) return std::unexpected(stored.error());

  auto token = openai_access_token_for_request(*refreshed, now_seconds);
  if (!token) return std::unexpected(token.error());
  return refreshed;
}

ava::core::Result<OpenAICredential> openai_credential_for_request(const XdgPaths& paths,
                                                                   const OpenAICredential& credential,
                                                                   ava::provider::Transport& transport) {
  return openai_credential_for_request(paths, credential, transport, unix_time_seconds());
}

ava::core::Result<std::optional<ProviderCredential>> provider_credential_for_request(
    const XdgPaths& paths, std::string_view provider_id, ava::provider::Transport& transport) {
  if (provider_id == "openai") {
    auto stored = load_openai_credential(paths);
    if (!stored) return std::unexpected(std::move(stored.error()));
    if (*stored) {
      auto credential = openai_credential_for_request(paths, **stored, transport);
      if (!credential) return std::unexpected(std::move(credential.error()));
      auto access_token = openai_access_token_for_request(*credential);
      if (!access_token) return std::unexpected(std::move(access_token.error()));
      std::string account_id = credential->account_id;
      if (credential->type == OpenAICredentialType::OAuth && account_id.empty()) {
        account_id = openai_oauth_account_id_from_token(credential->access_token).value_or("");
      }
      return ProviderCredential{.provider_id = "openai",
                                .access_token = *access_token,
                                .credential_type = credential->type == OpenAICredentialType::OAuth ? "oauth" : "api_key",
                                .account_id = std::move(account_id),
                                .source = credential->source_path.empty() ? "auth_file" : credential->source_path.string()};
    }
  } else {
    auto stored = load_provider_credential_from_auth_file(paths, provider_id);
    if (!stored) return std::unexpected(std::move(stored.error()));
    if (*stored) return *stored;
  }

  if (auto env_credential = provider_credential_from_env(provider_id)) return env_credential;
  return std::optional<ProviderCredential>{};
}

ava::core::VoidResult store_provider_credential(const XdgPaths& paths, const ProviderCredential& credential) {
  auto body = provider_credential_object_json(credential);
  if (!body) return std::unexpected(std::move(body.error()));
  return store_provider_object(paths, credential.provider_id, std::move(*body));
}

std::string authorization_header_value(const OpenAICredential& credential) {
  return "Bearer " + credential.access_token;
}

}  // namespace ava::config
