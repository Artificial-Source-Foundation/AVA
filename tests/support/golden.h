#pragma once

#include "tests/support/test_harness.h"
#include "ava/core/json.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#ifndef AVA_080_GOLDEN_DIR
#define AVA_080_GOLDEN_DIR ""
#endif

namespace ava::tests {

inline std::filesystem::path ava_080_golden_dir()
{
  std::filesystem::path const path{AVA_080_GOLDEN_DIR};
  expect(!path.empty(), "AVA 0.80 golden fixture path is configured");
  expect(std::filesystem::is_directory(path), "AVA 0.80 golden fixture directory exists");
  return path;
}

inline std::string read_golden_fixture(std::filesystem::path const& relative_path)
{
  auto const path = ava_080_golden_dir() / relative_path;
  std::ifstream file(path, std::ios::binary);
  expect(static_cast<bool>(file), "golden fixture opens: " + path.string());
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

inline std::string canonical_json_for_golden(std::string_view value)
{
  // AVA 0.80 golden fixtures intentionally lock representative key order for
  // hand-built serializers. This is not a generic JSON semantic comparator.
  std::string canonical;
  canonical.reserve(value.size());
  bool in_string = false;
  bool escaped = false;
  for (char const ch : value)
  {
    if (in_string)
    {
      canonical.push_back(ch);
      if (escaped)
      {
        escaped = false;
      }
      else if (ch == '\\')
      {
        escaped = true;
      }
      else if (ch == '"')
      {
        in_string = false;
      }
      continue;
    }
    if (ch == '"')
    {
      in_string = true;
      canonical.push_back(ch);
      continue;
    }
    if (std::isspace(static_cast<unsigned char>(ch)) == 0)
      canonical.push_back(ch);
  }
  return canonical;
}

inline void expect_json_matches_golden(std::string_view actual, std::filesystem::path const& relative_path, std::string const& message)
{
  auto const expected = read_golden_fixture(relative_path);
  auto const expected_json = canonical_json_for_golden(expected);
  auto const actual_json = canonical_json_for_golden(actual);
  expect(ava::core::json::is_valid_object(expected_json), "golden fixture is a valid JSON object: " + relative_path.string());
  expect(ava::core::json::is_valid_object(actual_json), message + " actual JSON is a valid object");
  expect(actual_json == expected_json, message + "\nexpected: " + expected_json + "\nactual:   " + actual_json);
}

}  // namespace ava::tests
