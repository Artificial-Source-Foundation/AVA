#pragma once

#include <string>

namespace ava::types {

struct BuildInfo {
  std::string name;
  std::string version;
};

[[nodiscard]] BuildInfo current_build_info();

}  // namespace ava::types
