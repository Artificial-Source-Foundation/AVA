#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ava::types {

enum class ContextAttachmentKind {
  File,
  Folder,
  CodebaseQuery,
};

struct ContextAttachment {
  ContextAttachmentKind kind{ContextAttachmentKind::File};
  std::filesystem::path path;
  std::string query;

  [[nodiscard]] static ContextAttachment file(std::filesystem::path value) {
    return ContextAttachment{ContextAttachmentKind::File, std::move(value), {}};
  }

  [[nodiscard]] static ContextAttachment folder(std::filesystem::path value) {
    return ContextAttachment{ContextAttachmentKind::Folder, std::move(value), {}};
  }

  [[nodiscard]] static ContextAttachment codebase_query(std::string value) {
    return ContextAttachment{ContextAttachmentKind::CodebaseQuery, {}, std::move(value)};
  }

  [[nodiscard]] std::string label() const;
  [[nodiscard]] std::string_view mention_prefix() const;
};

[[nodiscard]] std::pair<std::vector<ContextAttachment>, std::string> parse_mentions(std::string_view text);

}  // namespace ava::types
