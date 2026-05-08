# AVA Plugin And MCP Foundation

This document defines AVA's current 1.0 plugin and MCP foundation. It is grounded in external extension-system lessons, but adapted for AVA's constraints: native C++23, one binary, explicit permission boundaries, inspectable local files, and a core that keeps working when plugins fail.

Advanced extension features remain 1.1+ roadmap work.

## External Reference Lessons

The external reference systems use trusted TypeScript extensions loaded in-process. The relevant behavior areas are:

- Extension user-facing documentation.
- Extension type definitions.
- Extension loading and runner lifecycle code.
- Extension process/wrapper code.
- Package and skill resource documentation.

Useful external-baseline ideas:

- Extensions are easy to author: a small module gets an API object and calls `registerTool`, `registerCommand`, or `on` for events.
- Extension resources are discovered from global, project, package, and explicit CLI/config paths.
- Packages can bundle extensions, skills, prompts, and themes through package metadata.
- Tools have names, descriptions, schemas, execution modes, optional prompt snippets, and optional UI rendering.
- Runtime events cover session, input, agent, turn, provider request/response, message streaming, tool call/result, compaction, and model selection.
- Tool hooks can block calls or transform results, which lets extensions implement custom permission gates.
- Skills and prompt templates are filesystem resources, not compiled code.
- RPC mode streams events and can bridge extension UI prompts to non-TTY clients.

External-baseline limits that AVA should not copy directly:

- In-process extensions run with full user permissions. That trust model is clear, but it means a bad extension can crash or compromise the process.
- Native MCP support is not always part of the reference extension model, which pushes MCP through extension code instead.
- Broad extension APIs can expose UI customization, providers, and provider request interception. AVA should start narrower until safety, events, permissions, and sessions are stable.

## AVA Direction

AVA ships a small, stable, local plugin foundation for 1.0.

The default plugin shape is an out-of-process executable that speaks a versioned JSONL protocol over stdin/stdout. AVA owns discovery, enablement, validation, permission checks, event emission, audit/session records, cancellation, timeouts, output bounds, diagnostics, and lifecycle cleanup.

AVA does not load third-party native shared libraries in-process for 1.0. A native plugin ABI would make crashes, memory corruption, and C++ ABI compatibility part of the public support burden.

The compatibility rules for this surface live in [`docs/plugin-compatibility-policy.md`](plugin-compatibility-policy.md). That policy defines compatible `ava.plugin.v1` additions, breaking-change handling, golden contract expectations, and the explicit MCP resource deferral.

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

Supported locations are explicit and inspectable:

- Global plugins: `$XDG_CONFIG_HOME/ava/plugins/<plugin-id>/plugin.json`, falling back to `~/.config/ava/plugins/<plugin-id>/plugin.json`.
- Project plugins: `.ava/plugins/<plugin-id>/plugin.json`.
- Explicit CLI/config paths can be added later for development, but should not be the primary stable interface.

Discovered executable plugins are disabled by default. AVA can inspect their manifests without starting a process, but it does not run entrypoints until the user enables the plugin. Enablement is machine-local state stored under `$XDG_STATE_HOME/ava/plugin-enablement.json`, falling back to `~/.local/state/ava/plugin-enablement.json`, keyed by canonical workspace path, plugin id, and scope.

## Plugin Authoring Guide

The stable 1.0 authoring surface is a local directory with a `plugin.json` manifest and an out-of-process entrypoint. Start from the checked-in sample at `examples/plugins/todo/`; it is intentionally small, uses POSIX shell, and is exercised by regression tests.

Recommended layout:

```text
.ava/plugins/com.example.todo/
  plugin.json
  plugin.sh
  prompts/todo-review.md
  skills/todo-triage.md
  README.md
```

Use the same layout under `$XDG_CONFIG_HOME/ava/plugins/<plugin-id>/` for a global plugin. Project plugins under `.ava/plugins/<plugin-id>/` are discovered from the active workspace but remain disabled until explicitly enabled on the local machine.

Authoring checklist:

- Choose a stable lowercase `id` such as reverse-DNS (`com.example.todo`) or an owner-prefixed id. It may contain letters, digits, `.`, `_`, and `-`, but cannot start empty, use uppercase, repeat separators, or end with a separator.
- Set `schema_version` to `1` and `api_version` to `ava.plugin.v1`.
- Declare an `entrypoint` with a command and optional string `args`. For portable shell samples, prefer `"command": "/bin/sh", "args": ["plugin.sh"]` so the plugin does not depend on executable file mode.
- List static `contributes.tools`, `contributes.commands`, `contributes.prompts`, `contributes.skills`, and `contributes.event_hooks` that AVA can inspect before running the plugin. Prompt and skill paths must be safe relative paths inside the plugin directory.
- Keep stdout reserved for LF-delimited JSON protocol records. Write diagnostics to stderr; AVA captures a bounded stderr tail for failures.
- Use a real JSON parser in non-trivial plugins. The shell sample only parses AVA's compact demo records and is not a general JSON parser.

Entrypoint behavior:

- AVA launches the plugin process with the plugin directory as the child working directory.
- AVA sends one `initialize` record before any tool, command, or event request.
- The plugin must answer every request with one LF-delimited JSON object and echo the request `id` exactly.
- `list`, `inspect`, `validate`, `prompts`, `prompt`, `skills`, and `skill` read metadata or static resources only; they do not start the plugin process.
- `enable` and `disable` update local enablement state only; they do not start or stop a resident daemon.
- `/plugin run` and model-dispatched plugin tools start a fresh plugin process for the call path today, subject to permission checks and timeouts.

Minimum JSONL records:

```json
{"id":"ava_1","type":"initialize","api_version":"ava.plugin.v1","plugin_id":"com.example.todo","workspace":"/repo"}
{"id":"ava_1","type":"initialized","api_version":"ava.plugin.v1","plugin_version":"0.1.0","contributions":{"tools":[],"commands":[],"prompts":[],"skills":[],"event_hooks":[]}}
{"id":"ava_command_cmd_1","type":"command.call","command":"status","arguments":{},"context":{"call_id":"cmd_1","workspace":"/repo"}}
{"id":"ava_command_cmd_1","type":"command.result","ok":true,"content":"Todo sample plugin is ready.","metadata":{"open_items":0}}
{"id":"ava_tool_call_1","type":"tool.call","tool":"todo_add","arguments":{"text":"write tests"},"context":{"call_id":"call_1","workspace":"/repo"}}
{"id":"ava_tool_call_1","type":"tool.result","ok":true,"content":"Todo item accepted by the sample plugin.","metadata":{"items":1}}
{"id":"ava_event_event_1","type":"event.observe","event":"tool_result","payload":{"tool":"read_file","status":"success"},"context":{"call_id":"event_1","workspace":"/repo"}}
{"id":"ava_event_event_1","type":"event.observed","ok":true,"content":"Todo sample observed the event.","metadata":{"events":1}}
```

Static prompts and skills are plain markdown resources. AVA reads them through `/plugins prompt <id> <name>` and `/plugins skill <id> <name>` after manifest validation and path containment checks. They are useful for reusable instructions even when the plugin entrypoint is disabled or failing.

Permissions:

- Plugin process launch requires `plugin.execute`.
- Tool calls require `plugin.tool.call` after launch permission.
- Command calls require `plugin.command.run` after launch permission.
- Event hooks require `plugin.event.observe` after launch permission.
- A manifest `permissions` block is useful documentation for reviewers, but the current 1.0 runtime enforcement is the permission prompt around process launch and contributed operations. The deferred core-service proxy below is not available yet.

Local workflow:

```sh
mkdir -p .ava/plugins
cp -R examples/plugins/todo .ava/plugins/com.example.todo
ava --continue
```

Inside AVA:

```text
/plugins validate .ava/plugins/com.example.todo/plugin.json
/plugins list
/plugins inspect com.example.todo
/plugins prompts com.example.todo
/plugins prompt com.example.todo todo-review
/plugins skills com.example.todo
/plugins skill com.example.todo todo-triage
/plugins enable com.example.todo
/plugin run com.example.todo status {}
/plugins disable com.example.todo
```

Troubleshooting:

- `plugin manifest must be a valid JSON object`: fix `plugin.json`; `validate` never starts the entrypoint.
- `api_version is unsupported`: use `ava.plugin.v1`.
- `plugin is disabled`: run `/plugins enable <id>` in the workspace that discovered the plugin.
- `permission_denied`: approve the permission prompt in an interactive session or wire an RPC permission reply; headless modes fail closed by default.
- `plugin initialize response is malformed`: stdout contained non-protocol text, missing fields, the wrong `id`, or unsupported `api_version`.
- `timed out waiting`: the entrypoint did not answer within the startup or request timeout.
- Missing prompt or skill content: check that the resource path is relative, inside the plugin directory, a regular file, and below the resource size cap.
- Duplicate ids are disabled and reported through `/plugins failures`.

Current core-service proxy limit: plugins cannot yet ask AVA over the plugin protocol to read files, edit files, run shell commands, fetch network resources, or inspect session state. If a plugin performs those side effects directly inside its own process, AVA only mediates the plugin launch/call permission and cannot provide built-in file/shell/network policy for the internal operation. Keep early plugins narrow and prefer static resources or explicit AVA built-in tools until the proxy surface lands.

## Plugin Manifest

The manifest is the stable authoring surface. JSON is preferred because AVA already has JSON infrastructure and plugin protocol records are JSONL.

Example:

```json
{
  "schema_version": 1,
  "id": "com.example.todo",
  "name": "Todo Sample Plugin",
  "version": "0.1.0",
  "api_version": "ava.plugin.v1",
  "description": "Minimal local plugin that demonstrates a command, a tool, static resources, and an event hook.",
  "entrypoint": {
    "command": "/bin/sh",
    "args": ["plugin.sh"]
  },
  "capabilities": ["tools", "commands", "prompts", "skills", "event_hooks"],
  "permissions": {
    "file": [],
    "shell": [],
    "network": [],
    "session": []
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
        "name": "status",
        "description": "Report that the sample plugin is ready."
      }
    ],
    "prompts": [
      {
        "name": "todo-review",
        "description": "Review todo state for follow-up work.",
        "path": "prompts/todo-review.md"
      }
    ],
    "skills": [
      {
        "name": "todo-triage",
        "description": "Triage todo items before implementation.",
        "path": "skills/todo-triage.md"
      }
    ],
    "event_hooks": [
      { "event": "tool.result" }
    ]
  }
}
```

Manifest rules:

- `id` is stable, lowercase, and globally unique by convention, such as reverse-DNS or GitHub-owner style.
- `api_version` must match a supported AVA plugin API version.
- The entrypoint runs with the plugin directory as its working directory. Use `/bin/sh plugin.sh` for scripts that should not depend on executable mode, or a relative path with a slash such as `./plugin` for executable files inside the plugin directory.
- `contributes` may declare static contributions that AVA can inspect before execution.
- Runtime registration may add dynamic contributions only after handshake and core-side validation.
- Unknown manifest fields are ignored unless they appear under a schema-controlled contribution object.
- Invalid contributions are disabled individually when possible; invalid manifests disable the plugin.

## Plugin Protocol

The plugin runner uses strict LF-delimited JSON records. Stderr is diagnostics only and is bounded. Stdout is protocol only.

Minimum handshake:

```json
{"id":"ava_1","type":"initialize","api_version":"ava.plugin.v1","plugin_id":"com.example.todo","workspace":"/repo"}
{"id":"ava_1","type":"initialized","api_version":"ava.plugin.v1","plugin_version":"0.1.0","contributions":{"tools":[],"commands":[],"prompts":[],"skills":[],"event_hooks":[]}}
```

Tool call:

```json
{"id":"ava_tool_call_...","type":"tool.call","tool":"todo_add","arguments":{"text":"write tests"},"context":{"call_id":"call_...","workspace":"/repo"}}
{"id":"ava_tool_call_...","type":"tool.result","ok":true,"content":"Added todo: write tests","metadata":{"count":1}}
```

Command call:

```json
{"id":"ava_command_cmd_...","type":"command.call","command":"status","arguments":{},"context":{"call_id":"cmd_...","workspace":"/repo"}}
{"id":"ava_command_cmd_...","type":"command.result","ok":true,"content":"Todo sample plugin is ready.","metadata":{"open_items":0}}
```

Event observation:

```json
{"id":"ava_event_event_...","type":"event.observe","event":"tool_result","payload":{"tool":"read_file","status":"success"},"context":{"call_id":"event_...","workspace":"/repo"}}
{"id":"ava_event_event_...","type":"event.observed","ok":true,"content":"optional diagnostic","metadata":{"count":1}}
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
- Event hook manifests may declare dotted names such as `tool.result`; AVA normalizes dots to underscores and sends canonical runtime event names such as `tool_result` in `event.observe` requests.
- Event hook failures are best-effort diagnostics. A failed hook does not fail the user turn, slash command, or MCP command that emitted the observed runtime event.

## Contribution Types

1.0 supports these contribution types:

- Tools: model-callable operations with JSON-schema-like input and structured bounded results.
- Slash/backend commands: user-invoked commands routed through the backend command dispatcher.
- Prompt templates and skills: static markdown resources discovered from plugin directories.
- Non-mutating event hooks: observe lifecycle events and optionally add diagnostics, but not rewrite provider requests in 1.0.
- MCP servers: configured endpoints that AVA can launch and adapt into AVA tools. MCP server declarations in plugin manifests are deferred.

Provider plugins, custom UI renderers, and prompt/provider interception can come later after the core event and audit model proves safe.

## Permissions And Audit

Plugin-contributed operations must not get side-effect authority merely by registering a tool. AVA enforces launch and call permissions today; future core-service proxy operations will reuse the existing file, shell, network, and session permission categories.

Permission categories:

- `plugin.execute`: run a plugin entrypoint.
- `plugin.tool.call`: call a plugin tool.
- `plugin.command.run`: run a plugin command.
- `plugin.event.observe`: subscribe to runtime events.
- `mcp.server.launch`: launch a local MCP server process.
- `mcp.server.connect`: connect to a configured MCP server.
- `mcp.tool.call`: call an MCP tool.
- Existing categories such as `file.read`, `file.write`, `shell.run`, `network.fetch`, and `external.directory` still apply when a plugin asks AVA to perform those operations through a core service proxy.

Audit records emitted today include:

- Permission request id.
- Operation, such as `plugin.execute`, `plugin.tool.call`, `plugin.command.run`, `plugin.event.observe`, `mcp.server.launch`, `mcp.server.connect`, or `mcp.tool.call`.
- Agent mode.
- Model-facing tool or command name when applicable.
- Policy action, reason, and risk.
- Target path or command when applicable.
- Resolver resolution, source, and reason when applicable.

Dedicated plugin id/version, contribution id/type, requested capability, core operation, MCP server id, and MCP tool-name audit fields are deferred compatibility-preserving metadata. Today, those identities are carried indirectly through operation, tool name, command, path, and error context.

## Deferred Core Service Proxy

AVA should eventually expose safe operations to plugins through protocol requests instead of encouraging plugins to perform side effects directly. The 1.0 foundation keeps plugin execution permissioned and audited, but does not yet provide this proxy surface.

Initial proxy operations:

- Read/search files through AVA file/search tools.
- Request file mutations through AVA write/edit/apply-patch paths.
- Run shell commands through AVA process policy.
- Fetch network resources through AVA network policy when `webfetch` exists.
- Read limited session metadata through explicit session permissions.

This does not sandbox arbitrary plugin code. It makes well-behaved plugins easy to write safely and gives AVA one auditable path for operations that go through AVA.

## MCP Integration

MCP is a first-class extension surface that shares AVA's tool registry, permission, runtime event, and audit paths. Current 1.0 support uses explicit global/project MCP config files rather than plugin manifest contributions.

Config locations:

- Global: `$XDG_CONFIG_HOME/ava/mcp.json`, falling back to `~/.config/ava/mcp.json`.
- Project: `.ava/mcp.json` under the active workspace.

Config shape:

```json
{
  "servers": [
    {
      "id": "demo",
      "name": "Demo MCP",
      "command": "demo-mcp-server",
      "args": ["--stdio"],
      "enabled": true
    }
  ]
}
```

Global servers default to enabled when `enabled` is omitted. Project servers default to disabled when `enabled` is omitted, so repositories cannot silently opt users into running project-local MCP server commands.

Current 1.0 MCP scope:

- Stdio MCP server transport.
- Server definitions from global and project config.
- Explicit `enabled:true` for project MCP servers before command execution.
- MCP `initialize` lifecycle with server capability capture.
- `tools/list` and `tools/call`, adapted into AVA's tool registry.
- `prompts/list` and `prompts/get`, surfaced through the command registry as dynamic `/mcp:<server_id>:<prompt_name>` prompt commands.
- Per-server startup timeout, initialize timeout, request timeout, cancellation, and process-tree cleanup.
- Health status and diagnostics visible through plugin/MCP inspect commands.
- Schema conversion from MCP tool input schemas to AVA/provider-compatible tool schemas, with unsupported schemas disabled and explained.
- Tool naming that avoids collisions, such as `mcp_<server_id>_<tool_name>` or another deterministic sanitized prefix.
- Session audit entries for server launch, tool calls, errors, and permission decisions.

Deferred MCP scope:

- `resources/list` and `resources/read`, deferred until a separate read-style command/tool design with explicit permissions and bounds lands; they are not implemented or silently injected into context for 1.0 contract hardening.
- Streamable HTTP transport for remote MCP servers.
- Progress/log notifications surfaced as runtime events.
- Resource subscriptions if they can be made bounded and cancellable.
- MCP marketplace/discovery beyond explicit configured servers.
- Automatic trust of server-declared side-effect safety.
- Complex OAuth flows for remote MCP servers.
- MCP sampling callbacks that let servers ask AVA's model to complete arbitrary requests.
- Cross-platform OS sandbox guarantees for local MCP server processes.

MCP safety rules:

- MCP servers are programs chosen by the user. Treat them as untrusted at the AVA boundary, but do not claim they are OS-sandboxed unless a real sandbox exists.
- Calling an MCP tool is a permissioned operation even if its schema looks read-only.
- Launching a local MCP server is a permissioned process execution event.
- Connecting to an MCP server session is permissioned; future remote transports also need explicit network permission.
- MCP tool results are bounded before they enter the model context.
- MCP server stderr is diagnostics only and is kept as a bounded tail.
- MCP errors are surfaced as MCP/plugin failures, not as core AVA crashes.

## Commands And Diagnostics

AVA provides backend commands that work in TUI, print/RPC where applicable, and tests:

- `/plugins list`
- `/plugins inspect <id>`
- `/plugins enable <id>`
- `/plugins disable <id>`
- `/plugins validate <path>`
- `/plugins failures`
- `/plugins prompts <id>`
- `/plugins prompt <id> <name>`
- `/plugins skills <id>`
- `/plugins skill <id> <name>`
- `/plugin run <id> <command> [arguments_json]`
- `/mcp list`
- `/mcp inspect <server>`
- `/mcp tools <server>`
- `/mcp restart <server>`

The same operations have RPC command forms for external editor integrations. MCP prompts are exposed through `list_commands`/`invoke_command` as dynamic command-registry entries, not as direct `/mcp prompts` slash commands. `/mcp tools` launches a fresh stdio process for discovery, emits tool start/result events, and requires `mcp.server.launch` and `mcp.server.connect` permission approval. `/mcp restart` is informational because current MCP stdio servers are per-discovery/per-tool-call processes rather than resident daemons.

## Testing Requirements

Minimum regression coverage:

- Manifest parse/validation for valid, invalid, and unknown-field cases.
- Project plugin discovery without execution until enablement.
- Plugin initialization success, unsupported API version, startup timeout, malformed handshake, oversized record, and clean shutdown.
- Plugin tool registration, successful call, invalid arguments, invalid result, timeout, cancellation, and crash.
- Permission denial for plugin execution and plugin tool calls.
- Audit/session records for plugin execution and tool calls.
- Fake MCP server initialize/list-tools/call-tool success.
- Fake MCP server prompt list/get success through command-registry discovery and invocation.
- Fake MCP server tool error, malformed initialize response, early startup/discovery exit, timeout, cancellation,
  stderr bounding, and process cleanup.
- Direct `ava --rpc` headless smoke coverage for plugin list/failures/inspect/validate/resource/enable/disable
  commands, real-sample plugin discovery/resource/enable/disable flow, fail-closed plugin command execution, MCP
  list/inspect/restart commands, invalid-config containment, and fail-closed `list_mcp_tools` behavior without a TUI
  resolver.
- Tool name collision behavior between built-in, plugin, and MCP tools.

## Implemented 1.0 Foundation

- Internal tool registry with built-in, plugin, and MCP tool registration.
- Plugin manifest parsing, diagnostics, discovery, local enable/disable state, and validation commands.
- Out-of-process plugin runner with initialize, tool call, command call, event observation, cancellation, bounded stderr, timeouts, and shutdown.
- Plugin tool contributions, plugin command contributions, static prompt/skill resources, and non-mutating event hooks.
- Direct headless RPC plugin command smokes for discovery, diagnostics, static resources, real-sample project plugin
  coverage, enablement, and fail-closed execution.
- Stdio MCP config loading, initialize, `tools/list`, `tools/call`, `prompts/list`, `prompts/get`, bounded stderr
  diagnostics, tool broker registration, command-registry prompt exposure, slash/RPC diagnostics, direct headless
  command smokes, and fake-server success/error/exit regression coverage.

## 1.0 Decisions

- Plugin manifests are JSON only for 1.0. TOML can be reconsidered later if AVA's broader config format settles on TOML.
- Project plugin enablement is stored outside the repository by default under `$XDG_STATE_HOME/ava/plugin-enablement.json`, falling back to `~/.local/state/ava/plugin-enablement.json`. The state file is keyed by canonical workspace path and plugin id so a repository cannot enable executable plugin code for other users by committing `.ava` files.
- Global plugin enablement uses the same state file with a global scope key. Explicit config may discover global plugins, but execution still requires enablement unless the plugin ships as a trusted built-in later.
- Built-in tool names are reserved. Plugin tool model names use `plugin_<sanitized_plugin_id>_<tool_name>` by default. MCP tool model names use `mcp_<sanitized_server_id>_<tool_name>` by default. Name collisions disable the later contribution and produce diagnostics instead of overriding a tool silently.
- Stdio MCP transport is required for 1.0. Streamable HTTP MCP remains strongly desired, not required.
- Plugin process launch uses a separate permission category from shell commands. If a plugin asks AVA to run a shell command through the core service proxy, that proxied command still goes through the normal shell policy.
- Plugin and MCP stderr capture is bounded. The initial target is an in-memory tail of the last 64 KiB per process, with larger log spill files deferred until diagnostics prove the need.
- Plugin restart is manual for 1.0. Current plugin and MCP stdio processes are launched per call or discovery; `/mcp restart` reports that the next discovery or tool call will launch a fresh process.
- Plugins do not get a plugin-to-plugin event bus in 1.0. Event hooks observe AVA runtime events only, and inter-plugin communication is deferred.
- `ava.plugin.v1` compatibility is governed by the plugin/MCP compatibility policy; additive optional fields are preferred, while breaking manifest/protocol/schema changes require an explicit versioned transition.

## Remaining Implementation Choices

- Exact JSON schema subset accepted for plugin and MCP tool inputs.
- Exact timeout defaults for plugin startup, per-request calls, MCP initialize, and MCP tool/resource/prompt calls.
- Whether plugin enablement should support a separate machine-local project nickname for moved worktrees.
- Plugin-manifest MCP server contributions.
- MCP resources, Streamable HTTP, and progress/log notification surfacing.
