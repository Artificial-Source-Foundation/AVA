<div align="center">

# AVA

**A practical AI coding agent for terminal, desktop, and web.**

[![Rust](https://img.shields.io/badge/Rust-100%25-dea584?style=flat-square&logo=rust)](https://www.rust-lang.org/)
[![License: MIT](https://img.shields.io/badge/license-MIT-yellow?style=flat-square)](LICENSE)

AVA reads code, edits files, runs commands, and helps you finish software work without leaving your repo.
</div>

AVA is a Rust-first coding agent built for real repository work.

- Use the `ava` CLI for terminal work: TUI by default, headless with `--headless`
- Use AVA Desktop for a native desktop shell on top of the same backend
- Use web mode with `ava serve` when you build AVA with the `web` feature

## Get Started

### Choose Your Path

| I want to... | Use this | Notes |
|---|---|---|
| Try AVA quickly in the terminal on Linux/macOS | `curl -fsSL https://raw.githubusercontent.com/Artificial-Source/AVA/develop/install.sh | sh` | Prebuilt CLI binary, no Rust toolchain required |
| Try AVA quickly in the terminal on Windows | [GitHub Releases](https://github.com/Artificial-Source/AVA/releases) | Download the Windows CLI asset |
| Build the CLI from source | `cargo build --release --bin ava` | Best loop for contributors and power users |
| Install the CLI from source | `cargo install --path crates/ava-tui --bin ava` | Installs `ava` onto your `PATH` |
| Use the desktop app | [GitHub Releases](https://github.com/Artificial-Source/AVA/releases) or [desktop guide](docs/how-to/download-desktop.md) | Desktop bundles are not published on every release |

### Quick Start

Install the `ava` CLI on Linux/macOS:

```bash
curl -fsSL https://raw.githubusercontent.com/Artificial-Source/AVA/develop/install.sh | sh
```

On Windows, use <https://github.com/Artificial-Source/AVA/releases> and download the CLI asset for your platform.

Requires:

1. Linux or macOS plus `curl` and `tar` for the one-line installer
2. GitHub Releases download for Windows

Add credentials:

```bash
ava auth login openrouter
```

Run AVA:

```bash
ava
ava "fix the login bug" --headless
ava --cwd /path/to/project "fix the login bug" --headless
AVA_WORKING_DIRECTORY=/path/to/project ava "fix the login bug" --headless
```

To verify the install:

```bash
ava --help
```

Web mode is available through `ava serve`, but it requires a web-enabled source build. See [docs/how-to/install.md](docs/how-to/install.md#install-with-web-support).

### Products

AVA ships two user-facing products:

1. `ava` CLI for terminal use
2. AVA Desktop for a native Tauri app

If you only want the terminal app, use the CLI install path above. A full repo clone or source build also includes the desktop/web frontend files under `src/`, such as `.ts` and `.tsx`, because this repository contains both products.

### CLI Install Options

1. Binary install on Linux/macOS:

```bash
curl -fsSL https://raw.githubusercontent.com/Artificial-Source/AVA/develop/install.sh | sh
```

2. Manual binary download on Windows, Linux, or macOS from <https://github.com/Artificial-Source/AVA/releases>
3. Source build without installing:

```bash
git clone https://github.com/Artificial-Source/AVA.git && cd AVA
cargo build --release --bin ava
./target/release/ava
```

Optional compiler-cache variant for repeat source builds:

```bash
git clone https://github.com/Artificial-Source/AVA.git && cd AVA
RUSTC_WRAPPER=sccache cargo build --release --bin ava
./target/release/ava
```

4. Source install:

```bash
git clone https://github.com/Artificial-Source/AVA.git && cd AVA
cargo install --path crates/ava-tui --bin ava
```

5. Optional source-build helper:

```bash
./install-from-source.sh --help
```

Use `install-from-source.sh` only if you want a repo-provided convenience wrapper. For explicit control, use the Cargo commands above.

Full CLI install details: [docs/how-to/install.md](docs/how-to/install.md)

### Desktop

Desktop is the native desktop shell for AVA.

Quick path when a release includes desktop bundles:

1. Open <https://github.com/Artificial-Source/AVA/releases>
2. Download the desktop bundle for your platform when that release includes one

Desktop bundles are not published on every release. If the release you want does not include one, build it from source instead:

```bash
./install-from-source.sh --desktop
```

Desktop build and release details: [docs/how-to/download-desktop.md](docs/how-to/download-desktop.md), [docs/reference/install-and-release-paths.md](docs/reference/install-and-release-paths.md)

Release-repo note: release-related links in this checkout are aligned to `Artificial-Source/AVA`.

Security note:

1. Prefer AVA's connect flow, environment variables, or keychain-backed credential storage.
2. Avoid manually editing `~/.ava/credentials.json` unless you intentionally want plaintext local storage.

## What AVA Includes

- Solo-first coding agent
- 9 default built-in tools: `read`, `write`, `edit`, `bash`, `glob`, `grep`, `web_fetch`, `web_search`, `git_read`
- Works in TUI, desktop, and web
- MCP support
- Commands and Skills support
- Stable plugin architecture for optional advanced capability
- Session persistence and safety features for real repo work

## Supported Providers

AVA 0.6 actively supports and tests these providers:

1. Anthropic
2. OpenAI
3. Google Gemini
4. Ollama
5. OpenRouter
6. GitHub Copilot
7. Inception
8. Alibaba
9. ZAI / ZhipuAI
10. Kimi
11. MiniMax

Provider variants should appear as routing or region options inside a provider, not as separate providers.

## Customization

The main user-visible customization surface is:

1. MCPs
2. Commands
3. Skills

Plugins are a core part of AVA's identity, but plugin-owned UI and settings should only appear when installed.

## Configuration

```text
$XDG_CONFIG_HOME/ava/
├── config.yaml          # core settings

$XDG_CONFIG_HOME/AVA/
└── AGENTS.md            # global instructions

~/.ava/
├── credentials.json     # API keys
├── mcp.json             # MCP servers
├── tools/               # custom tool definitions
└── themes/              # custom themes
```

## Documentation

User docs:

- [docs/index.md](docs/index.md) - public docs entrypoint
- [docs/how-to/install.md](docs/how-to/install.md) - install AVA for CLI or desktop use
- [docs/tutorials/first-run.md](docs/tutorials/first-run.md) - first success path
- [docs/how-to/configure.md](docs/how-to/configure.md) - provider auth and local settings
- [docs/how-to/run-locally.md](docs/how-to/run-locally.md) - run AVA in TUI, headless, desktop, or web mode
- [docs/troubleshooting/common-errors.md](docs/troubleshooting/common-errors.md) - common setup and runtime fixes
- [docs/reference/README.md](docs/reference/README.md) - commands, providers, configuration, and storage reference

Contributor and maintainer docs:

- [docs/README.md](docs/README.md) - internal docs map
- [AGENTS.md](AGENTS.md) - repo workflow, conventions, and architecture for contributors and AI coding agents
- [docs/contributing/README.md](docs/contributing/README.md) - contributor workflow and release docs
- [docs/testing/README.md](docs/testing/README.md) - testing and verification
- [docs/project/roadmap.md](docs/project/roadmap.md)
- [docs/project/backlog.md](docs/project/backlog.md)
- [docs/architecture/README.md](docs/architecture/README.md)
- [docs/extend/README.md](docs/extend/README.md)
- [CLAUDE.md](CLAUDE.md)

## Contributing

```bash
just check
```

For the fuller verification flow and PR-era check policy (including hook behavior and desktop/frontend split), see:

- [How to run tests and checks](docs/how-to/test.md)
- [Development workflow](docs/contributing/development-workflow.md)
- [Testing and verification](docs/testing/README.md)

## License

[MIT](LICENSE)
