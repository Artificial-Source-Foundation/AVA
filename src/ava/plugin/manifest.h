#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "ava/core/result.h"

namespace ava::plugin {

inline constexpr std::string_view kPluginApiVersion = "ava.plugin.v1";

enum class PluginScope {
  Global,
  Project,
};

[[nodiscard]] std::string_view to_string(PluginScope scope);

struct PluginEntrypoint {
  std::string command;
  std::vector<std::string> args;
};

struct PluginToolContribution {
  std::string name;
  std::string description;
  std::string input_schema_json;
};

struct PluginCommandContribution {
  std::string name;
  std::string description;
};

struct PluginResourceContribution {
  std::string name;
  std::string description;
  std::string path;
};

struct PluginEventHookContribution {
  std::string event;
};

struct PluginContributions {
  std::vector<PluginToolContribution> tools;
  std::vector<PluginCommandContribution> commands;
  std::vector<PluginResourceContribution> prompts;
  std::vector<PluginResourceContribution> skills;
  std::vector<PluginEventHookContribution> event_hooks;
};

struct PluginManifest {
  int schema_version = 0;
  std::string id;
  std::string name;
  std::string version;
  std::string api_version;
  std::string description;
  PluginEntrypoint entrypoint;
  std::vector<std::string> capabilities;
  PluginContributions contributes;
  std::filesystem::path path;
  std::filesystem::path directory;
};

struct DiscoveredPlugin {
  PluginManifest manifest;
  PluginScope scope = PluginScope::Global;
};

[[nodiscard]] ava::core::Result<PluginManifest> parse_plugin_manifest(std::string_view json,
                                                                      std::filesystem::path manifest_path = {});
[[nodiscard]] ava::core::Result<PluginManifest> load_plugin_manifest(const std::filesystem::path& manifest_path);

}  // namespace ava::plugin
