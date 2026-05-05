#include <optional>
#include <string>
#include <vector>

#include "ava/app/event_json_support.h"
#include "tests/support/test_harness.h"

namespace {

void test_event_field_appenders()
{
  std::string json = "{\"root\":true";
  ava::app::detail::append_event_required_string_field(json, "text", "hello\n\"ava\"");
  ava::app::detail::append_event_string_field(json, "empty", "");
  ava::app::detail::append_event_number_field(json, "zero", 0);
  ava::app::detail::append_event_number_field(json, "count", 3);
  ava::app::detail::append_event_bool_field(json, "false_value", false);
  ava::app::detail::append_event_bool_field(json, "true_value", true);
  ava::app::detail::append_event_optional_string_field(json, "missing", std::nullopt);
  ava::app::detail::append_event_optional_string_field(json, "empty_optional", std::string());
  ava::app::detail::append_event_optional_string_field(json, "optional", std::string("value"));
  json += '}';

  expect(json ==
             "{\"root\":true,\"text\":\"hello\\n\\\"ava\\\"\","
             "\"count\":3,\"true_value\":true,\"optional\":\"value\"}",
         "event JSON support appends required fields and omits empty/zero/false optional fields");
}

void test_event_array_and_object_appenders()
{
  std::string json = "{\"root\":true";
  ava::app::detail::append_event_string_array_field(json, "empty_paths", {});
  ava::app::detail::append_event_string_array_field(json, "paths", {"src/main.cpp", "quote\"path"});
  ava::app::detail::append_event_json_object_field(json, "args", "{\"path\":\"src/main.cpp\"}");
  ava::app::detail::append_event_json_object_field(json, "bad", "{broken\n");
  json += '}';

  expect(json ==
             "{\"root\":true,\"paths\":[\"src/main.cpp\",\"quote\\\"path\"],"
             "\"args\":{\"path\":\"src/main.cpp\"},\"bad_json\":\"{broken\\n\"}",
         "event JSON support appends string arrays, object values, and readable fallback JSON strings");
}

void test_payload_field_tracking()
{
  std::string json = "{";
  bool has_field = false;
  ava::app::detail::append_payload_string_field(json, has_field, "empty", "");
  ava::app::detail::append_payload_number_field(json, has_field, "zero", 0);
  ava::app::detail::append_payload_bool_field(json, has_field, "false_value", false);
  expect(!has_field && json == "{", "payload JSON support keeps field state unchanged for omitted fields");

  ava::app::detail::append_payload_string_field(json, has_field, "text", "hello");
  ava::app::detail::append_payload_number_field(json, has_field, "count", 7);
  ava::app::detail::append_payload_bool_field(json, has_field, "truncated", true);
  ava::app::detail::append_payload_json_object_field(json, has_field, "result", "{\"ok\":true}");
  ava::app::detail::append_payload_string_array_field(json, has_field, "changed_paths", {"a.cpp", "b.cpp"});
  json += '}';

  expect(json ==
             "{\"text\":\"hello\",\"count\":7,\"truncated\":true,"
             "\"result\":{\"ok\":true},\"changed_paths\":[\"a.cpp\",\"b.cpp\"]}",
         "payload JSON support inserts commas only between emitted fields");
}

void test_payload_json_object_fallback()
{
  std::string json = "{";
  bool has_field = false;
  ava::app::detail::append_payload_json_object_field(json, has_field, "args", "{bad\"json");
  json += '}';

  expect(json == "{\"args_json\":\"{bad\\\"json\"}",
         "payload JSON support preserves invalid object text as escaped fallback content");
}

}  // namespace

void run_app_event_json_support_tests()
{
  test_event_field_appenders();
  test_event_array_and_object_appenders();
  test_payload_field_tracking();
  test_payload_json_object_fallback();
}
