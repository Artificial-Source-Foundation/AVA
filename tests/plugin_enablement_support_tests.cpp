#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "ava/plugin/enablement.h"
#include "ava/plugin/enablement_support.h"
#include "tests/support/test_harness.h"

namespace {

void write_text(std::filesystem::path const& path, std::string const& text)
{
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output << text;
}

std::string read_text(std::filesystem::path const& path)
{
  std::ifstream input(path, std::ios::binary);
  std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  return text;
}

void test_bool_and_scope_parsing()
{
  auto const enabled = ava::plugin::detail::bool_field(R"({"enabled":true,"other":false})", "enabled");
  auto const disabled = ava::plugin::detail::bool_field(R"({"enabled": false})", "enabled");
  auto const malformed = ava::plugin::detail::bool_field(R"({"enabled":trueish})", "enabled");
  expect(enabled && *enabled && disabled && !*disabled && !malformed,
         "plugin enablement support parses only strict JSON bool fields");

  auto const global = ava::plugin::detail::parse_plugin_scope("global");
  auto const project = ava::plugin::detail::parse_plugin_scope("project");
  auto const bad = ava::plugin::detail::parse_plugin_scope("workspace");
  expect(global && *global == ava::plugin::PluginScope::Global && project &&
             *project == ava::plugin::PluginScope::Project && !bad,
         "plugin enablement support parses known scope keys only");
}

void test_object_entries_with_object_values()
{
  auto entries = ava::plugin::detail::object_entries_with_object_values(
      R"({"plain":{"enabled":true},"escaped\"key":{"enabled":false},"nonobject":7,"array":[{"skip":{}}],"braces":{"text":"{inside}"}})");
  expect(entries.size() == 3 && entries[0].first == "plain" && entries[1].first == "escaped\"key" &&
             entries[2].first == "braces" && entries[2].second.find("{inside}") != std::string::npos,
         "plugin enablement support extracts object-valued entries with escaped keys and nested strings");
}

void test_parse_enablement_json()
{
  auto records = ava::plugin::detail::parse_plugin_enablement_json(
      R"({"workspaces":{"/tmp/ws":{"project":{"com.example.todo":{"enabled":true},"Bad.Id":{"enabled":true},"com.example.default":{}},"unknown":{"com.example.skip":{"enabled":true}}},"/tmp/other":{"global":{"com.example.global":{"enabled":false}}}}})",
      "/tmp/plugin-enablement.json");
  expect(records && records->size() == 3,
         records ? "plugin enablement support parses valid state records"
                 : "plugin enablement support parses valid state records: " + records.error().format());
  if (!records) return;
  expect((*records)[0].workspace == "/tmp/ws" && (*records)[0].plugin_id == "com.example.todo" &&
             (*records)[0].scope == ava::plugin::PluginScope::Project && (*records)[0].enabled,
         "plugin enablement support parses enabled project records");
  expect((*records)[1].plugin_id == "com.example.default" && !(*records)[1].enabled,
         "plugin enablement support defaults missing enabled fields to false");
  expect((*records)[2].workspace == "/tmp/other" && (*records)[2].scope == ava::plugin::PluginScope::Global,
         "plugin enablement support parses global records and ignores unknown scopes");

  auto malformed = ava::plugin::detail::parse_plugin_enablement_json("not-json", "/tmp/plugin-enablement.json");
  expect(!malformed && malformed.error().message().find("valid JSON") != std::string::npos,
         "plugin enablement support rejects malformed state JSON");
}

void test_sort_upsert_and_json()
{
  std::vector<ava::plugin::PluginEnablementRecord> records{
      ava::plugin::PluginEnablementRecord{.workspace = "/tmp/z",
                                          .plugin_id = "com.example.z",
                                          .scope = ava::plugin::PluginScope::Project,
                                          .enabled = true},
      ava::plugin::PluginEnablementRecord{.workspace = "/tmp/a",
                                          .plugin_id = "com.example.b",
                                          .scope = ava::plugin::PluginScope::Project,
                                          .enabled = false},
      ava::plugin::PluginEnablementRecord{.workspace = "/tmp/a",
                                          .plugin_id = "com.example.a",
                                          .scope = ava::plugin::PluginScope::Global,
                                          .enabled = true}};
  ava::plugin::detail::upsert_plugin_enablement_record(records, "/tmp/a", "com.example.b",
                                                       ava::plugin::PluginScope::Project, true);
  expect(records.size() == 3 && records[1].enabled, "plugin enablement support updates existing records in place");

  ava::plugin::detail::upsert_plugin_enablement_record(records, "/tmp/a", "com.example.c",
                                                       ava::plugin::PluginScope::Project, false);
  ava::plugin::detail::sort_plugin_enablement_records(records);
  expect(records.size() == 4 && records[0].workspace == "/tmp/a" &&
             records[0].scope == ava::plugin::PluginScope::Global && records[1].plugin_id == "com.example.b",
         "plugin enablement support sorts records deterministically");

  auto const json = ava::plugin::detail::plugin_enablement_json(records);
  expect(
      json ==
          R"({"workspaces":{"/tmp/a":{"global":{"com.example.a":{"enabled":true}},"project":{"com.example.b":{"enabled":true},"com.example.c":{"enabled":false}}},"/tmp/z":{"project":{"com.example.z":{"enabled":true}}}}})",
      "plugin enablement support serializes deterministic enablement JSON");
}

void test_file_read_and_atomic_write()
{
  auto const root = temp_root() / "plugin-enablement-support";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const state_file = root / "state" / "plugin-enablement.json";

  auto missing = ava::plugin::detail::read_plugin_enablement_file(state_file);
  expect(!missing && missing.error().category() == ava::core::ErrorCategory::Io,
         "plugin enablement support reports read errors for missing files");

  auto written = ava::plugin::detail::write_plugin_enablement_file_atomic(state_file, R"({"workspaces":{}})");
  expect(written.has_value(),
         written ? "plugin enablement support atomically writes state files"
                 : "plugin enablement support atomically writes state files: " + written.error().format());
  expect(read_text(state_file) == R"({"workspaces":{}})", "plugin enablement support writes exact state content");

  write_text(state_file, R"({"workspaces":{"a":{"project":{"com.example.todo":{"enabled":true}}}}})");
  auto read = ava::plugin::detail::read_plugin_enablement_file(state_file);
  expect(read && read->find("com.example.todo") != std::string::npos,
         read ? "plugin enablement support reads state files"
              : "plugin enablement support reads state files: " + read.error().format());
}

void test_public_enablement_path_uses_support_helpers()
{
  auto const root = temp_root() / "plugin-enablement-public";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const state_file = root / "state" / "plugin-enablement.json";
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);

  auto enabled = ava::plugin::set_plugin_enabled(state_file, workspace, "com.example.todo", true,
                                                 ava::plugin::PluginScope::Project);
  expect(enabled.has_value(), enabled ? "plugin enablement public writer still succeeds"
                                      : "plugin enablement public writer still succeeds: " + enabled.error().format());

  auto checked =
      ava::plugin::plugin_enabled(state_file, workspace, "com.example.todo", ava::plugin::PluginScope::Project);
  expect(checked && *checked, checked ? "plugin enablement public reader still succeeds"
                                      : "plugin enablement public reader still succeeds: " + checked.error().format());
}

}  // namespace

void run_plugin_enablement_support_tests()
{
  test_bool_and_scope_parsing();
  test_object_entries_with_object_values();
  test_parse_enablement_json();
  test_sort_upsert_and_json();
  test_file_read_and_atomic_write();
  test_public_enablement_path_uses_support_helpers();
}
