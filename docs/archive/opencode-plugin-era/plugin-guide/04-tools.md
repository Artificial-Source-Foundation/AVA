# Tool Definition

## Basic Tool Pattern

```typescript
import { tool } from "@opencode-ai/plugin";

const myTool = tool({
  description: "What this tool does. Be descriptive for the AI.",
  args: {
    query: tool.schema.string().describe("Search query"),
    limit: tool.schema.number().optional().describe("Max results (default: 10)"),
    filters: tool.schema.object({
      type: tool.schema.enum(["code", "docs", "all"]).optional(),
      path: tool.schema.string().optional(),
    }).optional().describe("Optional filters"),
  },
  async execute(args, context) {
    // context.sessionID: Current session
    // args: Validated input

    try {
      const results = await performSearch(args.query, args.limit ?? 10);
      return JSON.stringify({ success: true, results });
    } catch (error) {
      return JSON.stringify({
        success: false,
        error: error instanceof Error ? error.message : "Unknown error"
      });
    }
  },
});
```

---

## Factory Pattern (Dependency Injection)

```typescript
export const CreateSearchTool = (client: OpencodeClient, config: Config) => {
  return tool({
    description: "Search with injected dependencies",
    args: {
      query: tool.schema.string(),
    },
    async execute(args, context) {
      // Use injected client
      const session = await client.session.get({ path: { id: context.sessionID } });

      // Use injected config
      const results = await search(args.query, config.maxResults);

      return formatResults(results);
    },
  });
};

// Usage in plugin
return {
  tool: {
    search: CreateSearchTool(ctx.client, config),
  },
};
```

---

## Tools Returning Structured Data

```typescript
const analysisTool = tool({
  description: "Analyze code and return structured results",
  args: {
    filePath: tool.schema.string(),
  },
  async execute(args) {
    const analysis = await analyzeFile(args.filePath);

    // Return structured JSON for the AI to parse
    return JSON.stringify({
      success: true,
      data: {
        complexity: analysis.complexity,
        issues: analysis.issues,
        suggestions: analysis.suggestions,
      },
      metadata: {
        analyzedAt: new Date().toISOString(),
        linesAnalyzed: analysis.lineCount,
      },
    });
  },
});
```

---

## Schema Types Reference

```typescript
tool.schema.string()
tool.schema.number()
tool.schema.boolean()
tool.schema.array(itemSchema)
tool.schema.object({ key: schema })
tool.schema.enum(["a", "b", "c"])
tool.schema.optional()
tool.schema.describe("Description for AI")
```

---

## Tool Context

The `context` object in `execute()` provides:

| Property | Type | Description |
|----------|------|-------------|
| `context.sessionID` | `string` | Current session ID |

---

## Source Reference

- `oh-my-opencode/src/tools/` - Various tool implementations
- `handoff/src/tools.ts` - Clean factory pattern example
- `canvas/src/index.ts` - Complex tools with IPC
