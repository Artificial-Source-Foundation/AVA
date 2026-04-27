#include "ava/config/model_config.h"

#include <array>
#include <fstream>
#include <utility>

#include "ava/core/json.h"

namespace ava::config {
namespace {

constexpr std::size_t max_model_config_bytes = 1024 * 1024;

ava::core::Result<std::string> read_text(const std::filesystem::path& path) {
  std::error_code status_error;
  if (!std::filesystem::is_regular_file(path, status_error)) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "model config is not a regular file");
    error.with_context("path", path.string());
    if (status_error) error.with_context("cause", status_error.message());
    return std::unexpected(std::move(error));
  }
  std::error_code size_error;
  const auto size = std::filesystem::file_size(path, size_error);
  if (size_error || size > max_model_config_bytes) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "model config is too large");
    error.with_context("path", path.string());
    error.with_context("max_bytes", std::to_string(max_model_config_bytes));
    if (size_error) error.with_context("cause", size_error.message());
    return std::unexpected(std::move(error));
  }
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to open model config");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }
  std::string content;
  std::array<char, 4096> buffer{};
  while (file) {
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    if (file.gcount() > 0) content.append(buffer.data(), static_cast<std::size_t>(file.gcount()));
    if (content.size() > max_model_config_bytes) {
      auto error = ava::core::Error(ava::core::ErrorCategory::Io, "model config is too large");
      error.with_context("path", path.string());
      error.with_context("max_bytes", std::to_string(max_model_config_bytes));
      return std::unexpected(std::move(error));
    }
  }
  if (!file.eof() && file.fail()) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed while reading model config");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }
  return content;
}

std::string family_from_model_id(std::string_view model_id) {
  if (model_id == "gpt-5" || model_id.starts_with("gpt-5.") || model_id.starts_with("gpt-5-")) return "gpt-5";
  const auto dash = model_id.find_last_of('-');
  if (dash == std::string_view::npos) return std::string(model_id);
  return std::string(model_id.substr(0, dash));
}

}  // namespace

ModelRegistry builtin_model_registry() {
  return ModelRegistry{
      .default_provider_id = "openai",
      .default_model_id = "gpt-5.5",
      .models = {ModelInfo{
          .provider_id = "openai",
          .model_id = "gpt-5.5",
          .display_name = "GPT-5.5",
          .family = "gpt-5",
      }},
  };
}

ModelRegistry parse_model_registry(std::string_view content) {
  auto registry = builtin_model_registry();
  if (auto provider = ava::core::json::string_field(content, "default_provider")) registry.default_provider_id = *provider;
  if (auto model = ava::core::json::string_field(content, "default_model")) registry.default_model_id = *model;

  for (const auto& item : ava::core::json::objects_in_array_field(content, "models")) {
    auto provider = ava::core::json::string_field(item, "provider");
    auto id = ava::core::json::string_field(item, "id");
    if (!provider || !id) continue;
    registry.models.push_back(ModelInfo{
        .provider_id = *provider,
        .model_id = *id,
        .display_name = ava::core::json::string_field(item, "name").value_or(*id),
        .family = ava::core::json::string_field(item, "family").value_or(family_from_model_id(*id)),
    });
  }
  return registry;
}

ava::core::Result<ModelRegistry> load_model_registry(const XdgPaths& paths) {
  if (!std::filesystem::exists(paths.models_file)) return builtin_model_registry();
  auto content = read_text(paths.models_file);
  if (!content) return std::unexpected(content.error());
  return parse_model_registry(*content);
}

ModelInfo select_default_model(const ModelRegistry& registry) {
  for (const auto& model : registry.models) {
    if (model.provider_id == registry.default_provider_id && model.model_id == registry.default_model_id) return model;
  }
  return ModelInfo{
      .provider_id = registry.default_provider_id,
      .model_id = registry.default_model_id,
      .display_name = registry.default_model_id,
      .family = family_from_model_id(registry.default_model_id),
  };
}

}  // namespace ava::config
