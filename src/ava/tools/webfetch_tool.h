#pragma once

#include "ava/tools/file_tools.h"

#include "ava/provider/provider.h"

#include "ava/core/result.h"

#include <cstddef>
#include <string>

namespace ava::tools {

enum class WebFetchFormat {
  Markdown,
  Text,
  Html,
};

struct WebFetchOptions {
  std::size_t max_bytes = 1024 * 1024;
  int timeout_ms = 30000;
  WebFetchFormat format = WebFetchFormat::Markdown;
  ava::provider::Transport* transport = nullptr;
};

struct WebFetchResult {
  std::string url;
  int status_code = 0;
  std::string content_type;
  std::string content;
  bool truncated = false;
  std::size_t total_bytes = 0;
  std::size_t output_bytes = 0;
};

[[nodiscard]] ava::core::Result<WebFetchResult> webfetch(ToolContext const& context, std::string_view url,
                                                         WebFetchOptions options = {});

}  // namespace ava::tools
