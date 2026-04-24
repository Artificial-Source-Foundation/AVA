#include "ava/agent/response.hpp"

#include <algorithm>
#include <string_view>

#include <nlohmann/json.hpp>

namespace ava::agent::response {

namespace {

[[nodiscard]] std::string fallback_id(std::size_t index) {
  return "tool_call_" + std::to_string(index + 1);
}

[[nodiscard]] std::optional<nlohmann::json> parse_json(std::string_view value) {
  try {
    return nlohmann::json::parse(value);
  } catch(const std::exception&) {
    return std::nullopt;
  }
}

void merge_tool_arguments(std::string& existing, const std::string& incoming) {
  if(incoming.empty()) {
    return;
  }

  if(existing.empty()) {
    existing = incoming;
    return;
  }

  if(existing == incoming) {
    return;
  }

  const auto incoming_trimmed = std::string_view(incoming);
  if((incoming_trimmed.starts_with('{') || incoming_trimmed.starts_with('[')) && parse_json(incoming_trimmed).has_value()) {
    existing = incoming;
    return;
  }

  if(incoming_trimmed.starts_with(',') && !existing.empty() && existing.back() == '}') {
    existing.pop_back();
  }

  existing += incoming;
}

std::vector<ava::types::ToolCall> parse_tool_call_array(const nlohmann::json& calls_json) {
  std::vector<ava::types::ToolCall> calls;
  if(!calls_json.is_array()) {
    return calls;
  }

  calls.reserve(calls_json.size());
  for(std::size_t idx = 0; idx < calls_json.size(); ++idx) {
    const auto& call = calls_json.at(idx);
    if(!call.is_object()) {
      continue;
    }

    auto name = call.value("name", std::string{});
    auto id = call.value("id", std::string{});
    auto arguments = call.contains("arguments") ? call.at("arguments") : nlohmann::json::object();

    if(name.empty() && call.contains("function") && call.at("function").is_object()) {
      const auto& function = call.at("function");
      name = function.value("name", std::string{});
      if(function.contains("arguments") && function.at("arguments").is_string()) {
        const auto parsed_args = parse_json(function.at("arguments").get_ref<const std::string&>());
        arguments = parsed_args.value_or(nlohmann::json::object());
      }
    }

    if(!arguments.is_object() && !arguments.is_array()) {
      arguments = nlohmann::json::object();
    }

    if(name.empty()) {
      continue;
    }

    if(id.empty()) {
      id = fallback_id(idx);
    }

    calls.push_back(ava::types::ToolCall{
        .id = std::move(id),
        .name = std::move(name),
        .arguments = std::move(arguments),
    });
  }

  return calls;
}

}  // namespace

bool ToolCallAccumulator::is_complete() const {
  if(name.empty()) {
    return false;
  }
  if(arguments_json.empty()) {
    return true;
  }
  return parse_json(arguments_json).has_value();
}

std::optional<ava::types::ToolCall> ToolCallAccumulator::to_tool_call() const {
  if(name.empty()) {
    return std::nullopt;
  }

  const auto parsed_args = arguments_json.empty() ? std::optional<nlohmann::json>{nlohmann::json::object()} : parse_json(arguments_json);
  if(!parsed_args.has_value()) {
    return std::nullopt;
  }

  return ava::types::ToolCall{
      .id = id.empty() ? fallback_id(index) : id,
      .name = name,
      .arguments = *parsed_args,
  };
}

void accumulate_tool_call(std::vector<ToolCallAccumulator>& accumulators, const ava::types::StreamToolCall& delta) {
  auto* target = [&]() -> ToolCallAccumulator* {
    for(auto& accumulator : accumulators) {
      if(accumulator.index == delta.index) {
        return &accumulator;
      }
    }
    accumulators.push_back(ToolCallAccumulator{.index = delta.index});
    return &accumulators.back();
  }();

  if(delta.id.has_value()) {
    target->id = *delta.id;
  }
  if(delta.name.has_value()) {
    target->name = *delta.name;
  }
  if(delta.arguments_delta.has_value()) {
    merge_tool_arguments(target->arguments_json, *delta.arguments_delta);
  }
}

std::vector<ava::types::ToolCall> finalize_tool_calls(std::vector<ToolCallAccumulator> accumulators) {
  std::sort(accumulators.begin(), accumulators.end(), [](const auto& left, const auto& right) {
    return left.index < right.index;
  });

  std::vector<ava::types::ToolCall> calls;
  for(const auto& accumulator : accumulators) {
    if(const auto call = accumulator.to_tool_call(); call.has_value()) {
      calls.push_back(*call);
    }
  }
  return calls;
}

std::vector<ava::types::ToolCall> parse_tool_calls_from_content(const std::string& content) {
  const auto payload = parse_json(content);
  if(!payload.has_value() || !payload->is_object()) {
    return {};
  }

  if(payload->contains("tool_calls")) {
    return parse_tool_call_array(payload->at("tool_calls"));
  }

  if(payload->contains("tool_call")) {
    nlohmann::json singleton = nlohmann::json::array();
    singleton.push_back(payload->at("tool_call"));
    return parse_tool_call_array(singleton);
  }

  return {};
}

std::vector<ava::types::ToolCall> coalesce_tool_calls(const ava::llm::LlmResponse& response) {
  if(!response.tool_calls.empty()) {
    return response.tool_calls;
  }
  return parse_tool_calls_from_content(response.content);
}

}  // namespace ava::agent::response
