#include "sys.h"
#include "ava/agent/question.h"
#include "ava/core/json.h"

#include <cctype>
#include <optional>
#include <string>
#include <utility>

namespace ava::agent {
namespace {

constexpr std::size_t kMaxQuestionOptions = 64;

std::string json_bool(bool value)
{
  return value ? "true" : "false";
}

ava::core::Error argument_error(std::string_view tool_name, std::string_view argument, std::string message)
{
  auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, std::move(message));
  error.with_context("tool", std::string(tool_name));
  error.with_context("argument", std::string(argument));
  return error;
}

ava::core::VoidResult reject_control_arg(std::string_view value, std::string_view tool_name, std::string_view argument)
{
  for (char const ch : value)
  {
    auto const byte = static_cast<unsigned char>(ch);
    if (byte < 0x20 || byte == 0x7F)
    {
      return std::unexpected(argument_error(tool_name, argument, "tool argument contains a forbidden control byte"));
    }
  }
  return {};
}

void skip_ws(std::string_view text, std::size_t& index)
{
  while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index])) != 0)
  {
    ++index;
  }
}

int hex_value(char ch)
{
  if (ch >= '0' && ch <= '9')
    return ch - '0';
  if (ch >= 'a' && ch <= 'f')
    return ch - 'a' + 10;
  if (ch >= 'A' && ch <= 'F')
    return ch - 'A' + 10;
  return -1;
}

void append_utf8(std::string& out, int codepoint)
{
  if (codepoint <= 0x7F)
  {
    out.push_back(static_cast<char>(codepoint));
  }
  else if (codepoint <= 0x7FF)
  {
    out.push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
  else if (codepoint <= 0xFFFF)
  {
    out.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
  else
  {
    out.push_back(static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
}

std::optional<int> parse_hex_code_unit(std::string_view text, std::size_t hex_start)
{
  if (hex_start + 3 >= text.size())
    return std::nullopt;
  int const a = hex_value(text[hex_start]);
  int const b = hex_value(text[hex_start + 1]);
  int const c = hex_value(text[hex_start + 2]);
  int const d = hex_value(text[hex_start + 3]);
  if (a < 0 || b < 0 || c < 0 || d < 0)
    return std::nullopt;
  return (a << 12) | (b << 8) | (c << 4) | d;
}

bool is_high_surrogate(int code_unit)
{
  return code_unit >= 0xD800 && code_unit <= 0xDBFF;
}

bool is_low_surrogate(int code_unit)
{
  return code_unit >= 0xDC00 && code_unit <= 0xDFFF;
}

std::optional<std::string> parse_string_token(std::string_view text, std::size_t& index)
{
  if (index >= text.size() || text[index] != '"')
    return std::nullopt;
  std::string result;
  bool escaped = false;
  for (std::size_t cursor = index + 1; cursor < text.size(); ++cursor)
  {
    char const ch = text[cursor];
    if (escaped)
    {
      switch (ch)
      {
        case '"':
          result.push_back('"');
          break;
        case '\\':
          result.push_back('\\');
          break;
        case '/':
          result.push_back('/');
          break;
        case 'n':
          result.push_back('\n');
          break;
        case 'r':
          result.push_back('\r');
          break;
        case 't':
          result.push_back('\t');
          break;
        case 'b':
          result.push_back('\b');
          break;
        case 'f':
          result.push_back('\f');
          break;
        case 'u': {
          auto const code_unit = parse_hex_code_unit(text, cursor + 1);
          if (!code_unit)
            return std::nullopt;
          if (is_high_surrogate(*code_unit))
          {
            if (cursor + 10 < text.size() && text[cursor + 5] == '\\' && text[cursor + 6] == 'u')
            {
              auto const low = parse_hex_code_unit(text, cursor + 7);
              if (low && is_low_surrogate(*low))
              {
                append_utf8(result, ((*code_unit - 0xD800) << 10) + (*low - 0xDC00) + 0x10000);
                cursor += 10;
              }
              else
              {
                append_utf8(result, 0xFFFD);
                cursor += 4;
              }
            }
            else
            {
              append_utf8(result, 0xFFFD);
              cursor += 4;
            }
          }
          else if (is_low_surrogate(*code_unit))
          {
            append_utf8(result, 0xFFFD);
            cursor += 4;
          }
          else
          {
            append_utf8(result, *code_unit);
            cursor += 4;
          }
          break;
        }
        default:
          return std::nullopt;
      }
      escaped = false;
      continue;
    }
    if (ch == '\\')
    {
      escaped = true;
      continue;
    }
    if (ch == '"')
    {
      index = cursor + 1;
      return result;
    }
    result.push_back(ch);
  }
  return std::nullopt;
}

std::optional<std::string> parse_balanced(std::string_view text, std::size_t start, char open, char close)
{
  if (start >= text.size() || text[start] != open)
    return std::nullopt;
  bool in_string = false;
  bool escaped = false;
  int depth = 0;
  for (std::size_t index = start; index < text.size(); ++index)
  {
    char const ch = text[index];
    if (escaped)
    {
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
      in_string = !in_string;
      continue;
    }
    if (in_string)
      continue;
    if (ch == open)
      ++depth;
    if (ch == close)
    {
      --depth;
      if (depth == 0)
        return std::string(text.substr(start, index - start + 1));
    }
  }
  return std::nullopt;
}

ava::core::Result<std::string> required_text_field(std::string_view arguments, std::string_view field, std::string_view tool_name)
{
  auto value = ava::core::json::string_field(arguments, field);
  if (!value)
    return std::unexpected(argument_error(tool_name, field, "tool argument is required"));
  if (auto safe = reject_control_arg(*value, tool_name, field); !safe)
    return std::unexpected(safe.error());
  return *value;
}

ava::core::Result<std::string> optional_text_field(std::string_view arguments, std::string_view field, std::string_view tool_name)
{
  if (!ava::core::json::field_value_start(arguments, field))
    return std::string{};
  auto value = ava::core::json::string_field(arguments, field);
  if (!value)
    return std::unexpected(argument_error(tool_name, field, "tool argument must be a string"));
  if (auto safe = reject_control_arg(*value, tool_name, field); !safe)
    return std::unexpected(safe.error());
  return *value;
}

ava::core::Result<std::optional<bool>> optional_bool_field(std::string_view arguments, std::string_view field, std::string_view tool_name)
{
  auto const start = ava::core::json::field_value_start(arguments, field);
  if (!start)
    return std::optional<bool>{};
  auto const is_value_boundary = [&arguments](std::size_t index) {
    return index >= arguments.size() || std::isspace(static_cast<unsigned char>(arguments[index])) != 0 || arguments[index] == ',' || arguments[index] == '}' ||
           arguments[index] == ']';
  };
  if (arguments.substr(*start, 4) == "true" && is_value_boundary(*start + 4))
    return std::optional<bool>{true};
  if (arguments.substr(*start, 5) == "false" && is_value_boundary(*start + 5))
    return std::optional<bool>{false};
  return std::unexpected(argument_error(tool_name, field, "tool argument must be a boolean"));
}

ava::core::Result<QuestionOption> parse_option_object(std::string_view object, std::string_view tool_name)
{
  auto const value = ava::core::json::string_field(object, "value");
  auto const label = ava::core::json::string_field(object, "label");
  if (!value && !label)
  {
    return std::unexpected(argument_error(tool_name, "options", "question option requires a value or label"));
  }
  auto const option_value = value.value_or(*label);
  auto const option_label = label.value_or(option_value);
  if (auto safe = reject_control_arg(option_value, tool_name, "options"); !safe)
    return std::unexpected(safe.error());
  if (auto safe = reject_control_arg(option_label, tool_name, "options"); !safe)
    return std::unexpected(safe.error());
  return QuestionOption{.value = option_value, .label = option_label};
}

ava::core::Result<std::vector<QuestionOption>> parse_options(std::string_view arguments, std::string_view tool_name)
{
  auto const start = ava::core::json::field_value_start(arguments, "options");
  if (!start)
    return std::vector<QuestionOption>{};
  if (*start >= arguments.size() || arguments[*start] != '[')
  {
    return std::unexpected(argument_error(tool_name, "options", "question options must be an array"));
  }
  auto const array = parse_balanced(arguments, *start, '[', ']');
  if (!array)
    return std::unexpected(argument_error(tool_name, "options", "question options array is malformed"));

  std::vector<QuestionOption> options;
  std::size_t index = 1;
  while (index + 1 < array->size())
  {
    skip_ws(*array, index);
    if (index + 1 >= array->size() || (*array)[index] == ']')
      break;
    if (options.size() >= kMaxQuestionOptions)
    {
      return std::unexpected(argument_error(tool_name, "options", "question has too many options"));
    }

    if ((*array)[index] == '"')
    {
      auto label = parse_string_token(*array, index);
      if (!label)
        return std::unexpected(argument_error(tool_name, "options", "question option string is malformed"));
      if (auto safe = reject_control_arg(*label, tool_name, "options"); !safe)
        return std::unexpected(safe.error());
      options.push_back(QuestionOption{.value = *label, .label = *label});
    }
    else if ((*array)[index] == '{')
    {
      auto const object = parse_balanced(*array, index, '{', '}');
      if (!object)
        return std::unexpected(argument_error(tool_name, "options", "question option object is malformed"));
      auto option = parse_option_object(*object, tool_name);
      if (!option)
        return std::unexpected(option.error());
      options.push_back(std::move(*option));
      index += object->size();
    }
    else
    {
      return std::unexpected(argument_error(tool_name, "options", "question options must be strings or objects"));
    }

    skip_ws(*array, index);
    if (index + 1 >= array->size() || (*array)[index] == ']')
      break;
    if ((*array)[index] != ',')
    {
      return std::unexpected(argument_error(tool_name, "options", "question options array is malformed"));
    }
    ++index;
    skip_ws(*array, index);
    if (index >= array->size() || (*array)[index] == ']')
    {
      return std::unexpected(argument_error(tool_name, "options", "question options array is malformed"));
    }
  }
  return options;
}

ava::core::Result<bool> read_bool_aliases(std::string_view arguments, std::string_view primary, std::string_view alias, std::string_view tool_name)
{
  auto primary_value = optional_bool_field(arguments, primary, tool_name);
  if (!primary_value)
    return std::unexpected(primary_value.error());
  auto alias_value = optional_bool_field(arguments, alias, tool_name);
  if (!alias_value)
    return std::unexpected(alias_value.error());
  return primary_value->value_or(false) || alias_value->value_or(false);
}

}  // namespace

ava::core::Result<QuestionPrompt> parse_question_prompt(std::string_view arguments_json, std::string_view tool_name)
{
  auto header = optional_text_field(arguments_json, "header", tool_name);
  if (!header)
    return std::unexpected(header.error());
  auto question = required_text_field(arguments_json, "question", tool_name);
  if (!question)
    return std::unexpected(question.error());
  auto options = parse_options(arguments_json, tool_name);
  if (!options)
    return std::unexpected(options.error());
  auto multiple = read_bool_aliases(arguments_json, "multiple", "allow_multiple", tool_name);
  if (!multiple)
    return std::unexpected(multiple.error());
  auto custom = read_bool_aliases(arguments_json, "custom", "allow_custom", tool_name);
  if (!custom)
    return std::unexpected(custom.error());
  if (ava::core::json::field_value_start(arguments_json, "secret"))
  {
    return std::unexpected(argument_error(tool_name, "secret", "secret prompts are only available to trusted local commands"));
  }

  return QuestionPrompt{.header = std::move(*header),
                        .question = std::move(*question),
                        .options = std::move(*options),
                        .multiple = *multiple,
                        .allow_custom = *custom,
                        .secret = false};
}

std::string serialize_question_answer_result(QuestionPrompt const& prompt, QuestionAnswer const& answer)
{
  std::string text = "{\"tool\":\"question\",\"ok\":true,\"question\":\"" + ava::core::json::escape(prompt.question) +
                     "\",\"multiple\":" + json_bool(prompt.multiple) + ",\"allow_custom\":" + json_bool(prompt.allow_custom) +
                     ",\"answer\":{\"selected_options\":[";
  for (std::size_t index = 0; index < answer.selected_options.size(); ++index)
  {
    if (index > 0)
      text += ',';
    text += "\"" + ava::core::json::escape(answer.selected_options[index]) + "\"";
  }
  text += "],\"custom_text\":\"" + ava::core::json::escape(answer.custom_text) + "\"}}";
  return text;
}

}  // namespace ava::agent
