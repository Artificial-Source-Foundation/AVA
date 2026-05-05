#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ava/core/result.h"
#include "ava/plugin/enablement.h"

namespace ava::plugin::detail {

[[nodiscard]] std::optional<bool> bool_field(std::string_view object, std::string_view key);
[[nodiscard]] std::optional<PluginScope> parse_plugin_scope(std::string_view scope);
[[nodiscard]] std::vector<std::pair<std::string, std::string>> object_entries_with_object_values(
    std::string_view object);
[[nodiscard]] ava::core::Result<std::string> read_plugin_enablement_file(std::filesystem::path const& path);
[[nodiscard]] ava::core::VoidResult write_plugin_enablement_file_atomic(std::filesystem::path const& path,
                                                                        std::string_view content);
[[nodiscard]] ava::core::Result<std::vector<PluginEnablementRecord>> parse_plugin_enablement_json(
    std::string_view json, std::filesystem::path const& state_file);
[[nodiscard]] std::string plugin_enablement_json(std::vector<PluginEnablementRecord> const& records);
void sort_plugin_enablement_records(std::vector<PluginEnablementRecord>& records);
void upsert_plugin_enablement_record(std::vector<PluginEnablementRecord>& records,
                                     std::filesystem::path const& workspace, std::string_view plugin_id,
                                     PluginScope scope, bool enabled);

}  // namespace ava::plugin::detail
