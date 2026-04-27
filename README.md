# AVA

AVA is a native C++23 agentic coding tool. This branch is a ground-up 0.1 implementation: one terminal binary, OpenAI first, safe built-in tools, build/plan modes, a simple TUI, and append-only JSONL sessions.

## Build

```sh
cmake -S . -B build -DAVA_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Sanitizer build:

```sh
cmake -S . -B build-sanitize -DAVA_ENABLE_SANITIZERS=ON -DAVA_BUILD_TESTS=ON
cmake --build build-sanitize
ctest --test-dir build-sanitize --output-on-failure
```

## Run

```sh
./build/ava
./build/ava --mode plan
./build/ava --continue
./build/ava --session <id-or-prefix>
```

When stdin/stdout are not a terminal, AVA falls back to a line-oriented shell for scripting:

```sh
printf '/glob **/*.cpp\n/quit\n' | ./build/ava --continue
```

## Configuration

AVA follows XDG paths on Linux:

- Config: `$XDG_CONFIG_HOME/ava/`, fallback `~/.config/ava/`
- Auth: `$XDG_CONFIG_HOME/ava/auth.json`, fallback `~/.config/ava/auth.json`
- Sessions: `$XDG_STATE_HOME/ava/sessions/`, fallback `~/.local/state/ava/sessions/`

OpenAI auth supports OAuth-style tokens and API keys:

```json
{"openai":{"type":"oauth","access_token":"...","refresh_token":"...","expires_at":1893456000}}
```

```json
{"openai":{"type":"api_key","api_key":"sk-..."}}
```

The built-in default is `openai/gpt-5.5`. Override models with `$XDG_CONFIG_HOME/ava/models.json`, and prompts with `$XDG_CONFIG_HOME/ava/prompts/<provider>/<family>/<mode>.txt`.

## Interactive Commands

- `/help`: show commands
- `/mode`: toggle build/plan mode
- `/sessions`: list resumable sessions for the current workspace
- `/read <path>`: read a file through permissions
- `/write <path> <text>`: write a file through permissions
- `/glob <pattern>`: list readable matching files
- `/grep <text> [glob]`: literal text search
- `/bash <command>`: run an argv-style permissioned command
- `/quit`: exit and print a resume command

## 0.1 Notes

- Real OpenAI requests use the local `curl` executable as the HTTP transport.
- Tool calling is implemented through the provider contract and the built-in dispatcher.
- `apply_patch` currently supports up to 32 exact text replacements through an `edits` array.
- `question` is exposed as a tool, but AVA 0.1 does not yet have a modal user-question workflow; the assistant is instructed to ask directly.
- Deferred: multiple providers, plugins, MCP, full session tree UI, compaction, LSP, web fetch, and polished permission modals.

## Planning Docs

- `docs/versions/0.1.md`
- `docs/CONFIG.md`
- `docs/USAGE.md`
- `docs/TESTING.md`
- `docs/product/product-plan.md`
- `docs/product/tooling-plan.md`
- `docs/product/architecture-plan.md`
- `docs/engineering/cpp-safety-rules.md`
