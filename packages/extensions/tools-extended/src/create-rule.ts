/**
 * create_rule tool — creates a coding rule file with YAML frontmatter.
 *
 * Creates `.ava/rules/<name>.md` and emits `rules:register` for immediate activation.
 */

import { emitEvent } from '@ava/core-v2/extensions'
import { getPlatform } from '@ava/core-v2/platform'
import { defineTool, resolvePathSafe } from '@ava/core-v2/tools'
import * as z from 'zod'

const activationSchema = z.enum(['always', 'auto', 'manual']).default('auto')

export const createRuleTool = defineTool({
  name: 'create_rule',
  description:
    'Create a coding rule file (.ava/rules/<name>.md) with YAML frontmatter. Rules inject into the system prompt based on activation mode and file globs.',
  schema: z.object({
    name: z
      .string()
      .regex(/^[a-z0-9-]+$/, 'Must be kebab-case (lowercase, hyphens only)')
      .describe('Rule name in kebab-case (e.g. "testing-conventions")'),
    description: z.string().describe('Short description of what this rule enforces'),
    globs: z
      .array(z.string())
      .default([])
      .describe('File glob patterns this rule applies to (e.g. ["**/*.test.ts"])'),
    activation: activationSchema.describe(
      'When to activate: always (every turn), auto (when files match globs), manual (only via explicit load)'
    ),
    content: z.string().describe('The rule content / coding instructions'),
  }),
  permissions: ['write'],
  locations: (input) => [{ path: `.ava/rules/${input.name}.md`, type: 'write' }],
  async execute(input, ctx) {
    if (ctx.signal?.aborted) {
      return { success: false, output: '', error: 'Aborted' }
    }

    const fs = getPlatform().fs
    const filePath = await resolvePathSafe(`.ava/rules/${input.name}.md`, ctx.workingDirectory)

    // Build YAML frontmatter
    const frontmatterLines = ['---', `description: ${input.description}`]
    if (input.activation !== 'auto') {
      frontmatterLines.push(`activation: ${input.activation}`)
    }
    if (input.globs.length > 0) {
      frontmatterLines.push('globs:')
      for (const glob of input.globs) {
        frontmatterLines.push(`  - "${glob}"`)
      }
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
    emitEvent('rules:register', {
      name: input.name,
      description: input.description,
      globs: input.globs,
      activation: input.activation,
      content: input.content,
      source: filePath,
    })

    return {
      success: true,
      output: `Created rule "${input.name}" at ${filePath} (activation: ${input.activation})`,
    }
  },
})
