/**
 * create_pr tool — creates a GitHub pull request using the gh CLI.
 *
 * Requires `gh` to be installed and authenticated.
 */

import { getPlatform } from '@ava/core-v2/platform'
import { defineTool } from '@ava/core-v2/tools'
import * as z from 'zod'

export const createPrTool = defineTool({
  name: 'create_pr',
  description:
    'Create a GitHub pull request using the gh CLI. Requires gh to be installed and authenticated.',
  schema: z.object({
    title: z.string().describe('PR title'),
    body: z.string().optional().describe('PR body/description'),
    base: z.string().optional().describe('Base branch (default: main)'),
    head: z.string().optional().describe('Head branch (default: current branch)'),
    draft: z.boolean().optional().describe('Create as draft PR'),
  }),
  permissions: ['execute'],
  async execute(input, ctx) {
    if (ctx.signal?.aborted) {
      return { success: false, output: '', error: 'Aborted' }
    }

    const shell = getPlatform().shell
    const cwd = ctx.workingDirectory

    // Build the gh pr create command
    const parts = ['gh', 'pr', 'create', '--title', escapeShellArg(input.title)]
    if (input.body) parts.push('--body', escapeShellArg(input.body))
    if (input.base) parts.push('--base', input.base)
    if (input.head) parts.push('--head', input.head)
    if (input.draft) parts.push('--draft')

    const command = `cd "${cwd}" && ${parts.join(' ')}`

    try {
      const result = await shell.exec(command)
      if (result.exitCode !== 0) {
        return {
          success: false,
          output: '',
          error: result.stderr.trim() || `gh pr create failed with exit code ${result.exitCode}`,
        }
      }
      return { success: true, output: result.stdout.trim() }
    } catch (err) {
      return {
        success: false,
        output: '',
        error: err instanceof Error ? err.message : 'Failed to create PR',
      }
    }
  },
})

function escapeShellArg(arg: string): string {
  return `'${arg.replace(/'/g, "'\\''")}'`
}
