#include "ava/tools/file_tools.h"

#include <array>
#include <fstream>
#include <string_view>
#include <utility>

namespace ava::tools {

namespace {

ava::core::VoidResult ensure_permission(const ToolContext& context,
                                        ava::permissions::Operation operation,
                                        const std::filesystem::path& path) {
  const auto decision = ava::permissions::decide(ava::permissions::PermissionRequest{
      .operation = operation,
      .mode = context.mode,
      .workspace_dir = context.workspace_dir,
      .target_path = path,
      .command = "",
  });
  if (decision.action == ava::permissions::PermissionAction::Allow) {
    return {};
  }

  auto error = ava::core::Error(decision.action == ava::permissions::PermissionAction::Ask
                                   ? ava::core::ErrorCategory::PermissionDenied
                                   : ava::core::ErrorCategory::PermissionDenied,
                               "tool requires permission");
  error.with_context("action", ava::permissions::to_string(decision.action));
  error.with_context("reason", decision.reason);
  error.with_context("path", path.string());
  return std::unexpected(std::move(error));
}

ava::core::Result<std::string> read_all_text(const std::filesystem::path& path) {
  constexpr std::size_t max_edit_file_bytes = 10 * 1024 * 1024;
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to open file for reading");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }
  std::string content;
  content.reserve(4096);
  std::array<char, 4096> buffer{};
  while (file) {
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto count = file.gcount();
    if (count > 0) {
      if (content.size() + static_cast<std::size_t>(count) > max_edit_file_bytes) {
        auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "file is too large for exact edit");
        error.with_context("path", path.string());
        error.with_context("max_bytes", std::to_string(max_edit_file_bytes));
        return std::unexpected(std::move(error));
      }
      content.append(buffer.data(), static_cast<std::size_t>(count));
    }
  }
  if (!file.eof() && file.fail()) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed while reading file");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }
  return content;
}

ava::core::Result<TextOutput> read_head_text(const std::filesystem::path& path, std::size_t max_bytes) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to open file for reading");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }

  TextOutput output;
  std::array<char, 4096> buffer{};
  while (file) {
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto count = static_cast<std::size_t>(file.gcount());
    if (count == 0) {
      continue;
    }
    output.total_bytes += count;
    if (output.content.size() < max_bytes) {
      const auto remaining = max_bytes - output.content.size();
      output.content.append(buffer.data(), std::min(remaining, count));
    }
  }
  if (!file.eof() && file.fail()) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed while reading file");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }
  output.output_bytes = output.content.size();
  output.truncated = output.output_bytes < output.total_bytes;
  return output;
}

}  // namespace

ava::core::Result<TextOutput> read_file(const ToolContext& context,
                                        const std::filesystem::path& path,
                                        ReadOptions options) {
  if (auto permission = ensure_permission(context, ava::permissions::Operation::ReadFile, path); !permission) {
    return std::unexpected(permission.error());
  }

  return read_head_text(path, options.max_bytes);
}

ava::core::Result<FileMutationResult> write_file(const ToolContext& context,
                                                 const std::filesystem::path& path,
                                                 std::string_view content) {
  if (auto permission = ensure_permission(context, ava::permissions::Operation::EditFile, path); !permission) {
    return std::unexpected(permission.error());
  }

  std::error_code mkdir_error;
  std::filesystem::create_directories(path.parent_path(), mkdir_error);
  if (mkdir_error) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to create parent directory");
    error.with_context("path", path.parent_path().string());
    error.with_context("cause", mkdir_error.message());
    return std::unexpected(std::move(error));
  }

  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to open file for writing");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }
  file << content;
  if (!file) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to write file");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }

  return FileMutationResult{.path = path, .bytes_written = content.size()};
}

ava::core::Result<FileMutationResult> edit_file(const ToolContext& context,
                                                const std::filesystem::path& path,
                                                std::string_view old_text,
                                                std::string_view new_text) {
  if (auto permission = ensure_permission(context, ava::permissions::Operation::ReadFile, path); !permission) {
    return std::unexpected(permission.error());
  }
  if (auto permission = ensure_permission(context, ava::permissions::Operation::EditFile, path); !permission) {
    return std::unexpected(permission.error());
  }
  if (old_text.empty()) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "old_text must not be empty");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }

  auto content = read_all_text(path);
  if (!content) {
    return std::unexpected(content.error());
  }

  const auto first = content->find(old_text);
  if (first == std::string::npos) {
    auto error = ava::core::Error(ava::core::ErrorCategory::NotFound, "old_text was not found");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }

  if (content->find(old_text, first + old_text.size()) != std::string::npos) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "old_text is not unique");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }

  content->replace(first, old_text.size(), new_text);
  return write_file(context, path, *content);
}

}  // namespace ava::tools
