# Tauri Linux Toolchain Checklist

Use this checklist before running `npm run tauri dev` on Linux.

## 1) Verify compiler and linker

```bash
which gcc
gcc --version
which rustc
rustc -vV
```

Expected: both `gcc` and `rustc` are present.

## 2) Verify Tauri system deps (Ubuntu/Debian)

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential \
  pkg-config \
  libwebkit2gtk-4.1-dev \
  libgtk-3-dev \
  libayatana-appindicator3-dev \
  librsvg2-dev \
  patchelf
```

## 3) Optional: install gcc-14 (if your distro provides it)

Most setups work with plain `gcc`. Only install `gcc-14` if you explicitly need it:

```bash
sudo apt-get install -y gcc-14 g++-14
```

## 4) Project linker config

This project now uses portable defaults in `src-tauri/.cargo/config.toml`:

- `linker = "gcc"`
- `CC = "gcc"`

If you want to force GCC 14 locally, update those fields to `gcc-14`.

## 5) Smoke validation

```bash
npm run verify:mvp
npm run tauri dev
```

Success criteria:
- `verify:mvp` passes
- Tauri app compiles and opens a desktop window
