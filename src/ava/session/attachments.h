#pragma once

#include "ava/core/result.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace ava::session {

class SessionStore;

struct ImageAttachmentRef {
  std::string id;
  std::string mime_type;
  std::string storage_path;
  std::string sha256;
  std::size_t byte_size = 0;
};

struct LoadedImageAttachment {
  ImageAttachmentRef metadata;
  std::string bytes;
  std::filesystem::path path;
};

[[nodiscard]] std::filesystem::path attachment_storage_root(SessionStore const& store);
[[nodiscard]] ava::core::Result<std::filesystem::path> resolve_attachment_storage_path(SessionStore const& store,
                                                                                        std::string_view storage_path);
[[nodiscard]] ava::core::Result<LoadedImageAttachment> load_image_attachment(SessionStore const& store,
                                                                             ImageAttachmentRef const& attachment);

}  // namespace ava::session
