#include "ava/agent/tool_result_json.h"

#include <cstddef>

#include "ava/core/json.h"

namespace ava::agent {

std::string json_bool_literal(bool value)
{
  return value ? "true" : "false";
}

std::string tool_error_result_json(std::string_view tool, ava::core::Error const& error)
{
  return "{\"tool\":\"" + ava::core::json::escape(tool) + "\",\"ok\":false,\"error\":{\"category\":\"" +
         ava::core::json::escape(ava::core::to_string(error.category())) + "\",\"message\":\"" +
         ava::core::json::escape(error.message()) + "\",\"details\":\"" + ava::core::json::escape(error.format()) +
         "\"}}";
}

void append_tool_result_spill_fields(std::string& text, std::filesystem::path const& path, bool spill_truncated)
{
  if (path.empty()) return;
  text += ",\"spill_file\":\"" + ava::core::json::escape(path.filename().generic_string()) + "\"";
  text += ",\"spill_truncated\":" + json_bool_literal(spill_truncated);
}

void append_changed_files_json(std::string& text, std::vector<std::filesystem::path> const& paths)
{
  text += ",\"changed_files\":[";
  for (std::size_t index = 0; index < paths.size(); ++index) {
    if (index > 0) text += ',';
    text += "\"" + ava::core::json::escape(paths[index].generic_string()) + "\"";
  }
  text += ']';
}

void append_diff_json(std::string& text, std::string_view diff, bool truncated)
{
  text += ",\"diff\":\"" + ava::core::json::escape(diff) + "\"";
  text += ",\"diff_truncated\":" + json_bool_literal(truncated);
}

}  // namespace ava::agent
