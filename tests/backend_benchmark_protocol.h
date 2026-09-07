#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace ava::benchmark {

using JsonScalar = std::variant<bool, std::int64_t, std::uint64_t, double, std::string>;
using JsonFields = std::vector<std::pair<std::string, JsonScalar>>;

struct Observation
{
  std::uint64_t ordinal = 0;
  double value = 0.0;
  JsonFields metrics;
  JsonFields checks;
};

void emit_helper_measurement(std::string_view benchmark_case, std::string_view primary_metric, std::string_view unit,
                             std::vector<Observation> const& observations, JsonFields const& case_metrics = {});
void emit_helper_unsupported(std::string_view benchmark_case, std::string_view primary_metric, std::string_view unit, std::string_view reason_code,
                             std::string_view reason, JsonFields const& case_metrics = {});

}  // namespace ava::benchmark
