#include <optional>
#include <string>
#include <utility>

#include "ava/session/export_markdown_support.h"
#include "tests/support/test_harness.h"

namespace {

ava::session::SessionEntry entry_with_data(std::string data_json)
{
  return ava::session::SessionEntry{.id = "entry\"1",
                                    .parent_id = "parent\n1",
                                    .type = ava::session::EntryType::AssistantMessage,
                                    .timestamp = "2026-05-05T14:00:00Z",
                                    .data_json = std::move(data_json)};
}

void test_export_fence_helpers()
{
  expect(ava::session::detail::export_longest_backtick_run("a``b```c") == 3,
         "session export support measures longest backtick run");
  expect(ava::session::detail::export_fence_for("no backticks") == "```",
         "session export support uses a minimum three-backtick fence");
  expect(ava::session::detail::export_fence_for("contains ``` run") == "````",
         "session export support expands fences around content backticks");
}

void test_export_content_sanitization()
{
  std::string raw = "line\n\t";
  raw.push_back('\0');
  raw.push_back('\r');
  raw.push_back('\x1B');
  raw.push_back('\x7F');
  raw += "tail";

  auto sanitized = ava::session::detail::sanitize_fenced_export_content(raw);
  expect(sanitized.find("line\n\t") != std::string::npos && sanitized.find("\\u0000") != std::string::npos &&
             sanitized.find("\\u000D") != std::string::npos && sanitized.find("\\u001B") != std::string::npos &&
             sanitized.find("\\u007F") != std::string::npos && sanitized.find('\0') == std::string::npos &&
             sanitized.find('\r') == std::string::npos && sanitized.find('\x1B') == std::string::npos &&
             sanitized.find('\x7F') == std::string::npos,
         "session export support preserves newlines/tabs and escapes unsafe control bytes");
}

void test_export_fenced_blocks()
{
  std::string out;
  ava::session::detail::append_optional_export_fenced_block(out, "Missing", std::nullopt);
  ava::session::detail::append_optional_export_fenced_block(out, "Empty", std::string());
  expect(out.empty(), "session export support skips empty optional fenced blocks");

  ava::session::detail::append_export_fenced_block(out, "Message", "line with ``` fence", "md");
  expect(out == "Message:\n\n````md\nline with ``` fence\n````\n\n",
         "session export support renders fenced blocks with a safe dynamic fence");
}

void test_export_metadata_and_fields()
{
  auto entry = entry_with_data(
      "{\"text\":\"hello\",\"usage\":{\"input_tokens\":1},\"count\":42,"
      "\"ok\":true,\"bad\":trueish,\"success\":false}");

  expect(ava::session::detail::export_json_string("quote\"value") == "\"quote\\\"value\"",
         "session export support renders escaped JSON strings");
  expect(ava::session::detail::export_metadata_json(entry) ==
             "{\"id\":\"entry\\\"1\",\"parent_id\":\"parent\\n1\","
             "\"type\":\"assistant_message\",\"timestamp\":\"2026-05-05T14:00:00Z\"}",
         "session export support renders deterministic metadata JSON");
  expect(ava::session::detail::export_string_field(entry, "text") == std::string("hello") &&
             ava::session::detail::export_integer_field(entry, "count") == 42 &&
             ava::session::detail::export_object_field(entry, "usage") == std::string("{\"input_tokens\":1}"),
         "session export support reads string, integer, and object fields");
  expect(ava::session::detail::export_bool_field_is_true(entry, "ok") &&
             !ava::session::detail::export_bool_field_is_true(entry, "bad"),
         "session export support parses only exact true boolean fields");
  expect(ava::session::detail::export_success_text(entry) == "false",
         "session export support renders exact false success fields");
}

void test_export_metadata_append_respects_options()
{
  auto entry = entry_with_data("{}");
  std::string out;
  ava::session::ExportOptions hidden_metadata_options;
  hidden_metadata_options.include_metadata = false;
  ava::session::detail::append_export_metadata(out, entry, hidden_metadata_options);
  expect(out.empty(), "session export support omits metadata unless requested");

  ava::session::ExportOptions visible_metadata_options;
  visible_metadata_options.include_metadata = true;
  ava::session::detail::append_export_metadata(out, entry, visible_metadata_options);
  expect(out.find("Metadata:\n\n```json\n") == 0 && out.find("\"type\":\"assistant_message\"") != std::string::npos,
         "session export support appends metadata as a JSON fenced block when requested");
}

void test_export_success_unknown_for_invalid_boolean()
{
  auto invalid_true = entry_with_data("{\"success\":trueish}");
  auto invalid_false = entry_with_data("{\"success\":falsey}");
  auto missing = entry_with_data("{}");

  expect(ava::session::detail::export_success_text(invalid_true) == "unknown" &&
             ava::session::detail::export_success_text(invalid_false) == "unknown" &&
             ava::session::detail::export_success_text(missing) == "unknown",
         "session export support treats invalid or missing success booleans as unknown");
}

}  // namespace

void run_session_export_markdown_support_tests()
{
  test_export_fence_helpers();
  test_export_content_sanitization();
  test_export_fenced_blocks();
  test_export_metadata_and_fields();
  test_export_metadata_append_respects_options();
  test_export_success_unknown_for_invalid_boolean();
}
