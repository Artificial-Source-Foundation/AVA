#include <fmt/format.h>

#include "ava/control_plane/control_plane.hpp"
#include "ava/config/config.hpp"
#include "ava/platform/platform.hpp"

int main() {
  fmt::print("ava_smoke: {}\n", ava::platform::platform_tag());
  const auto summary = ava::config::summary();
  fmt::print(
      "ava_smoke config: xdg={} trust={} creds={} models={}\n",
      summary.xdg_paths,
      summary.trust_store,
      summary.credential_store,
      summary.embedded_model_registry
  );
  if(const auto* spec = ava::control_plane::command_spec_by_name("submit_goal")) {
    fmt::print("ava_smoke command: {}\n", spec->name);
  }
  return 0;
}
