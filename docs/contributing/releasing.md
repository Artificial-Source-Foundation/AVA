---
title: "Releasing"
description: "Prerequisites, signing, build, and GitHub release steps for AVA Desktop."
order: 3
updated: "2026-04-19"
---

# Releasing AVA Desktop

## Scope and split with CLI releases

This page is specifically for the desktop/Tauri release flow.

CLI release artifacts are published through the automated `cargo-dist` GitHub workflow:

1. Workflow: [`../../.github/workflows/release.yml`](../../.github/workflows/release.yml)
2. Dist targets/installers: [`../../dist-workspace.toml`](../../dist-workspace.toml)
3. Public install entrypoints: [`../../install.sh`](../../install.sh), [How-to install](../how-to/install.md)

Keep this doc focused on manual desktop signing/build/release steps unless and until desktop publishing is fully automated in CI.

Operational note: release-related org/repo references in this checkout are aligned to `Artificial-Source/AVA`.

## Prerequisites

- Tauri CLI (`npm install -g @tauri-apps/cli`)
- Signing key at `~/.tauri/ava.key` (generated once, see below)
- GitHub CLI (`gh`) for publishing releases

## Signing Key

The signing key was generated with:

```bash
mkdir -p ~/.tauri && npx tauri signer generate -w ~/.tauri/ava.key --ci
```

This produced:
- **Private key**: `~/.tauri/ava.key` (never commit this)
- **Public key**: `~/.tauri/ava.key.pub` (embedded in `src-tauri/tauri.conf.json`)

If you lose the private key, you must generate a new keypair and update the pubkey in `tauri.conf.json`. Existing installs will NOT be able to auto-update across key changes.

## Build a Release

```bash
# 1) Ensure everything passes
cargo fmt --all --check
cargo clippy --workspace -- -D warnings
cargo nextest run -p ava-agent --test agent_loop --test stack_test --test e2e_test --test reflection_loop -j 4
cargo nextest run -p ava-tools -p ava-review -j 4
pnpm typecheck
pnpm lint
pnpm exec tsc --noEmit

# 2) Build signed release bundle
TAURI_SIGNING_PRIVATE_KEY=$(cat ~/.tauri/ava.key) npm run tauri build
```

Build outputs are in `src-tauri/target/release/bundle/`:
- Linux: `.deb`, `.AppImage`, `.rpm`
- macOS: `.dmg`, `.app`
- Windows: `.msi`, `.exe`

Tauri also generates a `latest.json` manifest for the updater.

## Publish to GitHub Releases

```bash
# Tag the release
VERSION=$(grep '"version"' src-tauri/tauri.conf.json | head -1 | grep -oP '[\d.]+')
git tag -a "v${VERSION}" -m "Release v${VERSION}"
git push origin "v${VERSION}"

# Collect every desktop artifact that was actually built on this machine
ARTIFACTS=()
for pattern in \
  "src-tauri/target/release/bundle/deb/*.deb" \
  "src-tauri/target/release/bundle/rpm/*.rpm" \
  "src-tauri/target/release/bundle/appimage/*.AppImage" \
  "src-tauri/target/release/bundle/appimage/*.AppImage.tar.gz" \
  "src-tauri/target/release/bundle/appimage/*.AppImage.tar.gz.sig" \
  "src-tauri/target/release/bundle/dmg/*.dmg" \
  "src-tauri/target/release/bundle/macos/*.app.tar.gz" \
  "src-tauri/target/release/bundle/msi/*.msi" \
  "src-tauri/target/release/bundle/nsis/*.exe"
do
  for file in $pattern; do
    [ -e "$file" ] && ARTIFACTS+=("$file")
  done
done

# Create the GitHub release with every collected desktop artifact
gh release create "v${VERSION}" \
  --title "AVA v${VERSION}" \
  --generate-notes \
  "${ARTIFACTS[@]}"

# Upload updater manifests and signatures that may be emitted separately
find src-tauri/target/release/bundle \( -name "latest.json" -o -name "*.sig" \) -type f -print0 | \
  while IFS= read -r -d '' file; do
    gh release upload "v${VERSION}" "$file" --clobber
  done
```

### Cross-platform release checklist

Before publishing, confirm which desktop bundles were actually produced on the machine or CI runner that built the release:

1. Linux package artifacts: `.deb`, `.rpm`, `.AppImage`
2. Linux updater payloads where present: `.AppImage.tar.gz`, `.AppImage.tar.gz.sig`
3. macOS desktop bundle/archive: `.dmg`, plus any `.app` archive emitted for updater use
4. Windows installers: `.msi`, `.exe`
5. Updater metadata: `latest.json` and any matching signatures

If you are building on only one OS, do not assume you produced every platform artifact. The release should only claim the bundles you actually built and uploaded.

## How Auto-Update Works

1. App checks the endpoint configured in `src-tauri/tauri.conf.json` on launch. In this checkout that is `https://github.com/Artificial-Source/AVA/releases/latest/download/latest.json`.
2. If a newer version exists, the updater plugin prompts the user
3. The update bundle is downloaded, signature verified against the pubkey in `tauri.conf.json`
4. App restarts with the new version

The update endpoint is configured in `src-tauri/tauri.conf.json` under `plugins.updater.endpoints`.

## Version Bumping

Update the version in `src-tauri/tauri.conf.json` before building:

```json
{
  "version": "3.3.0"
}
```

Also update `Cargo.toml` workspace version if it should match.

## CI/CD (Future For Desktop)

CLI release artifacts are already automated by the `cargo-dist` workflow in `.github/workflows/release.yml` when version tags are pushed.

Desktop publishing is still a separate manual flow in this repo. For automated desktop releases, set these GitHub Actions secrets:
- `TAURI_SIGNING_PRIVATE_KEY`: contents of `~/.tauri/ava.key`
- `TAURI_SIGNING_PRIVATE_KEY_PASSWORD`: empty (no password was set)

Then use the [tauri-action](https://github.com/tauri-apps/tauri-action) GitHub Action to build + publish on tag push.

## Current Gaps Before Desktop Downloads Are Consistent Every Release

If the goal is "users can expect desktop downloads on every release", the repo still has a few concrete gaps:

1. Desktop release publishing is not yet wired into the tag-driven automated release workflow in `.github/workflows/release.yml`
2. Cross-platform desktop bundles still require coordinated builds across the relevant operating systems; one local machine will not reliably produce the full desktop matrix
3. The repo does not yet document or automate a guaranteed cross-platform build matrix for desktop bundles on each release tag
4. Automated desktop signing/secrets are still documented as future setup, not current CI behavior
5. The public user docs therefore need to treat GitHub Releases as availability-based for desktop, not guaranteed per version

Until those gaps are closed, the release page should be treated as the source of truth for which desktop artifacts exist for a given version.
