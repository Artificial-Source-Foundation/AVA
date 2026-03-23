#!/usr/bin/env bash
#
# Bump AVA version across all manifest files.
# Usage: ./scripts/bump-version.sh 2.2.0
#
set -euo pipefail

if [ $# -ne 1 ]; then
  echo "Usage: $0 <version>"
  echo "Example: $0 2.2.0"
  exit 1
fi

NEW_VERSION="$1"

# Validate version format (semver without leading v)
if ! echo "$NEW_VERSION" | grep -qE '^[0-9]+\.[0-9]+\.[0-9]+(-[a-zA-Z0-9.]+)?$'; then
  echo "Error: Version must be semver format (e.g. 2.2.0 or 2.2.0-beta.1)"
  exit 1
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

echo "Bumping AVA to v${NEW_VERSION}"
echo ""

# 1. Root Cargo.toml — workspace.package.version
echo "  Cargo.toml (workspace) ..."
sed -i "s/^version = \"[^\"]*\"/version = \"${NEW_VERSION}\"/" "$ROOT/Cargo.toml"

# 2. src-tauri/tauri.conf.json — "version" field
echo "  src-tauri/tauri.conf.json ..."
sed -i "s/\"version\": \"[^\"]*\"/\"version\": \"${NEW_VERSION}\"/" "$ROOT/src-tauri/tauri.conf.json"

# 3. src-tauri/Cargo.toml — package.version
echo "  src-tauri/Cargo.toml ..."
sed -i "s/^version = \"[^\"]*\"/version = \"${NEW_VERSION}\"/" "$ROOT/src-tauri/Cargo.toml"

# 4. package.json — "version" field
echo "  package.json ..."
sed -i "s/\"version\": \"[^\"]*\"/\"version\": \"${NEW_VERSION}\"/" "$ROOT/package.json"

echo ""
echo "Done. Updated 4 files to v${NEW_VERSION}."
echo ""
echo "Files changed:"
echo "  - Cargo.toml"
echo "  - src-tauri/tauri.conf.json"
echo "  - src-tauri/Cargo.toml"
echo "  - package.json"
echo ""
echo "Next steps:"
echo "  git add -p && git commit -m 'chore: bump version to v${NEW_VERSION}'"
echo "  git tag v${NEW_VERSION}"
echo "  git push origin main --tags"
