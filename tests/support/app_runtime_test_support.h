#pragma once

#include <termios.h>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <streambuf>
#include <string>
#include <string_view>
#include <vector>

#include "ava/config/xdg_paths.h"
#include "ava/core/result.h"
#include "ava/provider/provider.h"
#include "ava/session/session_store.h"

namespace ava::test {

class ScopedStdinTerminalState {
 public:
  ScopedStdinTerminalState();

  ScopedStdinTerminalState(ScopedStdinTerminalState const&) = delete;
  ScopedStdinTerminalState& operator=(ScopedStdinTerminalState const&) = delete;

  ~ScopedStdinTerminalState();

  void restore() noexcept;

 private:
  termios original_{};
  bool active_ = false;
};

[[nodiscard]] ava::config::XdgPaths app_test_paths(std::filesystem::path const& root);
[[nodiscard]] std::string app_test_plugin_manifest_json(std::string_view id, std::string_view name = "Test Plugin");
[[nodiscard]] std::string app_test_mcp_config_json(std::string_view id, std::string_view name,
                                                   std::string_view command);
void write_app_test_file(std::filesystem::path const& path, std::string const& text);

class BlockingInputBuf final : public std::streambuf {
 public:
  void push(std::string text);
  void close();

 protected:
  int underflow() override;

 private:
  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<char> buffer_;
  bool closed_ = false;
  char current_ = 0;
};

class ThreadSafeStringBuf final : public std::streambuf {
 public:
  [[nodiscard]] std::string str() const;
  [[nodiscard]] bool wait_contains(std::string_view value, std::chrono::milliseconds timeout) const;

 protected:
  int overflow(int ch) override;
  std::streamsize xsputn(char const* s, std::streamsize count) override;

 private:
  mutable std::mutex mutex_;
  mutable std::condition_variable cv_;
  std::string text_;
};

class ChunkedStreamingTransport final : public ava::provider::Transport {
 public:
  explicit ChunkedStreamingTransport(std::vector<std::string> chunks, int status_code = 200);

  [[nodiscard]] ava::core::Result<ava::provider::HttpResponse> send(
      ava::provider::HttpRequest const& request) override;
  [[nodiscard]] bool supports_streaming() const noexcept override;
  [[nodiscard]] ava::core::Result<ava::provider::HttpResponse> send_streaming(
      ava::provider::HttpRequest const& request, BodyChunkSink on_body_chunk,
      CancelCallback cancel_requested = nullptr) override;

  [[nodiscard]] std::vector<ava::provider::HttpRequest> const& requests() const noexcept;

 private:
  std::vector<std::string> chunks_;
  int status_code_ = 200;
  std::string response_body_;
  std::vector<ava::provider::HttpRequest> requests_;
};

class BlockingResponseTransport final : public ava::provider::Transport {
 public:
  explicit BlockingResponseTransport(ava::provider::HttpResponse response);

  [[nodiscard]] ava::core::Result<ava::provider::HttpResponse> send(
      ava::provider::HttpRequest const& request) override;
  [[nodiscard]] bool wait_for_request(std::chrono::milliseconds timeout) const;
  void release();
  [[nodiscard]] std::vector<ava::provider::HttpRequest> requests() const;

 private:
  ava::provider::HttpResponse response_;
  mutable std::mutex mutex_;
  mutable std::condition_variable cv_;
  bool requested_ = false;
  bool released_ = false;
  std::vector<ava::provider::HttpRequest> requests_;
};

[[nodiscard]] ava::provider::HttpResponse sse_response(std::string body);
[[nodiscard]] std::string read_file_call_sse(std::string_view path = "note.txt");
[[nodiscard]] std::string write_file_call_sse(std::string_view path, std::string_view content);
[[nodiscard]] std::string question_call_sse();
[[nodiscard]] std::string final_text_sse(std::string_view text);
[[nodiscard]] std::string extract_json_string_field(std::string_view text, std::string_view key);
[[nodiscard]] std::string extract_last_json_string_field(std::string_view text, std::string_view key);
[[nodiscard]] std::size_t count_substrings(std::string_view text, std::string_view needle);
[[nodiscard]] std::size_t count_compaction_entries(std::vector<ava::session::SessionEntry> const& entries);
[[nodiscard]] std::optional<ava::session::SessionEntry> latest_compaction_entry(
    std::vector<ava::session::SessionEntry> const& entries);

class MutatingSummaryTransport final : public ava::provider::Transport {
 public:
  MutatingSummaryTransport(ava::session::SessionStore& store, std::vector<ava::provider::HttpResponse> responses,
                           std::size_t mutate_requests = 1);

  [[nodiscard]] ava::core::Result<ava::provider::HttpResponse> send(
      ava::provider::HttpRequest const& request) override;
  [[nodiscard]] std::vector<ava::provider::HttpRequest> const& requests() const noexcept;

 private:
  ava::session::SessionStore& store_;
  std::vector<ava::provider::HttpResponse> responses_;
  std::size_t mutate_requests_ = 1;
  std::vector<ava::provider::HttpRequest> requests_;
};

}  // namespace ava::test
