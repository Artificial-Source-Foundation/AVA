#pragma once

#include <string>

namespace ava::agent {

struct ProviderToolCall {
  std::string id;
  std::string name;
  std::string arguments_json;
};

struct ToolDispatchResult {
  std::string call_id;
  std::string name;
  bool success = false;
  std::string result_text;
};

}  // namespace ava::agent
