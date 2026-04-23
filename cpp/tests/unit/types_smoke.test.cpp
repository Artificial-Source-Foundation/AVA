#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <string>

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
  REQUIRE(cleaned == "Please inspect and plus logic");
}
