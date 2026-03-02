/**
 * read_issue tool — reads a GitHub issue using the gh CLI.
 *
 * Returns issue title, body, comments, labels, and state.
 */

import { getPlatform } from '@ava/core-v2/platform'
import { defineTool } from '@ava/core-v2/tools'
import * as z from 'zod'

export interface GhIssue {
  title: string
  body: string
  state: string
  labels: Array<{ name: string }>
  comments: Array<{ body: string; author: { login: string } }>
}

export const readIssueTool = defineTool({
  name: 'read_issue',
  description:
    'Read a GitHub issue by number using the gh CLI. Returns title, body, comments, labels, and state.',
  schema: z.object({
    number: z.number().int().positive().describe('Issue number'),
    repo: z.string().optional().describe('Repository in owner/repo format (default: current repo)'),
  }),
  permissions: ['execute'],
  async execute(input, ctx) {
    if (ctx.signal?.aborted) {
      return { success: false, output: '', error: 'Aborted' }
    }

    const shell = getPlatform().shell
    const cwd = ctx.workingDirectory

    const parts = [
      'gh',
      'issue',
      'view',
      String(input.number),
      '--json',
      'title,body,comments,labels,state',
    ]
    if (input.repo) parts.push('--repo', input.repo)

    const command = `cd "${cwd}" && ${parts.join(' ')}`

    try {
      const result = await shell.exec(command)
      if (result.exitCode !== 0) {
        return {
          success: false,
          output: '',
          error: result.stderr.trim() || `Failed to read issue #${input.number}`,
        }
      }

      const json = result.stdout.trim()
      const issue = JSON.parse(json) as GhIssue

      const lines: string[] = [`# Issue #${input.number}: ${issue.title}`, `State: ${issue.state}`]

      if (issue.labels.length > 0) {
        lines.push(`Labels: ${issue.labels.map((l) => l.name).join(', ')}`)
      }

      lines.push('', '## Body', issue.body || '(no body)')

      if (issue.comments.length > 0) {
        lines.push('', '## Comments')
        for (const comment of issue.comments) {
          lines.push(``, `**${comment.author.login}:**`, comment.body)
        }
      }

      return { success: true, output: lines.join('\n') }
    } catch (err) {
      return {
        success: false,
        output: '',
        error: err instanceof Error ? err.message : 'Failed to read issue',
      }
    }
  },
})
