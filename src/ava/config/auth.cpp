#include "ava/config/auth.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <utility>

#include "ava/core/json.h"
#include "ava/config/openai_oauth.h"

namespace ava::config {
namespace {

constexpr std::size_t max_auth_file_bytes = 1024 * 1024;
constexpr long long kOAuthRefreshSkewSeconds = 300;

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

ava::core::Result<CandidateRead> missing_or_ignored_auth_file(const std::filesystem::path& path,
                                                              bool explicit_ava_auth_file,
                                                              std::string_view message,
                                                              std::string_view cause = {}) {
  if (!explicit_ava_auth_file) return CandidateRead{};
  auto error = auth_file_error(ava::core::ErrorCategory::Io, std::string(message), path);
  if (!cause.empty()) error.with_context("cause", std::string(cause));
  return std::unexpected(std::move(error));
}

ava::core::Result<CandidateRead> read_text_if_exists(const std::filesystem::path& path, bool explicit_ava_auth_file) {
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
  const std::array candidates{paths.auth_file, legacy_ava_credentials_path(), opencode_auth_path()};
  std::optional<OpenAICredential> first_api_key;
  for (std::size_t index = 0; index < candidates.size(); ++index) {
    const auto& path = candidates[index];
    auto content = read_text_if_exists(path, index == 0);
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

  std::string body;
  if (credential.type == OpenAICredentialType::ApiKey) {
    body = "{\n  \"openai\": {\n    \"type\": \"api_key\",\n    \"api_key\": \"" +
           ava::core::json::escape(credential.access_token) + "\"\n  }\n}\n";
  } else {
    body = "{\n  \"openai\": {\n    \"type\": \"oauth\",\n    \"access_token\": \"" +
           ava::core::json::escape(credential.access_token) + "\",\n    \"refresh_token\": \"" +
           ava::core::json::escape(credential.refresh_token) +
           "\",\n    \"expires_at\": " + std::to_string(credential.expires_at);
    if (!credential.account_id.empty()) {
      body += ",\n    \"account_id\": \"" + ava::core::json::escape(credential.account_id) + "\"";
    }
    body += "\n  }\n}\n";
  }

  return write_auth_file_atomic(paths.auth_file, body);
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

std::string authorization_header_value(const OpenAICredential& credential) {
  return "Bearer " + credential.access_token;
}

}  // namespace ava::config
