# Troubleshooting

## Tauri Toolchain

**Prerequisites:**
- gcc, rustc, Tauri system dependencies
- Linux: `libwebkit2gtk-4.1-dev libgtk-3-dev libayatana-appindicator3-dev librsvg2-dev patchelf`

**Linker not found** (Pop OS 24.04):
```
error: linker 'cc' not found
```
Fix: Project uses `linker = "gcc"` in `src-tauri/.cargo/config.toml`. Override to `"gcc-14"` if needed.

**Validate setup:** `npm run verify:mvp && npm run tauri dev`

## WebKitGTK Rendering

**DMABUF ghost rendering** (Cosmic DE, Hyprland, Sway + NVIDIA):
- SVG icons appear doubled/ghosted
- Fix: `WEBKIT_DISABLE_DMABUF_RENDERER=1` set in `src-tauri/src/main.rs` before WebKitGTK init

**Nested button crash** (all WebKitGTK versions):
- Crashes with `null is not an object` on nested `<button>` elements
- Fix: Replace outer `<button>` with `<div role="button" tabIndex={0}>` + keyboard handler

**CSS workarounds for WebKitGTK:**
- Use `width: 0 + overflow: hidden` for animations (not `display: none`)
- Use `transform: translateZ(0)` for scroll smoothness
- Avoid `transition-all` and `hover:-translate-y`

## Common Build Issues

**Core tsbuildinfo stale:** If types seem wrong after changes, delete and rebuild:
```bash
rm packages/core/tsconfig.tsbuildinfo
pnpm --filter @ava/core build
```

**Biome unstaged conflict:** If biome-check fails during commit:
```bash
git stash --keep-index   # Stash unstaged changes
git commit               # Commit staged changes
git stash pop            # Restore unstaged changes
```

**`find` aliased to `fd`:** This system aliases `find` to `fd`. Use `/usr/bin/find` for GNU find behavior.

## Test Issues

**ChatView.integration.test.tsx error:** Known issue — missing `logError` mock in GitControlStrip. Pre-existing, does not affect other tests.

**Extensions tests not running from root:** Extensions tests are included in root vitest.config.ts. If they don't run, check that `packages/extensions/**/*.test.ts` is in the include patterns.
