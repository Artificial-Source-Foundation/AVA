#pragma once

#include <filesystem>

namespace ava::config {

[[nodiscard]] bool is_project_trusted(const std::filesystem::path& project_root);
void trust_project(const std::filesystem::path& project_root);

// Test helper to avoid cross-test process cache leakage.
void clear_trust_cache_for_tests();

}  // namespace ava::config
