#! /bin/bash
# generate_ctags_json.sh -- produce the JSON tags file used for debug printer
# generation (goal 03).
#
# Runs list_ava_sources.sh (goal 02) to enumerate the ava library source files
# and pipes that list into ctags in JSON output mode, writing the result to the
# output path passed as $1. The ctags executable to use is passed as $2; it was
# located and JSON-capability-checked by CMake at configure time, so this script
# assumes a JSON-capable Universal Ctags.
#
# This is the implementation behind the manual `ctags-json` pseudo-target: it is
# not run on a normal build. ctags runs with the repository root as its working
# directory, so the recorded paths are repo-root-relative.
#
# Usage: generate_ctags_json.sh <output-json-path> <ctags-executable>
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: $0 <output-json-path> <ctags-executable>" >&2
  exit 2
fi

out="$1"
ctags_exe="$2"

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd -- "$script_dir/../.." && pwd)

# Create the output directory before ctags tries to write into it.
# Create the output directory before ctags tries to write into it.
mkdir -p "$(dirname -- "$out")"

# ctags refuses to overwrite a file that does not look like a tags file; a
# previously generated JSON tags.json is not a Vi tag file, so remove any stale
# output first to let ctags write it fresh.
rm -f "$out"

# Run from the repository root so ctags records repo-root-relative paths.
cd "$repo_root"

# -L -   read the list of input files from stdin (the script's output).
# -f out write the JSON tags to the requested path.
# -D ... expand the opt-in macro to a print_members definition so that the
#        generator can tell which classes/structs opted in straight from the
#        tags file (a type declares print_members iff it has the macro).
"$script_dir"/list_ava_sources.sh | "$ctags_exe" \
  --output-format=json \
  --language-force=C++ \
  --fields=+KinSz \
  --kinds-C++=+p \
  -D 'AVA_DEBUG_PRINT_MEMBERS_ON=void print_members_opt_in() { }' \
  -L - -f "$out"
