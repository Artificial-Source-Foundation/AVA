#include "ava/tools/output_fallback.hpp"

#include <sstream>

namespace ava::tools {

std::string apply_output_fallback(const std::string& tool_name, const std::string& content, std::size_t max_bytes) {
  if(content.size() <= max_bytes) {
    return content;
  }

  std::ostringstream oss;
  oss << content.substr(0, max_bytes);
  oss << "\n\n(Output truncated by " << tool_name << ": showing first " << max_bytes
      << " bytes)";
  return oss.str();
}

}  // namespace ava::tools
