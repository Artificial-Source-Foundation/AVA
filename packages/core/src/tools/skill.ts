/**
 * Skill Tool
 * Load reusable knowledge modules into LLM context
 *
 * Based on OpenCode's skill tool pattern
 */

import { z } from 'zod'
import { getSkillByName, getSkills, listSkillNames } from '../skills/index.js'
import { defineTool } from './define.js'
import type { ToolResult } from './types.js'

// ============================================================================
// Schema
// ============================================================================

const SkillSchema = z.object({
  name: z.string().describe('Name of the skill to load'),
  list: z
    .boolean()
    .optional()
    .describe('If true, list all available skills instead of loading one'),
})

type SkillParams = z.infer<typeof SkillSchema>

// ============================================================================
// Tool Implementation
// ============================================================================

export const skillTool = defineTool({
  name: 'skill',
  description: `Load a reusable knowledge module into context.

Skills are markdown files with specialized instructions, patterns, or knowledge.
They help maintain consistent approaches across tasks.

Usage:
- Load a skill by name: { "name": "typescript-patterns" }
- List available skills: { "name": "", "list": true }

Skill locations (searched in order):
1. .ava/skills/ (project-local)
2. ~/.ava/skills/ (user-global)

Example skill file (.ava/skills/typescript/SKILL.md):
\`\`\`markdown
---
name: typescript-patterns
description: TypeScript best practices
globs: ["**/*.ts", "**/*.tsx"]
---

# TypeScript Patterns

When writing TypeScript:
- Always use strict mode
- Prefer interfaces over types for public APIs
- Use const assertions for literal types
\`\`\``,

  schema: SkillSchema,

  permissions: ['read'],

  async execute(params: SkillParams, ctx): Promise<ToolResult> {
    // Check abort signal
    if (ctx.signal.aborted) {
      return {
        success: false,
        output: 'Operation was cancelled',
        error: 'ABORTED',
      }
    }

    // List mode
    if (params.list) {
      try {
        const skills = await getSkills(ctx.workingDirectory)

        if (skills.length === 0) {
          return {
            success: true,
            output: `No skills found.

Skills are loaded from:
- .ava/skills/*/SKILL.md (project-local)
- ~/.ava/skills/*/SKILL.md (user-global)

Create a skill by adding a SKILL.md file with YAML frontmatter:
\`\`\`markdown
---
name: my-skill
description: My custom skill
---

# Skill Content
Your instructions here...
\`\`\``,
          }
        }

        const lines = ['## Available Skills', '']
        for (const skill of skills) {
          const desc = skill.description ? ` - ${skill.description}` : ''
          const globs = skill.globs ? ` [${skill.globs.join(', ')}]` : ''
          lines.push(`- **${skill.name}**${desc}${globs}`)
        }

        return {
          success: true,
          output: lines.join('\n'),
          metadata: {
            skillCount: skills.length,
            skillNames: skills.map((s) => s.name),
          },
        }
      } catch (err) {
        const message = err instanceof Error ? err.message : String(err)
        return {
          success: false,
          output: `Failed to list skills: ${message}`,
          error: 'DISCOVERY_ERROR',
        }
      }
    }

    // Load mode
    if (!params.name || params.name.trim() === '') {
      return {
        success: false,
        output: 'Skill name is required. Use { "list": true } to see available skills.',
        error: 'MISSING_NAME',
      }
    }

    try {
      const skill = await getSkillByName(params.name, ctx.workingDirectory)

      if (!skill) {
        const availableNames = await listSkillNames(ctx.workingDirectory)
        let output = `Skill "${params.name}" not found.`

        if (availableNames.length > 0) {
          output += `\n\nAvailable skills: ${availableNames.join(', ')}`
        } else {
          output += '\n\nNo skills are currently available.'
        }

        return {
          success: false,
          output,
          error: 'SKILL_NOT_FOUND',
        }
      }

      // Build output with skill content
      const lines = [`## Skill: ${skill.name}`]

      if (skill.description) {
        lines.push(``)
        lines.push(`*${skill.description}*`)
      }

      if (skill.globs && skill.globs.length > 0) {
        lines.push(``)
        lines.push(`**Auto-activates for:** ${skill.globs.join(', ')}`)
      }

      lines.push(``)
      lines.push(`---`)
      lines.push(``)
      lines.push(skill.content)

      // Stream metadata if available
      if (ctx.metadata) {
        ctx.metadata({
          title: `Loaded skill: ${skill.name}`,
          metadata: {
            skillName: skill.name,
            skillPath: skill.path,
            contentLength: skill.content.length,
          },
        })
      }

      return {
        success: true,
        output: lines.join('\n'),
        metadata: {
          skillName: skill.name,
          skillPath: skill.path,
          skillDescription: skill.description,
          skillGlobs: skill.globs,
          contentLength: skill.content.length,
        },
      }
    } catch (err) {
      const message = err instanceof Error ? err.message : String(err)
      return {
        success: false,
        output: `Failed to load skill "${params.name}": ${message}`,
        error: 'LOAD_ERROR',
      }
    }
  },
})
