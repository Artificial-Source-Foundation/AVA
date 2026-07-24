#include "sys.h"
#include "ava/tui/composer_internal.h"
#include "ava/tui/terminal_image.h"
#include "ava/tui/text_wrap.h"
#include "ava/tui/theme.h"
#include "ava/tui/tool_cards.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <limits>
#include <numeric>
#include <vector>

namespace ava::tui {
namespace detail {
namespace {

constexpr auto kBlockMinWidth = std::size_t{32};
constexpr auto kTurnSpacingMinWidth = std::size_t{44};

bool wide_blocks(std::size_t width)
{
  return width >= kBlockMinWidth;
}

std::vector<std::string> render_thinking_block(std::string const& text, std::size_t width);

std::string trim_left(std::string_view text)
{
  std::size_t start = 0;
  while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0)
  {
    ++start;
  }
  return std::string(text.substr(start));
}

bool is_blank(std::string_view text)
{
  return std::ranges::all_of(text, [](char ch) { return std::isspace(static_cast<unsigned char>(ch)) != 0; });
}

std::string trim_ascii(std::string text)
{
  auto begin = text.begin();
  while (begin != text.end() && std::isspace(static_cast<unsigned char>(*begin)) != 0)
  {
    ++begin;
  }
  auto end = text.end();
  while (end != begin && std::isspace(static_cast<unsigned char>(*(end - 1))) != 0)
  {
    --end;
  }
  return std::string(begin, end);
}

std::string lower_ascii(std::string_view text)
{
  std::string lowered;
  lowered.reserve(text.size());
  for (char const ch : text) lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  return lowered;
}

std::string join_lines(std::vector<std::string> const& lines, std::size_t first)
{
  std::string joined;
  for (std::size_t index = first; index < lines.size(); ++index)
  {
    if (index > first)
      joined.push_back('\n');
    joined += lines[index];
  }
  return joined;
}

std::string remove_redacted_markers(std::string text)
{
  constexpr std::string_view marker = "[REDACTED]";
  for (auto found = text.find(marker); found != std::string::npos; found = text.find(marker, found))
  {
    text.erase(found, marker.size());
  }
  return text;
}

std::vector<std::string> hard_wrap_sanitized(std::string_view text, std::size_t width)
{
  auto const content_width = std::max<std::size_t>(1, width);
  std::vector<std::string> lines;
  std::string current;
  std::size_t columns = 0;
  for (std::size_t index = 0; index < text.size();)
  {
    auto const cell = terminal_text_cell(text, index);
    auto const chunk_length = cell.valid ? cell.bytes : std::size_t{1};
    auto const chunk_columns = cell.columns;
    if (columns + chunk_columns > content_width && !current.empty())
    {
      lines.push_back(std::move(current));
      current.clear();
      columns = 0;
      continue;
    }
    current.append(text.substr(index, chunk_length));
    columns += chunk_columns;
    index += chunk_length;
  }
  lines.push_back(std::move(current));
  return lines;
}

std::vector<std::string> words_in(std::string_view text)
{
  std::vector<std::string> words;
  for (std::size_t index = 0; index < text.size();)
  {
    while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index])) != 0)
    {
      ++index;
    }
    auto const start = index;
    while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index])) == 0)
    {
      ++index;
    }
    if (start < index)
      words.emplace_back(text.substr(start, index - start));
  }
  return words;
}

std::string text_span_sgr(Rendition const& rendition)
{
  std::string sgr;
  if (rendition.bold)
    sgr += std::string(kSgrBold);
  if (rendition.italic)
    sgr += std::string(kSgrItalic);
  if (rendition.underline)
    sgr += std::string(kSgrUnderline);
  if (rendition.strikethrough)
    sgr += std::string(kSgrStrikethrough);
  if (rendition.code)
  {
    sgr += std::string(kSgrWarning);
  }
  else if (rendition.color == TextColorRole::Thinking)
  {
    sgr += std::string(kSgrThinking);
  }
  else if (rendition.color == TextColorRole::Muted || rendition.dim)
  {
    sgr += std::string(kSgrDim);
  }
  else if (rendition.color == TextColorRole::Success || rendition.color == TextColorRole::Added)
  {
    sgr += std::string(kSgrSuccess);
  }
  else if (rendition.color == TextColorRole::Error || rendition.color == TextColorRole::Removed)
  {
    sgr += std::string(kSgrError);
  }
  else if (rendition.color == TextColorRole::Warning || rendition.color == TextColorRole::Code)
  {
    sgr += std::string(kSgrWarning);
  }
  else if (rendition.color == TextColorRole::Accent)
  {
    sgr += std::string(kSgrAccent);
  }
  return sgr;
}

char uppercase_hex_digit(unsigned value)
{
  return static_cast<char>(value < 10 ? ('0' + value) : ('A' + (value - 10)));
}

std::string sanitize_osc8_target(std::string_view target)
{
  std::string sanitized;
  sanitized.reserve(target.size());
  for (auto const ch : target)
  {
    auto const byte = static_cast<unsigned char>(ch);
    if (byte < 0x20 || byte == 0x7F)
    {
      sanitized.push_back('%');
      sanitized.push_back(uppercase_hex_digit(byte >> 4U));
      sanitized.push_back(uppercase_hex_digit(byte & 0x0FU));
      continue;
    }
    sanitized.push_back(ch);
  }
  return sanitized;
}

std::string osc8_open(std::string_view target)
{
  return std::string("\x1b]8;;") + sanitize_osc8_target(target) + "\x1b\\";
}

std::string osc8_close()
{
  return "\x1b]8;;\x1b\\";
}

bool terminal_hyperlinks_enabled()
{
  return !tui_plain_output() && active_terminal_image_capabilities().hyperlinks;
}

std::string render_inline_text(Text const& text, bool enable_hyperlinks = false)
{
  std::string rendered;
  for (auto const& run : text.runs)
  {
    if (std::holds_alternative<NewLine>(run))
    {
      rendered.push_back(' ');
    }
    else if (auto const* string = std::get_if<String>(&run))
    {
      rendered += string->text;
    }
    else if (auto const* span = std::get_if<TextSpan>(&run))
    {
      auto const sgr = text_span_sgr(span->rendition);
      auto const hyperlink = enable_hyperlinks && !span->link_target.empty();
      if (hyperlink)
        rendered += osc8_open(span->link_target);
      if (sgr.empty())
      {
        rendered += span->text;
      }
      else
      {
        rendered += sgr;
        rendered += span->text;
        rendered += std::string(kSgrReset);
      }
      if (hyperlink)
        rendered += osc8_close();
    }
  }
  return rendered;
}

std::string text_model_or(Text const& model, std::string const& fallback)
{
  if (text_empty(model))
    return fallback;
  return to_plain_text(model);
}

void push_wrapped_word(std::vector<std::string>& output, std::string const& word, std::string const& next_prefix, std::string& current,
                       std::size_t& current_cols, std::size_t width)
{
  auto remaining = word;
  while (!remaining.empty())
  {
    auto const prefix_cols = terminal_text_columns(current);
    auto const available = width > prefix_cols ? width - prefix_cols : std::size_t{1};
    auto pieces = hard_wrap_sanitized(remaining, available);
    auto const consumed = pieces.front().size();
    current += pieces.front();
    current_cols = terminal_text_columns(current);
    if (pieces.size() == 1)
      return;
    output.push_back(fit_line_preserving_sgr(std::move(current), width));
    current = next_prefix;
    current_cols = terminal_text_columns(current);
    remaining.erase(0, consumed);
  }
}

std::vector<std::string> wrap_words_with_prefix(std::string_view text, std::size_t width, std::string const& first_prefix = {},
                                                std::string const& next_prefix = {})
{
  auto const content_width = std::max<std::size_t>(1, width);
  std::vector<std::string> output;
  std::string current = first_prefix;
  std::size_t current_cols = terminal_text_columns(current);
  auto const continuation = next_prefix.empty() ? std::string(terminal_text_columns(first_prefix), ' ') : next_prefix;
  auto const continuation_cols = terminal_text_columns(continuation);
  auto const words = words_in(text);
  if (words.empty())
  {
    output.push_back(std::move(current));
    return output;
  }

  bool line_has_word = false;
  for (auto const& word : words)
  {
    auto const word_cols = terminal_text_columns(word);
    if (line_has_word && current_cols + 1 + word_cols <= content_width)
    {
      current.push_back(' ');
      ++current_cols;
      current += word;
      current_cols += word_cols;
      continue;
    }

    if (line_has_word)
    {
      output.push_back(std::move(current));
      current = continuation;
      current_cols = continuation_cols;
      line_has_word = false;
    }
    if (current_cols + word_cols <= content_width)
    {
      current += word;
      current_cols += word_cols;
      line_has_word = true;
    }
    else
    {
      push_wrapped_word(output, word, continuation, current, current_cols, content_width);
      line_has_word = current_cols > continuation_cols;
    }
  }
  output.push_back(std::move(current));
  return output;
}

std::string render_inline_markup(std::string_view sanitized_text)
{
  auto const hyperlinks = terminal_hyperlinks_enabled();
  return render_inline_text(text_from_markdown(sanitized_text, !hyperlinks), hyperlinks);
}

bool is_identifier_start(char ch)
{
  auto const value = static_cast<unsigned char>(ch);
  return std::isalpha(value) != 0 || ch == '_';
}

bool is_identifier_continue(char ch)
{
  auto const value = static_cast<unsigned char>(ch);
  return std::isalnum(value) != 0 || ch == '_';
}

std::string first_fence_language(std::string_view fence)
{
  if (!fence.starts_with("```"))
    return {};
  auto language = trim_ascii(std::string(fence.substr(3)));
  auto const space = language.find_first_of(" \t");
  if (space != std::string::npos)
    language.erase(space);
  return lower_ascii(language);
}

bool language_is_diff(std::string_view language)
{
  return language == "diff" || language == "patch";
}

bool language_is_json(std::string_view language)
{
  return language == "json" || language == "jsonc";
}

bool language_is_shell(std::string_view language)
{
  return language == "sh" || language == "bash" || language == "zsh" || language == "shell";
}

bool language_is_javascript(std::string_view language)
{
  return language == "js" || language == "jsx" || language == "mjs" || language == "cjs" || language == "javascript";
}

bool language_is_typescript(std::string_view language)
{
  return language == "ts" || language == "tsx" || language == "typescript";
}

bool language_is_powershell(std::string_view language)
{
  return language == "ps1" || language == "pwsh" || language == "powershell";
}

bool language_is_markup(std::string_view language)
{
  return language == "html" || language == "htm" || language == "xml";
}

bool language_is_css(std::string_view language)
{
  return language == "css" || language == "scss" || language == "sass" || language == "less";
}

bool language_is_yaml(std::string_view language)
{
  return language == "yaml" || language == "yml";
}

bool language_is_cmake(std::string_view language)
{
  return language == "cmake" || language == "cmakelists";
}

bool language_is_toml(std::string_view language)
{
  return language == "toml";
}

bool language_is_ini(std::string_view language)
{
  return language == "ini" || language == "conf" || language == "cfg" || language == "dotenv" || language == "env" || language == "properties";
}

bool language_has_c_comments(std::string_view language)
{
  return language == "c" || language == "cc" || language == "cpp" || language == "cxx" || language == "h" || language == "hpp" || language == "hxx" ||
         language_is_javascript(language) || language_is_typescript(language) || language == "go" || language == "rs" || language == "java" ||
         language == "cs" || language == "csharp";
}

bool language_supported_for_highlight(std::string_view language)
{
  return language_is_diff(language) || language_is_json(language) || language_is_shell(language) || language_is_powershell(language) ||
         language_is_markup(language) || language_is_css(language) || language_is_yaml(language) || language_is_cmake(language) || language_is_toml(language) ||
         language_is_ini(language) || language_has_c_comments(language) || language == "py" || language == "python";
}

bool syntax_keyword(std::string_view language, std::string_view word)
{
  static constexpr std::string_view common_keywords[] = {
      "alignas",   "auto",     "bool",     "break",   "case",      "catch",  "char",     "class",  "concept", "const",    "constexpr", "continue", "co_await",
      "co_return", "co_yield", "decltype", "default", "delete",    "do",     "double",   "else",   "enum",    "explicit", "export",    "extern",   "false",
      "final",     "float",    "for",      "friend",  "if",        "import", "inline",   "int",    "long",    "module",   "namespace", "new",      "noexcept",
      "nullptr",   "operator", "override", "private", "protected", "public", "requires", "return", "short",   "signed",   "sizeof",    "static",   "struct",
      "switch",    "template", "this",     "throw",   "true",      "try",    "typename", "using",  "virtual", "void",     "while"};
  static constexpr std::string_view shell_keywords[] = {"case",     "do", "done", "elif",  "else",     "esac", "export", "fi",   "for",
                                                        "function", "if", "in",   "local", "readonly", "then", "until",  "while"};
  static constexpr std::string_view python_keywords[] = {"and",  "as",     "assert", "async",  "await",  "break",   "class",    "continue", "def",
                                                         "del",  "elif",   "else",   "except", "False",  "finally", "for",      "from",     "global",
                                                         "if",   "import", "in",     "is",     "lambda", "None",    "nonlocal", "not",      "or",
                                                         "pass", "raise",  "return", "True",   "try",    "while",   "with",     "yield"};
  static constexpr std::string_view javascript_keywords[] = {
      "async", "await",   "break", "case",   "catch",    "class", "const", "continue", "debugger", "default",    "delete", "do",   "else",  "export", "extends",
      "false", "finally", "for",   "from",   "function", "get",   "if",    "import",   "in",       "instanceof", "let",    "new",  "null",  "of",     "return",
      "set",   "static",  "super", "switch", "this",     "throw", "true",  "try",      "typeof",   "undefined",  "var",    "void", "while", "yield"};
  static constexpr std::string_view typescript_keywords[] = {"abstract",   "any",    "as",        "asserts", "bigint",    "boolean", "declare",   "enum",
                                                             "implements", "infer",  "interface", "is",      "keyof",     "module",  "namespace", "never",
                                                             "number",     "object", "override",  "private", "protected", "public",  "readonly",  "satisfies",
                                                             "string",     "symbol", "type",      "unknown", "unique"};
  static constexpr std::string_view powershell_keywords[] = {"begin",   "break",  "catch",    "class", "continue", "data",   "do",      "dynamicparam",
                                                             "else",    "elseif", "end",      "enum",  "exit",     "filter", "finally", "for",
                                                             "foreach", "from",   "function", "if",    "in",       "param",  "process", "return",
                                                             "switch",  "throw",  "trap",     "try",   "until",    "using",  "var",     "while"};
  if (language_is_shell(language))
    return std::ranges::find(shell_keywords, word) != std::ranges::end(shell_keywords);
  if (language == "py" || language == "python")
    return std::ranges::find(python_keywords, word) != std::ranges::end(python_keywords);
  if (language_is_powershell(language))
    return std::ranges::find(powershell_keywords, word) != std::ranges::end(powershell_keywords);
  if (language_is_typescript(language))
    return std::ranges::find(typescript_keywords, word) != std::ranges::end(typescript_keywords) ||
           std::ranges::find(javascript_keywords, word) != std::ranges::end(javascript_keywords);
  if (language_is_javascript(language))
    return std::ranges::find(javascript_keywords, word) != std::ranges::end(javascript_keywords);
  return std::ranges::find(common_keywords, word) != std::ranges::end(common_keywords);
}

std::string syntax_segment(std::string_view text, TextColorRole role)
{
  return std::string(kSgrReset) + text_span_sgr(Rendition{.color = role}) + std::string(text) + std::string(kSgrReset) + std::string(kSgrDim);
}

std::string highlight_plain_code_line(std::string_view code)
{
  return std::string(kSgrDim) + std::string(code) + std::string(kSgrReset);
}

std::string highlight_diff_code_line(std::string_view code)
{
  if (code.starts_with("+") && !code.starts_with("+++"))
    return text_span_sgr(Rendition{.color = TextColorRole::Added}) + std::string(code) + std::string(kSgrReset);
  if (code.starts_with("-") && !code.starts_with("---"))
    return text_span_sgr(Rendition{.color = TextColorRole::Removed}) + std::string(code) + std::string(kSgrReset);
  if (code.starts_with("@@"))
    return text_span_sgr(Rendition{.color = TextColorRole::Accent}) + std::string(code) + std::string(kSgrReset);
  return highlight_plain_code_line(code);
}

std::size_t quoted_string_end(std::string_view code, std::size_t start)
{
  auto const quote = code[start];
  auto end = start + 1;
  bool escaped = false;
  while (end < code.size())
  {
    auto const ch = code[end++];
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
    if (ch == quote)
      break;
  }
  return end;
}

std::string highlight_markup_code_line(std::string_view code)
{
  std::string output = std::string(kSgrDim);
  for (std::size_t index = 0; index < code.size();)
  {
    if (code.substr(index).starts_with("<!--"))
    {
      auto end = code.find("-->", index + 4);
      end = end == std::string_view::npos ? code.size() : end + 3;
      output += syntax_segment(code.substr(index, end - index), TextColorRole::Muted);
      index = end;
      continue;
    }

    if (code[index] == '"' || code[index] == '\'')
    {
      auto const end = quoted_string_end(code, index);
      output += syntax_segment(code.substr(index, end - index), TextColorRole::Success);
      index = end;
      continue;
    }

    if (code[index] == '<')
    {
      output += syntax_segment(code.substr(index, 1), TextColorRole::Muted);
      ++index;
      while (index < code.size() && (code[index] == '/' || code[index] == '!' || code[index] == '?'))
      {
        output += syntax_segment(code.substr(index, 1), TextColorRole::Muted);
        ++index;
      }
      if (index < code.size() && is_identifier_start(code[index]))
      {
        auto end = index + 1;
        while (end < code.size() && (is_identifier_continue(code[end]) || code[end] == '-' || code[end] == ':')) ++end;
        output += syntax_segment(code.substr(index, end - index), TextColorRole::Accent);
        index = end;
      }
      continue;
    }

    if (is_identifier_start(code[index]))
    {
      auto end = index + 1;
      while (end < code.size() && (is_identifier_continue(code[end]) || code[end] == '-' || code[end] == ':')) ++end;
      auto lookahead = end;
      while (lookahead < code.size() && std::isspace(static_cast<unsigned char>(code[lookahead])) != 0) ++lookahead;
      if (lookahead < code.size() && code[lookahead] == '=')
      {
        output += syntax_segment(code.substr(index, end - index), TextColorRole::Warning);
      }
      else
      {
        output += code.substr(index, end - index);
      }
      index = end;
      continue;
    }

    if (std::isdigit(static_cast<unsigned char>(code[index])) != 0)
    {
      auto end = index + 1;
      while (end < code.size() && std::isalnum(static_cast<unsigned char>(code[end])) != 0) ++end;
      output += syntax_segment(code.substr(index, end - index), TextColorRole::Accent);
      index = end;
      continue;
    }

    if (std::ispunct(static_cast<unsigned char>(code[index])) != 0)
    {
      output += syntax_segment(code.substr(index, 1), TextColorRole::Muted);
      ++index;
      continue;
    }

    output.push_back(code[index]);
    ++index;
  }
  output += std::string(kSgrReset);
  return output;
}

std::string highlight_css_code_line(std::string_view code)
{
  std::string output = std::string(kSgrDim);
  for (std::size_t index = 0; index < code.size();)
  {
    if (code.substr(index).starts_with("/*"))
    {
      auto end = code.find("*/", index + 2);
      end = end == std::string_view::npos ? code.size() : end + 2;
      output += syntax_segment(code.substr(index, end - index), TextColorRole::Muted);
      index = end;
      continue;
    }

    if (code[index] == '"' || code[index] == '\'')
    {
      auto const end = quoted_string_end(code, index);
      output += syntax_segment(code.substr(index, end - index), TextColorRole::Success);
      index = end;
      continue;
    }

    if (code[index] == '#')
    {
      auto end = index + 1;
      while (end < code.size() && std::isxdigit(static_cast<unsigned char>(code[end])) != 0) ++end;
      if (end > index + 1)
      {
        output += syntax_segment(code.substr(index, end - index), TextColorRole::Accent);
        index = end;
        continue;
      }
    }

    if (std::isdigit(static_cast<unsigned char>(code[index])) != 0)
    {
      auto end = index + 1;
      while (end < code.size())
      {
        auto const ch = code[end];
        if (std::isalnum(static_cast<unsigned char>(ch)) == 0 && ch != '.' && ch != '%')
          break;
        ++end;
      }
      output += syntax_segment(code.substr(index, end - index), TextColorRole::Accent);
      index = end;
      continue;
    }

    if (is_identifier_start(code[index]) || code[index] == '-')
    {
      auto end = index + 1;
      while (end < code.size() && (is_identifier_continue(code[end]) || code[end] == '-')) ++end;
      auto lookahead = end;
      while (lookahead < code.size() && std::isspace(static_cast<unsigned char>(code[lookahead])) != 0) ++lookahead;
      if (lookahead < code.size() && code[lookahead] == ':')
      {
        output += syntax_segment(code.substr(index, end - index), TextColorRole::Warning);
      }
      else
      {
        output += code.substr(index, end - index);
      }
      index = end;
      continue;
    }

    if (std::ispunct(static_cast<unsigned char>(code[index])) != 0)
    {
      output += syntax_segment(code.substr(index, 1), TextColorRole::Muted);
      ++index;
      continue;
    }

    output.push_back(code[index]);
    ++index;
  }
  output += std::string(kSgrReset);
  return output;
}

std::string highlight_yaml_code_line(std::string_view code)
{
  std::string output = std::string(kSgrDim);
  for (std::size_t index = 0; index < code.size();)
  {
    if (code[index] == '#')
    {
      output += syntax_segment(code.substr(index), TextColorRole::Muted);
      break;
    }

    if (code[index] == '"' || code[index] == '\'')
    {
      auto const end = quoted_string_end(code, index);
      output += syntax_segment(code.substr(index, end - index), TextColorRole::Success);
      index = end;
      continue;
    }

    if (std::isdigit(static_cast<unsigned char>(code[index])) != 0)
    {
      auto end = index + 1;
      while (end < code.size())
      {
        auto const ch = code[end];
        if (std::isalnum(static_cast<unsigned char>(ch)) == 0 && ch != '.' && ch != '_' && ch != '+' && ch != '-')
          break;
        ++end;
      }
      output += syntax_segment(code.substr(index, end - index), TextColorRole::Accent);
      index = end;
      continue;
    }

    if (is_identifier_start(code[index]))
    {
      auto end = index + 1;
      while (end < code.size() && (is_identifier_continue(code[end]) || code[end] == '-')) ++end;
      auto const word = code.substr(index, end - index);
      auto lookahead = end;
      while (lookahead < code.size() && std::isspace(static_cast<unsigned char>(code[lookahead])) != 0) ++lookahead;
      if ((lookahead < code.size() && code[lookahead] == ':') || word == "true" || word == "false" || word == "null")
      {
        output += syntax_segment(word, TextColorRole::Warning);
      }
      else
      {
        output += word;
      }
      index = end;
      continue;
    }

    if (std::ispunct(static_cast<unsigned char>(code[index])) != 0)
    {
      output += syntax_segment(code.substr(index, 1), TextColorRole::Muted);
      ++index;
      continue;
    }

    output.push_back(code[index]);
    ++index;
  }
  output += std::string(kSgrReset);
  return output;
}

bool config_scalar_word(std::string_view word)
{
  auto const lowered = lower_ascii(word);
  return lowered == "true" || lowered == "false" || lowered == "null" || lowered == "on" || lowered == "off" || lowered == "yes" || lowered == "no";
}

std::string highlight_cmake_code_line(std::string_view code)
{
  std::string output = std::string(kSgrDim);
  for (std::size_t index = 0; index < code.size();)
  {
    if (code[index] == '#')
    {
      output += syntax_segment(code.substr(index), TextColorRole::Muted);
      break;
    }

    if (code.substr(index).starts_with("${"))
    {
      auto end = code.find('}', index + 2);
      end = end == std::string_view::npos ? code.size() : end + 1;
      output += syntax_segment(code.substr(index, end - index), TextColorRole::Accent);
      index = end;
      continue;
    }

    if (code[index] == '"' || code[index] == '\'')
    {
      auto const end = quoted_string_end(code, index);
      output += syntax_segment(code.substr(index, end - index), TextColorRole::Success);
      index = end;
      continue;
    }

    if (std::isdigit(static_cast<unsigned char>(code[index])) != 0)
    {
      auto end = index + 1;
      while (end < code.size())
      {
        auto const ch = code[end];
        if (std::isalnum(static_cast<unsigned char>(ch)) == 0 && ch != '.' && ch != '_' && ch != '+' && ch != '-')
          break;
        ++end;
      }
      output += syntax_segment(code.substr(index, end - index), TextColorRole::Accent);
      index = end;
      continue;
    }

    if (is_identifier_start(code[index]))
    {
      auto end = index + 1;
      while (end < code.size() && is_identifier_continue(code[end])) ++end;
      auto const word = code.substr(index, end - index);
      auto lookahead = end;
      while (lookahead < code.size() && std::isspace(static_cast<unsigned char>(code[lookahead])) != 0) ++lookahead;
      if (lookahead < code.size() && code[lookahead] == '(')
      {
        output += syntax_segment(word, TextColorRole::Accent);
      }
      else if (config_scalar_word(word))
      {
        output += syntax_segment(word, TextColorRole::Warning);
      }
      else
      {
        output += word;
      }
      index = end;
      continue;
    }

    if (std::ispunct(static_cast<unsigned char>(code[index])) != 0)
    {
      output += syntax_segment(code.substr(index, 1), TextColorRole::Muted);
      ++index;
      continue;
    }

    output.push_back(code[index]);
    ++index;
  }
  output += std::string(kSgrReset);
  return output;
}

std::string highlight_key_value_config_code_line(std::string_view code, bool semicolon_comments)
{
  std::string output = std::string(kSgrDim);
  for (std::size_t index = 0; index < code.size();)
  {
    if (code[index] == '#' || (semicolon_comments && code[index] == ';'))
    {
      output += syntax_segment(code.substr(index), TextColorRole::Muted);
      break;
    }

    if (code[index] == '[')
    {
      auto end = code.find(']', index + 1);
      if (end != std::string_view::npos)
      {
        output += syntax_segment(code.substr(index, 1), TextColorRole::Muted);
        if (end > index + 1)
          output += syntax_segment(code.substr(index + 1, end - index - 1), TextColorRole::Accent);
        output += syntax_segment(code.substr(end, 1), TextColorRole::Muted);
        index = end + 1;
        continue;
      }
    }

    if (code[index] == '"' || code[index] == '\'')
    {
      auto const end = quoted_string_end(code, index);
      output += syntax_segment(code.substr(index, end - index), TextColorRole::Success);
      index = end;
      continue;
    }

    if (std::isdigit(static_cast<unsigned char>(code[index])) != 0)
    {
      auto end = index + 1;
      while (end < code.size())
      {
        auto const ch = code[end];
        if (std::isalnum(static_cast<unsigned char>(ch)) == 0 && ch != '.' && ch != '_' && ch != '+' && ch != '-')
          break;
        ++end;
      }
      output += syntax_segment(code.substr(index, end - index), TextColorRole::Accent);
      index = end;
      continue;
    }

    if (is_identifier_start(code[index]) || code[index] == '-')
    {
      auto end = index + 1;
      while (end < code.size() && (is_identifier_continue(code[end]) || code[end] == '-' || code[end] == '.')) ++end;
      auto const word = code.substr(index, end - index);
      auto lookahead = end;
      while (lookahead < code.size() && std::isspace(static_cast<unsigned char>(code[lookahead])) != 0) ++lookahead;
      if (lookahead < code.size() && (code[lookahead] == '=' || code[lookahead] == ':'))
      {
        output += syntax_segment(word, TextColorRole::Warning);
      }
      else if (config_scalar_word(word))
      {
        output += syntax_segment(word, TextColorRole::Warning);
      }
      else
      {
        output += word;
      }
      index = end;
      continue;
    }

    if (std::ispunct(static_cast<unsigned char>(code[index])) != 0)
    {
      output += syntax_segment(code.substr(index, 1), TextColorRole::Muted);
      ++index;
      continue;
    }

    output.push_back(code[index]);
    ++index;
  }
  output += std::string(kSgrReset);
  return output;
}

std::string highlight_source_code_line(std::string_view code, std::string_view language)
{
  if (!language_supported_for_highlight(language))
    return highlight_plain_code_line(code);
  if (language_is_diff(language))
    return highlight_diff_code_line(code);
  if (language_is_markup(language))
    return highlight_markup_code_line(code);
  if (language_is_css(language))
    return highlight_css_code_line(code);
  if (language_is_yaml(language))
    return highlight_yaml_code_line(code);
  if (language_is_cmake(language))
    return highlight_cmake_code_line(code);
  if (language_is_toml(language))
    return highlight_key_value_config_code_line(code, false);
  if (language_is_ini(language))
    return highlight_key_value_config_code_line(code, true);

  std::string output = std::string(kSgrDim);
  for (std::size_t index = 0; index < code.size();)
  {
    if ((language_has_c_comments(language) && code.substr(index).starts_with("//")) ||
        ((language_is_shell(language) || language_is_powershell(language) || language == "py" || language == "python" || language_is_json(language)) &&
         code[index] == '#'))
    {
      output += syntax_segment(code.substr(index), TextColorRole::Muted);
      index = code.size();
      break;
    }

    if (code[index] == '"' || code[index] == '\'' || (language_is_shell(language) && code[index] == '`'))
    {
      auto const end = quoted_string_end(code, index);
      output += syntax_segment(code.substr(index, end - index), TextColorRole::Success);
      index = end;
      continue;
    }

    if (std::isdigit(static_cast<unsigned char>(code[index])) != 0)
    {
      auto end = index + 1;
      while (end < code.size())
      {
        auto const ch = code[end];
        if (std::isalnum(static_cast<unsigned char>(ch)) == 0 && ch != '.' && ch != '_' && ch != '+' && ch != '-')
          break;
        ++end;
      }
      output += syntax_segment(code.substr(index, end - index), TextColorRole::Accent);
      index = end;
      continue;
    }

    if (is_identifier_start(code[index]))
    {
      auto end = index + 1;
      while (end < code.size() && is_identifier_continue(code[end])) ++end;
      auto const word = code.substr(index, end - index);
      if (syntax_keyword(language, word))
      {
        output += syntax_segment(word, TextColorRole::Warning);
      }
      else
      {
        auto lookahead = end;
        while (lookahead < code.size() && std::isspace(static_cast<unsigned char>(code[lookahead])) != 0) ++lookahead;
        if (lookahead < code.size() && code[lookahead] == '(')
        {
          output += syntax_segment(word, TextColorRole::Accent);
        }
        else
        {
          output += word;
        }
      }
      index = end;
      continue;
    }

    if (std::ispunct(static_cast<unsigned char>(code[index])) != 0)
    {
      output += syntax_segment(code.substr(index, 1), TextColorRole::Muted);
      ++index;
      continue;
    }

    output.push_back(code[index]);
    ++index;
  }
  output += std::string(kSgrReset);
  return output;
}

std::string render_code_line(std::string_view line, std::string_view language)
{
  auto code = line;
  std::string indent;
  if (code.starts_with("  "))
  {
    indent = std::string(kSgrDim) + "  " + std::string(kSgrReset);
    code.remove_prefix(2);
  }
  return indent + highlight_source_code_line(code, language);
}

std::string render_wide_content(std::string_view border_sgr, std::string content, std::size_t width)
{
  auto line = std::string("  ") + std::string(border_sgr) + "│" + std::string(kSgrReset) + " " + std::move(content);
  return fit_line_preserving_sgr(std::move(line), width);
}

std::vector<std::string> render_narrow_assistant_lines(std::vector<std::string> const& content_lines, std::size_t width)
{
  std::vector<std::string> lines;
  auto const prefix = std::string("  ");
  auto const content_width = width > prefix.size() ? width - prefix.size() : std::size_t{1};
  bool in_code = false;
  std::string code_language;
  for (auto const& content : content_lines)
  {
    auto const is_fence = content.rfind("```", 0) == 0;
    auto const format_inline_markup = !is_fence && !in_code;
    auto wrapped = wrap_words_with_prefix(content, content_width);
    for (auto& part : wrapped)
    {
      auto rendered_part = format_inline_markup
                               ? render_inline_markup(part)
                               : (in_code && !is_fence ? render_code_line(part, code_language) : std::string(kSgrDim) + part + std::string(kSgrReset));
      auto line = prefix + std::move(rendered_part);
      lines.push_back(fit_line_preserving_sgr(std::move(line), width));
    }
    if (is_fence)
    {
      if (!in_code)
        code_language = first_fence_language(content);
      in_code = !in_code;
      if (!in_code)
        code_language.clear();
    }
  }
  if (lines.empty())
    lines.push_back(fit_line(prefix, width));
  return lines;
}

std::vector<std::string> render_assistant_meta_lines(std::string const& meta, std::size_t width)
{
  if (meta.empty())
    return {};
  auto const sanitized = sanitize_terminal_text(meta);
  if (!wide_blocks(width))
  {
    auto line = std::string("     ") + std::string(kSgrAccent) + "* " + std::string(kSgrDim) + sanitized + std::string(kSgrReset);
    return {fit_line_preserving_sgr(std::move(line), width)};
  }
  auto line = std::string(kSgrAccent) + "* " + std::string(kSgrDim) + sanitized + std::string(kSgrReset);
  return {render_wide_content(kSgrMuted, std::move(line), width)};
}

std::vector<std::string> render_user_block(std::string const& text, std::size_t width)
{
  std::vector<std::string> lines;
  constexpr auto kUserPrefixColumns = std::size_t{4};
  auto const wrap_width = width > kUserPrefixColumns ? width - kUserPrefixColumns : std::size_t{1};
  auto const first_prefix = std::string("  ") + std::string(kSgrAccent) + "›" + std::string(kSgrReset) + " ";
  auto const continuation_prefix = std::string(kUserPrefixColumns, ' ');
  auto first_line = true;
  for (auto const& raw_line : split_lines(text))
  {
    auto const sanitized = sanitize_terminal_text(raw_line);
    auto wrapped = wrap_words_with_prefix(sanitized, wrap_width);
    for (auto& part : wrapped)
    {
      auto line = (first_line ? first_prefix : continuation_prefix) + std::string(kSgrBold) + std::string(kSgrText) + std::move(part) + std::string(kSgrReset);
      lines.push_back(fit_line_preserving_sgr(std::move(line), width));
      first_line = false;
    }
  }
  if (lines.empty())
    lines.push_back(fit_line_preserving_sgr(first_prefix + std::string(kSgrBold) + std::string(kSgrText) + std::string(kSgrReset), width));
  return lines;
}

std::string rendered_markdown_list_indent(std::string_view line)
{
  std::size_t leading_spaces = 0;
  while (leading_spaces < line.size() && line[leading_spaces] == ' ')
  {
    ++leading_spaces;
  }
  auto const depth = leading_spaces / 2;
  return std::string(depth * 4, ' ');
}

enum class MarkdownTableAlignment
{
  Left,
  Center,
  Right,
};

struct NumberedListParts
{
  std::string indent;
  char delimiter = '.';
  std::size_t source_number = 1;
  std::string body;
};

struct OrderedListState
{
  std::string indent;
  char delimiter = '.';
  std::size_t next_number = 1;
};

bool parse_markdown_table_row(std::string_view line, std::vector<std::string>& cells)
{
  auto trimmed = trim_ascii(std::string(line));
  if (trimmed.find('|') == std::string::npos)
    return false;
  if (!trimmed.empty() && trimmed.front() == '|')
    trimmed.erase(trimmed.begin());
  if (!trimmed.empty() && trimmed.back() == '|')
    trimmed.pop_back();

  cells.clear();
  std::string cell;
  bool escaped = false;
  for (char const ch : trimmed)
  {
    if (escaped)
    {
      cell.push_back(ch);
      escaped = false;
      continue;
    }
    if (ch == '\\')
    {
      escaped = true;
      continue;
    }
    if (ch == '|')
    {
      cells.push_back(trim_ascii(std::move(cell)));
      cell.clear();
      continue;
    }
    cell.push_back(ch);
  }
  if (escaped)
    cell.push_back('\\');
  cells.push_back(trim_ascii(std::move(cell)));
  return !cells.empty();
}

bool parse_markdown_table_separator(std::vector<std::string> const& cells, std::vector<MarkdownTableAlignment>& alignments)
{
  if (cells.empty())
    return false;
  alignments.clear();
  alignments.reserve(cells.size());
  for (auto const& raw_cell : cells)
  {
    auto cell = trim_ascii(raw_cell);
    if (cell.empty())
      return false;
    auto const starts_colon = cell.front() == ':';
    if (starts_colon)
      cell.erase(cell.begin());
    auto const ends_colon = !cell.empty() && cell.back() == ':';
    if (ends_colon)
      cell.pop_back();
    auto const alignment =
        starts_colon && ends_colon ? MarkdownTableAlignment::Center : (ends_colon ? MarkdownTableAlignment::Right : MarkdownTableAlignment::Left);
    if (cell.size() < 3 || !std::ranges::all_of(cell, [](char ch) { return ch == '-'; }))
      return false;
    alignments.push_back(alignment);
  }
  return true;
}

std::size_t longest_word_columns(std::string const& text)
{
  std::size_t longest = 1;
  for (auto const& word : words_in(text))
  {
    longest = std::max(longest, terminal_text_columns(word));
  }
  return longest;
}

std::vector<std::size_t> markdown_table_widths(std::vector<std::vector<std::string>> const& rows, std::size_t column_count, std::size_t content_width)
{
  auto widths = std::vector<std::size_t>(column_count, 1);
  for (auto const& row : rows)
  {
    for (std::size_t column = 0; column < column_count && column < row.size(); ++column)
    {
      auto const natural = terminal_text_columns(row[column]);
      widths[column] = std::max(widths[column], std::max(longest_word_columns(row[column]), natural));
    }
  }

  auto const border_columns = (column_count * 3) + 1;
  auto const available_cells = content_width > border_columns + column_count ? content_width - border_columns : column_count;
  auto width_sum = std::accumulate(widths.begin(), widths.end(), std::size_t{0});
  while (width_sum > available_cells)
  {
    auto widest = std::max_element(widths.begin(), widths.end());
    if (widest == widths.end() || *widest <= 1)
      break;
    --(*widest);
    --width_sum;
  }
  return widths;
}

std::string align_markdown_table_cell(std::string text, std::size_t width, MarkdownTableAlignment alignment)
{
  auto const columns = terminal_text_columns(text);
  if (columns >= width)
    return fit_line_preserving_sgr(std::move(text), width);
  auto const padding = width - columns;
  switch (alignment)
  {
    case MarkdownTableAlignment::Left:
      return text + std::string(padding, ' ');
    case MarkdownTableAlignment::Right:
      return std::string(padding, ' ') + text;
    case MarkdownTableAlignment::Center: {
      auto const left = padding / 2;
      auto const right = padding - left;
      return std::string(left, ' ') + text + std::string(right, ' ');
    }
  }
  return text;
}

std::string styled_text_segment(std::string_view text, Rendition const& rendition)
{
  auto const sgr = text_span_sgr(rendition);
  if (sgr.empty())
    return std::string(text);
  return sgr + std::string(text) + std::string(kSgrReset);
}

std::string take_cell_word_chunk(std::string_view& remaining, std::size_t width)
{
  auto const budget = std::max<std::size_t>(1, width);
  std::size_t columns = 0;
  std::size_t end = 0;
  while (end < remaining.size())
  {
    auto const cell = terminal_text_cell(remaining, end);
    auto const chunk_length = cell.valid ? cell.bytes : std::size_t{1};
    auto const chunk_columns = cell.columns;
    if (columns > 0 && columns + chunk_columns > budget)
      break;
    if (columns == 0 && chunk_columns > budget)
    {
      end += chunk_length;
      columns += chunk_columns;
      break;
    }
    end += chunk_length;
    columns += chunk_columns;
    if (columns >= budget)
      break;
  }
  if (end == 0)
    end = 1;
  auto chunk = std::string(remaining.substr(0, end));
  remaining.remove_prefix(end);
  return chunk;
}

void append_markdown_table_word(std::vector<std::string>& output, std::string& current, std::size_t& current_columns, bool& line_has_word,
                                std::string_view word, Rendition const& rendition, std::size_t width)
{
  auto remaining = word;
  while (!remaining.empty())
  {
    auto const leading_space = line_has_word ? std::size_t{1} : std::size_t{0};
    auto available = width > current_columns + leading_space ? width - current_columns - leading_space : std::size_t{0};
    if (available == 0 && line_has_word)
    {
      output.push_back(std::move(current));
      current.clear();
      current_columns = 0;
      line_has_word = false;
      available = width;
    }

    auto const remaining_columns = terminal_text_columns(remaining);
    if (remaining_columns <= available)
    {
      if (line_has_word)
      {
        current.push_back(' ');
        ++current_columns;
      }
      current += styled_text_segment(remaining, rendition);
      current_columns += remaining_columns;
      line_has_word = true;
      remaining = {};
      continue;
    }

    if (line_has_word)
    {
      output.push_back(std::move(current));
      current.clear();
      current_columns = 0;
      line_has_word = false;
      continue;
    }

    auto chunk = take_cell_word_chunk(remaining, width);
    auto const chunk_columns = terminal_text_columns(chunk);
    current += styled_text_segment(chunk, rendition);
    current_columns += chunk_columns;
    line_has_word = true;
    if (!remaining.empty())
    {
      output.push_back(std::move(current));
      current.clear();
      current_columns = 0;
      line_has_word = false;
    }
  }
}

std::vector<std::string> wrap_markdown_table_cell(std::string_view cell, std::size_t width)
{
  auto const content_width = std::max<std::size_t>(1, width);
  std::vector<std::string> output;
  std::string current;
  std::size_t current_columns = 0;
  bool line_has_word = false;

  auto const text = text_from_markdown(cell);
  for (auto const& run : text.runs)
  {
    if (std::holds_alternative<NewLine>(run))
    {
      if (line_has_word || !current.empty())
        output.push_back(std::move(current));
      current.clear();
      current_columns = 0;
      line_has_word = false;
      continue;
    }

    auto const* raw_text = std::get_if<String>(&run);
    auto const* span = std::get_if<TextSpan>(&run);
    auto const value = raw_text != nullptr ? std::string_view(raw_text->text) : std::string_view(span->text);
    auto const rendition = span != nullptr ? span->rendition : Rendition{};
    for (std::size_t index = 0; index < value.size();)
    {
      while (index < value.size() && std::isspace(static_cast<unsigned char>(value[index])) != 0) ++index;
      auto const start = index;
      while (index < value.size() && std::isspace(static_cast<unsigned char>(value[index])) == 0) ++index;
      if (start < index)
      {
        append_markdown_table_word(output, current, current_columns, line_has_word, value.substr(start, index - start), rendition, content_width);
      }
    }
  }

  if (line_has_word || !current.empty())
    output.push_back(std::move(current));
  if (output.empty())
    output.emplace_back();
  return output;
}

std::string markdown_table_divider(std::vector<std::size_t> const& widths)
{
  std::string line = "├";
  for (std::size_t index = 0; index < widths.size(); ++index)
  {
    if (index > 0)
      line += "┼";
    for (std::size_t repeat = 0; repeat < widths[index] + 2; ++repeat) line += "─";
  }
  line += "┤";
  return line;
}

std::string markdown_table_border(std::vector<std::size_t> const& widths, std::string_view left, std::string_view join, std::string_view right)
{
  std::string line(left);
  for (std::size_t index = 0; index < widths.size(); ++index)
  {
    if (index > 0)
      line += join;
    for (std::size_t repeat = 0; repeat < widths[index] + 2; ++repeat) line += "─";
  }
  line += right;
  return line;
}

std::vector<std::string> render_markdown_table_row(std::vector<std::string> const& row, std::vector<std::size_t> const& widths,
                                                   std::vector<MarkdownTableAlignment> const& alignments)
{
  std::vector<std::vector<std::string>> wrapped_cells;
  wrapped_cells.reserve(widths.size());
  std::size_t height = 1;
  for (std::size_t column = 0; column < widths.size(); ++column)
  {
    auto const cell = column < row.size() ? std::string_view(row[column]) : std::string_view{};
    auto wrapped = wrap_markdown_table_cell(cell, widths[column]);
    height = std::max(height, wrapped.size());
    wrapped_cells.push_back(std::move(wrapped));
  }

  std::vector<std::string> lines;
  lines.reserve(height);
  for (std::size_t line_index = 0; line_index < height; ++line_index)
  {
    std::string line = "│";
    for (std::size_t column = 0; column < widths.size(); ++column)
    {
      auto cell_line = line_index < wrapped_cells[column].size() ? wrapped_cells[column][line_index] : std::string{};
      auto const alignment = column < alignments.size() ? alignments[column] : MarkdownTableAlignment::Left;
      line += " " + align_markdown_table_cell(std::move(cell_line), widths[column], alignment) + " │";
    }
    lines.push_back(std::move(line));
  }
  return lines;
}

std::vector<std::string> render_markdown_table(std::vector<std::vector<std::string>> const& rows, std::vector<MarkdownTableAlignment> const& alignments,
                                               std::size_t content_width)
{
  if (rows.empty())
    return {};
  auto const column_count = rows.front().size();
  auto const widths = markdown_table_widths(rows, column_count, content_width);
  auto const divider = markdown_table_divider(widths);

  std::vector<std::string> output;
  output.push_back(markdown_table_border(widths, "┌", "┬", "┐"));
  auto header = render_markdown_table_row(rows.front(), widths, alignments);
  output.insert(output.end(), header.begin(), header.end());
  if (rows.size() > 1)
    output.push_back(divider);
  for (std::size_t row_index = 1; row_index < rows.size(); ++row_index)
  {
    auto rendered = render_markdown_table_row(rows[row_index], widths, alignments);
    output.insert(output.end(), rendered.begin(), rendered.end());
    if (row_index + 1 < rows.size())
      output.push_back(divider);
  }
  output.push_back(markdown_table_border(widths, "└", "┴", "┘"));
  return output;
}

bool collect_markdown_table(std::vector<std::string> const& raw_lines, std::size_t start, std::vector<std::string>& rendered, std::size_t& consumed,
                            std::size_t content_width)
{
  if (start + 1 >= raw_lines.size())
    return false;

  std::vector<std::string> header;
  std::vector<std::string> separator;
  if (!parse_markdown_table_row(sanitize_terminal_text(raw_lines[start]), header) ||
      !parse_markdown_table_row(sanitize_terminal_text(raw_lines[start + 1]), separator) || header.size() != separator.size())
  {
    return false;
  }

  std::vector<MarkdownTableAlignment> alignments;
  if (!parse_markdown_table_separator(separator, alignments))
    return false;

  std::vector<std::vector<std::string>> rows{header};
  consumed = 2;
  for (std::size_t index = start + 2; index < raw_lines.size(); ++index)
  {
    auto const sanitized = sanitize_terminal_text(raw_lines[index]);
    if (is_blank(sanitized))
      break;
    std::vector<std::string> row;
    if (!parse_markdown_table_row(sanitized, row))
      break;
    row.resize(header.size());
    rows.push_back(std::move(row));
    ++consumed;
  }

  auto const min_table_width = (header.size() * 4) + 1;
  if (content_width < min_table_width)
  {
    for (std::size_t index = start; index < start + consumed && index < raw_lines.size(); ++index)
    {
      auto wrapped = hard_wrap_sanitized(sanitize_terminal_text(raw_lines[index]), content_width);
      rendered.insert(rendered.end(), wrapped.begin(), wrapped.end());
    }
    return !rendered.empty();
  }

  rendered = render_markdown_table(rows, alignments, content_width);
  return !rendered.empty();
}

bool parse_bullet(std::string_view line, std::string& marker, std::string& body)
{
  auto const trimmed = trim_left(line);
  if (trimmed.size() >= 2 && (trimmed[0] == '-' || trimmed[0] == '*' || trimmed[0] == '+') && trimmed[1] == ' ')
  {
    auto const list_indent = rendered_markdown_list_indent(line);
    if (trimmed.size() >= 6 && trimmed[2] == '[' && (trimmed[3] == ' ' || trimmed[3] == 'x' || trimmed[3] == 'X') && trimmed[4] == ']' && trimmed[5] == ' ')
    {
      marker = list_indent + std::string(trimmed.substr(0, 6));
      body = std::string(trimmed.substr(6));
      return true;
    }
    marker = list_indent + std::string(1, trimmed[0]) + " ";
    body = std::string(trimmed.substr(2));
    return true;
  }
  return false;
}

bool parse_numbered(std::string_view line, std::string& marker, std::string& body)
{
  auto const trimmed = trim_left(line);
  std::size_t index = 0;
  while (index < trimmed.size() && std::isdigit(static_cast<unsigned char>(trimmed[index])) != 0)
  {
    ++index;
  }
  if (index == 0 || index + 1 >= trimmed.size() || (trimmed[index] != '.' && trimmed[index] != ')') || trimmed[index + 1] != ' ')
  {
    return false;
  }
  marker = rendered_markdown_list_indent(line) + std::string(trimmed.substr(0, index + 2));
  body = std::string(trimmed.substr(index + 2));
  return true;
}

bool parse_numbered_parts(std::string_view line, NumberedListParts& parts)
{
  auto const trimmed = trim_left(line);
  std::size_t index = 0;
  std::size_t number = 0;
  while (index < trimmed.size() && std::isdigit(static_cast<unsigned char>(trimmed[index])) != 0)
  {
    auto const digit = static_cast<std::size_t>(trimmed[index] - '0');
    if (number <= (std::numeric_limits<std::size_t>::max() - digit) / 10)
    {
      number = (number * 10) + digit;
    }
    ++index;
  }
  if (index == 0 || index + 1 >= trimmed.size() || (trimmed[index] != '.' && trimmed[index] != ')') || trimmed[index + 1] != ' ')
  {
    return false;
  }
  parts.indent = rendered_markdown_list_indent(line);
  parts.delimiter = trimmed[index];
  parts.source_number = number == 0 ? std::size_t{1} : number;
  parts.body = std::string(trimmed.substr(index + 2));
  return true;
}

std::string normalized_numbered_marker(NumberedListParts const& parts, std::vector<OrderedListState>& states)
{
  auto state = std::ranges::find_if(
      states, [&](OrderedListState const& candidate) { return candidate.indent == parts.indent && candidate.delimiter == parts.delimiter; });
  if (state == states.end())
  {
    states.push_back(OrderedListState{.indent = parts.indent, .delimiter = parts.delimiter, .next_number = parts.source_number});
    state = std::prev(states.end());
  }

  auto const number = state->next_number;
  if (state->next_number < std::numeric_limits<std::size_t>::max())
    ++state->next_number;
  std::erase_if(states, [&](OrderedListState const& candidate) { return candidate.indent.size() > parts.indent.size(); });
  return parts.indent + std::to_string(number) + std::string(1, parts.delimiter) + " ";
}

void clear_ordered_list_state_at_or_below(std::vector<OrderedListState>& states, std::size_t indent_columns)
{
  std::erase_if(states, [&](OrderedListState const& state) { return state.indent.size() >= indent_columns; });
}

bool parse_blockquote(std::string_view line, std::string& body)
{
  auto const trimmed = trim_left(line);
  if (!trimmed.empty() && trimmed[0] == '>')
  {
    auto const start = trimmed.size() >= 2 && trimmed[1] == ' ' ? std::size_t{2} : std::size_t{1};
    body = std::string(trimmed.substr(start));
    return true;
  }
  return false;
}

bool is_markdown_horizontal_rule(std::string_view line)
{
  auto const trimmed = trim_ascii(std::string(line));
  if (trimmed.size() < 3)
    return false;
  auto const marker = trimmed.front();
  if (marker != '-' && marker != '*' && marker != '_')
    return false;
  std::size_t marker_count = 0;
  for (char const ch : trimmed)
  {
    if (ch == marker)
    {
      ++marker_count;
      continue;
    }
    if (ch != ' ')
      return false;
  }
  return marker_count >= 3;
}

std::string render_markdown_horizontal_rule(std::size_t content_width)
{
  std::string rule;
  auto const width = std::max<std::size_t>(1, content_width);
  for (std::size_t index = 0; index < width; ++index) rule += "─";
  return rule;
}

bool is_markdown_heading(std::string_view line)
{
  auto const trimmed = trim_left(line);
  std::size_t marker_size = 0;
  while (marker_size < trimmed.size() && marker_size < 6 && trimmed[marker_size] == '#')
  {
    ++marker_size;
  }
  return marker_size > 0 && marker_size < trimmed.size() && trimmed[marker_size] == ' ';
}

bool markdown_lazy_blockquote_continuation(std::string_view line)
{
  if (is_blank(line) || line.rfind("```", 0) == 0 || is_markdown_heading(line) || is_markdown_horizontal_rule(line))
    return false;
  std::string marker;
  std::string body;
  if (parse_bullet(line, marker, body) || parse_numbered(line, marker, body))
    return false;
  std::vector<std::string> cells;
  auto const trimmed = trim_ascii(std::string(line));
  return !(trimmed.starts_with('|') && parse_markdown_table_row(trimmed, cells));
}

void append_blockquote_lines(std::vector<std::string>& output, std::vector<std::string> const& bodies, std::size_t content_width)
{
  for (auto const& body : bodies)
  {
    auto wrapped = wrap_words_with_prefix(body, content_width, "│ ", "│ ");
    output.insert(output.end(), wrapped.begin(), wrapped.end());
  }
}

std::vector<std::string> assistant_content_lines(std::string const& text, std::size_t content_width)
{
  std::vector<std::string> output;
  auto const raw_lines = split_lines(text);
  bool in_code = false;
  bool pending_block_separator = false;
  std::string list_continuation_prefix;
  std::vector<OrderedListState> ordered_list_states;
  auto push_blank_line_once = [&output] {
    if (!output.empty() && !output.back().empty())
      output.emplace_back();
  };
  auto flush_pending_block_separator = [&] {
    if (!pending_block_separator)
      return;
    push_blank_line_once();
    pending_block_separator = false;
  };

  for (std::size_t raw_index = 0; raw_index < raw_lines.size(); ++raw_index)
  {
    auto const& raw_line = raw_lines[raw_index];
    auto const sanitized = sanitize_terminal_text(raw_line);
    if (sanitized.rfind("```", 0) == 0)
    {
      if (!in_code)
      {
        flush_pending_block_separator();
        push_blank_line_once();
      }
      in_code = !in_code;
      auto fence_label = std::string("```");
      if (sanitized.size() > 3)
        fence_label += " " + trim_left(std::string_view(sanitized).substr(3));
      output.push_back(fence_label);
      pending_block_separator = !in_code;
      continue;
    }
    if (in_code)
    {
      auto wrapped = hard_wrap_sanitized(sanitized, content_width > 2 ? content_width - 2 : std::size_t{1});
      for (auto& part : wrapped)
      {
        output.push_back("  " + std::move(part));
      }
      continue;
    }
    if (is_blank(sanitized))
    {
      push_blank_line_once();
      pending_block_separator = false;
      continue;
    }
    flush_pending_block_separator();

    if (is_markdown_horizontal_rule(sanitized))
    {
      ordered_list_states.clear();
      list_continuation_prefix.clear();
      output.push_back(render_markdown_horizontal_rule(content_width));
      pending_block_separator = true;
      continue;
    }

    if (is_markdown_heading(sanitized))
    {
      ordered_list_states.clear();
      list_continuation_prefix.clear();
      auto wrapped = wrap_words_with_prefix(sanitized, content_width);
      output.insert(output.end(), wrapped.begin(), wrapped.end());
      pending_block_separator = true;
      continue;
    }

    std::vector<std::string> table;
    std::size_t consumed_table_lines = 0;
    if (collect_markdown_table(raw_lines, raw_index, table, consumed_table_lines, content_width))
    {
      ordered_list_states.clear();
      list_continuation_prefix.clear();
      output.insert(output.end(), table.begin(), table.end());
      raw_index += consumed_table_lines - 1;
      continue;
    }

    std::string marker;
    std::string body;
    if (parse_bullet(sanitized, marker, body))
    {
      clear_ordered_list_state_at_or_below(ordered_list_states, rendered_markdown_list_indent(sanitized).size());
      auto const indent = std::string(terminal_text_columns(marker), ' ');
      list_continuation_prefix = indent;
      std::string quote_body;
      if (parse_blockquote(body, quote_body))
      {
        auto wrapped = wrap_words_with_prefix(quote_body, content_width, marker + "│ ", indent + "│ ");
        output.insert(output.end(), wrapped.begin(), wrapped.end());
        continue;
      }
      auto wrapped = wrap_words_with_prefix(body, content_width, marker, indent);
      output.insert(output.end(), wrapped.begin(), wrapped.end());
      continue;
    }
    NumberedListParts numbered;
    if (parse_numbered_parts(sanitized, numbered))
    {
      marker = normalized_numbered_marker(numbered, ordered_list_states);
      body = numbered.body;
      auto const indent = std::string(terminal_text_columns(marker), ' ');
      list_continuation_prefix = indent;
      std::string quote_body;
      if (parse_blockquote(body, quote_body))
      {
        auto wrapped = wrap_words_with_prefix(quote_body, content_width, marker + "│ ", indent + "│ ");
        output.insert(output.end(), wrapped.begin(), wrapped.end());
        continue;
      }
      auto wrapped = wrap_words_with_prefix(body, content_width, marker, indent);
      output.insert(output.end(), wrapped.begin(), wrapped.end());
      continue;
    }
    if (!list_continuation_prefix.empty() && !sanitized.empty() && sanitized.front() == ' ')
    {
      auto wrapped = wrap_words_with_prefix(trim_left(sanitized), content_width, list_continuation_prefix, list_continuation_prefix);
      output.insert(output.end(), wrapped.begin(), wrapped.end());
      continue;
    }
    if (parse_blockquote(sanitized, body))
    {
      ordered_list_states.clear();
      list_continuation_prefix.clear();
      std::vector<std::string> quote_bodies{body};
      while (raw_index + 1 < raw_lines.size())
      {
        auto const next = sanitize_terminal_text(raw_lines[raw_index + 1]);
        std::string next_body;
        if (parse_blockquote(next, next_body))
        {
          quote_bodies.push_back(std::move(next_body));
          ++raw_index;
          continue;
        }
        if (!markdown_lazy_blockquote_continuation(next))
          break;
        quote_bodies.push_back(next);
        ++raw_index;
      }
      append_blockquote_lines(output, quote_bodies, content_width);
      continue;
    }

    ordered_list_states.clear();
    list_continuation_prefix.clear();
    auto wrapped = wrap_words_with_prefix(sanitized, content_width);
    output.insert(output.end(), wrapped.begin(), wrapped.end());
  }
  if (output.empty())
    output.emplace_back();
  return output;
}

std::vector<std::string> render_assistant_text_block(std::vector<std::string> const& content, std::size_t width)
{
  std::vector<std::string> lines;
  bool in_code = false;
  std::string code_language;
  for (auto const& part : content)
  {
    auto const is_fence = part.rfind("```", 0) == 0;
    std::string rendered;
    if (is_fence)
    {
      rendered = std::string(kSgrDim) + part + std::string(kSgrReset);
    }
    else if (in_code)
    {
      rendered = render_code_line(part, code_language);
    }
    else
    {
      rendered = render_inline_markup(part);
    }
    lines.push_back(fit_line_preserving_sgr(std::string("  ") + std::move(rendered), width));
    if (is_fence)
    {
      if (!in_code)
        code_language = first_fence_language(part);
      in_code = !in_code;
      if (!in_code)
        code_language.clear();
    }
  }
  return lines;
}

std::vector<std::string> render_assistant_block(std::string const& text, std::string const& meta, std::string const& thinking, std::size_t width)
{
  auto const content_width = width > 4 ? width - 4 : std::size_t{1};
  auto const content = text.empty() ? std::vector<std::string>{} : assistant_content_lines(text, content_width);
  std::vector<std::string> lines;

  if (!thinking.empty())
  {
    auto meta_lines = render_assistant_meta_lines(meta, width);
    lines.insert(lines.end(), meta_lines.begin(), meta_lines.end());
    auto thinking_lines = render_thinking_block(thinking, width);
    lines.insert(lines.end(), thinking_lines.begin(), thinking_lines.end());
  }

  if (!content.empty())
  {
    auto text_lines = wide_blocks(width) ? render_assistant_text_block(content, width) : render_narrow_assistant_lines(content, width);
    lines.insert(lines.end(), text_lines.begin(), text_lines.end());
  }

  if (thinking.empty())
  {
    auto meta_lines = render_assistant_meta_lines(meta, width);
    lines.insert(lines.end(), meta_lines.begin(), meta_lines.end());
  }

  return lines;
}

std::vector<std::string> render_thinking_block(std::string const& text, std::size_t width)
{
  auto const visible_text = trim_ascii(remove_redacted_markers(text));
  if (visible_text.empty())
    return {};

  auto const content_width = width > 4 ? width - 4 : std::size_t{1};
  std::vector<std::string> lines;
  bool first_content_line = true;
  for (auto const& raw_line : split_lines(visible_text))
  {
    if (is_blank(raw_line))
    {
      auto const blank = wide_blocks(width) ? render_wide_content(kSgrThinking, {}, width) : std::string{};
      lines.push_back(fit_line_preserving_sgr(blank, width));
      continue;
    }

    auto const sanitized = sanitize_terminal_text(raw_line);
    auto const first_prefix = first_content_line ? std::string("Thinking: ") : std::string{};
    auto const next_prefix = first_content_line ? std::string(first_prefix.size(), ' ') : std::string{};
    auto wrapped = wrap_words_with_prefix(sanitized, content_width, first_prefix, next_prefix);
    bool first_wrapped_line = true;
    for (auto& part : wrapped)
    {
      std::string styled;
      if (first_content_line && first_wrapped_line && part.rfind("Thinking: ", 0) == 0)
      {
        styled = std::string(kSgrThinking) + "Thinking:" + std::string(kSgrReset) + " " + std::string(kSgrThinking) +
                 part.substr(std::string_view("Thinking: ").size()) + std::string(kSgrReset);
      }
      else
      {
        styled = std::string(kSgrThinking) + std::move(part) + std::string(kSgrReset);
      }
      if (wide_blocks(width))
      {
        lines.push_back(render_wide_content(kSgrThinking, std::move(styled), width));
      }
      else
      {
        lines.push_back(fit_line_preserving_sgr(std::move(styled), width));
      }
      first_wrapped_line = false;
    }
    first_content_line = false;
  }
  return lines;
}

std::string render_error_line(std::string const& text, std::size_t width)
{
  auto prefix = std::string("  ") + std::string(kSgrError) + '!' + std::string(kSgrReset) + " ";
  constexpr auto kPrefixCols = std::size_t{4};
  auto content_width = width > kPrefixCols ? width - kPrefixCols : 0;
  auto content = fit_line(sanitize_terminal_text(text), content_width);
  return prefix + content;
}

std::vector<std::string> render_error_detail_lines(std::string const& details, std::size_t width)
{
  if (details.empty())
    return {};
  constexpr auto kMaxErrorDetailLines = std::size_t{8};
  auto raw_lines = split_lines(details);
  std::vector<std::string> lines;
  auto const prefix = wide_blocks(width) ? std::string("  │     ") : std::string("      ");
  auto const content_prefix = wide_blocks(width) ? std::string("  │       ") : std::string("        ");
  auto header = prefix + std::string(kSgrDim) + "details:" + std::string(kSgrReset);
  lines.push_back(fit_line_preserving_sgr(std::move(header), width));
  auto const visible = std::min(raw_lines.size(), kMaxErrorDetailLines);
  for (std::size_t index = 0; index < visible; ++index)
  {
    lines.push_back(
        fit_line_preserving_sgr(content_prefix + std::string(kSgrMuted) + sanitize_terminal_text(raw_lines[index]) + std::string(kSgrReset), width));
  }
  if (raw_lines.size() > visible)
  {
    auto hidden = content_prefix + std::string(kSgrDim) + "… " + std::to_string(raw_lines.size() - visible) + " more detail lines" + std::string(kSgrReset);
    lines.push_back(fit_line_preserving_sgr(std::move(hidden), width));
  }
  return lines;
}

std::vector<std::string> render_error_block(std::string const& text, std::string const& meta, std::size_t width, bool details_visible)
{
  auto raw_lines = split_lines(text);
  auto summary = raw_lines.empty() ? std::string{} : raw_lines.front();
  if (summary.empty())
    summary = "error";

  auto details = meta.empty() && raw_lines.size() > 1 ? join_lines(raw_lines, 1) : meta;
  std::vector<std::string> lines{render_error_line(summary, width)};
  if (details.empty())
    return lines;

  if (!details_visible)
  {
    auto const prefix = wide_blocks(width) ? std::string("  │     ") : std::string("      ");
    auto hint = prefix + std::string(kSgrDim) + "details hidden · /details" + std::string(kSgrReset);
    lines.push_back(fit_line_preserving_sgr(std::move(hint), width));
    return lines;
  }

  auto detail_lines = render_error_detail_lines(details, width);
  lines.insert(lines.end(), detail_lines.begin(), detail_lines.end());
  return lines;
}

std::vector<std::string> render_compaction_block(std::string const& text, std::size_t width)
{
  auto const sanitized = sanitize_terminal_text(text);
  auto const first_prefix = std::string("  ") + std::string(kSgrWarning) + "compact" + std::string(kSgrReset) + " ";
  auto const next_prefix = std::string(terminal_text_columns(first_prefix), ' ');
  auto wrapped = wrap_words_with_prefix(sanitized, width, first_prefix, next_prefix);
  for (auto& line : wrapped) line = fit_line_preserving_sgr(std::move(line), width);
  return wrapped;
}

}  // namespace

std::string render_generic_line(std::string const& text, std::size_t width)
{
  auto prefix = std::string("  ") + std::string(kSgrDim) + "·" + std::string(kSgrReset) + " ";
  constexpr auto kPrefixCols = std::size_t{4};
  auto content_width = width > kPrefixCols ? width - kPrefixCols : 0;
  auto content = fit_line(sanitize_terminal_text(text), content_width);
  return prefix + content;
}

namespace {

enum class TranscriptGroup
{
  User,
  AssistantFlow,
  Error,
  System
};

TranscriptGroup transcript_group(TranscriptItem const& item)
{
  if (item.tool || item.label == "ava" || item.label == "thinking")
    return TranscriptGroup::AssistantFlow;
  if (item.label == "you")
    return TranscriptGroup::User;
  if (item.label == "error")
    return TranscriptGroup::Error;
  return TranscriptGroup::System;
}

bool transcript_item_is_visible(TranscriptItem const& item, bool thinking_visible)
{
  if (item.tool || item.label == "you" || item.label == "error" || item.label == "compaction")
    return true;
  if (item.label == "ava")
  {
    auto assistant_text = item.text;
    if (assistant_text.empty() && !text_empty(item.text_model))
      assistant_text = to_plain_text(item.text_model);
    if (!assistant_text.empty() || !item.meta.empty())
      return true;
    return thinking_visible && !trim_ascii(remove_redacted_markers(text_model_or(item.thinking_model, item.thinking))).empty();
  }
  if (item.label == "thinking")
    return thinking_visible && !trim_ascii(remove_redacted_markers(text_model_or(item.text_model, item.text))).empty();
  return true;
}

bool has_preceding_visible_item(std::vector<TranscriptItem> const& transcript, std::size_t index, bool thinking_visible)
{
  for (auto previous = index; previous > 0; --previous)
  {
    if (transcript_item_is_visible(transcript[previous - 1], thinking_visible))
      return true;
  }
  return false;
}

bool starts_logical_group(std::vector<TranscriptItem> const& transcript, std::size_t index, bool thinking_visible)
{
  auto const group = transcript_group(transcript[index]);
  if (group == TranscriptGroup::User || group == TranscriptGroup::Error)
    return true;
  for (auto previous = index; previous > 0; --previous)
  {
    auto const previous_index = previous - 1;
    if (transcript_item_is_visible(transcript[previous_index], thinking_visible))
      return group != transcript_group(transcript[previous_index]);
  }
  return true;
}

bool roomy_transcript_spacing(std::size_t width, bool compact_spacing)
{
  return width >= kTurnSpacingMinWidth && !compact_spacing;
}

void trim_trailing_ascii_whitespace(std::string& text)
{
  while (!text.empty())
  {
    auto const ch = text.back();
    if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r' && ch != '\f' && ch != '\v')
      break;
    text.pop_back();
  }
}

std::string normalize_tool_result_identity(std::string text)
{
  trim_trailing_ascii_whitespace(text);
  text = sanitize_terminal_text(text);
  trim_trailing_ascii_whitespace(text);
  return text;
}

bool suppress_adjacent_tool_result(std::vector<TranscriptItem> const& transcript, std::size_t index)
{
  if (index == 0 || index >= transcript.size())
    return false;
  auto const& item = transcript[index];
  auto const& previous = transcript[index - 1];
  if (item.tool || item.label != "ava" || !previous.tool)
    return false;

  auto assistant_text = item.text;
  if (assistant_text.empty() && !text_empty(item.text_model))
    assistant_text = to_plain_text(item.text_model);
  auto const sanitized_assistant_text = normalize_tool_result_identity(std::move(assistant_text));
  if (sanitized_assistant_text.empty())
    return false;

  auto const sanitized_result = normalize_tool_result_identity(previous.tool->result_summary);
  if (!sanitized_result.empty() && sanitized_result == sanitized_assistant_text)
    return true;

  if (previous.tool->permissions.size() == 1)
  {
    auto const& audit = previous.tool->permissions.front();
    auto permission = std::string("permission ") + (audit.decision.empty() ? std::string("pending") : audit.decision);
    if (!audit.risk.empty())
      permission += " · risk " + audit.risk;
    if (!audit.reason.empty())
      permission += " · reason " + audit.reason;
    return normalize_tool_result_identity(std::move(permission)) == sanitized_assistant_text;
  }
  return false;
}

bool is_context_gathering_tool(TranscriptItem const& item)
{
  if (!item.tool)
    return false;
  auto const name = lower_ascii(item.tool->name);
  return name == "read_file" || name == "glob" || name == "grep" || name == "list_directory" || name == "lsp_diagnostics";
}

std::size_t context_tool_run_size(std::vector<TranscriptItem> const& transcript, std::size_t index)
{
  std::size_t count = 0;
  while (index + count < transcript.size() && is_context_gathering_tool(transcript[index + count])) ++count;
  return count;
}

std::string render_context_tool_group_heading(std::size_t count, std::size_t width)
{
  auto label = std::string(kSgrAccent) + "context gathering" + std::string(kSgrReset) + " " + std::string(kSgrDim) + "· " + std::to_string(count) +
               (count == 1 ? " tool" : " tools") + std::string(kSgrReset);
  if (wide_blocks(width))
    return render_wide_content(kSgrMuted, std::move(label), width);
  return fit_line_preserving_sgr("  " + std::move(label), width);
}

std::vector<std::string> render_transcript_item_lines(TranscriptItem const& item, std::size_t width, bool tool_details_visible, bool thinking_visible,
                                                      bool suppress_result_summary)
{
  if (item.tool)
    return render_tool_card(*item.tool, width, tool_details_visible, suppress_result_summary);
  if (item.label == "you")
    return render_user_block(text_model_or(item.text_model, item.text), width);
  if (item.label == "ava")
  {
    auto assistant_text = item.text;
    if (assistant_text.empty() && !text_empty(item.text_model))
      assistant_text = to_plain_text(item.text_model);
    auto thinking_text = thinking_visible ? text_model_or(item.thinking_model, item.thinking) : std::string{};
    return render_assistant_block(assistant_text, item.meta, thinking_text, width);
  }
  if (item.label == "thinking")
  {
    if (thinking_visible)
      return render_thinking_block(text_model_or(item.text_model, item.text), width);
    return {};
  }
  if (item.label == "compaction")
    return render_compaction_block(text_model_or(item.text_model, item.text), width);
  if (item.label == "setup" && item.text.starts_with("! "))
  {
    auto const marker = tui_plain_output() ? std::string("!") : std::string(kSgrWarning) + "!" + std::string(kSgrReset);
    auto line = std::string("  ") + marker + " " + sanitize_terminal_text(item.text.substr(2));
    return {fit_line_preserving_sgr(std::move(line), width)};
  }
  if (item.label == "error")
  {
    return render_error_block(text_model_or(item.text_model, item.text), item.meta, width, tool_details_visible);
  }

  std::vector<std::string> lines;
  auto text = text_model_or(item.text_model, item.text);
  if (text.empty())
  {
    text = sanitize_terminal_text(item.label);
    if (text.empty())
      text = "event";
  }
  auto const text_lines = split_lines(text);
  for (auto const& part : text_lines)
  {
    for (auto const& wrapped : wrap_transcript_text(part, width))
    {
      lines.push_back(render_generic_line(wrapped, width));
    }
  }
  return lines;
}

}  // namespace

TranscriptViewportAnchor capture_transcript_viewport_anchor(TranscriptLayout const& layout, std::size_t max_scroll_offset, std::size_t scroll_offset)
{
  if (layout.message_starts.empty() || layout.message_starts.size() != layout.message_item_indices.size() ||
      layout.content_starts.size() != layout.message_starts.size())
    return {};
  auto const visible_start = max_scroll_offset - std::min(scroll_offset, max_scroll_offset);
  auto message = std::ranges::upper_bound(layout.message_starts, visible_start);
  if (message == layout.message_starts.begin())
    message = layout.message_starts.begin();
  else
    --message;
  auto const position = static_cast<std::size_t>(std::distance(layout.message_starts.begin(), message));
  auto const content_start = layout.content_starts[position];
  auto const content_relative = visible_start >= content_start;
  auto const relative_start = content_relative ? content_start : *message;
  return TranscriptViewportAnchor{
      .valid = true, .content_relative = content_relative, .item_index = layout.message_item_indices[position], .line_offset = visible_start - relative_start};
}

std::size_t restore_transcript_viewport_anchor(TranscriptViewportAnchor anchor, TranscriptLayout const& layout, std::size_t max_scroll_offset,
                                               std::ptrdiff_t item_index_shift)
{
  if (!anchor.valid || layout.message_starts.empty() || layout.message_starts.size() != layout.message_item_indices.size() ||
      layout.content_starts.size() != layout.message_starts.size())
    return max_scroll_offset;

  auto const shifted_item_index = static_cast<std::ptrdiff_t>(anchor.item_index) + item_index_shift;
  if (shifted_item_index < 0)
    return max_scroll_offset;
  auto const retained_item_index = static_cast<std::size_t>(shifted_item_index);
  auto message = std::ranges::find(layout.message_item_indices, retained_item_index);
  if (message == layout.message_item_indices.end())
    return max_scroll_offset;
  auto const position = static_cast<std::size_t>(std::distance(layout.message_item_indices.begin(), message));
  auto const block_start = layout.message_starts[position];
  auto const content_start = layout.content_starts[position];
  auto const block_end = position + 1 < layout.message_starts.size() ? layout.message_starts[position + 1] : layout.lines.size();
  if (!anchor.content_relative && anchor.line_offset >= content_start - block_start)
    return max_scroll_offset;
  auto const relative_start = anchor.content_relative ? content_start : block_start;
  auto const last_block_line = block_end > relative_start ? block_end - 1 : relative_start;
  auto const desired_start = std::min(relative_start + anchor.line_offset, last_block_line);
  return max_scroll_offset - std::min(desired_start, max_scroll_offset);
}

TranscriptLayout render_transcript_layout(std::vector<TranscriptItem> const& transcript, std::size_t width, bool tool_details_visible, bool thinking_visible,
                                          bool compact_spacing)
{
  TranscriptLayout layout;
  for (std::size_t index = 0; index < transcript.size(); ++index)
  {
    if (suppress_adjacent_tool_result(transcript, index))
      continue;
    auto const& item = transcript[index];
    auto block = render_transcript_item_lines(item, width, tool_details_visible, thinking_visible, false);
    if (block.empty())
      continue;

    if (roomy_transcript_spacing(width, compact_spacing) && !layout.lines.empty() && starts_logical_group(transcript, index, thinking_visible))
      layout.lines.emplace_back();
    layout.message_starts.push_back(layout.lines.size());
    layout.message_item_indices.push_back(index);
    auto const context_run_size = context_tool_run_size(transcript, index);
    auto const context_heading = context_run_size >= 2 && (index == 0 || !is_context_gathering_tool(transcript[index - 1]));
    layout.content_starts.push_back(layout.lines.size() + (context_heading ? 1 : 0));
    if (context_heading)
      block.insert(block.begin(), render_context_tool_group_heading(context_run_size, width));
    layout.lines.insert(layout.lines.end(), block.begin(), block.end());
  }
  return layout;
}

std::vector<std::string> render_transcript_lines(std::vector<TranscriptItem> const& transcript, std::size_t width, bool tool_details_visible,
                                                 bool thinking_visible, bool compact_spacing)
{
  return render_transcript_layout(transcript, width, tool_details_visible, thinking_visible, compact_spacing).lines;
}

std::vector<std::string> render_transcript_tail_lines(std::vector<TranscriptItem> const& transcript, std::size_t width, std::size_t max_tail_lines,
                                                      bool tool_details_visible, bool thinking_visible, bool compact_spacing)
{
  if (max_tail_lines == 0 || transcript.empty())
    return {};

  std::vector<std::vector<std::string>> reversed_blocks;
  std::size_t collected_lines = 0;
  for (std::size_t index = transcript.size(); index > 0 && collected_lines < max_tail_lines; --index)
  {
    auto const item_index = index - 1;
    if (suppress_adjacent_tool_result(transcript, item_index))
      continue;
    auto const& item = transcript[item_index];
    auto block = render_transcript_item_lines(item, width, tool_details_visible, thinking_visible, false);
    if (block.empty())
      continue;

    auto const context_run_size = context_tool_run_size(transcript, item_index);
    if (context_run_size >= 2 && (item_index == 0 || !is_context_gathering_tool(transcript[item_index - 1])))
      block.insert(block.begin(), render_context_tool_group_heading(context_run_size, width));
    if (roomy_transcript_spacing(width, compact_spacing) && has_preceding_visible_item(transcript, item_index, thinking_visible) &&
        starts_logical_group(transcript, item_index, thinking_visible))
      block.insert(block.begin(), std::string{});
    collected_lines += block.size();
    reversed_blocks.push_back(std::move(block));
  }

  std::vector<std::string> rendered_tail;
  rendered_tail.reserve(collected_lines);
  for (auto block = reversed_blocks.rbegin(); block != reversed_blocks.rend(); ++block)
  {
    rendered_tail.insert(rendered_tail.end(), block->begin(), block->end());
  }
  if (rendered_tail.size() > max_tail_lines)
  {
    rendered_tail.erase(rendered_tail.begin(), rendered_tail.begin() + static_cast<std::ptrdiff_t>(rendered_tail.size() - max_tail_lines));
  }
  return rendered_tail;
}

std::vector<std::string> render_transcript_tail_lines_cached(TranscriptTailRenderCache& cache, std::vector<TranscriptItem> const& transcript,
                                                             std::size_t transcript_generation, std::size_t width, std::size_t max_tail_lines,
                                                             bool tool_details_visible, bool thinking_visible, bool compact_spacing)
{
  auto const plain_output = tui_plain_output();
  if (cache.valid && cache.transcript_generation == transcript_generation && cache.width == width && cache.max_tail_lines == max_tail_lines &&
      cache.tool_details_visible == tool_details_visible && cache.thinking_visible == thinking_visible && cache.compact_spacing == compact_spacing &&
      cache.plain_output == plain_output)
  {
    return cache.rendered_tail;
  }

  auto const streaming_item = transcript.empty() ? nullptr : &transcript.back();
  auto const assistant_stream = streaming_item && !streaming_item->tool && streaming_item->label == "ava" && streaming_item->append_only_stream &&
                                !streaming_item->stream_id.empty() && (!streaming_item->text.empty() || !streaming_item->thinking.empty());
  auto const simple_tool_stream = streaming_item && streaming_item->tool && streaming_item->append_only_stream && !streaming_item->stream_id.empty() &&
                                  !tool_details_visible && !streaming_item->tool->details_visible.value_or(false) &&
                                  !streaming_item->tool->argument_summary.empty() && streaming_item->tool->result_summary.empty() &&
                                  streaming_item->tool->arguments_json.empty() && streaming_item->tool->result_json.empty() &&
                                  streaming_item->tool->permission_request_ids.empty() && streaming_item->tool->permissions.empty() &&
                                  streaming_item->tool->diff.empty() && streaming_item->tool->changed_paths.empty() && streaming_item->tool->spill_path.empty();
  auto const stream_eligible = assistant_stream || simple_tool_stream;
  auto const source_is_tool = simple_tool_stream;
  auto const source_is_thinking = assistant_stream && streaming_item->text.empty();
  auto const source = !stream_eligible     ? std::string_view{}
                      : source_is_tool     ? std::string_view(streaming_item->tool->argument_summary)
                      : source_is_thinking ? std::string_view(streaming_item->thinking)
                                           : std::string_view(streaming_item->text);
  auto const counterpart_size = !stream_eligible || source_is_tool ? std::size_t{0}
                                : source_is_thinking               ? streaming_item->text.size()
                                                                   : streaming_item->thinking.size();
  auto const source_budget = std::max<std::size_t>(4096, width * (max_tail_lines + 24) * 6 + 4096);
  auto choose_tail_start = [&](std::string_view text) {
    auto const viewport_allowance = std::max<std::size_t>(256, width * (max_tail_lines + 4) * 2 + 128);
    auto const source_allowance = std::min(source_budget, viewport_allowance);
    if (text.size() <= source_allowance)
      return std::size_t{0};
    auto start = text.size() - source_allowance;
    auto const preceding_newline = text.rfind('\n', start);
    if (preceding_newline != std::string_view::npos && start - preceding_newline <= width * 2)
      start = preceding_newline + 1;
    while (start < text.size() && is_utf8_continuation(static_cast<unsigned char>(text[start]))) ++start;
    auto const line_start = start == 0 ? std::size_t{0} : text.rfind('\n', start - 1) + 1;
    if (auto const escape = text.rfind('\x1b', start); escape != std::string_view::npos && escape >= line_start)
    {
      auto escape_end = escape;
      if ((skip_sgr_sequence(text, escape_end) || skip_osc_sequence(text, escape_end)) && escape_end > start)
        start = escape_end;
    }
    return start;
  };
  auto advance_source_state = [&](bool& code_open, std::string& code_language, std::size_t& line_columns,
                                  std::vector<TranscriptTailOrderedListState>& ordered_states, bool& blockquote_open, std::string_view chunk) {
    auto const content_width = width > 4 ? width - 4 : std::size_t{1};
    std::size_t offset = 0;
    while (offset < chunk.size())
    {
      auto const newline = chunk.find('\n', offset);
      auto const end = newline == std::string_view::npos ? chunk.size() : newline;
      auto const line = chunk.substr(offset, end - offset);
      auto const at_line_start = line_columns == 0;
      if (!source_is_thinking && at_line_start && line.starts_with("```"))
      {
        if (!code_open)
        {
          auto language = line.substr(3);
          while (!language.empty() && (language.front() == ' ' || language.front() == '\t')) language.remove_prefix(1);
          code_language = std::string(language);
        }
        else
        {
          code_language.clear();
        }
        code_open = !code_open;
        blockquote_open = false;
      }
      else if (!source_is_thinking && !source_is_tool && at_line_start && !code_open)
      {
        auto const sanitized = sanitize_terminal_text(line);
        std::string quote_body;
        if (parse_blockquote(sanitized, quote_body))
          blockquote_open = true;
        else if (!blockquote_open || !markdown_lazy_blockquote_continuation(sanitized))
          blockquote_open = false;
        std::string marker;
        std::string body;
        NumberedListParts numbered;
        if (is_blank(sanitized))
        {
        }
        else if (parse_bullet(sanitized, marker, body))
        {
          auto const indent_columns = rendered_markdown_list_indent(sanitized).size();
          std::erase_if(ordered_states, [&](TranscriptTailOrderedListState const& state) { return state.indent.size() >= indent_columns; });
        }
        else if (parse_numbered_parts(sanitized, numbered))
        {
          auto state = std::ranges::find_if(ordered_states, [&](TranscriptTailOrderedListState const& candidate) {
            return candidate.indent == numbered.indent && candidate.delimiter == numbered.delimiter;
          });
          if (state == ordered_states.end())
          {
            ordered_states.push_back(
                TranscriptTailOrderedListState{.indent = numbered.indent, .delimiter = numbered.delimiter, .next_number = numbered.source_number});
            state = std::prev(ordered_states.end());
          }
          if (state->next_number < std::numeric_limits<std::size_t>::max())
            ++state->next_number;
          std::erase_if(ordered_states, [&](TranscriptTailOrderedListState const& candidate) { return candidate.indent.size() > numbered.indent.size(); });
        }
        else if (is_markdown_heading(sanitized) || is_markdown_horizontal_rule(sanitized) || !sanitized.starts_with(' '))
        {
          ordered_states.clear();
        }
      }
      line_columns = (line_columns + terminal_text_columns(line)) % content_width;
      if (newline == std::string_view::npos)
        break;
      line_columns = 0;
      offset = newline + 1;
    }
  };
  auto remember_baseline = [&](std::vector<std::string> rendered) {
    cache.transcript_generation = transcript_generation;
    cache.width = width;
    cache.max_tail_lines = max_tail_lines;
    cache.tool_details_visible = tool_details_visible;
    cache.thinking_visible = thinking_visible;
    cache.compact_spacing = compact_spacing;
    cache.plain_output = plain_output;
    cache.source_budget_bytes = source_budget;
    cache.rendered_tail = std::move(rendered);
    cache.rendered_stream_prefix.clear();
    cache.valid = stream_eligible;
    if (!stream_eligible)
      return cache.rendered_tail;
    if (!source_is_tool)
    {
      auto const stream_block = render_transcript_item_lines(*streaming_item, width, tool_details_visible, thinking_visible, false);
      auto overlap = std::min(cache.rendered_tail.size(), stream_block.size());
      while (overlap > 0 && !std::equal(cache.rendered_tail.end() - static_cast<std::ptrdiff_t>(overlap), cache.rendered_tail.end(),
                                        stream_block.end() - static_cast<std::ptrdiff_t>(overlap)))
      {
        --overlap;
      }
      cache.rendered_stream_prefix.assign(cache.rendered_tail.begin(), cache.rendered_tail.end() - static_cast<std::ptrdiff_t>(overlap));
    }
    cache.stream_id = streaming_item->stream_id;
    cache.item_index = transcript.size() - 1;
    cache.source_size = source.size();
    cache.counterpart_size = counterpart_size;
    cache.source_is_thinking = source_is_thinking;
    cache.source_is_tool = source_is_tool;
    if (source_is_tool)
    {
      cache.tool_status = streaming_item->tool->status;
      cache.tool_lifecycle = streaming_item->tool->lifecycle;
      cache.tool_name = streaming_item->tool->name;
      cache.tool_call_id = streaming_item->tool->call_id;
      cache.tool_source_safe = source.find('\x1b') == std::string_view::npos;
      cache.tool_prefix_saturated = cache.tool_source_safe && terminal_text_columns(sanitize_terminal_text(source)) > width * 2;
    }
    cache.meta = streaming_item->meta;
    cache.source_tail_start = choose_tail_start(source);
    cache.fenced_code_open = false;
    cache.fenced_code_language.clear();
    cache.line_columns = 0;
    cache.ordered_list_states.clear();
    cache.blockquote_open = false;
    advance_source_state(cache.fenced_code_open, cache.fenced_code_language, cache.line_columns, cache.ordered_list_states, cache.blockquote_open,
                         source.substr(0, cache.source_tail_start));
    cache.retained_source_bytes = source.size() - cache.source_tail_start;
    cache.full_source_bytes += source.size();
    return cache.rendered_tail;
  };

  auto const tool_identity_unchanged =
      !source_is_tool ||
      (cache.source_is_tool && cache.tool_status == streaming_item->tool->status && cache.tool_lifecycle == streaming_item->tool->lifecycle &&
       cache.tool_name == streaming_item->tool->name && cache.tool_call_id == streaming_item->tool->call_id);
  auto const append_update = cache.valid && stream_eligible && cache.stream_id == streaming_item->stream_id && cache.item_index == transcript.size() - 1 &&
                             cache.source_is_thinking == source_is_thinking && cache.source_is_tool == source_is_tool && tool_identity_unchanged &&
                             cache.counterpart_size == counterpart_size && source.size() >= cache.source_size && cache.width == width &&
                             cache.max_tail_lines == max_tail_lines && cache.tool_details_visible == tool_details_visible &&
                             cache.thinking_visible == thinking_visible && cache.compact_spacing == compact_spacing && cache.plain_output == plain_output;
  if (!append_update)
  {
    return remember_baseline(render_transcript_tail_lines(transcript, width, max_tail_lines, tool_details_visible, thinking_visible, compact_spacing));
  }

  if (source_is_tool)
  {
    auto const appended = source.substr(cache.source_size);
    if (!cache.tool_prefix_saturated || !cache.tool_source_safe || appended.find('\x1b') != std::string_view::npos ||
        appended.find('/') != std::string_view::npos)
    {
      return remember_baseline(render_transcript_tail_lines(transcript, width, max_tail_lines, tool_details_visible, thinking_visible, compact_spacing));
    }
    cache.transcript_generation = transcript_generation;
    cache.incremental_source_bytes += appended.size();
    cache.source_size = source.size();
    cache.retained_source_bytes = 0;
    ++cache.incremental_updates;
    return cache.rendered_tail;
  }

  auto const appended_bytes = source.size() - cache.source_size;
  auto const next_tail_start = std::max(cache.source_tail_start, choose_tail_start(source));
  auto code_open = cache.fenced_code_open;
  auto code_language = cache.fenced_code_language;
  auto line_columns = cache.line_columns;
  auto ordered_states = cache.ordered_list_states;
  auto blockquote_open = cache.blockquote_open;
  advance_source_state(code_open, code_language, line_columns, ordered_states, blockquote_open,
                       source.substr(cache.source_tail_start, next_tail_start - cache.source_tail_start));

  std::string fragment_source;
  fragment_source.reserve(source.size() - next_tail_start + width + 32);
  if (!source_is_thinking && !source_is_tool)
  {
    for (auto const& state : ordered_states)
    {
      fragment_source += state.indent;
      fragment_source += std::to_string(state.next_number > 1 ? state.next_number - 1 : std::size_t{1});
      fragment_source.push_back(state.delimiter);
      fragment_source += " seed\n";
    }
    if (blockquote_open)
      fragment_source += "> seed\n";
  }
  if (!source_is_thinking && code_open)
  {
    fragment_source += "```";
    fragment_source += code_language;
    fragment_source.push_back('\n');
  }
  if (line_columns > 0)
  {
    auto const content_width = width > 4 ? width - 4 : std::size_t{1};
    fragment_source.append(line_columns % content_width, 'x');
  }
  fragment_source.append(source.substr(next_tail_start));

  TranscriptItem fragment;
  fragment.label = "ava";
  if (!source_is_thinking || next_tail_start == 0)
    fragment.meta = streaming_item->meta;
  if (source_is_thinking)
    fragment.thinking = fragment_source;
  else
    fragment.text = fragment_source;
  auto rendered_fragment = render_transcript_lines({std::move(fragment)}, width, tool_details_visible, thinking_visible, compact_spacing);
  if (next_tail_start == 0 && !cache.rendered_stream_prefix.empty())
  {
    rendered_fragment.insert(rendered_fragment.begin(), cache.rendered_stream_prefix.begin(), cache.rendered_stream_prefix.end());
  }
  auto const suffix_start = rendered_fragment.size() > max_tail_lines ? rendered_fragment.size() - max_tail_lines : std::size_t{0};
  std::vector<std::string> rendered_tail(rendered_fragment.begin() + static_cast<std::ptrdiff_t>(suffix_start), rendered_fragment.end());

  cache.transcript_generation = transcript_generation;
  cache.source_size = source.size();
  cache.source_tail_start = next_tail_start;
  cache.fenced_code_open = code_open;
  cache.fenced_code_language = std::move(code_language);
  cache.line_columns = line_columns;
  cache.ordered_list_states = std::move(ordered_states);
  cache.blockquote_open = blockquote_open;
  cache.meta = streaming_item->meta;
  cache.rendered_tail = std::move(rendered_tail);
  cache.retained_source_bytes = fragment_source.size();
  cache.incremental_source_bytes += appended_bytes;
  auto const carry_bytes = fragment_source.size() - std::min(fragment_source.size(), appended_bytes);
  cache.carry_source_bytes += carry_bytes;
  cache.max_carry_source_bytes = std::max(cache.max_carry_source_bytes, carry_bytes);
  ++cache.incremental_updates;
  return cache.rendered_tail;
}

std::vector<std::size_t> transcript_message_start_lines(std::vector<TranscriptItem> const& transcript, std::size_t width, bool tool_details_visible,
                                                        bool thinking_visible, bool compact_spacing)
{
  return render_transcript_layout(transcript, width, tool_details_visible, thinking_visible, compact_spacing).message_starts;
}

void refresh_transcript_layout_cache(TranscriptLayoutCache& cache, std::vector<TranscriptItem> const& transcript, std::size_t transcript_generation,
                                     std::size_t width, bool tool_details_visible, bool thinking_visible, bool compact_spacing)
{
  if (cache.valid && cache.transcript_generation == transcript_generation && cache.width == width && cache.tool_details_visible == tool_details_visible &&
      cache.thinking_visible == thinking_visible && cache.compact_spacing == compact_spacing)
  {
    return;
  }
  cache.transcript_generation = transcript_generation;
  cache.width = width;
  cache.tool_details_visible = tool_details_visible;
  cache.thinking_visible = thinking_visible;
  cache.compact_spacing = compact_spacing;
  cache.layout = render_transcript_layout(transcript, width, tool_details_visible, thinking_visible, compact_spacing);
  cache.valid = true;
  ++cache.layout_build_count;
}

std::size_t cached_transcript_max_scroll_offset(TranscriptLayoutCache const& cache, std::size_t transcript_height)
{
  if (!cache.valid || transcript_height == 0 || cache.layout.lines.size() <= transcript_height)
    return 0;
  return cache.layout.lines.size() - transcript_height;
}

std::vector<std::string> cached_visible_transcript_lines(TranscriptLayoutCache const& cache, std::size_t transcript_height,
                                                         std::size_t transcript_scroll_offset)
{
  if (!cache.valid)
    return {};
  ++cache.visible_slice_count;
  return visible_transcript_lines(cache.layout.lines, cache.width, transcript_height, transcript_scroll_offset);
}

std::vector<std::string> visible_transcript_lines(std::vector<std::string> const& rendered_transcript, std::size_t width, std::size_t transcript_height,
                                                  std::size_t transcript_scroll_offset)
{
  static_cast<void>(width);
  std::vector<std::string> visible_transcript;
  if (rendered_transcript.size() > transcript_height && transcript_height > 0)
  {
    auto const visible_count = transcript_height;
    auto const max_offset = rendered_transcript.size() > visible_count ? rendered_transcript.size() - visible_count : 0;
    auto const scroll_offset = std::min(transcript_scroll_offset, max_offset);
    auto const end = rendered_transcript.size() - scroll_offset;
    auto const start = end > visible_count ? end - visible_count : 0;
    for (std::size_t index = start; index < end; ++index)
    {
      visible_transcript.push_back(rendered_transcript[index]);
    }
  }
  else if (transcript_height > 0)
  {
    for (std::size_t index = 0; index < rendered_transcript.size(); ++index)
    {
      visible_transcript.push_back(rendered_transcript[index]);
    }
  }
  return visible_transcript;
}

}  // namespace detail

std::string to_string(ToolTimelineStatus status)
{
  switch (status)
  {
    case ToolTimelineStatus::Running:
      return "running";
    case ToolTimelineStatus::Success:
      return "success";
    case ToolTimelineStatus::Canceled:
      return "canceled";
    case ToolTimelineStatus::Error:
      return "error";
  }
  return "unknown";
}

}  // namespace ava::tui
