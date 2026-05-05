#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ava/app/runtime_json.h"
#include "ava/app/runtime_json_support.h"
#include "tests/support/test_harness.h"

namespace {

ava::config::ModelInfo model_info(std::string provider, std::string model)
{
  return ava::config::ModelInfo{.provider_id = std::move(provider),
                                .model_id = std::move(model),
                                .display_name = "Runtime Test",
                                .family = "reasoning",
                                .context_window_tokens = 128000,
                                .max_output_tokens = 4096,
                                .pricing = std::nullopt,
                                .api_family = "responses",
                                .input_modalities = {"text", "image"},
                                .supports_tools = true,
                                .supports_streaming = false,
                                .supports_reasoning = true,
                                .reports_usage = true,
                                .reasoning_levels = {"low", "high"},
                                .compatibility_quirks = {"quirk"},
                                .output_modalities = {"text"},
                                .reasoning_format = "summary"};
}

void test_json_escape_decoding_helpers()
{
  expect(ava::app::runtime::detail::hex_value('9') == 9 && ava::app::runtime::detail::hex_value('F') == 15 &&
             ava::app::runtime::detail::hex_value('g') == -1,
         "runtime JSON support parses hex digits");

  auto code_unit = ava::app::runtime::detail::parse_hex_code_unit("xx0041", 2);
  auto bad_code_unit = ava::app::runtime::detail::parse_hex_code_unit("xx00zz", 2);
  expect(code_unit && *code_unit == 0x41 && !bad_code_unit, "runtime JSON support parses hex code units");

  expect(ava::app::runtime::detail::is_high_surrogate(0xD83D) && ava::app::runtime::detail::is_low_surrogate(0xDE00) &&
             !ava::app::runtime::detail::is_high_surrogate(0x0041),
         "runtime JSON support classifies surrogate code units");

  std::string decoded;
  std::size_t index = 1;
  ava::app::runtime::detail::append_json_escaped_char(decoded, "\\uD83D\\uDE00", index);
  expect(decoded == std::string("\xF0\x9F\x98\x80", 4) && index == 11,
         "runtime JSON support decodes surrogate-pair escapes");

  decoded.clear();
  index = 1;
  ava::app::runtime::detail::append_json_escaped_char(decoded, "\\uD83D", index);
  expect(decoded == std::string("\xEF\xBF\xBD", 3) && index == 5,
         "runtime JSON support replaces dangling high surrogates");
}

void test_public_json_field_helpers()
{
  std::vector<std::string> const values = {"alpha", "quote\"value", "emoji \xF0\x9F\x98\x80"};
  auto const json = "{\"values\":" + ava::app::runtime::string_array_json(values) +
                    ",\"enabled\":true,"
                    "\"disabled\":false,\"bad\":trueish}";
  auto parsed = ava::app::runtime::string_array_field(json, "values");
  expect(parsed == values, "runtime JSON support round-trips escaped string arrays");
  expect(ava::app::runtime::string_array_field("{\"values\":[\"unterminated\"", "values").empty(),
         "runtime JSON support ignores unterminated string arrays");
  expect(ava::app::runtime::bool_json_field(json, "enabled") == true &&
             ava::app::runtime::bool_json_field(json, "disabled") == false &&
             !ava::app::runtime::bool_json_field(json, "bad"),
         "runtime JSON support parses only complete boolean values");
}

void test_runtime_payload_builders()
{
  auto model = model_info("provider\"id", "model-a");
  ava::config::PromptSelection const prompt{.text = "ignored", .from_override = true};
  auto session_json = ava::app::runtime::detail::session_start_data_json(ava::agent::Mode::Build, model, prompt, 3);
  expect(session_json.find("\"mode\":\"build\"") != std::string::npos &&
             session_json.find("\"provider\":\"provider\\\"id\"") != std::string::npos &&
             session_json.find("\"prompt_override\":true") != std::string::npos &&
             session_json.find("\"context_sources\":3") != std::string::npos &&
             session_json.find("\"supports_tools\":true") != std::string::npos &&
             session_json.find("\"supports_streaming\":false") != std::string::npos &&
             session_json.find("\"reasoning_format\":\"summary\"") != std::string::npos,
         "runtime JSON support builds semantic session-start metadata");

  auto previous = model_info("old-provider", "old-model");
  auto model_change = ava::app::runtime::detail::model_change_data_json(previous, model);
  expect(model_change.find("\"previous_provider\":\"old-provider\"") != std::string::npos &&
             model_change.find("\"previous_model\":\"old-model\"") != std::string::npos &&
             model_change.find("\"model\":\"model-a\"") != std::string::npos &&
             model_change.find("\"context_window_tokens\":128000") != std::string::npos,
         "runtime JSON support builds semantic model-change metadata");

  ava::app::RuntimeReasoningSelection const selection{.level = "high", .budget_tokens = 2048, .display = "summary"};
  auto reasoning_enabled = ava::app::runtime::detail::reasoning_change_data_json(model, selection);
  auto reasoning_disabled = ava::app::runtime::detail::reasoning_change_data_json(model, std::nullopt);
  expect(reasoning_enabled.find("\"enabled\":true") != std::string::npos &&
             reasoning_enabled.find("\"level\":\"high\"") != std::string::npos &&
             reasoning_enabled.find("\"budget_tokens\":2048") != std::string::npos &&
             reasoning_disabled.find("\"enabled\":false") != std::string::npos,
         "runtime JSON support builds semantic reasoning-change metadata");
}

}  // namespace

void run_app_runtime_json_support_tests()
{
  test_json_escape_decoding_helpers();
  test_public_json_field_helpers();
  test_runtime_payload_builders();
}
