# OpenCode Analysis

> Comprehensive analysis of the OpenCode codebase for comparison with Estela

## Documents

| File | Size | Coverage |
|------|------|----------|
| [01-core-cli.md](./01-core-cli.md) | 24KB | Agent architecture, session management, 20 tools |
| [02-providers.md](./02-providers.md) | 18KB | 21 providers, streaming, tool calling |
| [03-mcp-permissions.md](./03-mcp-permissions.md) | 32KB | MCP transports, permissions, snapshots |
| [04-config-project.md](./04-config-project.md) | 29KB | Config system, project detection, storage |
| [05-cli-tui.md](./05-cli-tui.md) | 24KB | CLI commands, TUI components, PTY |
| [06-acp-lsp-ide.md](./06-acp-lsp-ide.md) | 19KB | ACP protocol, 30+ LSP servers, skills |
| [07-auxiliary-packages.md](./07-auxiliary-packages.md) | 32KB | Desktop app, SDK, UI, plugins |
| **[COMPARISON.md](./COMPARISON.md)** | 15KB | **Feature comparison & action items** |

**Total: ~193KB of documentation**

## Quick Reference

### Critical Missing Features in Estela

1. **Batch Tool** - Parallel execution of up to 25 tools
2. **Fuzzy Edit Strategies** - 9 strategies for reliable text replacement
3. **Skill System** - Reusable knowledge modules
4. **Doom Loop Detection** - Safety feature for repeated identical calls

### Estela Advantages

- Browser automation (Puppeteer)
- Native desktop app (Tauri + SolidJS)
- Explicit file operations (create, delete)

## How to Use

1. Start with **[COMPARISON.md](./COMPARISON.md)** for prioritized action items
2. Deep dive into specific areas using the numbered documents
3. Reference OpenCode source at `docs/reference-code/opencode/`

## Source

Analysis based on: `docs/reference-code/opencode/` (cloned from OpenCode repository)
