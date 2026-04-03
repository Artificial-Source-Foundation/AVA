# AVA Docs

## Structure

```
docs/
  README.md            # you are here
  backlog.md           # open work items
  hq/                  # HQ architecture + UX notes
  install.md           # install paths for users and contributors
  plugins.md           # extensions guide: MCP, tools, commands, skills, plugins
  releasing.md         # desktop release and updater flow
  troubleshooting/     # platform-specific fixes
    README.md          # troubleshooting index
  screenshots/         # UI screenshots
```

## Root-Level References

These live at the repo root rather than under `docs/`:

```text
CHANGELOG.md           # release history
CLAUDE.md              # architecture + contributor conventions
AGENTS.md              # AI coding agent instructions
CODEBASE_STRUCTURE.md  # lightweight repo map
design/ava-ui.pen      # Pencil source for the desktop redesign
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
| Release history | [../CHANGELOG.md](../CHANGELOG.md) |
| Current backlog | [backlog.md](backlog.md) |
| HQ architecture + UX | [hq/README.md](hq/README.md) |
| Installation | [install.md](install.md) |
| Extensions guide | [plugins.md](plugins.md) |
| Repo structure | [../CODEBASE_STRUCTURE.md](../CODEBASE_STRUCTURE.md) |
| Releasing desktop app | [releasing.md](releasing.md) |
| Troubleshooting | [troubleshooting/README.md](troubleshooting/README.md) |
| Desktop redesign source | [../design/ava-ui.pen](../design/ava-ui.pen) |
