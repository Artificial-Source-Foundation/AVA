#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "ava/core/string_utils.hpp"
#include "ava/tools/registry.hpp"

namespace ava::tools {

[[nodiscard]] inline int score_tool_search_match(const ToolSearchMatch& match, const std::string& query_lower) {
  const auto query_words = ava::core::split_words(query_lower);
  const auto lower_name = ava::core::lowercase_ascii(match.definition.name);
  const auto lower_description = ava::core::lowercase_ascii(match.definition.description);
  const auto lower_hint = ava::core::lowercase_ascii(match.search_hint);

  int score = 0;
  if(!query_lower.empty() && lower_name == query_lower) {
    score += 100;
  }
  if(!query_lower.empty() && lower_name.find(query_lower) != std::string::npos) {
    score += 50;
  }

  for(const auto& word : query_words) {
    if(word.empty()) {
      continue;
    }
    if(lower_hint.find(word) != std::string::npos) {
      score += 30;
    }
    if(lower_name.find(word) != std::string::npos) {
      score += 20;
    }
    if(lower_description.find(word) != std::string::npos) {
      score += 10;
    }
  }
  return score;
}

inline void sort_tool_search_matches(std::vector<ToolSearchMatch>& matches) {
  std::sort(matches.begin(), matches.end(), [](const ToolSearchMatch& left, const ToolSearchMatch& right) {
    if(left.score != right.score) {
      return left.score > right.score;
    }
    return left.definition.name < right.definition.name;
  });
}

}  // namespace ava::tools
