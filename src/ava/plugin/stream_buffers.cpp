#include "ava/plugin/stream_buffers.h"

namespace ava::plugin {

PluginRecordBuffer::PluginRecordBuffer(std::size_t max_record_bytes) : max_record_bytes_(max_record_bytes)
{
}

void PluginRecordBuffer::set_limit(std::size_t max_record_bytes) noexcept
{
  max_record_bytes_ = max_record_bytes;
}

void PluginRecordBuffer::append(std::string_view chunk)
{
  buffer_.append(chunk);
}

std::optional<std::string> PluginRecordBuffer::take_record()
{
  auto const newline = buffer_.find('\n');
  if (newline == std::string::npos) return std::nullopt;
  auto record = buffer_.substr(0, newline);
  buffer_.erase(0, newline + 1);
  if (!record.empty() && record.back() == '\r') record.pop_back();
  return record;
}

bool PluginRecordBuffer::exceeds_limit() const noexcept
{
  auto const newline = buffer_.find('\n');
  if (newline == std::string::npos) return buffer_.size() > max_record_bytes_;
  return newline > max_record_bytes_;
}

bool PluginRecordBuffer::empty() const noexcept
{
  return buffer_.empty();
}

std::size_t PluginRecordBuffer::size() const noexcept
{
  return buffer_.size();
}

void PluginRecordBuffer::trim_front_to_limit()
{
  if (buffer_.size() > max_record_bytes_) buffer_.erase(0, buffer_.size() - max_record_bytes_);
}

PluginStderrTail::PluginStderrTail(std::size_t max_bytes) : max_bytes_(max_bytes)
{
}

void PluginStderrTail::set_limit(std::size_t max_bytes) noexcept
{
  max_bytes_ = max_bytes;
  if (text_.size() > max_bytes_) {
    text_.erase(0, text_.size() - max_bytes_);
    truncated_ = true;
  }
}

void PluginStderrTail::append(std::string_view chunk)
{
  if (chunk.empty()) return;
  if (chunk.size() >= max_bytes_) {
    text_.assign(chunk.substr(chunk.size() - max_bytes_));
    truncated_ = true;
    return;
  }
  auto const next_size = text_.size() + chunk.size();
  if (next_size > max_bytes_) {
    text_.erase(0, next_size - max_bytes_);
    truncated_ = true;
  }
  text_.append(chunk);
}

std::string const& PluginStderrTail::text() const noexcept
{
  return text_;
}

bool PluginStderrTail::truncated() const noexcept
{
  return truncated_;
}

}  // namespace ava::plugin
