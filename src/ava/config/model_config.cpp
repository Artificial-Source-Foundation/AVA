#include "sys.h"
#include "ava/config/model_config.h"
#include "ava/config/model_profiles.h"
#include "ava/core/json.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ava::config {
namespace {

constexpr std::size_t max_model_config_bytes = 1024 * 1024;

ava::core::Result<std::string> read_text(std::filesystem::path const& path)
{
  std::error_code status_error;
  if (!std::filesystem::is_regular_file(path, status_error))
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "model config is not a regular file");
    error.with_context("path", path.string());
    if (status_error)
      error.with_context("cause", status_error.message());
    return std::unexpected(std::move(error));
  }
  std::error_code size_error;
  auto const size = std::filesystem::file_size(path, size_error);
  if (size_error || size > max_model_config_bytes)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "model config is too large");
    error.with_context("path", path.string());
    error.with_context("max_bytes", std::to_string(max_model_config_bytes));
    if (size_error)
      error.with_context("cause", size_error.message());
    return std::unexpected(std::move(error));
  }
  std::ifstream file(path, std::ios::binary);
  if (!file)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to open model config");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }
  std::string content;
  std::array<char, 4096> buffer{};
  while (file)
  {
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    if (file.gcount() > 0)
      content.append(buffer.data(), static_cast<std::size_t>(file.gcount()));
    if (content.size() > max_model_config_bytes)
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::Io, "model config is too large");
      error.with_context("path", path.string());
      error.with_context("max_bytes", std::to_string(max_model_config_bytes));
      return std::unexpected(std::move(error));
    }
  }
  if (!file.eof() && file.fail())
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed while reading model config");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }
  return content;
}

std::string family_from_model_id(std::string_view model_id)
{
  if (model_id == "gpt-5" || model_id.starts_with("gpt-5.") || model_id.starts_with("gpt-5-"))
    return "gpt-5";
  auto const dash = model_id.find_last_of('-');
  if (dash == std::string_view::npos)
    return std::string(model_id);
  return std::string(model_id.substr(0, dash));
}

bool is_number_delimiter(char ch)
{
  return ch == ',' || ch == '}' || ch == ']' || std::isspace(static_cast<unsigned char>(ch)) != 0;
}

std::optional<long double> number_field(std::string_view object, std::string_view key)
{
  auto const start = ava::core::json::field_value_start(object, key);
  if (!start || *start >= object.size())
    return std::nullopt;

  std::size_t index = *start;
  if (object[index] == '-')
    ++index;
  auto const digits_start = index;
  while (index < object.size() && std::isdigit(static_cast<unsigned char>(object[index])) != 0) ++index;
  if (index == digits_start)
    return std::nullopt;
  if (index < object.size() && object[index] == '.')
  {
    ++index;
    auto const fraction_start = index;
    while (index < object.size() && std::isdigit(static_cast<unsigned char>(object[index])) != 0) ++index;
    if (index == fraction_start)
      return std::nullopt;
  }
  if (index < object.size() && (object[index] == 'e' || object[index] == 'E'))
  {
    ++index;
    if (index < object.size() && (object[index] == '+' || object[index] == '-'))
      ++index;
    auto const exponent_start = index;
    while (index < object.size() && std::isdigit(static_cast<unsigned char>(object[index])) != 0) ++index;
    if (index == exponent_start)
      return std::nullopt;
  }
  if (index < object.size() && !is_number_delimiter(object[index]))
    return std::nullopt;

  try
  {
    return std::stold(std::string(object.substr(*start, index - *start)));
  }
  catch (...)
  {
    return std::nullopt;
  }
}

std::optional<long double> first_number_field(std::string_view object, std::initializer_list<std::string_view> keys)
{
  for (auto const key : keys)
  {
    if (auto const value = number_field(object, key); value && *value >= 0.0L)
      return value;
  }
  return std::nullopt;
}

std::optional<long long> positive_integer_field(std::string_view object, std::initializer_list<std::string_view> keys)
{
  for (auto const key : keys)
  {
    auto const value = ava::core::json::integer_field(object, key);
    if (value && *value > 0)
      return value;
  }
  return std::nullopt;
}

std::optional<bool> bool_field(std::string_view object, std::initializer_list<std::string_view> keys)
{
  for (auto const key : keys)
  {
    auto const start = ava::core::json::field_value_start(object, key);
    if (!start)
      continue;
    auto const value = object.substr(*start);
    if (value.starts_with("true") && (value.size() == 4 || is_number_delimiter(value[4])))
      return true;
    if (value.starts_with("false") && (value.size() == 5 || is_number_delimiter(value[5])))
      return false;
  }
  return std::nullopt;
}

bool has_any_field(std::string_view object, std::initializer_list<std::string_view> keys)
{
  for (auto const key : keys)
  {
    if (ava::core::json::field_value_start(object, key))
      return true;
  }
  return false;
}

std::vector<std::string> string_array_field(std::string_view object, std::initializer_list<std::string_view> keys)
{
  for (auto const key : keys)
  {
    auto const start = ava::core::json::field_value_start(object, key);
    if (!start || *start >= object.size() || object[*start] != '[')
      continue;
    std::vector<std::string> values;
    bool in_string = false;
    bool escaped = false;
    bool collecting = false;
    int array_depth = 1;
    int object_depth = 0;
    std::string current;
    for (std::size_t index = *start + 1; index < object.size(); ++index)
    {
      char const ch = object[index];
      if (escaped)
      {
        if (collecting)
          current.push_back(ch);
        escaped = false;
        continue;
      }
      if (ch == '\\' && in_string)
      {
        escaped = true;
        continue;
      }
      if (ch == '"')
      {
        if (!in_string)
        {
          in_string = true;
          collecting = array_depth == 1 && object_depth == 0;
          if (collecting)
            current.clear();
        }
        else
        {
          if (collecting)
          {
            values.push_back(std::move(current));
            current.clear();
          }
          in_string = false;
          collecting = false;
        }
        continue;
      }
      if (in_string)
      {
        if (collecting)
          current.push_back(ch);
        continue;
      }
      if (ch == '[')
      {
        ++array_depth;
      }
      else if (ch == ']')
      {
        --array_depth;
        if (array_depth == 0)
          return values;
        if (array_depth < 0)
          break;
      }
      else if (ch == '{')
      {
        ++object_depth;
      }
      else if (ch == '}' && object_depth > 0)
      {
        --object_depth;
      }
    }
  }
  return {};
}

std::optional<ModelPricing> pricing_from_object(std::string_view object)
{
  ModelPricing pricing;
  pricing.input_per_million = first_number_field(object, {"input_per_million", "input_usd_per_1m"});
  pricing.output_per_million = first_number_field(object, {"output_per_million", "output_usd_per_1m"});
  pricing.cache_read_per_million = first_number_field(object, {"cache_read_per_million", "cache_read_usd_per_1m"});
  pricing.cache_write_per_million = first_number_field(object, {"cache_write_per_million", "cache_write_usd_per_1m"});
  pricing.reasoning_per_million = first_number_field(object, {"reasoning_per_million", "reasoning_usd_per_1m"});
  if (!pricing.input_per_million && !pricing.output_per_million && !pricing.cache_read_per_million && !pricing.cache_write_per_million &&
      !pricing.reasoning_per_million)
  {
    return std::nullopt;
  }
  return pricing;
}

std::optional<ModelPricing> model_pricing_from_item(std::string_view item)
{
  if (auto const object = ava::core::json::object_field(item, "pricing"))
    return pricing_from_object(*object);
  return pricing_from_object(item);
}

std::string string_array_json(std::vector<std::string> const& values)
{
  std::string output = "[";
  for (std::size_t index = 0; index < values.size(); ++index)
  {
    if (index > 0)
      output += ",";
    output += "\"";
    output += ava::core::json::escape(values[index]);
    output += "\"";
  }
  output += "]";
  return output;
}

struct JsonMemberRange
{
  std::size_t key_start = 0;
  std::size_t value_start = 0;
  std::size_t value_end = 0;
};

std::optional<std::size_t> json_string_value_end(std::string_view object, std::size_t start)
{
  bool escaped = false;
  for (std::size_t index = start + 1; index < object.size(); ++index)
  {
    char const ch = object[index];
    if (escaped)
    {
      escaped = false;
      continue;
    }
    if (ch == '\\')
    {
      escaped = true;
      continue;
    }
    if (ch == '"')
      return index + 1;
  }
  return std::nullopt;
}

std::optional<std::size_t> json_balanced_value_end(std::string_view object, std::size_t start, char open, char close)
{
  bool in_string = false;
  bool escaped = false;
  int depth = 0;
  for (std::size_t index = start; index < object.size(); ++index)
  {
    char const ch = object[index];
    if (in_string)
    {
      if (escaped)
      {
        escaped = false;
        continue;
      }
      if (ch == '\\')
      {
        escaped = true;
        continue;
      }
      if (ch == '"')
        in_string = false;
      continue;
    }
    if (ch == '"')
    {
      in_string = true;
      continue;
    }
    if (ch == open)
    {
      ++depth;
      continue;
    }
    if (ch == close)
    {
      --depth;
      if (depth == 0)
        return index + 1;
      if (depth < 0)
        return std::nullopt;
    }
  }
  return std::nullopt;
}

std::optional<std::size_t> json_value_end(std::string_view object, std::size_t start)
{
  while (start < object.size() && std::isspace(static_cast<unsigned char>(object[start])) != 0) ++start;
  if (start >= object.size())
    return std::nullopt;

  char const first = object[start];
  if (first == '"')
    return json_string_value_end(object, start);
  if (first == '{')
    return json_balanced_value_end(object, start, '{', '}');
  if (first == '[')
    return json_balanced_value_end(object, start, '[', ']');

  std::size_t end = start;
  while (end < object.size() && object[end] != ',' && object[end] != '}' && object[end] != ']') ++end;
  while (end > start && std::isspace(static_cast<unsigned char>(object[end - 1])) != 0) --end;
  return end > start ? std::optional<std::size_t>(end) : std::nullopt;
}

std::optional<JsonMemberRange> top_level_member_range(std::string_view object, std::string_view key)
{
  std::string const needle = "\"" + ava::core::json::escape(key) + "\"";
  bool in_string = false;
  bool escaped = false;
  int object_depth = 0;
  int array_depth = 0;
  for (std::size_t index = 0; index < object.size(); ++index)
  {
    char const ch = object[index];
    if (in_string)
    {
      if (escaped)
      {
        escaped = false;
        continue;
      }
      if (ch == '\\')
      {
        escaped = true;
        continue;
      }
      if (ch == '"')
        in_string = false;
      continue;
    }

    if (ch == '"')
    {
      if (object_depth == 1 && array_depth == 0 && object.substr(index, needle.size()) == needle)
      {
        auto colon = index + needle.size();
        while (colon < object.size() && std::isspace(static_cast<unsigned char>(object[colon])) != 0) ++colon;
        if (colon < object.size() && object[colon] == ':')
        {
          ++colon;
          while (colon < object.size() && std::isspace(static_cast<unsigned char>(object[colon])) != 0) ++colon;
          auto end = json_value_end(object, colon);
          if (!end)
            return std::nullopt;
          return JsonMemberRange{.key_start = index, .value_start = colon, .value_end = *end};
        }
      }
      in_string = true;
      continue;
    }

    if (ch == '{')
    {
      ++object_depth;
    }
    else if (ch == '}')
    {
      if (object_depth > 0)
        --object_depth;
    }
    else if (ch == '[')
    {
      ++array_depth;
    }
    else if (ch == ']')
    {
      if (array_depth > 0)
        --array_depth;
    }
  }
  return std::nullopt;
}

std::optional<std::size_t> top_level_closing_brace(std::string_view content)
{
  for (std::size_t index = content.size(); index > 0; --index)
  {
    if (std::isspace(static_cast<unsigned char>(content[index - 1])) != 0)
      continue;
    return content[index - 1] == '}' ? std::optional<std::size_t>(index - 1) : std::nullopt;
  }
  return std::nullopt;
}

bool object_has_members(std::string_view content, std::size_t closing_brace)
{
  auto open = content.find('{');
  if (open == std::string_view::npos || open >= closing_brace)
    return false;
  for (std::size_t index = open + 1; index < closing_brace; ++index)
  {
    if (std::isspace(static_cast<unsigned char>(content[index])) == 0)
      return true;
  }
  return false;
}

void erase_member(std::string& content, JsonMemberRange range)
{
  auto erase_begin = range.key_start;
  auto erase_end = range.value_end;
  auto before = erase_begin;
  while (before > 0 && std::isspace(static_cast<unsigned char>(content[before - 1])) != 0) --before;
  if (before > 0 && content[before - 1] == ',')
  {
    erase_begin = before - 1;
  }
  else
  {
    auto after = erase_end;
    while (after < content.size() && std::isspace(static_cast<unsigned char>(content[after])) != 0) ++after;
    if (after < content.size() && content[after] == ',')
    {
      erase_end = after + 1;
      while (erase_end < content.size() && std::isspace(static_cast<unsigned char>(content[erase_end])) != 0) ++erase_end;
    }
  }
  content.erase(erase_begin, erase_end - erase_begin);
}

ava::core::Result<std::string> update_scoped_model_cycle_json(std::string content,
                                                              std::optional<std::vector<std::string>> const& scoped_model_cycle)
{
  if (!ava::core::json::is_valid_object(content))
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "model config is not a valid JSON object"));

  auto const range = top_level_member_range(content, "scoped_model_cycle");
  if (!scoped_model_cycle)
  {
    if (range)
      erase_member(content, *range);
    return content;
  }

  auto const value = string_array_json(*scoped_model_cycle);
  if (range)
  {
    content.replace(range->value_start, range->value_end - range->value_start, value);
    return content;
  }

  auto const closing = top_level_closing_brace(content);
  if (!closing)
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "model config is missing a top-level closing object"));
  std::string insertion = object_has_members(content, *closing) ? ",\n  \"scoped_model_cycle\": " : "\n  \"scoped_model_cycle\": ";
  insertion += value;
  insertion += "\n";
  content.insert(*closing, insertion);
  return content;
}

ava::core::VoidResult write_text_atomic(std::filesystem::path const& path, std::string_view body)
{
  std::error_code create_error;
  std::filesystem::create_directories(path.parent_path(), create_error);
  if (create_error)
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to create model config directory")
                               .with_context("path", path.parent_path().string())
                               .with_context("cause", create_error.message()));
  }

  auto const temp_path = path.string() + ".tmp";
  {
    std::ofstream output(temp_path, std::ios::binary | std::ios::trunc);
    if (!output)
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to write model config")
                                 .with_context("path", temp_path));
    output << body;
    if (!output)
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to finish model config write")
                                 .with_context("path", temp_path));
  }

  std::error_code permission_error;
  std::filesystem::permissions(temp_path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::replace, permission_error);

  std::error_code rename_error;
  std::filesystem::rename(temp_path, path, rename_error);
  if (rename_error)
  {
    std::error_code remove_error;
    std::filesystem::remove(temp_path, remove_error);
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to replace model config")
                               .with_context("path", path.string())
                               .with_context("cause", rename_error.message()));
  }
  return {};
}

long double millionths(long long tokens, long double price_per_million)
{
  return (static_cast<long double>(tokens) * price_per_million) / 1'000'000.0L;
}

}  // namespace

ModelRegistry builtin_model_registry()
{
  return builtin_model_profiles();
}

ModelRegistry parse_model_registry(std::string_view content)
{
  auto registry = builtin_model_registry();
  if (auto provider = ava::core::json::string_field(content, "default_provider"))
    registry.default_provider_id = *provider;
  if (auto model = ava::core::json::string_field(content, "default_model"))
    registry.default_model_id = *model;
  if (auto const scoped_start = ava::core::json::field_value_start(content, "scoped_model_cycle");
      scoped_start && *scoped_start < content.size() && content[*scoped_start] == '[')
  {
    registry.scoped_model_cycle = ava::core::json::strings_in_array_field(content, "scoped_model_cycle");
  }

  for (auto const& item : ava::core::json::objects_in_array_field(content, "models"))
  {
    auto provider = ava::core::json::string_field(item, "provider");
    auto id = ava::core::json::string_field(item, "id");
    if (!provider || !id)
      continue;
    auto model = find_model(registry, *provider, *id)
                     .value_or(ModelInfo{.provider_id = *provider,
                                         .model_id = *id,
                                         .display_name = *id,
                                         .family = family_from_model_id(*id),
                                         .context_window_tokens = std::nullopt,
                                         .max_output_tokens = std::nullopt,
                                         .pricing = std::nullopt,
                                         .api_family = "",
                                         .input_modalities = {},
                                         .supports_tools = std::nullopt,
                                         .supports_streaming = std::nullopt,
                                         .supports_reasoning = std::nullopt,
                                         .reports_usage = std::nullopt,
                                         .reasoning_levels = {},
                                         .compatibility_quirks = {},
                                         .output_modalities = {},
                                         .reasoning_format = {}});
    model.provider_id = *provider;
    model.model_id = *id;
    if (auto name = ava::core::json::string_field(item, "name"))
      model.display_name = *name;
    if (auto family = ava::core::json::string_field(item, "family"))
      model.family = *family;
    if (auto context_window = positive_integer_field(item, {"context_window_tokens", "context_window"}))
    {
      model.context_window_tokens = context_window;
    }
    if (auto max_output = positive_integer_field(item, {"max_output_tokens"}))
      model.max_output_tokens = max_output;
    if (auto pricing = model_pricing_from_item(item))
      model.pricing = pricing;
    if (auto api_family = ava::core::json::string_field(item, "api_family"))
      model.api_family = *api_family;
    if (has_any_field(item, {"input_modalities", "input"}))
    {
      model.input_modalities = string_array_field(item, {"input_modalities", "input"});
    }
    if (has_any_field(item, {"output_modalities", "output"}))
    {
      model.output_modalities = string_array_field(item, {"output_modalities", "output"});
    }
    if (auto supports_tools = bool_field(item, {"supports_tools", "tool_support", "tools"}))
    {
      model.supports_tools = supports_tools;
    }
    if (auto supports_streaming = bool_field(item, {"supports_streaming", "streaming"}))
    {
      model.supports_streaming = supports_streaming;
    }
    if (auto supports_reasoning = bool_field(item, {"supports_reasoning", "reasoning"}))
    {
      model.supports_reasoning = supports_reasoning;
    }
    if (auto reports_usage = bool_field(item, {"reports_usage", "usage_support", "usage"}))
    {
      model.reports_usage = reports_usage;
    }
    if (has_any_field(item, {"reasoning_levels"}))
      model.reasoning_levels = string_array_field(item, {"reasoning_levels"});
    if (has_any_field(item, {"compatibility_quirks", "quirks"}))
    {
      model.compatibility_quirks = string_array_field(item, {"compatibility_quirks", "quirks"});
    }
    if (auto reasoning_format = ava::core::json::string_field(item, "reasoning_format"))
    {
      model.reasoning_format = *reasoning_format;
    }
    registry.models.push_back(std::move(model));
  }
  return registry;
}

ava::core::Result<ModelRegistry> load_model_registry(XdgPaths const& paths)
{
  if (!std::filesystem::exists(paths.models_file))
    return builtin_model_registry();
  auto content = read_text(paths.models_file);
  if (!content)
    return std::unexpected(content.error());
  return parse_model_registry(*content);
}

ava::core::VoidResult store_scoped_model_cycle(XdgPaths const& paths,
                                               std::optional<std::vector<std::string>> scoped_model_cycle)
{
  std::string content = "{}\n";
  if (std::filesystem::exists(paths.models_file))
  {
    auto loaded = read_text(paths.models_file);
    if (!loaded)
      return std::unexpected(std::move(loaded.error()));
    content = std::move(*loaded);
  }
  else if (!scoped_model_cycle)
  {
    return {};
  }

  auto updated = update_scoped_model_cycle_json(std::move(content), scoped_model_cycle);
  if (!updated)
  {
    auto error = std::move(updated.error());
    error.with_context("path", paths.models_file.string());
    return std::unexpected(std::move(error));
  }
  return write_text_atomic(paths.models_file, *updated);
}

std::optional<ModelInfo> find_model(ModelRegistry const& registry, std::string_view provider_id, std::string_view model_id)
{
  for (auto it = registry.models.rbegin(); it != registry.models.rend(); ++it)
  {
    auto const& model = *it;
    if (model.provider_id == provider_id && model.model_id == model_id)
      return model;
  }
  return std::nullopt;
}

ModelInfo select_default_model(ModelRegistry const& registry)
{
  if (auto model = find_model(registry, registry.default_provider_id, registry.default_model_id))
    return *model;
  return ModelInfo{
      .provider_id = registry.default_provider_id,
      .model_id = registry.default_model_id,
      .display_name = registry.default_model_id,
      .family = family_from_model_id(registry.default_model_id),
      .context_window_tokens = std::nullopt,
      .max_output_tokens = std::nullopt,
      .pricing = std::nullopt,
      .api_family = "",
      .input_modalities = {},
      .supports_tools = true,
      .supports_streaming = true,
      .supports_reasoning = std::nullopt,
      .reports_usage = std::nullopt,
      .reasoning_levels = {},
      .compatibility_quirks = {},
      .output_modalities = {},
      .reasoning_format = {},
  };
}

std::optional<long double> usage_cost_usd(ModelPricing const& pricing, ava::provider::TokenUsage const& usage)
{
  if (usage.estimated)
    return std::nullopt;

  long long const input_tokens = usage.input_tokens.value_or(0);
  long long const cache_read_tokens = usage.cache_read_tokens.value_or(0);
  long long const cache_write_tokens = usage.cache_write_tokens.value_or(0);
  if (cache_read_tokens > 0 && !pricing.cache_read_per_million)
    return std::nullopt;
  if (cache_write_tokens > 0 && !pricing.cache_write_per_million)
    return std::nullopt;
  long long regular_input_tokens = input_tokens;
  if (pricing.cache_read_per_million)
    regular_input_tokens -= std::min(regular_input_tokens, cache_read_tokens);
  if (pricing.cache_write_per_million)
    regular_input_tokens -= std::min(regular_input_tokens, cache_write_tokens);

  auto output_tokens = usage.output_tokens.or_else([&usage, input_tokens]() -> std::optional<long long> {
    if (!usage.total_tokens || *usage.total_tokens < input_tokens)
      return std::nullopt;
    return *usage.total_tokens - input_tokens;
  });
  long long const reasoning_tokens = usage.reasoning_tokens.value_or(0);
  long long regular_output_tokens = output_tokens.value_or(0);
  if (pricing.reasoning_per_million)
    regular_output_tokens -= std::min(regular_output_tokens, reasoning_tokens);

  if (regular_input_tokens > 0 && !pricing.input_per_million)
    return std::nullopt;
  if (regular_output_tokens > 0 && !pricing.output_per_million)
    return std::nullopt;

  long double total = 0.0L;
  bool has_billable_usage = false;
  if (regular_input_tokens > 0)
  {
    total += millionths(regular_input_tokens, *pricing.input_per_million);
    has_billable_usage = true;
  }
  if (pricing.cache_read_per_million && cache_read_tokens > 0)
  {
    total += millionths(cache_read_tokens, *pricing.cache_read_per_million);
    has_billable_usage = true;
  }
  if (pricing.cache_write_per_million && cache_write_tokens > 0)
  {
    total += millionths(cache_write_tokens, *pricing.cache_write_per_million);
    has_billable_usage = true;
  }
  if (regular_output_tokens > 0)
  {
    total += millionths(regular_output_tokens, *pricing.output_per_million);
    has_billable_usage = true;
  }
  if (pricing.reasoning_per_million && reasoning_tokens > 0)
  {
    total += millionths(reasoning_tokens, *pricing.reasoning_per_million);
    has_billable_usage = true;
  }

  return has_billable_usage ? std::optional<long double>(total) : std::nullopt;
}

}  // namespace ava::config
