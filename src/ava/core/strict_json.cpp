#include "sys.h"
#include "ava/core/json.h"
#include "ava/core/strict_json.h"

#include <string>
#include <unordered_set>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>

namespace ava::core {
namespace {

bool nesting_within_limit(std::string_view input, std::size_t max_depth)
{
  std::size_t depth = 0;
  bool in_string = false;
  bool escaped = false;
  for (char const ch : input)
  {
    if (in_string)
    {
      if (escaped)
        escaped = false;
      else if (ch == '\\')
        escaped = true;
      else if (ch == '"')
        in_string = false;
      continue;
    }
    if (ch == '"')
      in_string = true;
    else if (ch == '{' || ch == '[')
    {
      ++depth;
      if (depth > max_depth)
        return false;
    }
    else if ((ch == '}' || ch == ']') && depth > 0)
      --depth;
  }
  return true;
}

class DuplicateRejectingSax final : public nlohmann::json_sax<nlohmann::json>
{
 public:
  bool null() override { return true; }
  bool boolean(bool) override { return true; }
  bool number_integer(number_integer_t) override { return true; }
  bool number_unsigned(number_unsigned_t) override { return true; }
  bool number_float(number_float_t, string_t const&) override { return true; }
  bool string(string_t&) override { return true; }
  bool binary(binary_t&) override { return true; }

  bool start_object(std::size_t) override
  {
    objects_.emplace_back();
    return true;
  }

  bool key(string_t& value) override
  {
    if (objects_.empty())
      return false;
    if (!objects_.back().insert(value).second)
    {
      duplicate_ = true;
      return false;
    }
    return true;
  }

  bool end_object() override
  {
    if (objects_.empty())
      return false;
    objects_.pop_back();
    return true;
  }

  bool start_array(std::size_t) override { return true; }
  bool end_array() override { return true; }

  bool parse_error(std::size_t, std::string const&, nlohmann::detail::exception const&) override
  {
    syntax_error_ = true;
    return false;
  }

  [[nodiscard]] bool duplicate() const noexcept { return duplicate_; }
  [[nodiscard]] bool syntax_error() const noexcept { return syntax_error_; }

 private:
  std::vector<std::unordered_set<std::string>> objects_;
  bool duplicate_ = false;
  bool syntax_error_ = false;
};

}  // namespace

StrictJsonStatus validate_strict_json(std::string_view value, std::size_t max_nesting_depth)
{
  if (!ava::core::json::is_valid_utf8(value))
    return StrictJsonStatus::Invalid;
  if (!nesting_within_limit(value, max_nesting_depth))
    return StrictJsonStatus::NestingTooDeep;

  DuplicateRejectingSax sax;
  bool const valid = nlohmann::json::sax_parse(value.begin(), value.end(), &sax, nlohmann::json::input_format_t::json, true, false);
  if (sax.duplicate())
    return StrictJsonStatus::DuplicateObjectKey;
  if (!valid || sax.syntax_error())
    return StrictJsonStatus::Invalid;
  return StrictJsonStatus::Valid;
}

}  // namespace ava::core
