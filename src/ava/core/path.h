#pragma once

#include <filesystem>

namespace ava::core {

// Return best effort logical path for "the current working directory"; which means
// it doesn't - it return the value of PWD if that is set. We're relying on the fact
// that AVA never changes its current working directory away from its launch path.
//
// This function should only be used by normalized_absolute_path below and tests.
// Normally you want to use AnchorSet::launch_workspace_root.
std::filesystem::path logical_cwd();

// Make a path absolute and lexically normalize it (collapse "." and ".." components)
// without resolving symlinks.
//
// Relative paths are resolved against the logical current directory, which is obtained
// from $PWD (the shell-maintained logical path) when it matches the physical directory
// (verified via inode comparison). This avoids std::filesystem::absolute, which calls
// getcwd() and would resolve symlinks. If the path is already absolute, only lexical
// normalization is applied.
//
// Canonicalization (symlink resolution) is only permitted when the code needs to
// determine whether two logical paths refer to the same physical directory; in that
// case prefer inode comparison, or use std::filesystem::weakly_canonical at the narrow
// comparison site only — never store or forward the resolved form.
[[nodiscard]] std::filesystem::path normalized_absolute_path(std::filesystem::path const& path);

}  // namespace ava::core
