# File Structure Templates

## Simple Plugin

```
my-plugin/
├── src/
│   ├── index.ts          # Plugin entry point
│   ├── tools/            # Tool definitions
│   │   └── my-tool.ts
│   ├── hooks/            # Hook handlers
│   │   └── events.ts
│   └── lib/              # Utilities
│       ├── config.ts
│       └── utils.ts
├── package.json
├── tsconfig.json
└── README.md
```

---

## Complex Plugin

```
my-plugin/
├── src/
│   ├── index.ts              # Plugin entry point
│   ├── plugin-config.ts      # Config loading
│   ├── agents/               # Agent definitions
│   │   ├── my-agent.ts
│   │   └── index.ts
│   ├── hooks/                # Hook implementations
│   │   ├── events.ts
│   │   ├── tool-intercept.ts
│   │   └── index.ts
│   ├── tools/                # Tool implementations
│   │   ├── search.ts
│   │   ├── analyze.ts
│   │   └── index.ts
│   ├── features/             # Feature modules
│   │   ├── background-manager/
│   │   │   ├── manager.ts
│   │   │   ├── types.ts
│   │   │   └── index.ts
│   │   └── skill-loader.ts
│   ├── lib/                  # Shared utilities
│   │   ├── logger.ts
│   │   ├── errors.ts
│   │   └── validation.ts
│   └── types/                # Type definitions
│       └── index.ts
├── .opencode/                # OpenCode integration
│   ├── plugin.json
│   └── skills/
│       └── my-skill/
│           └── SKILL.md
├── package.json
├── tsconfig.json
└── README.md
```

---

## Minimal Plugin (Single File)

```typescript
// src/index.ts
import type { Plugin } from "@opencode-ai/plugin";
import { tool } from "@opencode-ai/plugin";

const myTool = tool({
  description: "Does something useful",
  args: {
    input: tool.schema.string(),
  },
  async execute(args) {
    return `Processed: ${args.input}`;
  },
});

export const MyPlugin: Plugin = async (ctx) => {
  return {
    tool: { my_tool: myTool },
  };
};

export default MyPlugin;
```

---

## Plugin with Skills

```
my-plugin/
├── src/
│   └── index.ts
├── .opencode/
│   └── skills/
│       ├── skill-one/
│       │   ├── SKILL.md
│       │   ├── references/
│       │   │   └── guide.md
│       │   └── scripts/
│       │       └── setup.sh
│       └── skill-two/
│           └── SKILL.md
├── package.json
└── tsconfig.json
```

---

## Plugin with Commands

```
my-plugin/
├── src/
│   ├── index.ts
│   └── commands/
│       ├── analyze.md      # /analyze command template
│       └── report.md       # /report command template
├── package.json
└── tsconfig.json
```

Command file format:

```markdown
---
description: Analyze the codebase
agent: analyzer
model: claude-sonnet
---

Analyze the following files and provide insights:

$ARGUMENTS
```

---

## Monorepo Plugin

```
my-plugin/
├── packages/
│   ├── core/
│   │   ├── src/
│   │   │   ├── index.ts
│   │   │   └── types.ts
│   │   └── package.json
│   ├── tools/
│   │   ├── src/
│   │   │   ├── index.ts
│   │   │   └── search.ts
│   │   └── package.json
│   └── hooks/
│       ├── src/
│       │   ├── index.ts
│       │   └── safety.ts
│       └── package.json
├── package.json
├── pnpm-workspace.yaml
└── tsconfig.json
```

---

## Source Reference

- `oh-my-opencode/` - Complex structure example
- `handoff/` - Simple structure example
- `opencode-plugin-template/` - Starter template
