#include "ava/types/attachment.hpp"

#include <optional>
#include <sstream>

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
  std::vector<std::string> cleaned_tokens;

  std::istringstream stream(std::string(text));
  std::string token;
  while(stream >> token) {
    if(token.starts_with('@')) {
      auto parsed = try_parse_mention(std::string_view(token).substr(1));
      if(parsed.has_value()) {
        attachments.push_back(std::move(*parsed));
        continue;
      }
    }
    cleaned_tokens.push_back(token);
  }

  std::string cleaned;
  for(std::size_t index = 0; index < cleaned_tokens.size(); ++index) {
    if(index != 0) {
      cleaned += ' ';
    }
    cleaned += cleaned_tokens[index];
  }

  return {std::move(attachments), std::move(cleaned)};
}

}  // namespace ava::types
