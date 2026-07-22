#pragma once

#include "ava/command/command.h"

#include <cstdint>

namespace ava::command::detail {

class EnvironmentFactory final
{
 public:
  // The private passkey makes this internal construction API uncallable by
  // consumers even if they can physically include this detail header.
  [[nodiscard]] static ava::core::Result<CommandEnvironment> make(CommandEnvironmentOptions const& options, std::vector<CommandPathEntry> const& path_entries,
                                                                  std::filesystem::path const& logical_cwd, PathMetadata const& trusted_home,
                                                                  SyntheticEnvironmentRoots roots, std::optional<PathMetadata> rustup_home_metadata,
                                                                  CommandLimits const& limits, std::shared_ptr<ava::core::AnchorSet const> const& anchor_set,
                                                                  CommandEnvironment::FactoryPasskey passkey);

  AVA_DEBUG_PRINT_MEMBERS_ON

 private:
  EnvironmentFactory() = delete;
};

class Sha256Builder final
{
 public:
  void append_bytes(std::string_view value);
  void append_field(std::string_view value);
  void append_number(std::uintmax_t value);
  [[nodiscard]] std::string hex() const;

  AVA_DEBUG_PRINT_MEMBERS_ON

 private:
  std::vector<std::uint8_t> bytes_;
};

[[nodiscard]] ava::core::VoidResult validate_environment_matches_plan(CommandEnvironment const& environment, CommandPlan const& plan);

}  // namespace ava::command::detail
