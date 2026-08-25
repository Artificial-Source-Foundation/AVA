#pragma once

#include "ava/permissions/permission_rules.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "debug.h"

namespace ava::permissions::permission_rules_internal {

class ScopedFd final
{
 public:
  ScopedFd();
  explicit ScopedFd(int fd);
  ScopedFd(ScopedFd const&) = delete;
  ScopedFd& operator=(ScopedFd const&) = delete;
  ScopedFd(ScopedFd&& other) noexcept;
  ScopedFd& operator=(ScopedFd&& other) noexcept;
  ~ScopedFd();

  [[nodiscard]] int get() const noexcept;

  AVA_DEBUG_PRINT_MEMBERS_ON

 private:
  void close_if_open() noexcept;

  int fd_ = -1;
};

struct RuleDirectory
{
  ScopedFd fd;
  std::filesystem::path path;
  std::string file_name;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct RuleLock
{
  RuleDirectory directory;
  ScopedFd fd;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

std::string errno_message();
ava::core::Error rule_file_error(ava::core::ErrorCategory category, std::string message, std::filesystem::path const& path);
ava::core::Error rule_parse_error(std::string message, std::filesystem::path const& path, std::string_view field = {},
                                  std::optional<std::size_t> index = std::nullopt);
std::filesystem::path normalized_path(std::filesystem::path const& path);
ava::core::Result<bool> paths_refer_to_same_file(std::filesystem::path const& a, std::filesystem::path const& b);
bool contains_parent_reference(std::filesystem::path const& path);
std::filesystem::path rules_file_path(PermissionRuleStore const& store, PermissionRuleScope scope);
ava::core::Result<std::optional<RuleDirectory>> open_rule_directory_for_read(std::filesystem::path const& file_path,
                                                                             std::shared_ptr<ava::core::AnchorSet> const& anchors);
ava::core::VoidResult validate_unsafe_replace_target(RuleDirectory const& directory);
ava::core::Result<RuleLock> acquire_rule_lock(std::filesystem::path const& file_path, std::shared_ptr<ava::core::AnchorSet> const& anchors);

bool valid_identifier(std::string_view value);
ava::core::VoidResult validate_rule(PersistentPermissionRule const& rule, std::filesystem::path const& path,
                                    std::optional<std::size_t> rule_index = std::nullopt);
ava::core::Result<std::vector<PersistentPermissionRule>> parse_rules_file(std::string_view content, std::filesystem::path const& path,
                                                                          PermissionRuleScope file_scope);
std::string rules_file_json(std::vector<PersistentPermissionRule> const& rules);

ava::core::Result<PersistentPermissionRule> rule_from_draft(PermissionRuleStore const& store, PermissionRuleDraft draft);
bool contains_legacy_command_allow(std::vector<PersistentPermissionRule> const& rules);

}  // namespace ava::permissions::permission_rules_internal
