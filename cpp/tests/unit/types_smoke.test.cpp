#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

#include "ava/types/types.hpp"

TEST_CASE("ava_types exposes build metadata", "[ava_types]") {
  const auto build = ava::types::current_build_info();
  REQUIRE_FALSE(build.name.empty());
  REQUIRE_FALSE(build.version.empty());
}

TEST_CASE("tool schemas serialize round-trip", "[ava_types]") {
  const ava::types::Tool tool{
      .name = "read_file",
      .description = "Read file contents",
      .parameters = nlohmann::json{{"type", "object"}},
  };

  const auto json = nlohmann::json(tool);
  const auto parsed = json.get<ava::types::Tool>();

  REQUIRE(parsed.name == "read_file");
  REQUIRE(parsed.description == "Read file contents");
  REQUIRE(parsed.parameters.at("type") == "object");
}

TEST_CASE("thinking level helpers support cycle and loose parsing", "[ava_types]") {
  REQUIRE(ava::types::cycle_thinking_level(ava::types::ThinkingLevel::Off) == ava::types::ThinkingLevel::Low);
  REQUIRE(ava::types::cycle_thinking_level(ava::types::ThinkingLevel::Max) == ava::types::ThinkingLevel::Off);
  REQUIRE(ava::types::cycle_thinking_level_binary(ava::types::ThinkingLevel::Off) == ava::types::ThinkingLevel::High);
  REQUIRE(ava::types::cycle_thinking_level_binary(ava::types::ThinkingLevel::Medium) == ava::types::ThinkingLevel::Off);

  REQUIRE(ava::types::thinking_level_from_string_loose("minimal") == ava::types::ThinkingLevel::Low);
  REQUIRE(ava::types::thinking_level_from_string_loose("XHIGH") == ava::types::ThinkingLevel::Max);
  REQUIRE_FALSE(ava::types::thinking_level_from_string_loose("unknown").has_value());
}

TEST_CASE("mention parsing extracts attachments and strips mention tokens", "[ava_types]") {
  const auto [attachments, cleaned] = ava::types::parse_mentions(
      "Please inspect @file:src/main.rs and @folder:docs/ plus @codebase:retry logic"
  );

  REQUIRE(attachments.size() == 3);
  REQUIRE(attachments[0].kind == ava::types::ContextAttachmentKind::File);
  REQUIRE(attachments[0].path.string() == "src/main.rs");
  REQUIRE(attachments[1].kind == ava::types::ContextAttachmentKind::Folder);
  REQUIRE(attachments[1].label().find("docs/") != std::string::npos);
  REQUIRE(attachments[2].kind == ava::types::ContextAttachmentKind::CodebaseQuery);
  REQUIRE(attachments[2].query == "retry");
  REQUIRE(cleaned == "Please inspect  and  plus  logic");
}

TEST_CASE("mention parsing preserves non-mention whitespace exactly", "[ava_types]") {
  const std::string input = "  keep\t@file:src/main.rs\nspacing  and @unknown-token  too";
  const auto [attachments, cleaned] = ava::types::parse_mentions(input);

  REQUIRE(attachments.size() == 1);
  REQUIRE(attachments.front().path.string() == "src/main.rs");
  REQUIRE(cleaned == "  keep\t\nspacing  and @unknown-token  too");
}

TEST_CASE("message dto round-trips rust-aligned fields", "[ava_types]") {
  const ava::types::Message message{
      .id = "msg-1",
      .role = ava::types::Role::Assistant,
      .content = "response",
      .timestamp = "2026-04-23T00:00:00Z",
      .tool_calls = {ava::types::ToolCall{.id = "call-1", .name = "read", .arguments = nlohmann::json{{"path", "a"}}}},
      .tool_results = {ava::types::ToolResult{.call_id = "call-1", .content = "ok", .is_error = false}},
      .tool_call_id = "call-1",
      .images = {ava::types::ImageContent{.data = "abc", .media_type = "image/png"}},
      .parent_id = "parent-1",
      .agent_visible = false,
      .user_visible = true,
      .original_content = "original",
      .structured_content = nlohmann::json::array({nlohmann::json{{"type", "text"}, {"text", "x"}}}),
      .metadata = nlohmann::json{{"k", "v"}},
  };

  const auto json = nlohmann::json(message);
  const auto parsed = json.get<ava::types::Message>();

  REQUIRE(parsed.id == "msg-1");
  REQUIRE(parsed.role == ava::types::Role::Assistant);
  REQUIRE(parsed.tool_calls.size() == 1);
  REQUIRE(parsed.tool_results.size() == 1);
  REQUIRE(parsed.images.size() == 1);
  REQUIRE(parsed.parent_id == std::optional<std::string>{"parent-1"});
  REQUIRE_FALSE(parsed.agent_visible);
  REQUIRE(parsed.structured_content.is_array());
  REQUIRE(parsed.metadata["k"] == "v");
}

TEST_CASE("conversation repair and interrupted tool cleanup work", "[ava_types]") {
  std::vector<ava::types::Message> messages{
      ava::types::Message{.id = "u1", .role = ava::types::Role::User, .content = "first", .timestamp = "t1"},
      ava::types::Message{.id = "u2", .role = ava::types::Role::User, .content = "second", .timestamp = "t2"},
      ava::types::Message{
          .id = "a1",
          .role = ava::types::Role::Assistant,
          .content = "",
          .timestamp = "t3",
          .tool_calls = {ava::types::ToolCall{.id = "call-2", .name = "write", .arguments = nlohmann::json::object()}},
      },
  };

  ava::types::cleanup_interrupted_tools(messages);
  REQUIRE(messages.back().role == ava::types::Role::Tool);
  REQUIRE(messages.back().tool_call_id == std::optional<std::string>{"call-2"});
  REQUIRE(messages.back().tool_results.size() == 1);
  REQUIRE(messages.back().tool_results.front().is_error);

  ava::types::repair_conversation(messages);
  REQUIRE(messages.front().role == ava::types::Role::User);
  REQUIRE(messages.front().content == "first\n\nsecond");
}

TEST_CASE("conversation repair preserves distinct same-content tool calls", "[ava_types]") {
  std::vector<ava::types::Message> messages{
      ava::types::Message{
          .id = "a1",
          .role = ava::types::Role::Assistant,
          .content = "working",
          .timestamp = "t1",
          .tool_calls = {ava::types::ToolCall{.id = "call-a", .name = "read", .arguments = nlohmann::json{{"path", "a"}}}},
      },
      ava::types::Message{
          .id = "a2",
          .role = ava::types::Role::Assistant,
          .content = "working",
          .timestamp = "t2",
          .tool_calls = {ava::types::ToolCall{.id = "call-b", .name = "write", .arguments = nlohmann::json{{"path", "b"}}}},
      },
  };

  ava::types::repair_conversation(messages);

  REQUIRE(messages.size() == 2);
  REQUIRE(messages[0].tool_calls.front().id == "call-a");
  REQUIRE(messages[1].tool_calls.front().id == "call-b");
}

TEST_CASE("conversation repair removes invalid messages and is idempotent", "[ava_types]") {
  std::vector<ava::types::Message> messages{
      ava::types::Message{.id = "u1", .role = ava::types::Role::User, .content = "goal", .timestamp = "t1"},
      ava::types::Message{.id = "empty", .role = ava::types::Role::Assistant, .content = " \t", .timestamp = "t2"},
      ava::types::Message{.id = "orphan", .role = ava::types::Role::Tool, .content = "orphan", .timestamp = "t3", .tool_call_id = "missing"},
      ava::types::Message{.id = "done", .role = ava::types::Role::Assistant, .content = "done", .timestamp = "t4"},
      ava::types::Message{.id = "tail", .role = ava::types::Role::Tool, .content = "tail", .timestamp = "t5", .tool_call_id = "tail-call"},
  };

  ava::types::repair_conversation(messages);
  const auto once = nlohmann::json(messages);
  ava::types::repair_conversation(messages);

  REQUIRE(messages.size() == 2);
  REQUIRE(messages[0].id == "u1");
  REQUIRE(messages[1].id == "done");
  REQUIRE(nlohmann::json(messages) == once);
}
