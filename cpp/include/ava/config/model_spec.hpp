#pragma once

#include <string>

namespace ava::config {

struct ParsedModelSpec {
  std::string provider;
  std::string model;
};

[[nodiscard]] ParsedModelSpec parse_model_spec(const std::string& spec);

}  // namespace ava::config
