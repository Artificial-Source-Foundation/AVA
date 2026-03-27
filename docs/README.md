# AVA Docs

## Structure

```
docs/
  README.md            # you are here
  CHANGELOG.md         # release history
  backlog.md           # open work items
  crate-map.md         # Rust crate dependency graph
  plugins.md           # TOML custom tools + MCP server config
  troubleshooting/     # platform-specific fixes
  screenshots/         # UI screenshots
  reference-code/      # competitor source code (12 repos)
```

## Quick Start

```bash
just run                         # TUI
just headless "your goal"        # headless
just check                       # fmt + clippy + test

cargo run --bin ava -- serve --port 8080   # web mode
pnpm install && pnpm tauri dev             # desktop app
```

## Key References

| What | Where |
|------|-------|
| Architecture + conventions | [CLAUDE.md](../CLAUDE.md) |
| AI agent instructions | [AGENTS.md](../AGENTS.md) |
| Crate dependency graph | [crate-map.md](crate-map.md) |
| Plugins + MCP | [plugins.md](plugins.md) |
| Changelog | [CHANGELOG.md](CHANGELOG.md) |
| Backlog | [backlog.md](backlog.md) |
| Troubleshooting | [troubleshooting/](troubleshooting/) |
