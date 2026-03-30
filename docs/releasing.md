# Releasing AVA Desktop

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
# 1. Ensure everything passes
just check && npx tsc --noEmit

# 2. Build signed release bundle
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

# Create GitHub release and upload artifacts
gh release create "v${VERSION}" \
  --title "AVA v${VERSION}" \
  --generate-notes \
  src-tauri/target/release/bundle/deb/*.deb \
  src-tauri/target/release/bundle/appimage/*.AppImage \
  src-tauri/target/release/bundle/appimage/*.AppImage.tar.gz \
  src-tauri/target/release/bundle/appimage/*.AppImage.tar.gz.sig

# Upload the updater manifest (CRITICAL for auto-updates)
# Find and upload latest.json from the bundle output
find src-tauri/target/release/bundle -name "latest.json" -exec \
  gh release upload "v${VERSION}" {} \;
```

## How Auto-Update Works

1. App checks `https://github.com/ASF-GROUP/AVA/releases/latest/download/latest.json` on launch
2. If a newer version exists, the updater plugin prompts the user
3. The update bundle is downloaded, signature verified against the pubkey in `tauri.conf.json`
4. App restarts with the new version

The update endpoint is configured in `src-tauri/tauri.conf.json` under `plugins.updater.endpoints`.

## Version Bumping

Update the version in `src-tauri/tauri.conf.json` before building:

```json
{
  "version": "2.1.0"
}
```

Also update `Cargo.toml` workspace version if it should match.

## CI/CD (Future)

For automated releases, set these GitHub Actions secrets:
- `TAURI_SIGNING_PRIVATE_KEY`: contents of `~/.tauri/ava.key`
- `TAURI_SIGNING_PRIVATE_KEY_PASSWORD`: empty (no password was set)

Then use the [tauri-action](https://github.com/tauri-apps/tauri-action) GitHub Action to build + publish on tag push.
