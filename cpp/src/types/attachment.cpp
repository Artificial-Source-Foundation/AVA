#include "ava/types/attachment.hpp"

#include <cctype>
#include <optional>

namespace ava::types {
namespace {

std::optional<ContextAttachment> try_parse_mention(std::string_view token) {
  if(token.starts_with("file:")) {
    token.remove_prefix(5);
    if(!token.empty()) {
      return ContextAttachment::file(std::filesystem::path(token));
    }
    return std::nullopt;
  }

  if(token.starts_with("folder:")) {
    token.remove_prefix(7);
    if(!token.empty()) {
      return ContextAttachment::folder(std::filesystem::path(token));
    }
    return std::nullopt;
  }

  if(token.starts_with("codebase:")) {
    token.remove_prefix(9);
    if(!token.empty()) {
      return ContextAttachment::codebase_query(std::string(token));
    }
    return std::nullopt;
  }

  if(!token.empty() && (token.find('/') != std::string_view::npos || token.find('.') != std::string_view::npos)) {
    if(token.ends_with('/')) {
      const auto trimmed = token.substr(0, token.size() - 1);
      return ContextAttachment::folder(std::filesystem::path(trimmed));
    }
    return ContextAttachment::file(std::filesystem::path(token));
  }

  return std::nullopt;
}

}  // namespace

std::string ContextAttachment::label() const {
  switch(kind) {
    case ContextAttachmentKind::File:
      return path.string();
    case ContextAttachmentKind::Folder:
      return path.string() + "/";
    case ContextAttachmentKind::CodebaseQuery:
      return "search:" + query;
  }
  return {};
}

std::string_view ContextAttachment::mention_prefix() const {
  switch(kind) {
    case ContextAttachmentKind::File:
      return "@file:";
    case ContextAttachmentKind::Folder:
      return "@folder:";
    case ContextAttachmentKind::CodebaseQuery:
      return "@codebase:";
  }
  return "@file:";
}

std::pair<std::vector<ContextAttachment>, std::string> parse_mentions(std::string_view text) {
  std::vector<ContextAttachment> attachments;
  std::string cleaned;
  cleaned.reserve(text.size());

  std::size_t index = 0;
  while(index < text.size()) {
    if(text[index] != '@') {
      cleaned.push_back(text[index]);
      ++index;
      continue;
    }

    auto end = index + 1;
    while(end < text.size() && !std::isspace(static_cast<unsigned char>(text[end]))) {
      ++end;
    }

    const auto token = text.substr(index + 1, end - (index + 1));
    auto parsed = try_parse_mention(token);
    if(parsed.has_value()) {
      attachments.push_back(std::move(*parsed));
      index = end;
      continue;
    }

    cleaned.append(text.substr(index, end - index));
    index = end;
  }

  return {std::move(attachments), std::move(cleaned)};
}

}  // namespace ava::types
