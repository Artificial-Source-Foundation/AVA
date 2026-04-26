#pragma once

#include <string>

namespace ava::config {

struct ParsedModelSpec {
  std::string provider;
  std::string model;
  std::string credential_provider;
};

[[nodiscard]] ParsedModelSpec parse_model_spec(const std::string& spec);

}  // namespace ava::config
