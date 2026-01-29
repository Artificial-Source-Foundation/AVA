# Configuration Patterns

## Hierarchical Configuration Loading

Load from multiple sources with precedence (project overrides user):

```typescript
import path from "path";
import { homedir } from "os";
import { parse as parseJsonc } from "jsonc-parser";

interface PluginConfig {
  enabled: boolean;
  features: string[];
  settings: Record<string, unknown>;
}

const DEFAULT_CONFIG: PluginConfig = {
  enabled: true,
  features: [],
  settings: {},
};

function getConfigPaths(directory: string, pluginName: string) {
  return {
    // User-level: ~/.config/opencode/{plugin}.jsonc
    user: path.join(homedir(), ".config", "opencode", `${pluginName}.jsonc`),
    // Project-level: .opencode/{plugin}.jsonc
    project: path.join(directory, ".opencode", `${pluginName}.jsonc`),
  };
}

async function loadConfig(directory: string): Promise<PluginConfig> {
  const paths = getConfigPaths(directory, "my-plugin");

  // Start with defaults
  let config = { ...DEFAULT_CONFIG };

  // Layer user config
  const userConfig = await loadConfigFile(paths.user);
  if (userConfig) {
    config = mergeConfigs(config, userConfig);
  }

  // Layer project config (highest priority)
  const projectConfig = await loadConfigFile(paths.project);
  if (projectConfig) {
    config = mergeConfigs(config, projectConfig);
  }

  return config;
}

async function loadConfigFile(filePath: string): Promise<Partial<PluginConfig> | null> {
  try {
    const content = await Bun.file(filePath).text();
    return parseJsonc(content); // Supports JSON with comments
  } catch {
    return null; // File doesn't exist
  }
}

function mergeConfigs(base: PluginConfig, override: Partial<PluginConfig>): PluginConfig {
  return {
    ...base,
    ...override,
    settings: { ...base.settings, ...override?.settings },
    features: [...new Set([...base.features, ...(override?.features ?? [])])],
  };
}
```

---

## Config Locations

| Priority | Path | Scope |
|----------|------|-------|
| Lowest | `~/.config/opencode/{plugin}.jsonc` | User-level defaults |
| Highest | `.opencode/{plugin}.jsonc` | Project-specific overrides |

---

## Zod Validation

Always validate configuration at load time:

```typescript
import { z } from "zod";

const PluginConfigSchema = z.object({
  enabled: z.boolean().default(true),
  features: z.array(z.string()).default([]),
  settings: z.record(z.string(), z.unknown()).default({}),
  // Nested objects
  background: z.object({
    concurrency: z.number().min(1).max(10).default(3),
    timeout: z.number().min(1000).max(600000).default(30000),
  }).default({}),
});

type PluginConfig = z.infer<typeof PluginConfigSchema>;

function parseConfig(raw: unknown): PluginConfig {
  const result = PluginConfigSchema.safeParse(raw);
  if (!result.success) {
    console.error("Config validation failed:", result.error.format());
    return PluginConfigSchema.parse({}); // Return defaults
  }
  return result.data;
}
```

---

## JSONC Support

Use `jsonc-parser` for JSON with comments:

```jsonc
// .opencode/my-plugin.jsonc
{
  "enabled": true,
  // Features to enable
  "features": ["auto-commit", "notifications"],
  "settings": {
    "timeout": 30000
  }
}
```

---

## Source Reference

- `oh-my-opencode/src/plugin-config.ts` - Full hierarchical loading
- `oh-my-opencode/src/config/schema.ts` - Zod schemas
