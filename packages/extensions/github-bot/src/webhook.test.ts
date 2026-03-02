import { describe, expect, it } from 'vitest'
import type { GitHubWebhookPayload } from './types.js'
import {
  extractTask,
  isRepoAllowed,
  isUserAllowed,
  parseWebhookEvent,
  verifySignature,
} from './webhook.js'

describe('webhook', () => {
  describe('verifySignature', () => {
    it('verifies valid HMAC-SHA256 signature', () => {
      const { createHmac } = require('node:crypto')
      const secret = 'test-secret'
      const payload = '{"test": true}'
      const hash = createHmac('sha256', secret).update(payload).digest('hex')
      const signature = `sha256=${hash}`

      expect(verifySignature(payload, signature, secret)).toBe(true)
    })

    it('rejects invalid signature', () => {
      expect(verifySignature('body', 'sha256=invalid', 'secret')).toBe(false)
    })

    it('rejects non-sha256 prefix', () => {
      expect(verifySignature('body', 'sha1=abc', 'secret')).toBe(false)
    })

    it('rejects empty signature', () => {
      expect(verifySignature('body', '', 'secret')).toBe(false)
    })
  })

  describe('extractTask', () => {
    it('extracts task after @ava mention', () => {
      expect(extractTask('@ava fix the login bug')).toBe('fix the login bug')
    })

    it('extracts multiline task', () => {
      const comment = '@ava please review this PR\nand check for security issues'
      const task = extractTask(comment)
      expect(task).toContain('please review this PR')
    })

    it('returns null for no @ava mention', () => {
      expect(extractTask('Just a regular comment')).toBeNull()
    })

    it('returns null for bare @ava without task', () => {
      expect(extractTask('@ava')).toBeNull()
    })

    it('is case-insensitive', () => {
      expect(extractTask('@AVA fix this')).toBe('fix this')
    })
  })

  describe('parseWebhookEvent', () => {
    const basePayload: GitHubWebhookPayload = {
      action: 'created',
      comment: {
        id: 1,
        body: '@ava fix the bug',
        user: { login: 'testuser' },
        html_url: 'https://github.com/test/repo/issues/1#comment-1',
        created_at: '2024-01-01T00:00:00Z',
      },
      issue: {
        number: 1,
        title: 'Bug report',
        body: 'Something is broken',
        html_url: 'https://github.com/test/repo/issues/1',
        labels: [],
      },
      repository: {
        full_name: 'test/repo',
        clone_url: 'https://github.com/test/repo.git',
        default_branch: 'main',
      },
      sender: { login: 'testuser' },
    }

    it('parses issue comment with @ava mention', () => {
      const task = parseWebhookEvent(basePayload)
      expect(task).not.toBeNull()
      expect(task!.task).toBe('fix the bug')
      expect(task!.repo).toBe('test/repo')
      expect(task!.issueNumber).toBe(1)
      expect(task!.isPR).toBe(false)
    })

    it('parses PR comment', () => {
      const prPayload: GitHubWebhookPayload = {
        ...basePayload,
        pull_request: {
          number: 42,
          title: 'Fix things',
          body: 'PR body',
          html_url: 'https://github.com/test/repo/pull/42',
          head: { ref: 'fix-branch', sha: 'abc123' },
          base: { ref: 'main' },
          diff_url: 'https://github.com/test/repo/pull/42.diff',
        },
      }

      const task = parseWebhookEvent(prPayload)
      expect(task).not.toBeNull()
      expect(task!.isPR).toBe(true)
      expect(task!.issueNumber).toBe(42)
    })

    it('returns null for non-created actions', () => {
      const payload = { ...basePayload, action: 'edited' }
      expect(parseWebhookEvent(payload)).toBeNull()
    })

    it('returns null when no comment', () => {
      const payload = { ...basePayload, comment: undefined }
      expect(parseWebhookEvent(payload)).toBeNull()
    })

    it('returns null when no @ava mention', () => {
      const payload = {
        ...basePayload,
        comment: { ...basePayload.comment!, body: 'Just a comment' },
      }
      expect(parseWebhookEvent(payload)).toBeNull()
    })
  })

  describe('isUserAllowed', () => {
    it('allows all users when no list specified', () => {
      expect(isUserAllowed('anyone')).toBe(true)
      expect(isUserAllowed('anyone', [])).toBe(true)
    })

    it('allows listed users', () => {
      expect(isUserAllowed('alice', ['alice', 'bob'])).toBe(true)
    })

    it('rejects unlisted users', () => {
      expect(isUserAllowed('eve', ['alice', 'bob'])).toBe(false)
    })
  })

  describe('isRepoAllowed', () => {
    it('allows all repos when no list specified', () => {
      expect(isRepoAllowed('any/repo')).toBe(true)
    })

    it('allows listed repos', () => {
      expect(isRepoAllowed('org/repo', ['org/repo'])).toBe(true)
    })

    it('rejects unlisted repos', () => {
      expect(isRepoAllowed('other/repo', ['org/repo'])).toBe(false)
    })
  })
})
