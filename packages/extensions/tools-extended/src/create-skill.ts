/**
 * create_skill tool — creates a skill file with YAML frontmatter.
 *
 * Creates `.ava/skills/<name>/SKILL.md` and emits `skills:register` for immediate activation.
 */

import { emitEvent } from '@ava/core-v2/extensions'
import { getPlatform } from '@ava/core-v2/platform'
import { defineTool, resolvePathSafe } from '@ava/core-v2/tools'
import * as z from 'zod'

const activationSchema = z.enum(['auto', 'agent', 'always', 'manual']).default('auto')

export const createSkillTool = defineTool({
  name: 'create_skill',
  description:
    'Create a skill file (.ava/skills/<name>/SKILL.md) with YAML frontmatter. Skills are domain-specific knowledge modules that inject into the system prompt.',
  schema: z.object({
    name: z
      .string()
      .regex(/^[a-z0-9-]+$/, 'Must be kebab-case (lowercase, hyphens only)')
      .describe('Skill name in kebab-case (e.g. "react-patterns")'),
    description: z.string().describe('Short description of this skill'),
    globs: z
      .array(z.string())
      .min(1)
      .describe('File glob patterns that trigger this skill (e.g. ["**/*.tsx"])'),
    activation: activationSchema.describe(
      'When to activate: auto (file globs match), agent (LLM can call load_skill), always (every turn), manual (explicit load_skill only)'
    ),
    content: z.string().describe('The skill content / domain knowledge'),
  }),
  permissions: ['write'],
  locations: (input) => [{ path: `.ava/skills/${input.name}/SKILL.md`, type: 'write' }],
  async execute(input, ctx) {
    if (ctx.signal?.aborted) {
      return { success: false, output: '', error: 'Aborted' }
    }

    const fs = getPlatform().fs
    const filePath = await resolvePathSafe(
      `.ava/skills/${input.name}/SKILL.md`,
      ctx.workingDirectory
    )

    // Build YAML frontmatter
    const frontmatterLines = ['---', `name: ${input.name}`, `description: ${input.description}`]
    if (input.activation !== 'auto') {
      frontmatterLines.push(`activation: ${input.activation}`)
    }
    frontmatterLines.push('globs:')
    for (const glob of input.globs) {
      frontmatterLines.push(`  - "${glob}"`)
    }
    frontmatterLines.push('---')

    const fileContent = `${frontmatterLines.join('\n')}\n${input.content}\n`

    // Ensure directory exists
    const parentDir = filePath.substring(0, filePath.lastIndexOf('/'))
    try {
      await fs.mkdir(parentDir)
    } catch (err) {
      const code = (err as NodeJS.ErrnoException)?.code
      if (code && code !== 'EEXIST' && code !== 'EISDIR') {
        return {
          success: false,
          output: '',
          error: `Failed to create directory ${parentDir}: ${(err as Error).message}`,
        }
      }
    }

    await fs.writeFile(filePath, fileContent)

    // Emit for immediate activation
    emitEvent('skills:register', {
      name: input.name,
      description: input.description,
      globs: input.globs,
      activation: input.activation,
      content: input.content,
      source: filePath,
    })

    return {
      success: true,
      output: `Created skill "${input.name}" at ${filePath} (activation: ${input.activation})`,
    }
  },
})
