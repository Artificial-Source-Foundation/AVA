#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace ava::config {

enum class MigrationResultKind {
  NoFileFound,
  Migrated,
};

struct MigrationResult {
  MigrationResultKind kind{MigrationResultKind::NoFileFound};
  std::size_t count{0};
};

[[nodiscard]] std::string redact_key_for_log(std::string_view key);
[[nodiscard]] bool os_keychain_available();

}  // namespace ava::config
