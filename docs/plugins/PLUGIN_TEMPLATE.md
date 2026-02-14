# AVA Plugin Template

Use `ava plugin init <name>` to scaffold a new plugin package.

## Generated structure

```text
<plugin-name>/
  package.json
  tsconfig.json
  README.md
  src/
    index.ts
```

## Conventions

- Package scope: `@ava-plugin/<plugin-name>`
- Entry file: `src/index.ts`
- Build output: `dist/`
- Keep plugin IDs kebab-case.

## Recommended workflow

```bash
ava plugin init my-plugin --dir ./plugins
ava plugin dev my-plugin --dir ./plugins
ava plugin test my-plugin --dir ./plugins
cd plugins/my-plugin
pnpm install
pnpm run build
pnpm run test
```

## CLI plugin dev commands

- `ava plugin init <name> [--dir <path>] [--force]` - create plugin scaffold
- `ava plugin dev <name> [--dir <path>]` - run plugin development/watch script
- `ava plugin test <name> [--dir <path>]` - run plugin test suite

## Next Sprint 2.4 follow-ups

- Plugin hot-reload in AVA development mode
- Plugin test fixtures and mock host utilities
- Plugin registry publish flow
- Version management and update checks
