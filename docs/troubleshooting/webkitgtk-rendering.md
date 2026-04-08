---
title: "WebKitGTK Rendering Issues"
description: "Known Linux WebKitGTK rendering failures and workarounds for AVA's Tauri webview."
order: 3
updated: "2026-04-08"
---

# WebKitGTK Rendering Issues (Tauri/Linux)

Tauri uses WebKitGTK as its webview on Linux. WebKitGTK has known rendering bugs on certain Wayland compositors and GPU driver combinations.

---

## DMABUF Renderer Ghost Rendering

**Affected**: Cosmic DE, Hyprland, Sway + NVIDIA proprietary drivers
**Not affected**: GNOME (Mutter handles DMABUF correctly)

### Symptoms

- All SVG icons appear doubled/ghosted with a shadow copy slightly offset
- `MESA-LOADER: failed to open nvidia-drm` errors in console
- Every DOM element may have a faint ghost duplicate on top of it

### Root Cause

WebKitGTK's DMABUF renderer uses GBM (Generic Buffer Manager) for GPU buffer sharing. On non-GNOME Wayland compositors with NVIDIA, the GBM driver (`nvidia-drm_gbm.so`) fails to load. The renderer falls back in a broken way, producing ghost copies of DOM elements.

This is an upstream WebKitGTK bug introduced in version 2.46.6.

### Fix

Set `WEBKIT_DISABLE_DMABUF_RENDERER=1` before WebKitGTK initializes.

In `src-tauri/src/main.rs`:

```rust
#[cfg(target_os = "linux")]
if std::env::var("WEBKIT_DISABLE_DMABUF_RENDERER").is_err() {
    std::env::set_var("WEBKIT_DISABLE_DMABUF_RENDERER", "1");
}
```

This only applies on Linux and respects the user's own setting if already defined.

### References

- https://github.com/tauri-apps/tauri/issues/13157
- https://github.com/tauri-apps/tauri/issues/9394
- https://github.com/pop-os/cosmic-epoch/issues/510

---

## Nested Button Crash

**Affected**: All WebKitGTK versions

### Symptoms

- Settings page crashes with `null is not an object evaluating '_el$.firstChild'`
- Tauri logs `malformed HTML` warning
- Any page with a `<button>` inside another `<button>` may crash

### Root Cause

HTML spec forbids nested `<button>` elements. Chromium silently fixes this, but WebKitGTK re-parses and reparents the DOM, breaking SolidJS's internal references.

### Fix

Replace the outer `<button>` with `<div role="button" tabIndex={0}>` and add a keyboard handler for Enter/Space:

```tsx
{/* biome-ignore lint/a11y/useSemanticElements: div+role=button avoids nested button which crashes WebKitGTK */}
<div
  role="button"
  tabIndex={0}
  onClick={handleClick}
  onKeyDown={(e) => (e.key === 'Enter' || e.key === ' ') && handleClick()}
>
  {/* Inner buttons are fine here */}
  <button onClick={handleEdit}>Edit</button>
</div>
```

### Files Fixed (Session 38)

- `src/components/settings/tabs/ProvidersTab.tsx`
- `src/components/sessions/SessionListItem.tsx`
- `src/components/panels/TerminalPanel.tsx`

---

## Cargo Linker Not Found (Pop OS 24.04)

### Symptoms

```
error: linker `cc` not found
```

Rust compilation fails at the linking step.

### Root Cause

Pop OS 24.04 ships `gcc-14` but does not create a `cc` symlink. Cargo defaults to `cc` as its linker.

### Fix

Create `src-tauri/.cargo/config.toml`:

```toml
[target.x86_64-unknown-linux-gnu]
linker = "gcc-14"
```

---

## CSS/Rendering Tips

| Issue | Workaround |
|-------|-----------|
| `pointer-events: none` on fixed pseudo-elements ignored | Don't use overlay pseudo-elements |
| Sidebar slide animation jank | Use `width: 0` + `overflow: hidden`, not `margin-left: -Xpx` |
| Scroll containers flicker | Add `transform: translateZ(0)` for GPU compositing |
| `transition-all` causes jank | Use `transition-colors` or specific properties only |
| `hover:-translate-y` causes reflow | Use opacity/color changes instead |
