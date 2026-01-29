# Dependencies & Setup

## Package.json Template

```json
{
  "name": "opencode-my-plugin",
  "version": "1.0.0",
  "type": "module",
  "main": "dist/index.js",
  "types": "dist/index.d.ts",
  "scripts": {
    "build": "bun build src/index.ts --outdir dist --target bun --format esm",
    "dev": "bun --watch src/index.ts",
    "typecheck": "tsc --noEmit",
    "lint": "biome check src/",
    "test": "bun test"
  },
  "dependencies": {
    "@opencode-ai/plugin": "^1.1.34",
    "@opencode-ai/sdk": "^1.1.34",
    "zod": "^3.23.0"
  },
  "devDependencies": {
    "@biomejs/biome": "^1.9.0",
    "@types/bun": "latest",
    "typescript": "^5.7.0"
  },
  "peerDependencies": {
    "bun": ">=1.0.0"
  }
}
```

---

## TypeScript Config

```json
{
  "compilerOptions": {
    "target": "ESNext",
    "module": "ESNext",
    "moduleResolution": "bundler",
    "strict": true,
    "esModuleInterop": true,
    "skipLibCheck": true,
    "declaration": true,
    "outDir": "dist",
    "rootDir": "src",
    "types": ["bun-types"]
  },
  "include": ["src/**/*"],
  "exclude": ["node_modules", "dist"]
}
```

---

## Plugin Manifest

`.opencode/plugin.json`:

```json
{
  "name": "my-plugin",
  "version": "1.0.0",
  "description": "What the plugin does",
  "entry": "../dist/index.js",
  "hooks": ["tool.execute.before", "event"],
  "tools": ["my_tool", "another_tool"],
  "config": {
    "schema": {
      "enabled": { "type": "boolean", "default": true },
      "features": { "type": "array", "items": { "type": "string" } }
    }
  }
}
```

---

## Common Dependencies

| Package | Purpose |
|---------|---------|
| `@opencode-ai/plugin` | Plugin SDK |
| `@opencode-ai/sdk` | API client types |
| `zod` | Schema validation |
| `jsonc-parser` | JSON with comments |
| `yaml` | YAML parsing |
| `node-notifier` | Desktop notifications |

---

## Optional Dependencies

| Package | Purpose |
|---------|---------|
| `drizzle-orm` | Database ORM |
| `better-sqlite3` | SQLite bindings |
| `@xenova/transformers` | Embeddings |
| `unique-names-generator` | Human-readable IDs |

---

## Biome Config

`biome.json`:

```json
{
  "$schema": "https://biomejs.dev/schemas/1.9.0/schema.json",
  "organizeImports": { "enabled": true },
  "linter": {
    "enabled": true,
    "rules": { "recommended": true }
  },
  "formatter": {
    "enabled": true,
    "indentStyle": "space",
    "indentWidth": 2
  }
}
```

---

## Build Commands

```bash
# Development (watch mode)
bun --watch src/index.ts

# Build for production
bun build src/index.ts --outdir dist --target bun --format esm

# Type checking
tsc --noEmit

# Lint and format
biome check --apply src/

# Run tests
bun test
```

---

## Installation in OpenCode

Add to `.opencode/config.json`:

```json
{
  "plugins": [
    "./path/to/my-plugin/dist/index.js"
  ]
}
```

Or install from npm:

```json
{
  "plugins": [
    "opencode-my-plugin"
  ]
}
```

---

## Source Reference

- `opencode-plugin-template/` - Starter template
- `oh-my-opencode/package.json` - Full example
