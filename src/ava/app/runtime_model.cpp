#include "sys.h"
#include "ava/app/runtime_json.h"
#include "ava/app/runtime_model.h"
#include "ava/app/runtime_prompt.h"
#include "ava/config/provider_profiles.h"
#include "ava/session/validation.h"
#include "ava/provider/registry.h"
#include "ava/core/json.h"

#include <algorithm>
#include <iterator>
#include <utility>

namespace ava::app::runtime {
namespace {

constexpr std::size_t kMaxImageAttachmentsPerRequest = 16;
constexpr std::size_t kMaxImageBytesPerRequest = 40 * 1024 * 1024;
constexpr std::size_t kAnthropicMaxImageBytes = 5 * 1024 * 1024;

ava::config::ModelInfo fallback_persisted_model(std::string provider_id, std::string model_id, std::string display_name, std::string family,
                                                std::string api_family, std::optional<long long> context_window_tokens,
                                                std::optional<long long> max_output_tokens, std::optional<bool> supports_tools,
                                                std::optional<bool> supports_streaming, std::optional<bool> supports_reasoning,
                                                std::optional<bool> reports_usage, std::vector<std::string> input_modalities,
                                                std::vector<std::string> output_modalities, std::vector<std::string> reasoning_levels,
                                                std::vector<std::string> compatibility_quirks, std::string reasoning_format)
{
  if (display_name.empty())
    display_name = model_id;
  if (family.empty())
    family = model_id;
  if (compatibility_quirks.empty())
    compatibility_quirks = {"persisted_unknown_model"};
  return ava::config::ModelInfo{.provider_id = std::move(provider_id),
                                .model_id = std::move(model_id),
                                .display_name = std::move(display_name),
                                .family = std::move(family),
                                .context_window_tokens = context_window_tokens,
                                .max_output_tokens = max_output_tokens,
                                .pricing = std::nullopt,
                                .api_family = std::move(api_family),
                                .input_modalities = std::move(input_modalities),
                                .supports_tools = supports_tools,
                                .supports_streaming = supports_streaming,
                                .supports_reasoning = supports_reasoning,
                                .reports_usage = reports_usage,
                                .reasoning_levels = std::move(reasoning_levels),
                                .compatibility_quirks = std::move(compatibility_quirks),
                                .output_modalities = std::move(output_modalities),
                                .reasoning_format = std::move(reasoning_format)};
}

bool model_accepts_reasoning_format(ava::config::ModelInfo const& model, std::string_view format)
{
  return ava::config::provider_accepts_reasoning_format(model, format);
}

bool model_accepts_images(ava::config::ModelInfo const& model)
{
  return std::find(model.input_modalities.begin(), model.input_modalities.end(), "image") != model.input_modalities.end();
}

bool bool_json_field_is_true(std::string_view object, std::string_view key)
{
  auto const start = ava::core::json::field_value_start(object, key);
  return start && object.substr(*start, 4) == "true";
}

std::vector<std::string> non_redacted_image_attachments(ava::session::SessionEntry const& entry)
{
  std::vector<std::string> attachments;
  if (entry.type != ava::session::EntryType::UserMessage)
    return attachments;
  auto const data_json = ava::session::sanitized_message_data_json(entry.data_json);
  for (auto const& attachment : ava::core::json::objects_in_array_field(data_json, "attachments"))
  {
    if (ava::core::json::string_field(attachment, "type").value_or("") == "image" && !bool_json_field_is_true(attachment, "redacted"))
    {
      attachments.push_back(attachment);
    }
  }
  return attachments;
}

ava::core::Error incompatible_model_switch_error(ava::config::ModelInfo const& model, std::string_view reason, ava::session::SessionEntry const& entry)
{
  auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "model switch cannot safely replay current session history");
  error.with_context("provider", model.provider_id);
  error.with_context("model", model.model_id);
  error.with_context("reason", std::string(reason));
  error.with_context("entry_id", entry.id);
  error.with_context("entry_type", ava::session::to_string(entry.type));
  return error;
}

ava::core::VoidResult validate_model_history_entries(std::vector<ava::session::SessionEntry> const& entries, ava::config::ModelInfo const& target)
{
  auto replay_start = entries.begin();
  for (auto it = entries.begin(); it != entries.end(); ++it)
  {
    if (it->type == ava::session::EntryType::Compaction)
      replay_start = std::next(it);
  }

  std::size_t image_count = 0;
  std::size_t total_image_bytes = 0;
  for (auto it = replay_start; it != entries.end(); ++it)
  {
    auto const& entry = *it;
    if ((entry.type == ava::session::EntryType::ToolCall || entry.type == ava::session::EntryType::ToolResult) && !target.supports_tools.value_or(false))
    {
      return std::unexpected(incompatible_model_switch_error(target, "target model does not declare tool support required by existing tool history", entry));
    }

    for (auto const& attachment : non_redacted_image_attachments(entry))
    {
      if (!model_accepts_images(target))
      {
        return std::unexpected(
            incompatible_model_switch_error(target, "target model does not declare image input support required by existing image attachments", entry));
      }
      ++image_count;
      if (image_count > kMaxImageAttachmentsPerRequest)
      {
        return std::unexpected(incompatible_model_switch_error(target, "target model cannot safely replay current image attachment count", entry));
      }
      auto const byte_size = ava::core::json::integer_field(attachment, "byte_size").value_or(0);
      if (byte_size <= 0)
        continue;
      auto const image_bytes = static_cast<std::size_t>(byte_size);
      if (target.api_family == "anthropic_messages" && image_bytes > kAnthropicMaxImageBytes)
      {
        return std::unexpected(
            incompatible_model_switch_error(target, "target provider image byte-size limit is lower than existing image attachments", entry));
      }
      if (image_bytes > kMaxImageBytesPerRequest - total_image_bytes)
      {
        return std::unexpected(incompatible_model_switch_error(target, "target model cannot safely replay current aggregate image bytes", entry));
      }
      total_image_bytes += image_bytes;
    }

    if (entry.type != ava::session::EntryType::ReasoningBlock)
      continue;
    auto const source_provider = ava::core::json::string_field(entry.data_json, "provider").value_or("");
    auto const format = ava::core::json::string_field(entry.data_json, "format").value_or("");
    if (!source_provider.empty() && source_provider != target.provider_id)
    {
      auto error = incompatible_model_switch_error(target, "target model cannot replay provider-native reasoning from another provider", entry);
      error.with_context("reasoning_provider", source_provider);
      if (!format.empty())
        error.with_context("reasoning_format", format);
      return std::unexpected(std::move(error));
    }
    if (model_accepts_reasoning_format(target, format))
      continue;

    auto error = incompatible_model_switch_error(target,
                                                 format.empty() ? "target model cannot safely replay provider-native reasoning history without a format"
                                                                : "target model cannot replay provider-native reasoning format",
                                                 entry);
    if (!format.empty())
      error.with_context("reasoning_format", format);
    return std::unexpected(std::move(error));
  }

  return {};
}

}  // namespace

ava::core::VoidResult validate_runtime_model_history(ava::session::SessionStore const& store, ava::config::ModelInfo const& target,
                                                     ava::session::SessionReadLimits read_limits)
{
  auto entries = store.load_bounded(read_limits);
  if (!entries)
    return std::unexpected(std::move(entries.error()));
  return validate_model_history_entries(*entries, target);
}

std::optional<ava::config::ModelInfo> latest_persisted_model(ava::config::ModelRegistry const& registry, std::vector<ava::session::SessionEntry> const& entries)
{
  std::optional<std::string> provider_id;
  std::optional<std::string> model_id;
  std::string display_name;
  std::string family;
  std::string api_family;
  std::optional<long long> context_window_tokens;
  std::optional<long long> max_output_tokens;
  std::optional<bool> supports_tools;
  std::optional<bool> supports_streaming;
  std::optional<bool> supports_reasoning;
  std::optional<bool> reports_usage;
  std::vector<std::string> input_modalities;
  std::vector<std::string> output_modalities;
  std::vector<std::string> reasoning_levels;
  std::vector<std::string> compatibility_quirks;
  std::string reasoning_format;
  for (auto const& entry : entries)
  {
    if (entry.type != ava::session::EntryType::SessionStart && entry.type != ava::session::EntryType::ModelChange)
    {
      continue;
    }
    auto provider = ava::core::json::string_field(entry.data_json, "provider");
    auto model = ava::core::json::string_field(entry.data_json, "model");
    if (!provider || !model || provider->empty() || model->empty())
      continue;
    provider_id = std::move(*provider);
    model_id = std::move(*model);
    display_name = ava::core::json::string_field(entry.data_json, "display_name").value_or("");
    family = ava::core::json::string_field(entry.data_json, "family").value_or("");
    api_family = ava::core::json::string_field(entry.data_json, "api_family").value_or("");
    context_window_tokens = ava::core::json::integer_field(entry.data_json, "context_window_tokens");
    max_output_tokens = ava::core::json::integer_field(entry.data_json, "max_output_tokens");
    supports_tools = bool_json_field(entry.data_json, "supports_tools");
    supports_streaming = bool_json_field(entry.data_json, "supports_streaming");
    supports_reasoning = bool_json_field(entry.data_json, "supports_reasoning");
    reports_usage = bool_json_field(entry.data_json, "reports_usage");
    input_modalities = string_array_field(entry.data_json, "input_modalities");
    output_modalities = string_array_field(entry.data_json, "output_modalities");
    reasoning_levels = string_array_field(entry.data_json, "reasoning_levels");
    compatibility_quirks = string_array_field(entry.data_json, "compatibility_quirks");
    reasoning_format = ava::core::json::string_field(entry.data_json, "reasoning_format").value_or("");
  }
  if (!provider_id || !model_id)
    return std::nullopt;
  if (auto model = ava::config::find_model(registry, *provider_id, *model_id))
    return model;
  return fallback_persisted_model(std::move(*provider_id), std::move(*model_id), std::move(display_name), std::move(family), std::move(api_family),
                                  context_window_tokens, max_output_tokens, supports_tools, supports_streaming, supports_reasoning, reports_usage,
                                  std::move(input_modalities), std::move(output_modalities), std::move(reasoning_levels), std::move(compatibility_quirks),
                                  std::move(reasoning_format));
}

}  // namespace ava::app::runtime

namespace ava::app {

ava::core::Result<ava::config::ModelInfo> resolve_runtime_model(ava::config::XdgPaths const& paths, std::string_view provider_id, std::string_view model_id)
{
  auto const trimmed_provider_id = runtime::trimmed_copy(provider_id);
  auto const trimmed_model_id = runtime::trimmed_copy(model_id);
  if (trimmed_provider_id.empty() || trimmed_model_id.empty())
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "provider and model are required"));
  }

  auto const providers = ava::provider::builtin_provider_registry();
  if (!providers.contains(trimmed_provider_id))
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::NotFound, "provider is not registered");
    error.with_context("provider", trimmed_provider_id);
    return std::unexpected(std::move(error));
  }

  auto registry = ava::config::load_model_registry(paths);
  if (!registry)
    return std::unexpected(std::move(registry.error()));
  auto model = ava::config::find_model(*registry, trimmed_provider_id, trimmed_model_id);
  if (!model)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::NotFound, "model is not configured");
    error.with_context("provider", trimmed_provider_id);
    error.with_context("model", trimmed_model_id);
    return std::unexpected(std::move(error));
  }
  return *model;
}

ava::core::Result<bool> switch_runtime_model(runtime::Session& session, ava::config::ModelInfo model)
{
  if (session.model.provider_id == model.provider_id && session.model.model_id == model.model_id)
    return false;

  auto compatible = runtime::validate_runtime_model_history(session.store, model, ava::session::SessionReadLimits{});
  if (!compatible)
    return std::unexpected(std::move(compatible.error()));

  auto prompt_state = runtime::load_runtime_prompt_state(session.paths, model, session.mode, session.workspace_dir, session.current_dir,
                                                         project_resources_trusted(session.project_trust), session.prompt_overrides);
  if (!prompt_state)
    return std::unexpected(std::move(prompt_state.error()));

  auto const previous = session.model;
  auto appended = session.append_owned(runtime::make_model_change_entry(previous, model));
  if (!appended)
    return std::unexpected(std::move(appended.error()));

  session.model = std::move(model);
  session.mode = prompt_state->mode;
  session.base_prompt = std::move(prompt_state->base_prompt);
  session.context_sources = std::move(prompt_state->context_sources);
  session.freshness_sources = std::move(prompt_state->freshness_sources);
  session.system_prompt = std::move(prompt_state->system_prompt);
  session.reasoning = std::nullopt;
  return true;
}

}  // namespace ava::app
