import { isAbsolute, resolve } from 'node:path'
import { getPlatform } from '@ava/core-v2/platform'
import { defineTool } from '@ava/core-v2/tools'
import { z } from 'zod'

import type { HunkReviewState } from './hunk-review/state.js'

const DiffReviewSchema = z.object({
  action: z.enum(['list', 'status', 'accept', 'reject', 'apply']),
  sessionId: z.string().optional(),
  path: z.string().optional(),
  hunkId: z.string().optional(),
})

function getAddedLines(content: string): string[] {
  return content
    .split('\n')
    .filter((line) => line.startsWith('+') && !line.startsWith('+++'))
    .map((line) => line.slice(1))
}

function toLines(content: string): string[] {
  if (content.length === 0) {
    return []
  }

  const normalized = content.endsWith('\n') ? content.slice(0, -1) : content
  return normalized.length > 0 ? normalized.split('\n') : []
}

function toContent(lines: string[]): string {
  return lines.join('\n')
}

function applyHunks(
  content: string,
  hunks: Array<{ oldStart: number; oldLines: number; newLines: number; content: string }>
): string {
  const lines = toLines(content)
  const ordered = [...hunks].sort((a, b) => a.oldStart - b.oldStart)
  let offset = 0

  for (const hunk of ordered) {
    const startIndex = Math.max(0, hunk.oldStart - 1 + offset)
    const replacement = getAddedLines(hunk.content).slice(0, hunk.newLines)
    lines.splice(startIndex, hunk.oldLines, ...replacement)
    offset += replacement.length - hunk.oldLines
  }

  return toContent(lines)
}

export function createDiffReviewTool(state: HunkReviewState) {
  return defineTool({
    name: 'diff_review',
    description: 'Review diff hunks and mark each as accepted/rejected',
    schema: DiffReviewSchema,
    permissions: ['read', 'write'],
    async execute(input, ctx) {
      const sessionId = input.sessionId ?? ctx.sessionId

      if (input.action === 'list') {
        const items = state.list(sessionId, input.path)
        return {
          success: true,
          output: JSON.stringify(items, null, 2),
          metadata: { items } as unknown as Record<string, unknown>,
        }
      }

      if (input.action === 'status') {
        const summary = state.summary(sessionId)
        return {
          success: true,
          output: JSON.stringify(summary, null, 2),
          metadata: summary as unknown as Record<string, unknown>,
        }
      }

      if (input.action === 'apply') {
        const items = state.list(sessionId, input.path)
        const accepted = items.filter((item) => item.status === 'accepted')

        if (accepted.length === 0) {
          return {
            success: true,
            output: 'No accepted hunks to apply',
            metadata: {
              appliedHunks: 0,
              appliedFiles: 0,
              skippedHunks: items.length,
            },
          }
        }

        const hunksByPath = new Map<string, typeof accepted>()
        for (const item of accepted) {
          const list = hunksByPath.get(item.path) ?? []
          list.push(item)
          hunksByPath.set(item.path, list)
        }

        const fs = getPlatform().fs
        for (const [path, hunks] of hunksByPath) {
          const absolutePath = isAbsolute(path) ? path : resolve(ctx.workingDirectory, path)
          const current = await fs.readFile(absolutePath)
          const updated = applyHunks(current, hunks)
          await fs.writeFile(absolutePath, updated)
        }

        return {
          success: true,
          output: `Applied ${accepted.length} accepted hunk${accepted.length === 1 ? '' : 's'} across ${hunksByPath.size} file${hunksByPath.size === 1 ? '' : 's'}`,
          metadata: {
            appliedHunks: accepted.length,
            appliedFiles: hunksByPath.size,
            skippedHunks: items.length - accepted.length,
          },
        }
      }

      if (!input.hunkId) {
        return {
          success: false,
          output: '',
          error: 'hunkId is required for accept/reject actions',
        }
      }

      const ok = state.updateStatus(
        sessionId,
        input.hunkId,
        input.action === 'accept' ? 'accepted' : 'rejected'
      )

      if (!ok) {
        return {
          success: false,
          output: '',
          error: `Hunk not found: ${input.hunkId}`,
        }
      }

      return {
        success: true,
        output: `${input.hunkId} marked ${input.action === 'accept' ? 'accepted' : 'rejected'}`,
        metadata: state.summary(sessionId) as unknown as Record<string, unknown>,
      }
    },
  })
}
