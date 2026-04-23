#include "ava/types/build_info.hpp"

#include "ava/core/version.hpp"

namespace ava::types {

BuildInfo current_build_info() {
  return BuildInfo{std::string(ava::core::kProjectName), std::string(ava::core::kVersion)};
}

}  // namespace ava::types
