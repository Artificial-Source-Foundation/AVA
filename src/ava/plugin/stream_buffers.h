#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace ava::plugin {

class PluginRecordBuffer {
 public:
  explicit PluginRecordBuffer(std::size_t max_record_bytes = 64 * 1024);

  void set_limit(std::size_t max_record_bytes) noexcept;
  void append(std::string_view chunk);
  [[nodiscard]] std::optional<std::string> take_record();
  [[nodiscard]] bool exceeds_limit() const noexcept;
  [[nodiscard]] bool empty() const noexcept;
  [[nodiscard]] std::size_t size() const noexcept;
  void trim_front_to_limit();

 private:
  std::size_t max_record_bytes_ = 64 * 1024;
  std::string buffer_;
};

class PluginStderrTail {
 public:
  explicit PluginStderrTail(std::size_t max_bytes = 64 * 1024);

  void set_limit(std::size_t max_bytes) noexcept;
  void append(std::string_view chunk);
  [[nodiscard]] std::string const& text() const noexcept;
  [[nodiscard]] bool truncated() const noexcept;

 private:
  std::size_t max_bytes_ = 64 * 1024;
  bool truncated_ = false;
  std::string text_;
};

}  // namespace ava::plugin
