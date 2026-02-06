/**
 * Tool Truncation Strategy Tests
 */

import { describe, expect, it } from 'vitest'
import type { Message } from '../types.js'
import { createToolTruncation, truncateContent } from './tool-truncation.js'

// ============================================================================
// Helpers
// ============================================================================

function msg(role: 'user' | 'assistant' | 'system', content: string, id?: string): Message {
  return {
    id: id ?? `msg-${Math.random().toString(36).slice(2, 6)}`,
    sessionId: 'test',
    role,
    content,
    createdAt: Date.now(),
  }
}

function largeTool(lineCount: number): string {
  return Array.from({ length: lineCount }, (_, i) => `  line ${i + 1}: const x = ${i};`).join('\n')
}

// ============================================================================
// Tests
// ============================================================================

describe('tool-truncation', () => {
  describe('truncateContent', () => {
    it('should keep all lines if under limit', () => {
      const content = 'line 1\nline 2\nline 3'
      expect(truncateContent(content, 10)).toBe(content)
    })

    it('should truncate to last N lines', () => {
      const lines = Array.from({ length: 100 }, (_, i) => `line ${i}`)
      const content = lines.join('\n')

      const result = truncateContent(content, 5)

      expect(result).toContain('line 95')
      expect(result).toContain('line 99')
      expect(result).not.toContain('line 0\n')
      expect(result).toContain('[95 lines truncated]')
    })

    it('should add truncation marker', () => {
      const lines = Array.from({ length: 50 }, (_, i) => `line ${i}`)
      const result = truncateContent(lines.join('\n'), 10)

      expect(result).toContain('lines truncated')
      expect(result).toContain('output truncated')
    })
  })

  describe('createToolTruncation', () => {
    it('should return messages unchanged if no tool responses', async () => {
      const strategy = createToolTruncation()
      const messages = [msg('user', 'Hello'), msg('assistant', 'Hi there!')]

      const result = await strategy.compact(messages, 10000)
      expect(result).toEqual(messages)
    })

    it('should return empty array for empty input', async () => {
      const strategy = createToolTruncation()
      const result = await strategy.compact([], 10000)
      expect(result).toEqual([])
    })

    it('should truncate large assistant messages', async () => {
      const strategy = createToolTruncation({
        perResponseBudget: 100, // Very small budget
        truncateKeepLines: 5,
        preserveRecentCount: 0, // Don't protect any
      })

      const messages = [msg('user', 'Show me the file'), msg('assistant', largeTool(200))]

      const result = await strategy.compact(messages, 10000)

      // Should be truncated
      expect(result[1]!.content.length).toBeLessThan(messages[1]!.content.length)
      expect(result[1]!.content).toContain('lines truncated')
    })

    it('should preserve recent tool responses', async () => {
      const strategy = createToolTruncation({
        perResponseBudget: 100,
        truncateKeepLines: 5,
        preserveRecentCount: 1,
      })

      const oldToolOutput = largeTool(200)
      const newToolOutput = largeTool(200)

      const messages = [
        msg('user', 'Read old file'),
        msg('assistant', oldToolOutput, 'old-tool'),
        msg('user', 'Read new file'),
        msg('assistant', newToolOutput, 'new-tool'),
      ]

      const result = await strategy.compact(messages, 10000)

      // Old should be truncated
      expect(result[1]!.content.length).toBeLessThan(oldToolOutput.length)
      // New should be preserved (last 1 tool response protected)
      expect(result[3]!.content).toBe(newToolOutput)
    })

    it('should not truncate small assistant messages', async () => {
      const strategy = createToolTruncation({
        perResponseBudget: 100000,
        preserveRecentCount: 0,
      })

      const messages = [msg('user', 'What time is it?'), msg('assistant', 'It is 3pm.')]

      const result = await strategy.compact(messages, 10000)
      expect(result).toEqual(messages)
    })

    it('should preserve user and system messages', async () => {
      const strategy = createToolTruncation({
        perResponseBudget: 100,
        truncateKeepLines: 3,
        preserveRecentCount: 0,
      })

      const messages = [
        msg('system', 'You are a helpful assistant.'),
        msg('user', 'Do something'),
        msg('assistant', largeTool(100)),
      ]

      const result = await strategy.compact(messages, 10000)

      expect(result[0]!.role).toBe('system')
      expect(result[0]!.content).toBe('You are a helpful assistant.')
      expect(result[1]!.role).toBe('user')
      expect(result[1]!.content).toBe('Do something')
    })

    it('should return original messages if nothing truncated', async () => {
      const strategy = createToolTruncation({
        perResponseBudget: 1000000, // Huge budget
      })

      const messages = [msg('user', 'Hello'), msg('assistant', largeTool(20))]

      const result = await strategy.compact(messages, 10000)
      expect(result).toBe(messages) // Same reference (no copy)
    })

    it('should handle multiple tool responses', async () => {
      const strategy = createToolTruncation({
        perResponseBudget: 100,
        truncateKeepLines: 5,
        preserveRecentCount: 1,
      })

      const messages = [
        msg('user', 'Q1'),
        msg('assistant', largeTool(100)),
        msg('user', 'Q2'),
        msg('assistant', largeTool(100)),
        msg('user', 'Q3'),
        msg('assistant', largeTool(100)),
      ]

      const result = await strategy.compact(messages, 10000)

      // First two tool responses truncated, last one preserved
      expect(result[1]!.content).toContain('truncated')
      expect(result[3]!.content).toContain('truncated')
      expect(result[5]!.content).not.toContain('truncated')
    })
  })
})
