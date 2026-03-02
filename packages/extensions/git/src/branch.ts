/**
 * Branch management tools — create_branch and switch_branch.
 */

import { getPlatform } from '@ava/core-v2/platform'
import { defineTool } from '@ava/core-v2/tools'
import * as z from 'zod'

const BRANCH_NAME_REGEX = /^[a-zA-Z0-9._\-/]+$/

export const createBranchTool = defineTool({
  name: 'create_branch',
  description: 'Create a new git branch and switch to it.',
  schema: z.object({
    name: z.string().describe('Branch name to create'),
    from: z
      .string()
      .optional()
      .describe('Base branch or commit to branch from (default: current HEAD)'),
  }),
  permissions: ['execute'],
  async execute(input, ctx) {
    if (ctx.signal?.aborted) {
      return { success: false, output: '', error: 'Aborted' }
    }

    if (!BRANCH_NAME_REGEX.test(input.name)) {
      return {
        success: false,
        output: '',
        error: `Invalid branch name: ${input.name}. Use alphanumeric, dots, hyphens, underscores, or slashes.`,
      }
    }

    const shell = getPlatform().shell
    const cwd = ctx.workingDirectory

    const args = ['git', 'checkout', '-b', input.name]
    if (input.from) args.push(input.from)

    const command = `cd "${cwd}" && ${args.join(' ')}`

    try {
      const result = await shell.exec(command)
      if (result.exitCode !== 0) {
        return {
          success: false,
          output: '',
          error: result.stderr.trim() || `Failed to create branch: ${input.name}`,
        }
      }
      const output = result.stderr.trim() || result.stdout.trim()
      return { success: true, output: output || `Switched to new branch '${input.name}'` }
    } catch (err) {
      return {
        success: false,
        output: '',
        error: err instanceof Error ? err.message : 'Failed to create branch',
      }
    }
  },
})

export const switchBranchTool = defineTool({
  name: 'switch_branch',
  description: 'Switch to an existing git branch.',
  schema: z.object({
    name: z.string().describe('Branch name to switch to'),
  }),
  permissions: ['execute'],
  async execute(input, ctx) {
    if (ctx.signal?.aborted) {
      return { success: false, output: '', error: 'Aborted' }
    }

    const shell = getPlatform().shell
    const cwd = ctx.workingDirectory
    const command = `cd "${cwd}" && git checkout ${input.name}`

    try {
      const result = await shell.exec(command)
      if (result.exitCode !== 0) {
        return {
          success: false,
          output: '',
          error: result.stderr.trim() || `Failed to switch to branch: ${input.name}`,
        }
      }
      const output = result.stderr.trim() || result.stdout.trim()
      return { success: true, output: output || `Switched to branch '${input.name}'` }
    } catch (err) {
      return {
        success: false,
        output: '',
        error: err instanceof Error ? err.message : 'Failed to switch branch',
      }
    }
  },
})
