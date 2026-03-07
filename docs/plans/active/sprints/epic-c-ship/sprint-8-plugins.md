# Sprint 8: Plugin Ecosystem

**Epic:** C — Ship
**Duration:** 1 week
**Goal:** Users can create, install, and use plugins — Obsidian-style
**Parallel with:** Sprint 7 (Desktop UX)

---

## Competitive Landscape

| Tool | Extension model | Discovery | Install method |
|---|---|---|---|
| **Goose** | MCP servers (100+ ecosystem) | CLI `goose configure` | npm/uvx/docker |
| **Cline** | MCP servers + built-in tools | Settings UI | npm/uvx |
| **Continue** | IDE extension marketplace | VS Code/JetBrains store | IDE install |
| **AVA** | ExtensionAPI (skills + commands + hooks) | Catalog UI | Local path / URL |

**Target:** Our plugin model is more powerful than MCP-only (we support tools + middleware + modes).
Focus on making it easy to create and share.

---

## Story 8.1: Verify Plugin SDK

**Reference:** `docs/plugins/PLUGIN_SDK.md`, `docs/examples/plugins/`

**What to verify:**
1. `ava plugin init my-plugin` scaffolds a working plugin
2. Plugin can register: a tool, a slash command, a hook
3. Plugin activates when loaded
4. Plugin can be disabled without crashing

**Test with the 5 existing example plugins:**
- `docs/examples/plugins/hello-world/`
- `docs/examples/plugins/custom-tool/`
- `docs/examples/plugins/provider-plugin/`
- `docs/examples/plugins/middleware-plugin/`
- `docs/examples/plugins/full-plugin/`

**Fix any breakage** from Sprint 1's extension merges.

**Acceptance criteria:**
- [ ] `ava plugin init` scaffolds working plugin
- [ ] All 5 example plugins pass tests
- [ ] Plugin hot-reload works (change file → plugin reloads)

---

## Story 8.2: Plugin Install & Catalog UI

**Reference:** `src/stores/plugins-catalog.ts` (existing catalog store)

**What to verify/fix:**
1. Catalog UI shows available plugins
2. Install from local path works
3. Uninstall cleans up properly
4. Enable/disable per plugin works
5. Plugin state persists across sessions (via `plugin_state.rs` Tauri command)

**What to add:**
- Search/filter in catalog UI
- Plugin details view (description, author, tools registered)
- "Installed" vs "Available" tabs

**Acceptance criteria:**
- [ ] Catalog UI renders without errors
- [ ] Local plugin install works end-to-end
- [ ] Enable/disable toggles work
- [ ] Plugin state survives app restart

---

## Story 8.3: MCP Server as Plugin Source

**Reference:** `docs/reference-code/goose/crates/goose/src/agents/extension_manager.rs`

**What Goose does:**
- MCP servers are first-class extensions
- Tools from MCP servers appear alongside built-in tools
- `get_prefixed_tools()` namespaces them: `server_name:tool_name`

**AVA already has MCP client** (`packages/extensions/mcp/`).
Make MCP servers installable as plugins:

```typescript
// In plugin install flow:
if (source.type === 'mcp') {
  // Connect to MCP server
  const client = await mcpConnect(source.config)
  // Register all MCP tools as AVA tools
  for (const tool of client.tools) {
    api.registerTool({
      name: `${source.name}:${tool.name}`,
      description: tool.description,
      schema: tool.inputSchema,
      execute: (input) => client.callTool(tool.name, input)
    })
  }
}
```

**Acceptance criteria:**
- [ ] MCP server can be added as a plugin
- [ ] MCP tools appear in tool list with server prefix
- [ ] MCP tool execution works through normal tool pipeline
- [ ] Permissions/middleware apply to MCP tools too
