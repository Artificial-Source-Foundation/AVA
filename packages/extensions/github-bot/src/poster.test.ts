import { describe, expect, it } from 'vitest'
import { buildCommentCommand, formatAckComment, formatResultComment } from './poster.js'
import type { BotResult, BotTask } from './types.js'

const mockTask: BotTask = {
  id: 'task-1',
  repo: 'test/repo',
  issueNumber: 42,
  isPR: true,
  task: 'Fix the login bug',
  triggerUser: 'alice',
  triggerUrl: 'https://github.com/test/repo/pull/42#comment-1',
  createdAt: Date.now(),
}

describe('poster', () => {
  describe('formatResultComment', () => {
    it('formats successful result', () => {
      const result: BotResult = {
        taskId: 'task-1',
        success: true,
        summary: 'Fixed the login validation logic',
        duration: 15000,
      }

      const comment = formatResultComment(mockTask, result)
      expect(comment).toContain(':white_check_mark:')
      expect(comment).toContain('15.0s')
      expect(comment).toContain('Fixed the login validation logic')
      expect(comment).toContain('Fix the login bug')
    })

    it('formats failed result with error', () => {
      const result: BotResult = {
        taskId: 'task-1',
        success: false,
        summary: 'Task failed',
        error: 'Could not find the file',
        duration: 5000,
      }

      const comment = formatResultComment(mockTask, result)
      expect(comment).toContain(':x:')
      expect(comment).toContain('Could not find the file')
    })

    it('includes files changed section', () => {
      const result: BotResult = {
        taskId: 'task-1',
        success: true,
        summary: 'Done',
        filesChanged: ['src/auth.ts', 'src/login.ts'],
        duration: 10000,
      }

      const comment = formatResultComment(mockTask, result)
      expect(comment).toContain('Files changed (2)')
      expect(comment).toContain('`src/auth.ts`')
      expect(comment).toContain('`src/login.ts`')
    })

    it('truncates long task descriptions', () => {
      const longTask = { ...mockTask, task: 'x'.repeat(300) }
      const result: BotResult = { taskId: 't', success: true, summary: 'ok', duration: 1000 }

      const comment = formatResultComment(longTask, result)
      expect(comment).toContain('...')
    })

    it('attributes trigger user', () => {
      const result: BotResult = { taskId: 't', success: true, summary: 'ok', duration: 1000 }
      const comment = formatResultComment(mockTask, result)
      expect(comment).toContain('@alice')
    })
  })

  describe('buildCommentCommand', () => {
    it('builds PR comment command', () => {
      const cmd = buildCommentCommand('test/repo', 42, 'Hello', true)
      expect(cmd).toContain('gh pr comment 42')
      expect(cmd).toContain('--repo test/repo')
    })

    it('builds issue comment command', () => {
      const cmd = buildCommentCommand('test/repo', 10, 'Hello', false)
      expect(cmd).toContain('gh issue comment 10')
    })

    it('escapes single quotes in body', () => {
      const cmd = buildCommentCommand('test/repo', 1, "It's a test", false)
      expect(cmd).toContain("'\\''")
    })
  })

  describe('formatAckComment', () => {
    it('formats acknowledgment comment', () => {
      const comment = formatAckComment(mockTask)
      expect(comment).toContain('AVA is working on this')
      expect(comment).toContain('Fix the login bug')
      expect(comment).toContain('@alice')
    })
  })
})
