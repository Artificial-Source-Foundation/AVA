#pragma once

#include <filesystem>

namespace ava::core {

// Make a path absolute and lexically normalize it (collapse "." and ".." components)
// without resolving symlinks.
//
// This is the logical-path alternative to std::filesystem::weakly_canonical, which
// resolves symlinks and must not be used in AVA because the physical path must never
// leak into stored state, output, or forwarded arguments. If the path is already
// absolute, only lexical normalization is applied. If std::filesystem::absolute fails
// (e.g., the path is empty), the original path is lexically normalized and returned.
//
// Canonicalization (symlink resolution) is only permitted when the code needs to
// determine whether two logical paths refer to the same physical directory; in that
// case prefer inode comparison, or use std::filesystem::weakly_canonical at the narrow
// comparison site only — never store or forward the resolved form.
[[nodiscard]] std::filesystem::path normalized_absolute_path(std::filesystem::path const& path);

}  // namespace ava::core
