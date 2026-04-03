# Install AVA

AVA currently supports four practical install paths:

1. quick CLI install from prebuilt binaries on Linux/macOS
2. desktop app download from GitHub Releases
3. build from source
4. contributor/developer setup

## 1. Quick CLI Install

This is the fastest path if you want the terminal app.

### Linux or macOS

Copy and paste:

```sh
curl -fsSL https://raw.githubusercontent.com/ASF-GROUP/AVA/master/install.sh | sh
```

What this does:

- downloads the latest released `ava` CLI binary
- installs it to `~/.ava/bin/ava`
- adds `~/.ava/bin` to your shell `PATH` when possible
- verifies the release checksum before installing

If you are installing from a private AVA fork, use the authenticated `gh` CLI so the installer can fetch release metadata and assets without exposing a token in process arguments.

Then restart your shell and run:

```sh
ava --version
ava --connect openrouter
ava auth test openrouter
```

Supported by the installer today:

- Linux `x86_64`
- Linux `aarch64`
- macOS `x86_64`
- macOS `arm64`

### Windows

There is no one-line PowerShell installer in the repo yet.

For Windows today, use one of these paths:

1. download the desktop `.msi` or `.exe` from GitHub Releases
2. build the CLI from source with Rust

For updates on Windows, download the latest installer again from Releases rather than relying on `ava update`.

## 2. Desktop App Downloads

If you want the desktop app, download a release from:

`https://github.com/ASF-GROUP/AVA/releases/latest`

Current release artifacts documented in the repo:

- Linux: `.deb`, `.rpm`, `.AppImage`
- macOS: `.dmg`, `.app`
- Windows: `.msi`, `.exe`

Recommended choices:

- Ubuntu/Debian/Pop!_OS: `.deb`
- Fedora/RHEL/Nobara: `.rpm`
- Other Linux distros: `.AppImage`
- macOS: `.dmg`
- Windows: `.msi` for normal install, `.exe` if that is what the release provides for your flow

After installing the desktop app, launch AVA and connect a provider from the onboarding flow. If you skip onboarding, you can still use `ava --connect openrouter` in a terminal or add credentials under `~/.ava/credentials.json`.

## 3. Build From Source

Use this if you want the latest code, unsupported platforms, or local modifications.

### CLI only

```bash
git clone https://github.com/ASF-GROUP/AVA.git
cd AVA
cargo install --path crates/ava-tui
```

Or build without installing globally:

```bash
git clone https://github.com/ASF-GROUP/AVA.git
cd AVA
cargo build --release --bin ava
./target/release/ava --version
```

### Interactive source installer

The repo also includes a guided source installer:

```bash
git clone https://github.com/ASF-GROUP/AVA.git
cd AVA
./install-from-source.sh
```

It can install:

- CLI/TUI only
- desktop app only
- both

## 4. Developer Setup

Use this if you plan to work on AVA itself.

### Rust CLI/TUI development

```bash
git clone https://github.com/ASF-GROUP/AVA.git
cd AVA
cargo test --workspace
just run
```

### Desktop development

Requirements:

- Rust toolchain
- Node.js
- `pnpm`
- Tauri system dependencies for your platform

Install frontend dependencies and start the desktop app:

```bash
git clone https://github.com/ASF-GROUP/AVA.git
cd AVA
pnpm install
pnpm tauri dev
```

Useful checks:

```bash
just check
pnpm lint
pnpm typecheck
```

Linux contributors should also read:

- [docs/troubleshooting/tauri-toolchain-checklist.md](troubleshooting/tauri-toolchain-checklist.md)
- [docs/troubleshooting/webkitgtk-rendering.md](troubleshooting/webkitgtk-rendering.md)

## First Run

After installing AVA, connect a provider:

```bash
ava --connect openrouter
ava auth list
ava auth test openrouter
```

You can also use the explicit auth subcommand:

```bash
ava auth login openrouter
```

Common provider IDs:

- `openrouter`
- `openai`
- `anthropic`
- `google`
- `copilot`
- `ollama`

Notes:

- `ava --connect <provider>` and `ava auth login <provider>` both launch the same provider-auth flow.
- In the TUI, you can also run `ava` and use `/connect`.
- `ava auth list` shows which providers are configured.
- `ava auth test <provider>` verifies that the configured provider actually works.

Then try:

```bash
ava
ava "fix the login bug"
ava serve
```

## Current Gaps

These install paths are not fully productized yet:

- no `winget` package
- no Homebrew formula documented here
- no PowerShell one-liner installer in the repo
- desktop install is download-based rather than package-manager-based
