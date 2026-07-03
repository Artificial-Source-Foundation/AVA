#include "ava/app/project_trust.h"

#include "ava/core/error.h"
#include "ava/core/json.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <optional>
#include <sstream>
#include <utility>

namespace ava::app {
namespace {

constexpr std::size_t kMaxTrustFileBytes = 256 * 1024;

struct TrustRecord
{
  std::filesystem::path path;
  bool trusted = false;
};

std::filesystem::path normalized_absolute(std::filesystem::path const& path)
{
  std::error_code error;
  auto normalized = std::filesystem::weakly_canonical(path, error);
  if (!error)
    return normalized.lexically_normal();
  auto absolute = std::filesystem::absolute(path, error);
  return error ? path.lexically_normal() : absolute.lexically_normal();
}

bool path_exists(std::filesystem::path const& path)
{
  std::error_code error;
  return std::filesystem::exists(path, error) && !error;
}

void add_resource_if_present(std::vector<ProjectTrustResource>& resources, std::string kind,
                             std::filesystem::path const& path)
{
  if (!path_exists(path))
    return;
  resources.push_back(ProjectTrustResource{.kind = std::move(kind), .path = normalized_absolute(path)});
}

std::vector<ProjectTrustResource> discover_protected_resources(std::filesystem::path const& workspace_dir)
{
  std::vector<ProjectTrustResource> resources;
  if (workspace_dir.empty())
    return resources;
  auto const workspace = normalized_absolute(workspace_dir);
  add_resource_if_present(resources, "prompt_commands", workspace / ".ava" / "commands");
  add_resource_if_present(resources, "prompt_commands", workspace / ".ava" / "command");
  add_resource_if_present(resources, "skills", workspace / ".ava" / "skills");
  add_resource_if_present(resources, "skills", workspace / ".agents" / "skills");
  add_resource_if_present(resources, "skills", workspace / ".claude" / "skills");
  add_resource_if_present(resources, "plugins", workspace / ".ava" / "plugins");
  add_resource_if_present(resources, "mcp_config", workspace / ".ava" / "mcp.json");
  add_resource_if_present(resources, "lsp_config", workspace / ".ava" / "lsp.json");
  add_resource_if_present(resources, "system_prompt", workspace / ".ava" / "SYSTEM.md");
  add_resource_if_present(resources, "system_prompt", workspace / ".ava" / "APPEND_SYSTEM.md");
  return resources;
}

std::optional<bool> bool_field(std::string_view object, std::string_view key)
{
  auto const start = ava::core::json::field_value_start(object, key);
  if (!start)
    return std::nullopt;
  auto const valid_terminator = [](std::string_view value, std::size_t offset) {
    while (offset < value.size() && std::isspace(static_cast<unsigned char>(value[offset])) != 0) ++offset;
    return offset >= value.size() || value[offset] == ',' || value[offset] == '}';
  };
  if (object.substr(*start, 4) == "true" && valid_terminator(object, *start + 4))
    return true;
  if (object.substr(*start, 5) == "false" && valid_terminator(object, *start + 5))
    return false;
  return std::nullopt;
}

ava::core::Result<std::string> read_trust_file(std::filesystem::path const& path)
{
  if (path.empty() || !path_exists(path))
    return std::string{};
  std::error_code size_error;
  auto const size = std::filesystem::file_size(path, size_error);
  if (size_error)
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to inspect project trust file")
                               .with_context("path", path.string())
                               .with_context("cause", size_error.message()));
  }
  if (size > kMaxTrustFileBytes)
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "project trust file is too large")
                               .with_context("path", path.string())
                               .with_context("max_bytes", std::to_string(kMaxTrustFileBytes)));
  }
  std::ifstream file(path, std::ios::binary);
  if (!file)
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to open project trust file")
                               .with_context("path", path.string()));
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  if (file.bad())
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to read project trust file")
                               .with_context("path", path.string()));
  }
  return buffer.str();
}

std::vector<TrustRecord> parse_trust_records(std::string_view content)
{
  std::vector<TrustRecord> records;
  if (content.empty() || !ava::core::json::is_valid_object(content))
    return records;
  for (auto const& object : ava::core::json::objects_in_array_field(content, "decisions"))
  {
    auto path = ava::core::json::string_field(object, "path");
    auto trusted = bool_field(object, "trusted");
    if (!path || !trusted)
      continue;
    records.push_back(TrustRecord{.path = normalized_absolute(*path), .trusted = *trusted});
  }
  return records;
}

std::string trust_records_json(std::vector<TrustRecord> const& records)
{
  std::string json = "{\"schema_version\":1,\"decisions\":[";
  for (std::size_t index = 0; index < records.size(); ++index)
  {
    if (index > 0)
      json += ",";
    json += "{\"path\":\"";
    json += ava::core::json::escape(records[index].path.string());
    json += "\",\"trusted\":";
    json += (records[index].trusted ? std::string("true") : std::string("false"));
    json += "}";
  }
  json += "]}\n";
  return json;
}

bool path_is_ancestor_or_same(std::filesystem::path const& ancestor, std::filesystem::path const& path)
{
  if (ancestor.empty())
    return false;
  if (ancestor == path)
    return true;
  std::error_code relative_error;
  auto const relative = std::filesystem::relative(path, ancestor, relative_error);
  if (relative_error || relative.empty())
    return false;
  return *relative.begin() != "..";
}

std::optional<TrustRecord> closest_matching_record(std::vector<TrustRecord> const& records,
                                                   std::filesystem::path const& workspace_dir)
{
  std::optional<TrustRecord> best;
  auto const workspace = normalized_absolute(workspace_dir);
  for (auto const& record : records)
  {
    if (!path_is_ancestor_or_same(record.path, workspace))
      continue;
    if (!best || record.path.string().size() > best->path.string().size())
      best = record;
  }
  return best;
}

ava::core::VoidResult write_trust_records(std::filesystem::path const& path,
                                          std::vector<TrustRecord> const& records)
{
  std::error_code directory_error;
  std::filesystem::create_directories(path.parent_path(), directory_error);
  if (directory_error)
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to create project trust directory")
                               .with_context("path", path.parent_path().string())
                               .with_context("cause", directory_error.message()));
  }
  auto const temp_path = path.string() + ".tmp";
  {
    std::ofstream file(temp_path, std::ios::binary | std::ios::trunc);
    if (!file)
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to write project trust file")
                                 .with_context("path", temp_path));
    file << trust_records_json(records);
    if (!file)
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to finish project trust file")
                                 .with_context("path", temp_path));
  }
  std::error_code rename_error;
  std::filesystem::rename(temp_path, path, rename_error);
  if (rename_error)
  {
    std::error_code remove_error;
    std::filesystem::remove(temp_path, remove_error);
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to replace project trust file")
                               .with_context("path", path.string())
                               .with_context("cause", rename_error.message()));
  }
  return {};
}

ava::core::Result<std::vector<TrustRecord>> load_records_for_write(std::filesystem::path const& path)
{
  auto content = read_trust_file(path);
  if (!content)
    return std::unexpected(std::move(content.error()));
  return parse_trust_records(*content);
}

}  // namespace

std::string_view to_string(ProjectTrustDecision decision)
{
  switch (decision)
  {
    case ProjectTrustDecision::Unknown:
      return "unknown";
    case ProjectTrustDecision::Trusted:
      return "trusted";
    case ProjectTrustDecision::Denied:
      return "denied";
  }
  return "unknown";
}

bool project_resources_trusted(ProjectTrustState const& state)
{
  return state.decision == ProjectTrustDecision::Trusted;
}

std::filesystem::path project_trust_file(ava::config::XdgPaths const& paths)
{
  return paths.ava_state_dir / "project-trust.json";
}

ProjectTrustState load_project_trust_state(ava::config::XdgPaths const& paths,
                                           std::filesystem::path const& workspace_dir)
{
  ProjectTrustState state;
  state.workspace_dir = normalized_absolute(workspace_dir);
  state.trust_file = project_trust_file(paths);
  state.protected_resources = discover_protected_resources(state.workspace_dir);

  auto content = read_trust_file(state.trust_file);
  if (!content)
  {
    state.diagnostic = content.error().format();
    return state;
  }
  auto records = parse_trust_records(*content);
  if (auto matched = closest_matching_record(records, state.workspace_dir))
  {
    state.decision = matched->trusted ? ProjectTrustDecision::Trusted : ProjectTrustDecision::Denied;
    state.matched_path = matched->path;
  }
  return state;
}

ava::core::VoidResult set_project_trust_decision(ava::config::XdgPaths const& paths,
                                                 std::filesystem::path const& workspace_dir,
                                                 bool trusted)
{
  auto const trust_path = project_trust_file(paths);
  auto records = load_records_for_write(trust_path);
  if (!records)
    return std::unexpected(std::move(records.error()));
  auto const workspace = normalized_absolute(workspace_dir);
  auto existing = std::ranges::find_if(*records, [&](TrustRecord const& record) { return record.path == workspace; });
  if (existing == records->end())
    records->push_back(TrustRecord{.path = workspace, .trusted = trusted});
  else
    existing->trusted = trusted;
  std::ranges::sort(*records, [](TrustRecord const& left, TrustRecord const& right) {
    return left.path.string() < right.path.string();
  });
  return write_trust_records(trust_path, *records);
}

ava::core::VoidResult clear_project_trust_decision(ava::config::XdgPaths const& paths,
                                                   std::filesystem::path const& workspace_dir)
{
  auto const trust_path = project_trust_file(paths);
  auto records = load_records_for_write(trust_path);
  if (!records)
    return std::unexpected(std::move(records.error()));
  auto const workspace = normalized_absolute(workspace_dir);
  auto const old_size = records->size();
  records->erase(std::remove_if(records->begin(), records->end(),
                                [&](TrustRecord const& record) { return record.path == workspace; }),
                 records->end());
  if (records->size() == old_size)
    return {};
  return write_trust_records(trust_path, *records);
}

}  // namespace ava::app
