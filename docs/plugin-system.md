# AVA Plugin And MCP Plan

This document defines the intended 1.0 plugin foundation for AVA. It is grounded in PI's extension model, but adapted for AVA's constraints: native C++23, one binary, explicit permission boundaries, inspectable local files, and a core that must keep working when plugins fail.

Phase 6 plugin/MCP foundation is required for the 1.0 backend MVP. Advanced extension features remain 1.1+ roadmap work.

## PI Reference Lessons

PI uses trusted TypeScript extensions loaded in-process. The relevant reference files are:

- `docs/reference-code/pi-mono/packages/coding-agent/docs/extensions.md`
- `docs/reference-code/pi-mono/packages/coding-agent/src/core/extensions/types.ts`
- `docs/reference-code/pi-mono/packages/coding-agent/src/core/extensions/loader.ts`
- `docs/reference-code/pi-mono/packages/coding-agent/src/core/extensions/runner.ts`
- `docs/reference-code/pi-mono/packages/coding-agent/src/core/extensions/wrapper.ts`
- `docs/reference-code/pi-mono/packages/coding-agent/docs/packages.md`
- `docs/reference-code/pi-mono/packages/coding-agent/docs/skills.md`

PI's useful ideas:

- Extensions are easy to author: a small module gets an API object and calls `registerTool`, `registerCommand`, or `on` for events.
- Extension resources are discovered from global, project, package, and explicit CLI/config paths.
- Packages can bundle extensions, skills, prompts, and themes through a `pi` field in `package.json`.
- Tools have names, descriptions, schemas, execution modes, optional prompt snippets, and optional UI rendering.
- Runtime events cover session, input, agent, turn, provider request/response, message streaming, tool call/result, compaction, and model selection.
- Tool hooks can block calls or transform results, which lets extensions implement custom permission gates.
- Skills and prompt templates are filesystem resources, not compiled code.
- RPC mode streams events and can bridge extension UI prompts to non-TTY clients.

PI's limits that AVA should not copy directly:

- PI extensions run in-process with full user permissions. PI documents this trust model clearly, but it means a bad extension can crash or compromise the process.
- PI has no native MCP support. Its README explicitly says "No MCP" and recommends building MCP as an extension.
- PI's extension APIs expose broad UI customization, providers, and provider request interception. AVA should start narrower until safety, events, permissions, and sessions are stable.

## AVA Direction

AVA should ship a small, stable, local plugin foundation for 1.0.

The default plugin shape is an out-of-process executable that speaks a versioned JSONL protocol over stdin/stdout. AVA owns discovery, enablement, validation, permission checks, event emission, audit/session records, cancellation, timeouts, output bounds, diagnostics, and lifecycle cleanup.

AVA should not load third-party native shared libraries in-process for 1.0. A native plugin ABI would make crashes, memory corruption, and C++ ABI compatibility part of the public support burden.

## Goals

- A developer or AI assistant can create a useful plugin without reading AVA C++ internals.
- Built-in tools and plugin tools use the same registry, validation, permission, event, audit, and cancellation paths.
- Plugin crashes, hangs, malformed JSONL, invalid schemas, or unsupported API versions produce contained plugin failures.
- Project plugins do not execute until explicitly enabled by the user.
- Plugin permissions are inspectable, revocable, and visible in session audit records.
- MCP servers work through the same plugin/runtime safety model rather than as a bypass around AVA permissions.

## Non-Goals For 1.0

- Extension marketplace, remote install flow, or package manager.
- In-process native plugin ABI.
- Arbitrary TUI slots, theme plugins, or custom renderer plugins.
- Provider/message interception that can silently rewrite prompts or provider requests.
- Hard cross-platform sandbox guarantees for arbitrary executables.
- Trusting MCP servers as safe just because they speak MCP.

These are non-goals for the 1.0 plugin/MCP foundation, not discarded product ideas. Marketplace/install flows, richer UI bridges, advanced remote MCP transports, and subagent orchestration should stay on the 1.1+ roadmap after the out-of-process safety model is proven.

## Plugin Discovery

Supported locations should be explicit and inspectable:

- Global plugins: `$XDG_CONFIG_HOME/ava/plugins/<plugin-id>/plugin.json`, falling back to `~/.config/ava/plugins/<plugin-id>/plugin.json`.
- Project plugins: `.ava/plugins/<plugin-id>/plugin.json`.
- Explicit CLI/config paths can be added later for development, but should not be the primary stable interface.

Project-local executable plugins are disabled by default. AVA may discover their manifests for inspection, but must not run their entrypoints until the user enables them.

## Plugin Manifest

The manifest is the stable authoring surface. JSON is preferred because AVA already has JSON infrastructure and plugin protocol records are JSONL.

Example:

```json
{
  "schema_version": 1,
  "id": "com.example.todo",
  "name": "Todo Tools",
  "version": "0.1.0",
  "api_version": "ava.plugin.v1",
  "description": "Adds a small session-local todo tool and slash command.",
  "entrypoint": {
    "command": "node",
    "args": ["plugin.js"]
  },
  "capabilities": ["tools", "commands", "prompts", "event_hooks"],
  "permissions": {
    "file": [],
    "shell": [],
    "network": [],
    "session": ["read_current"]
  },
  "contributes": {
    "tools": [
      {
        "name": "todo_add",
        "description": "Add one item to the session todo list.",
        "input_schema": {
          "type": "object",
          "properties": {
            "text": { "type": "string" }
          },
          "required": ["text"],
          "additionalProperties": false
        }
      }
    ],
    "commands": [
      {
        "name": "todo",
        "description": "Show or update the session todo list."
      }
    ],
    "prompts": []
  }
}
```

Manifest rules:

- `id` is stable, lowercase, and globally unique by convention, such as reverse-DNS or GitHub-owner style.
- `api_version` must match a supported AVA plugin API version.
- `entrypoint.command` is resolved relative to the plugin directory only when it is a relative path.
- `contributes` may declare static contributions that AVA can inspect before execution.
- Runtime registration may add dynamic contributions only after handshake and core-side validation.
- Unknown manifest fields are ignored unless they appear under a schema-controlled contribution object.
- Invalid contributions are disabled individually when possible; invalid manifests disable the plugin.

## Plugin Protocol

The plugin runner uses strict LF-delimited JSON records. Stderr is diagnostics only and is bounded. Stdout is protocol only.

Minimum handshake:

```json
{"id":"ava_1","type":"initialize","api_version":"ava.plugin.v1","plugin_id":"com.example.todo","workspace":"/repo"}
{"id":"ava_1","type":"initialized","api_version":"ava.plugin.v1","plugin_version":"0.1.0","contributions":{"tools":[],"commands":[],"prompts":[],"event_hooks":[]}}
```

Tool call:

```json
{"id":"ava_2","type":"tool.call","tool":"todo_add","arguments":{"text":"write tests"},"context":{"session_id":"session_..."}}
{"id":"ava_2","type":"tool.result","ok":true,"content":"Added todo: write tests","metadata":{"count":1}}
```

Cancellation:

```json
{"id":"ava_2","type":"cancel","reason":"user_cancelled"}
```

Protocol rules:

- Every request has a string `id`; every response echoes it exactly.
- Requests time out independently from plugin process startup.
- AVA sends cancellation before killing a plugin process when there is time to do so.
- Malformed records, unknown response ids, oversized records, or invalid result schemas are plugin errors.
- Plugin logs use explicit protocol records or bounded stderr; they are never mixed into tool results unless requested.
- AVA records plugin errors as runtime events and session audit entries.

## Contribution Types

1.0 should support these contribution types:

- Tools: model-callable operations with JSON-schema-like input and structured bounded results.
- Slash/backend commands: user-invoked commands routed through the backend command dispatcher.
- Prompt templates and skills: static markdown resources discovered from plugin directories.
- Non-mutating event hooks: observe lifecycle events and optionally add diagnostics, but not rewrite provider requests in 1.0.
- MCP servers: manifest/configured MCP endpoints that AVA can launch or connect to and adapt into AVA tools/resources/prompts.

Provider plugins, custom UI renderers, and prompt/provider interception can come later after the core event and audit model proves safe.

## Permissions And Audit

Plugins must not get side-effect authority by registering a tool. Side effects go through AVA permissions.

Permission categories:

- `plugin.execute`: run a plugin entrypoint.
- `plugin.tool.call`: call a plugin tool.
- `plugin.command.run`: run a plugin command.
- `plugin.event.observe`: subscribe to runtime events.
- `mcp.server.launch`: launch a local MCP server process.
- `mcp.server.connect`: connect to a remote MCP endpoint.
- `mcp.tool.call`: call an MCP tool.
- Existing categories such as `file.read`, `file.write`, `shell.run`, `network.fetch`, and `external.directory` still apply when a plugin asks AVA to perform those operations through a core service proxy.

Audit records should include:

- Plugin id and version.
- Contribution id and contribution type.
- Requested capability and requested operation.
- Permission decision and resolver actor.
- Core operation performed, if any.
- MCP server id and MCP tool/resource/prompt name when applicable.

## Core Service Proxy

AVA should expose safe operations to plugins through protocol requests instead of encouraging plugins to perform side effects directly.

Initial proxy operations:

- Read/search files through AVA file/search tools.
- Request file mutations through AVA write/edit/apply-patch paths.
- Run shell commands through AVA process policy.
- Fetch network resources through AVA network policy when `webfetch` exists.
- Read limited session metadata through explicit session permissions.

This does not sandbox arbitrary plugin code. It makes well-behaved plugins easy to write safely and gives AVA one auditable path for operations that go through AVA.

## MCP Integration

MCP should be a first-class plugin contribution type, not an afterthought.

1.0 required MCP scope:

- Stdio MCP server transport.
- Server definitions from global config, project config, or plugin manifests.
- Explicit enablement for project MCP servers before command execution.
- MCP `initialize` lifecycle with server capability capture.
- `tools/list` and `tools/call`, adapted into AVA's tool registry.
- `resources/list` and `resources/read`, exposed as explicit read-style commands or tools, not silently injected into context.
- `prompts/list` and `prompts/get`, exposed as prompt templates.
- Per-server startup timeout, initialize timeout, request timeout, cancellation, and process-tree cleanup.
- Health status and diagnostics visible through plugin/MCP inspect commands.
- Schema conversion from MCP tool input schemas to AVA/provider-compatible tool schemas, with unsupported schemas disabled and explained.
- Tool naming that avoids collisions, such as `mcp_<server_id>_<tool_name>` or another deterministic sanitized prefix.
- Session audit entries for server launch, tool calls, resource reads, prompt reads, errors, and permission decisions.

Strongly desired MCP scope:

- Streamable HTTP transport for remote MCP servers.
- Progress/log notifications surfaced as runtime events.
- Resource subscriptions if they can be made bounded and cancellable.

Deferred MCP scope:

- MCP marketplace/discovery beyond explicit configured servers.
- Automatic trust of server-declared side-effect safety.
- Complex OAuth flows for remote MCP servers.
- MCP sampling callbacks that let servers ask AVA's model to complete arbitrary requests.
- Cross-platform OS sandbox guarantees for local MCP server processes.

MCP safety rules:

- MCP servers are local or remote programs chosen by the user. Treat them as untrusted at the AVA boundary, but do not claim they are OS-sandboxed unless a real sandbox exists.
- Calling an MCP tool is a permissioned operation even if its schema looks read-only.
- Launching a local MCP server is a permissioned process execution event.
- Connecting to remote MCP is a permissioned network event.
- MCP tool results are bounded before they enter the model context.
- MCP errors are surfaced as MCP/plugin failures, not as core AVA crashes.

## Commands And Diagnostics

AVA should provide backend commands that work in TUI, print/RPC where applicable, and tests:

- `/plugins list`
- `/plugins inspect <id>`
- `/plugins enable <id>`
- `/plugins disable <id>`
- `/plugins validate <path>`
- `/plugins failures`
- `/mcp list`
- `/mcp inspect <server>`
- `/mcp tools <server>`
- `/mcp restart <server>`

The same operations should have RPC commands before external editor integrations depend on them.

## Testing Requirements

Minimum regression coverage:

- Manifest parse/validation for valid, invalid, and unknown-field cases.
- Project plugin discovery without execution until enablement.
- Plugin initialization success, unsupported API version, startup timeout, malformed handshake, oversized record, and clean shutdown.
- Plugin tool registration, successful call, invalid arguments, invalid result, timeout, cancellation, and crash.
- Permission denial for plugin execution and plugin tool calls.
- Audit/session records for plugin execution and tool calls.
- Fake MCP server initialize/list-tools/call-tool success.
- Fake MCP server tool error, malformed response, timeout, cancellation, and process cleanup.
- MCP resource and prompt listing/reading through bounded outputs.
- Tool name collision behavior between built-in, plugin, and MCP tools.

## Implementation Order

1. Build the internal tool registry and move built-in tools behind it.
2. Add contribution metadata and diagnostics without loading external plugins.
3. Add manifest parsing, discovery, enable/disable state, and validation commands.
4. Add the out-of-process plugin runner and fake-plugin tests.
5. Add plugin tool contributions through the registry.
6. Add plugin commands and static prompt/skill resources.
7. Add the MCP host with a fake stdio MCP server test harness.
8. Add real MCP server configuration docs and inspect/restart commands.
9. Add optional event hooks after tool/command/MCP paths are stable.

## 1.0 Decisions

- Plugin manifests are JSON only for 1.0. TOML can be reconsidered later if AVA's broader config format settles on TOML.
- Project plugin enablement is stored outside the repository by default under `$XDG_STATE_HOME/ava/plugin-enablement.json`, falling back to `~/.local/state/ava/plugin-enablement.json`. The state file is keyed by canonical workspace path and plugin id so a repository cannot enable executable plugin code for other users by committing `.ava` files.
- Global plugin enablement uses the same state file with a global scope key. Explicit config may discover global plugins, but execution still requires enablement unless the plugin ships as a trusted built-in later.
- Built-in tool names are reserved. Plugin tool model names use `plugin_<sanitized_plugin_id>_<tool_name>` by default. MCP tool model names use `mcp_<sanitized_server_id>_<tool_name>` by default. Name collisions disable the later contribution and produce diagnostics instead of overriding a tool silently.
- Stdio MCP transport is required for 1.0. Streamable HTTP MCP remains strongly desired, not required.
- Plugin process launch uses a separate permission category from shell commands. If a plugin asks AVA to run a shell command through the core service proxy, that proxied command still goes through the normal shell policy.
- Plugin stderr capture is bounded. The initial target is an in-memory tail of the last 64 KiB per plugin process, with larger log spill files deferred until diagnostics prove the need.
- Plugin restart is manual for 1.0. AVA may restart a plugin during `/plugins restart` or `/mcp restart`, but repeated automatic crash loops should disable the contribution and require user action.
- Plugins do not get a plugin-to-plugin event bus in 1.0. Event hooks observe AVA runtime events only, and inter-plugin communication is deferred.

## Remaining Implementation Choices

- Exact JSON schema subset accepted for plugin and MCP tool inputs.
- Exact timeout defaults for plugin startup, per-request calls, MCP initialize, and MCP tool/resource/prompt calls.
- Whether plugin enablement should support a separate machine-local project nickname for moved worktrees.
