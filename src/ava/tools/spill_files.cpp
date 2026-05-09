#include "ava/tools/spill_files.h"
#include "ava/core/ids.h"

#include <algorithm>
#include <fstream>
#include <system_error>

namespace ava::tools {
namespace {

std::string safe_filename_component(std::string_view value, std::string_view fallback)
{
  std::string out;
  out.reserve(value.size());
  for (char const ch : value)
  {
    auto const byte = static_cast<unsigned char>(ch);
    bool const safe = (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') || ch == '-' || ch == '_' || ch == '.';
    out.push_back(safe ? ch : '_');
    if (out.size() >= 80)
      break;
  }
  while (!out.empty() && (out.front() == '.' || out.front() == '-')) out.erase(out.begin());
  if (out.empty())
    return std::string(fallback);
  return out;
}

std::string normalized_extension(std::string_view extension)
{
  std::string out;
  for (char const ch : extension)
  {
    auto const byte = static_cast<unsigned char>(ch);
    bool const safe = (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9');
    if (safe)
      out.push_back(ch);
    if (out.size() >= 12)
      break;
  }
  if (out.empty())
    return "txt";
  return out;
}

}  // namespace

SpillBuffer::SpillBuffer(std::size_t max_bytes) : max_bytes_(max_bytes)
{
  content_.reserve(std::min(max_bytes, 64UL * 1024UL));
}

void SpillBuffer::append(std::string_view text)
{
  total_bytes_ += text.size();
  if (text.empty())
    return;
  if (content_.size() >= max_bytes_)
  {
    truncated_ = true;
    return;
  }
  auto const available = max_bytes_ - content_.size();
  if (text.size() <= available)
  {
    content_.append(text);
    return;
  }
  content_.append(text.substr(0, available));
  truncated_ = true;
}

std::string const& SpillBuffer::content() const noexcept
{
  return content_;
}

std::size_t SpillBuffer::total_bytes() const noexcept
{
  return total_bytes_;
}

bool SpillBuffer::truncated() const noexcept
{
  return truncated_;
}

ava::core::Result<SpillFileResult> write_spill_file(ToolContext const& context, std::string_view tool_name, std::string_view extension,
                                                    SpillBuffer const& buffer)
{
  if (context.spill_dir.empty())
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "spill directory is not configured");
    return std::unexpected(std::move(error));
  }

  std::error_code mkdir_error;
  std::filesystem::create_directories(context.spill_dir, mkdir_error);
  if (mkdir_error)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to create spill directory");
    error.with_context("path", context.spill_dir.string());
    error.with_context("cause", mkdir_error.message());
    return std::unexpected(std::move(error));
  }

  std::error_code permissions_error;
  std::filesystem::permissions(context.spill_dir, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace, permissions_error);
  if (permissions_error)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to secure spill directory permissions");
    error.with_context("path", context.spill_dir.string());
    error.with_context("cause", permissions_error.message());
    return std::unexpected(std::move(error));
  }

  auto const tool = safe_filename_component(tool_name.empty() ? context.current_tool_name : tool_name, "tool");
  auto const call_id = safe_filename_component(context.current_call_id, "call");
  auto const filename = tool + "-" + call_id + "-" + ava::core::make_id("spill") + "." + normalized_extension(extension);
  auto const path = context.spill_dir / filename;

  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to open spill file");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }
  file.write(buffer.content().data(), static_cast<std::streamsize>(buffer.content().size()));
  if (!file)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to write spill file");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }
  file.close();
  if (!file)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to close spill file");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }

  std::error_code file_permissions_error;
  std::filesystem::permissions(path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write, std::filesystem::perm_options::replace,
                               file_permissions_error);
  if (file_permissions_error)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to secure spill file permissions");
    error.with_context("path", path.string());
    error.with_context("cause", file_permissions_error.message());
    return std::unexpected(std::move(error));
  }

  return SpillFileResult{.path = path, .truncated = buffer.truncated(), .bytes_written = buffer.content().size()};
}

ava::core::VoidResult emit_tool_progress(ToolContext const& context, std::string text, std::string status)
{
  if (!context.progress_sink)
    return {};
  return context.progress_sink(
      ToolProgressEvent{.text = std::move(text), .call_id = context.current_call_id, .tool_name = context.current_tool_name, .status = std::move(status)});
}

}  // namespace ava::tools
