#include "ava/tools/tool_search_tool.hpp"

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "ava/core/string_utils.hpp"
#include "ava/tools/output_fallback.hpp"
#include "tool_search_scoring.hpp"

namespace ava::tools {
namespace {

constexpr std::size_t kToolSearchOutputBytes = 20 * 1024;
constexpr std::size_t kToolSearchMaxRows = 50;

[[nodiscard]] std::vector<ToolSearchMatch> search_snapshot(
    const std::vector<ToolSearchMatch>& index,
    const std::string& query
) {
  const auto query_lower = ava::core::lowercase_ascii(query);
  std::vector<ToolSearchMatch> matches;
  matches.reserve(index.size());

  for(auto match : index) {
    const auto score = score_tool_search_match(match, query_lower);

    if(score <= 0) {
      continue;
    }
    match.score = score;
    matches.push_back(std::move(match));
  }

  sort_tool_search_matches(matches);
  return matches;
}

}  // namespace

ToolSearchTool::ToolSearchTool(const ToolRegistry& registry)
    : search_index_(registry.search_index()) {}

std::string ToolSearchTool::name() const {
  return "tool_search";
}

std::string ToolSearchTool::description() const {
  return "Search available tools by keyword";
}

std::string ToolSearchTool::search_hint() const {
  return "find discover tools available search";
}

nlohmann::json ToolSearchTool::parameters() const {
  return nlohmann::json{
      {"type", "object"},
      {"required", nlohmann::json::array({"query"})},
      {"properties",
       {
           {"query", {{"type", "string"}, {"description", "Search keywords to find relevant tools"}}},
       }},
  };
}

ava::types::ToolResult ToolSearchTool::execute(const nlohmann::json& args) const {
  if(!args.contains("query") || !args.at("query").is_string()) {
    throw std::runtime_error("missing required field: query");
  }

  const auto query = ava::core::trim_copy(args.at("query").get<std::string>());
  if(query.empty()) {
    throw std::runtime_error("query must not be empty");
  }

  const auto matches = search_snapshot(search_index_, query);
  if(matches.empty()) {
    return ava::types::ToolResult{
        .call_id = "",
        .content = "No tools found matching '" + query + "'.",
        .is_error = false,
    };
  }

  std::string output = "Found " + std::to_string(matches.size()) + " tool(s) matching '" + query + "':\n\n";
  const auto rows = std::min(matches.size(), kToolSearchMaxRows);
  for(std::size_t index = 0; index < rows; ++index) {
    const auto& match = matches.at(index);
    output += "- **" + match.definition.name + "** (relevance: " + std::to_string(match.score) + "): " +
              match.definition.description + "\n";
    if(!match.search_hint.empty()) {
      output += "  hints: " + match.search_hint + "\n";
    }
  }
  if(matches.size() > rows) {
    output += "\n(Results truncated to first " + std::to_string(rows) + " entries.)";
  }

  return ava::types::ToolResult{
      .call_id = "",
      .content = apply_output_fallback(name(), output, kToolSearchOutputBytes),
      .is_error = false,
  };
}

}  // namespace ava::tools
