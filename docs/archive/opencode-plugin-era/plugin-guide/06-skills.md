# Skills System

## Skill Discovery Pattern

Pattern from `agent-skills`:

```typescript
interface Skill {
  name: string;
  description: string;
  template: string;
  path: string;
  label: "project" | "user" | "plugin";
  scripts: Array<{ relativePath: string; absolutePath: string }>;
}

const DISCOVERY_PATHS = [
  { path: ".opencode/skills", label: "project", maxDepth: 3 },
  { path: ".claude/skills", label: "project", maxDepth: 1 },
  { path: "~/.config/opencode/skills", label: "user", maxDepth: 3 },
  { path: "~/.claude/skills", label: "user", maxDepth: 1 },
];

async function discoverSkills(directory: string): Promise<Map<string, Skill>> {
  const skills = new Map<string, Skill>();

  for (const { path: basePath, label, maxDepth } of DISCOVERY_PATHS) {
    const fullPath = basePath.startsWith("~")
      ? path.join(homedir(), basePath.slice(1))
      : path.join(directory, basePath);

    const found = await findSkillsRecursive(fullPath, label, maxDepth);

    for (const skill of found) {
      // First match wins
      if (!skills.has(skill.name)) {
        skills.set(skill.name, skill);
      }
    }
  }

  return skills;
}

async function parseSkillFile(filePath: string): Promise<Skill | null> {
  const content = await Bun.file(filePath).text();
  const match = content.match(/^---\n([\s\S]*?)\n---\n([\s\S]*)$/);

  if (!match) return null;

  // Parse YAML frontmatter
  const frontmatter = parseYaml(match[1]);
  const template = match[2].trim();

  return {
    name: frontmatter.name,
    description: frontmatter.description,
    template,
    path: path.dirname(filePath),
    label: "project",
    scripts: await discoverScripts(path.dirname(filePath)),
  };
}
```

---

## SKILL.md Format

```markdown
---
name: my-skill
description: What this skill does (min 20 chars for discoverability)
license: MIT
allowed-tools:
  - Read
  - Write
metadata:
  category: development
  version: "1.0"
---

# My Skill

Instructions in imperative form.

## When to Use

Load this skill when:
- Condition 1
- Condition 2

## Instructions

1. Step one
2. Step two
3. Step three

## Resources

See `references/guide.md` for details.
Run `scripts/build.sh` for setup.
```

---

## Skill Injection Pattern

```typescript
const useSkillTool = tool({
  description: "Load a skill into the current context",
  args: {
    skill: tool.schema.string().describe("Skill name to load"),
  },
  async execute(args, ctx) {
    const skill = skills.get(args.skill);
    if (!skill) {
      return `Skill "${args.skill}" not found. Available: ${[...skills.keys()].join(", ")}`;
    }

    // Format skill content
    const content = `<skill name="${skill.name}">
  <metadata>
    <source>${skill.label}</source>
    <directory>${skill.path}</directory>
  </metadata>
  <content>
${skill.template}
  </content>
</skill>`;

    // Inject as synthetic message (persists across compaction)
    await client.session.prompt({
      path: { id: ctx.sessionID },
      body: {
        noReply: true, // Don't trigger AI response
        parts: [{
          type: "text",
          text: content,
          synthetic: true,
        }],
      },
    });

    return `Skill "${skill.name}" loaded successfully.`;
  },
});
```

---

## Model-Aware Rendering

Pattern from `opencode-skillful`:

```typescript
type RenderFormat = "xml" | "json" | "md";

const MODEL_FORMATS: Record<string, RenderFormat> = {
  "claude-3": "xml",
  "gpt-4": "json",
  "gemini": "md",
};

function getFormatForModel(providerId: string, modelId: string): RenderFormat {
  const key = `${providerId}-${modelId}`;
  if (key in MODEL_FORMATS) return MODEL_FORMATS[key];

  // Provider-level fallback
  if (providerId === "anthropic") return "xml";
  if (providerId === "openai") return "json";

  return "md"; // Default
}

function renderSkill(skill: Skill, format: RenderFormat): string {
  switch (format) {
    case "xml":
      return `<skill name="${skill.name}">
  <description>${skill.description}</description>
  <content>${skill.template}</content>
</skill>`;

    case "json":
      return JSON.stringify({ skill }, null, 2);

    case "md":
    default:
      return `## ${skill.name}\n\n${skill.description}\n\n${skill.template}`;
  }
}
```

---

## Skill Directory Structure

```
my-skill/
├── SKILL.md                    # Main skill definition (required)
├── references/                 # Documentation & guides
│   ├── api-docs.md
│   └── best-practices.md
├── scripts/                    # Executable scripts
│   ├── build.sh
│   └── test.sh
└── assets/                     # Templates & output files
    └── template.md
```

---

## Discovery Priority

| Priority | Path | Scope |
|----------|------|-------|
| 1 (highest) | `.opencode/skills/` | Project |
| 2 | `.claude/skills/` | Project (Claude compat) |
| 3 | `~/.config/opencode/skills/` | User |
| 4 (lowest) | `~/.claude/skills/` | User (Claude compat) |

First match wins - project skills override user skills.

---

## Source Reference

- `opencode-skillful/src/` - Full skill system
- `agent-skills/src/` - Alternative implementation
- `openskills/` - CLI installer
