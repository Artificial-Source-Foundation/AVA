#include <catch2/catch_test_macros.hpp>

#include "ava/control_plane/control_plane.hpp"

TEST_CASE("command inventory count matches frozen contract", "[ava_control_plane]") {
  const auto specs = ava::control_plane::canonical_command_specs();
  REQUIRE(specs.size() == 13);
}

TEST_CASE("command lookup by name exposes submit_goal contract", "[ava_control_plane]") {
  const auto* submit_goal = ava::control_plane::command_spec_by_name("submit_goal");
  REQUIRE(submit_goal != nullptr);
  REQUIRE(submit_goal->command == ava::control_plane::ControlPlaneCommand::SubmitGoal);
  REQUIRE(submit_goal->completion_mode == ava::control_plane::CompletionMode::AcceptedAndStreaming);
  REQUIRE(submit_goal->correlation_ids.accepted_response.size() == 1);
  REQUIRE(
      submit_goal->correlation_ids.accepted_response[0] == ava::control_plane::CorrelationIdKey::SessionId
  );
}

TEST_CASE("event inventory and required fields match canonical table", "[ava_control_plane]") {
  const auto specs = ava::control_plane::canonical_event_specs();
  REQUIRE(specs.size() == 9);

  const auto* approval = ava::control_plane::canonical_event_spec("approval_request");
  REQUIRE(approval != nullptr);
  REQUIRE(approval->required_fields.size() == 8);
  REQUIRE(approval->required_fields[0] == ava::control_plane::CanonicalEventField::RunId);
  REQUIRE(approval->required_fields[2] == ava::control_plane::CanonicalEventField::ToolCallId);
}

TEST_CASE("queue command and tier conversion are wired", "[ava_control_plane]") {
  const auto command = ava::control_plane::queue_command_from_tier(ava::types::MessageTier::post_complete(3));
  REQUIRE(command == ava::control_plane::ControlPlaneCommand::PostCompleteAgent);

  const auto tier = ava::control_plane::queue_message_tier(command, 7);
  REQUIRE(tier.has_value());
  REQUIRE(tier->kind == ava::types::MessageTierKind::PostComplete);
  REQUIRE(tier->post_complete_group == 7);
}
