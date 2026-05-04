#pragma once

#include <cstddef>
#include <string>

#include "ava/core/result.h"
#include "ava/provider/provider.h"
#include "ava/tools/file_tools.h"

namespace ava::tools {

struct WebFetchOptions {
  std::size_t max_bytes = 1024 * 1024;
  int timeout_ms = 30000;
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

[[nodiscard]] ava::core::Result<WebFetchResult> webfetch(const ToolContext& context, std::string_view url,
                                                         WebFetchOptions options = {});

}  // namespace ava::tools
