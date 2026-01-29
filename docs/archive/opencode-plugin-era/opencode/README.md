# OpenCode Reference

OpenCode platform documentation for plugin development.

## Files (Read in Order)

| # | File | Description |
|---|------|-------------|
| 0 | [00_OVERVIEW.md](00_OVERVIEW.md) | OpenCode fundamentals |
| 1 | [01_PLUGIN_SYSTEM.md](01_PLUGIN_SYSTEM.md) | Plugin architecture |
| 2 | [02_AGENTS.md](02_AGENTS.md) | Agent definitions |
| 3 | [03_TOOLS.md](03_TOOLS.md) | Built-in & custom tools |
| 4 | [04_HOOKS.md](04_HOOKS.md) | Event hooks |
| 5 | [05_MCP.md](05_MCP.md) | Model Context Protocol |
| 6 | [06_CONFIG.md](06_CONFIG.md) | Configuration |
| 7 | [07_SDK.md](07_SDK.md) | SDK reference |
| 8 | [08_MODELS.md](08_MODELS.md) | 75+ model providers |
| 9 | [09_ECOSYSTEM.md](09_ECOSYSTEM.md) | Plugins & community |

## Quick Reference

- Entry point: `Plugin` async function
- Config: `.opencode/config.json`
- Hooks: `event`, `tool.execute.before/after`, `chat.message`
- Tools: `tool()` helper with Zod schemas
