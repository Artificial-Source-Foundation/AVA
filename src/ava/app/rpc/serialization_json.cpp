#include "sys.h"
#include "ava/app/rpc/serialization_json.h"
#include "ava/core/json.h"

#include <iomanip>
#include <sstream>

namespace ava::app::rpc {

std::string decimal_field_json(std::string_view key, long double value)
{
  std::ostringstream out;
  out << std::setprecision(12) << value;
  return "\"" + std::string(key) + "\":" + out.str();
}

void append_optional_bool(std::string& json, std::string_view key, std::optional<bool> const& value)
{
  if (!value)
    return;
  json += ',';
  json += bool_field_json(key, *value);
}

void append_optional_integer(std::string& json, std::string_view key, std::optional<long long> const& value)
{
  if (!value)
    return;
  json += ',';
  json += integer_field_json(key, *value);
}

std::string string_field_json(std::string_view key, std::string_view value)
{
  return "\"" + std::string(key) + "\":\"" + ava::core::json::escape(value) + "\"";
}

std::string bool_field_json(std::string_view key, bool value)
{
  return "\"" + std::string(key) + "\":" + (value ? "true" : "false");
}

std::string number_field_json(std::string_view key, std::size_t value)
{
  return "\"" + std::string(key) + "\":" + std::to_string(value);
}

std::string integer_field_json(std::string_view key, long long value)
{
  return "\"" + std::string(key) + "\":" + std::to_string(value);
}

std::string output_array_json(std::vector<std::string> const& output)
{
  std::string json = "[";
  for (std::size_t index = 0; index < output.size(); ++index)
  {
    if (index > 0)
      json += ',';
    json += '"';
    json += ava::core::json::escape(output[index]);
    json += '"';
  }
  json += ']';
  return json;
}

std::string string_array_json(std::vector<std::string> const& values)
{
  std::string json = "[";
  for (std::size_t index = 0; index < values.size(); ++index)
  {
    if (index > 0)
      json += ',';
    json += '"';
    json += ava::core::json::escape(values[index]);
    json += '"';
  }
  json += ']';
  return json;
}

}  // namespace ava::app::rpc
