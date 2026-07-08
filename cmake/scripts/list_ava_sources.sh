#! /bin/bash
# list_ava_sources.sh -- enumerate the ava library source files.
#
# Prints, one per line and relative to the repository root, every source file
# under src/ava/ that belongs to the ava library: headers (.h) and
# implementations (.cpp, .cxx). The output is sorted (C locale) and is meant to
# be consumed by build-time tools, for example piped into ctags as the list of
# input files.
#
# The script auto-detects the repository root from its own location
# (cmake/scripts/list_ava_sources.sh -> two directories up), so it can be
# invoked from any working directory. The printed paths are relative to that
# root (for example "src/ava/core/Result.h"); consumers should resolve them
# against the repository root, for instance by running ctags with the
# repository root as its working directory.
set -euo pipefail

# Resolve the repository root from the location of this script.
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd -- "$script_dir/../.." && pwd)

src_root="$repo_root/src/ava"
if [[ ! -d "$src_root" ]]; then
  echo "list_ava_sources.sh: expected source tree at $src_root (not found)" >&2
  exit 1
fi

# Run find from the repository root so the printed paths are repo-root-relative.
cd "$repo_root"

find src/ava -type f \( -name '*.h' -o -name '*.cpp' -o -name '*.cxx' \) \
  -not -path '*/.git/*' \
  -print \
  | LC_ALL=C sort
