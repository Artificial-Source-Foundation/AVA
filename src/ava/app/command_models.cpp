#include "ava/app/command_models.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string_view>
#include <vector>

#include "ava/app/command_format.h"
#include "ava/config/reasoning_profiles.h"
#include "ava/provider/registry.h"

namespace ava::app {
namespace {

std::string model_key(std::string_view provider_id, std::string_view model_id) {
  return std::string(provider_id) + "\n" + std::string(model_id);
}

std::vector<ava::config::ModelInfo> effective_models(ava::config::ModelRegistry const& registry) {
  std::vector<ava::config::ModelInfo> models;
  std::vector<std::string> seen;
  for (auto model = registry.models.rbegin(); model != registry.models.rend(); ++model) {
    auto const key = model_key(model->provider_id, model->model_id);
    if (std::ranges::find(seen, key) != seen.end()) continue;
    seen.push_back(key);
    models.push_back(*model);
  }
  std::reverse(models.begin(), models.end());
  return models;
}

std::string trim_ascii(std::string_view text) {
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) text.remove_prefix(1);
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) text.remove_suffix(1);
  return std::string(text);
}

std::string lower_ascii(std::string_view text) {
  std::string lowered(text);
  std::ranges::transform(lowered, lowered.begin(),
                         [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return lowered;
}

bool contains_ascii_case_insensitive(std::string_view text, std::string_view query) {
  if (query.empty()) return true;
  return lower_ascii(text).find(lower_ascii(query)) != std::string::npos;
}

bool model_matches_query(ava::config::ModelInfo const& model, std::string_view query) {
  if (query.empty()) return true;
  return contains_ascii_case_insensitive(model.provider_id + "/" + model.model_id, query) ||
         contains_ascii_case_insensitive(model.model_id, query) ||
         contains_ascii_case_insensitive(model.display_name, query) ||
         contains_ascii_case_insensitive(model.family, query);
}

std::string optional_bool_text(std::optional<bool> const& value) {
  if (!value) return "unknown";
  return *value ? "yes" : "no";
}

std::string format_models_text(RuntimeSession const& session, ava::config::ModelRegistry const& registry,
                               std::string_view query) {
  auto const providers = ava::provider::builtin_provider_registry();
  auto models = effective_models(registry);
  bool current_in_catalog = false;

  std::string output = "Models:\n";
  output += "current " + session.model.provider_id + "/" + session.model.model_id + "\n";
  output += session.reasoning ? "reasoning current " + session.reasoning->level + "\n" : "reasoning current default\n";
  output += "default " + registry.default_provider_id + "/" + registry.default_model_id + "\n";
  if (!query.empty()) output += "filter " + sanitize_inline_text(std::string(query)) + "\n";
  std::size_t shown = 0;
  for (auto const& model : models) {
    current_in_catalog = current_in_catalog ||
                         (model.provider_id == session.model.provider_id && model.model_id == session.model.model_id);
    if (!model_matches_query(model, query)) continue;
    ++shown;
    bool const registered = providers.contains(model.provider_id);
    output += model.provider_id == session.model.provider_id && model.model_id == session.model.model_id ? "* " : "  ";
    output += model.provider_id + "/" + model.model_id;
    if (!model.display_name.empty()) output += "  " + model.display_name;
    output += registered ? "\n" : "  unavailable: provider not registered\n";
    output += "    tools=" + optional_bool_text(model.supports_tools) +
              " streaming=" + optional_bool_text(model.supports_streaming) +
              " reasoning=" + optional_bool_text(model.supports_reasoning) +
              " usage=" + optional_bool_text(model.reports_usage);
    if (model.context_window_tokens) output += " context=" + std::to_string(*model.context_window_tokens);
    if (model.max_output_tokens) output += " max_output=" + std::to_string(*model.max_output_tokens);
    output += '\n';
    if (!model.reasoning_levels.empty()) {
      output += "    reasoning levels: " + joined_strings(model.reasoning_levels, ", ") + "\n";
    }
    if (model.supports_reasoning.value_or(false)) {
      output += "    reasoning params: " + ava::config::reasoning_parameter_text(model) + "\n";
      if (!model.reasoning_format.empty()) output += "    reasoning format: " + model.reasoning_format + "\n";
    }
  }
  if (!current_in_catalog) {
    output += "* " + session.model.provider_id + "/" + session.model.model_id + "  current model outside catalog\n";
  }
  if (shown == 0 && !query.empty()) {
    output += "  no configured models match the filter\n";
  }
  output +=
      "\nModel switching is not enabled here. In the TUI, Ctrl+T cycles the current model's declared "
      "reasoning levels using the provider/model-specific reasoning parameters above.";
  return output;
}

}  // namespace

ava::core::Result<CommandResult> run_models_command(RuntimeSession& session, std::string_view query) {
  CommandResult result;
  result.handled = true;
  auto const trimmed_query = trim_ascii(query);
  auto registry = ava::config::load_model_registry(session.paths);
  if (!registry) return std::unexpected(std::move(registry.error()));
  add_output(result, format_models_text(session, *registry, trimmed_query));
  return result;
}

}  // namespace ava::app
