/**
 * Verified Summarization Strategy Tests
 */

import { describe, expect, it, vi } from 'vitest'
import type { Message, SummarizeFn } from '../types.js'
import {
  createVerifiedSummarize,
  extractStateSnapshot,
  STATE_SNAPSHOT_CLOSE_TAG,
  STATE_SNAPSHOT_TAG,
} from './verified-summarize.js'

// ============================================================================
// Helpers
// ============================================================================

function msg(role: 'user' | 'assistant' | 'system', content: string, id?: string): Message {
  return {
    id: id ?? `msg-${Math.random().toString(36).slice(2, 8)}`,
    sessionId: 'test',
    role,
    content,
    createdAt: Date.now(),
  }
}

/** Create a realistic conversation */
function createConversation(turns: number): Message[] {
  const messages: Message[] = []
  for (let i = 0; i < turns; i++) {
    messages.push(msg('user', `User question about task ${i}`))
    messages.push(msg('assistant', `Assistant response with details for task ${i}`))
  }
  return messages
}

/** Mock summarize function that returns a short summary */
function createMockSummarizer(response?: string): SummarizeFn {
  return vi.fn(async () => response ?? 'Summary of conversation context.')
}

// ============================================================================
// Tests
// ============================================================================

describe('verified-summarize', () => {
  describe('extractStateSnapshot', () => {
    it('should extract content between snapshot tags', () => {
      const content = `Some text ${STATE_SNAPSHOT_TAG}\nHello World\n${STATE_SNAPSHOT_CLOSE_TAG} more text`
      const snapshot = extractStateSnapshot(content)
      expect(snapshot).toBe('Hello World')
    })

    it('should return null for missing tags', () => {
      expect(extractStateSnapshot('no tags here')).toBeNull()
    })

    it('should return null for mismatched tags', () => {
      expect(extractStateSnapshot(`${STATE_SNAPSHOT_TAG}no closing`)).toBeNull()
    })

    it('should handle multiline snapshot content', () => {
      const content = `${STATE_SNAPSHOT_TAG}
Line 1
Line 2
Line 3
${STATE_SNAPSHOT_CLOSE_TAG}`

      const snapshot = extractStateSnapshot(content)
      expect(snapshot).toContain('Line 1')
      expect(snapshot).toContain('Line 3')
    })
  })

  describe('createVerifiedSummarize', () => {
    it('should return empty array for empty input', async () => {
      const strategy = createVerifiedSummarize({
        summarizeFn: createMockSummarizer(),
      })

      const result = await strategy.compact([], 10000)
      expect(result).toEqual([])
    })

    it('should return original for small conversations', async () => {
      const strategy = createVerifiedSummarize({
        summarizeFn: createMockSummarizer(),
      })

      const messages = [msg('user', 'Hi'), msg('assistant', 'Hello')]

      const result = await strategy.compact(messages, 10000)
      expect(result).toEqual(messages)
    })

    it('should summarize older messages', async () => {
      const summarizeFn = createMockSummarizer('Task was about file editing.')
      const strategy = createVerifiedSummarize({
        summarizeFn,
        preserveFraction: 0.3,
        enableVerification: false,
        sessionId: 'test',
      })

      const messages = createConversation(10) // 20 messages
      const result = await strategy.compact(messages, 10000)

      // Should have fewer messages than original
      expect(result.length).toBeLessThan(messages.length)

      // Should have a snapshot message
      const snapshotMsg = result.find((m) => m.content.includes('[Conversation State Snapshot]'))
      expect(snapshotMsg).toBeDefined()
      expect(snapshotMsg!.content).toContain(STATE_SNAPSHOT_TAG)
    })

    it('should preserve system message', async () => {
      const strategy = createVerifiedSummarize({
        summarizeFn: createMockSummarizer('Summary'),
        preserveFraction: 0.3,
        enableVerification: false,
      })

      const messages = [msg('system', 'You are helpful.'), ...createConversation(10)]

      const result = await strategy.compact(messages, 10000)

      // System message should be first
      expect(result[0]!.role).toBe('system')
      expect(result[0]!.content).toBe('You are helpful.')
    })

    it('should call summarizeFn with older messages', async () => {
      const summarizeFn = createMockSummarizer('Summary')
      const strategy = createVerifiedSummarize({
        summarizeFn,
        preserveFraction: 0.3,
        enableVerification: false,
      })

      const messages = createConversation(10)
      await strategy.compact(messages, 10000)

      expect(summarizeFn).toHaveBeenCalled()
    })

    it('should run verification when enabled', async () => {
      const summarizeFn = createMockSummarizer('Initial summary')
      const verifyFn = createMockSummarizer('Improved summary with more details')

      const strategy = createVerifiedSummarize({
        summarizeFn,
        verifyFn,
        enableVerification: true,
        preserveFraction: 0.3,
      })

      const messages = createConversation(10)
      const result = await strategy.compact(messages, 10000)

      // Both functions should be called
      expect(summarizeFn).toHaveBeenCalled()
      expect(verifyFn).toHaveBeenCalled()

      // Snapshot should contain verified content
      const snapshotMsg = result.find((m) => m.content.includes(STATE_SNAPSHOT_TAG))
      expect(snapshotMsg).toBeDefined()
    })

    it('should skip verification if disabled', async () => {
      const summarizeFn = createMockSummarizer('Summary only')
      const verifyFn = createMockSummarizer()

      const strategy = createVerifiedSummarize({
        summarizeFn,
        verifyFn,
        enableVerification: false,
        preserveFraction: 0.3,
      })

      const messages = createConversation(10)
      await strategy.compact(messages, 10000)

      expect(summarizeFn).toHaveBeenCalled()
      expect(verifyFn).not.toHaveBeenCalled()
    })

    it('should reject empty summary', async () => {
      const strategy = createVerifiedSummarize({
        summarizeFn: createMockSummarizer(''),
        enableVerification: false,
        preserveFraction: 0.3,
      })

      const messages = createConversation(10)
      const result = await strategy.compact(messages, 10000)

      // Should return original on failed summarization
      expect(result).toEqual(messages)
    })

    it('should reject inflated summary', async () => {
      // Summary is larger than original messages - should be rejected
      const summarizeFn = vi.fn(async (msgs: Message[]) => {
        const originalSize = msgs.map((m) => m.content).join('').length
        return 'x'.repeat(originalSize * 2) // 2x the size
      })

      const strategy = createVerifiedSummarize({
        summarizeFn,
        enableVerification: false,
        preserveFraction: 0.3,
      })

      const messages = createConversation(10)
      const result = await strategy.compact(messages, 10000)

      // Should return original since summary was inflated
      expect(result).toEqual(messages)
    })

    it('should reject inflated verification', async () => {
      const summarizeFn = createMockSummarizer('Short summary')
      // Verification returns something 10x larger
      const verifyFn = createMockSummarizer('x'.repeat(10000))

      const strategy = createVerifiedSummarize({
        summarizeFn,
        verifyFn,
        enableVerification: true,
        preserveFraction: 0.3,
      })

      const messages = createConversation(10)
      const result = await strategy.compact(messages, 10000)

      // Should still produce result (falling back to original summary)
      const snapshotMsg = result.find((m) => m.content.includes(STATE_SNAPSHOT_TAG))
      expect(snapshotMsg).toBeDefined()
      // Should use original, not inflated verification
      expect(snapshotMsg!.content).toContain('Short summary')
    })

    it('should integrate existing state snapshot', async () => {
      const summarizeFn = vi.fn(createMockSummarizer('Updated summary'))

      const strategy = createVerifiedSummarize({
        summarizeFn,
        enableVerification: false,
        preserveFraction: 0.3,
      })

      const messages = [
        msg('system', `${STATE_SNAPSHOT_TAG}\nOld context\n${STATE_SNAPSHOT_CLOSE_TAG}`),
        ...createConversation(10),
      ]

      await strategy.compact(messages, 10000)

      // Summarize function should receive the old snapshot context
      const callArgs = (summarizeFn as ReturnType<typeof vi.fn>).mock.calls[0]![0] as Message[]
      const hasOldSnapshot = callArgs.some((m: Message) => m.content.includes('Old context'))
      expect(hasOldSnapshot).toBe(true)
    })

    it('should have correct strategy name', () => {
      const strategy = createVerifiedSummarize({
        summarizeFn: createMockSummarizer(),
      })
      expect(strategy.name).toBe('verified-summarize')
    })

    it('should preserve recent messages intact', async () => {
      const strategy = createVerifiedSummarize({
        summarizeFn: createMockSummarizer('Summary'),
        preserveFraction: 0.3,
        enableVerification: false,
      })

      const messages = createConversation(10)
      const result = await strategy.compact(messages, 10000)

      // Last few messages should be preserved exactly
      const lastOriginal = messages[messages.length - 1]!
      const lastResult = result[result.length - 1]!
      expect(lastResult.content).toBe(lastOriginal.content)
    })
  })
})
