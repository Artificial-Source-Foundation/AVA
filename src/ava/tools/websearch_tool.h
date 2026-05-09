#pragma once

#include "ava/tools/file_tools.h"
#include "ava/provider/provider.h"
#include "ava/core/result.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ava::tools {

struct WebSearchOptions
{
  std::size_t max_results = 8;
  std::size_t context_max_chars = 10000;
  int timeout_ms = 25000;
  ava::provider::Transport* transport = nullptr;
};

struct WebSearchResultItem
{
  std::string title;
  std::string url;
  std::string snippet;
};

struct WebSearchResult
{
  std::string query;
  std::string engine;
  std::vector<WebSearchResultItem> results;
  bool truncated = false;
  std::size_t total_results = 0;
  std::size_t output_chars = 0;
};

[[nodiscard]] ava::core::Result<WebSearchResult> websearch(ToolContext const& context, std::string_view query, WebSearchOptions options = {});

}  // namespace ava::tools
