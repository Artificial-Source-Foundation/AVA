#include "sse_utils.hpp"

namespace ava::llm::providers {

std::string normalize_sse_newlines(std::string_view chunk, bool& pending_carriage_return) {
  std::string normalized;
  normalized.reserve(chunk.size() + (pending_carriage_return ? 1U : 0U));

  std::size_t index = 0;
  if(pending_carriage_return) {
    pending_carriage_return = false;
    if(!chunk.empty() && chunk.front() == '\n') {
      normalized.push_back('\n');
      index = 1;
    } else {
      normalized.push_back('\r');
    }
  }

  for(; index < chunk.size(); ++index) {
    const char ch = chunk.at(index);
    if(ch == '\r') {
      if(index + 1 < chunk.size() && chunk.at(index + 1) == '\n') {
        ++index;
        normalized.push_back('\n');
      } else if(index + 1 == chunk.size()) {
        pending_carriage_return = true;
      } else {
        normalized.push_back(ch);
      }
      continue;
    }

    normalized.push_back(ch);
  }

  return normalized;
}

std::optional<std::string> extract_sse_data_line(std::string_view line) {
  if(line.rfind("data:", 0) == 0) {
    auto payload = line.substr(5);
    while(!payload.empty() && payload.front() == ' ') {
      payload.remove_prefix(1);
    }
    return std::string(payload);
  }

  if(line == "data") {
    return std::string{};
  }

  return std::nullopt;
}

}  // namespace ava::llm::providers
