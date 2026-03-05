import type { ChatMessage } from '@ava/core-v2/llm'
import { afterEach, describe, expect, it, vi } from 'vitest'

import { summarizeStrategy } from './summarize.js'
import { targetForWindow, tieredCompactionStrategy } from './tiered-compaction.js'
import { truncateStrategy } from './truncate.js'

function msg(role: ChatMessage['role'], content: ChatMessage['content']): ChatMessage {
  return { role, content }
}

function extractFirstToolResultContent(messages: ChatMessage[]): string {
  for (const message of messages) {
    if (typeof message.content === 'string') continue
    for (const block of message.content) {
      if (block.type === 'tool_result') {
        return block.content
      }
    }
  }
  throw new Error('No tool_result content found')
}

describe('tiered-compaction strategy', () => {
  afterEach(() => {
    vi.restoreAllMocks()
  })

  it('tier 1 truncates oversized tool output to first 500 and last 500 chars', () => {
    const head = 'A'.repeat(500)
    const middle = 'MIDDLE-MARKER-SECTION'.repeat(120)
    const tail = 'Z'.repeat(500)
    const messages: ChatMessage[] = [
      msg('assistant', [{ type: 'tool_use', id: 'tool-1', name: 'read_file', input: {} }]),
      msg('user', [
        { type: 'tool_result', tool_use_id: 'tool-1', content: `${head}${middle}${tail}` },
      ]),
    ]

    const compacted = tieredCompactionStrategy.compact(messages, 64_000)
    const toolOutput = extractFirstToolResultContent(compacted)

    expect(toolOutput.startsWith(head)).toBe(true)
    expect(toolOutput.includes(tail)).toBe(true)
    expect(toolOutput).not.toContain('MIDDLE-MARKER-SECTION')
    expect(toolOutput).toContain('[tool output truncated by tiered compaction]')
  })

  it('tier 2 sliding window drops oldest turns and uses model thresholds', () => {
    expect(targetForWindow(64_000)).toBe(37_000)
    expect(targetForWindow(128_000)).toBe(98_000)
    expect(targetForWindow(200_000)).toBe(160_000)

    const messages: ChatMessage[] = [msg('system', 'system-instructions')]
    for (let i = 0; i < 20; i += 1) {
      messages.push(msg(i % 2 === 0 ? 'user' : 'assistant', `turn-${i}-${'x'.repeat(20_000)}`))
    }

    const compacted = tieredCompactionStrategy.compact(messages, 64_000)
    const merged = compacted.map((item) => JSON.stringify(item)).join('\n')

    expect(merged).not.toContain('turn-0-')
    expect(merged).toContain('turn-19-')
    expect(compacted[0]?.role).toBe('system')
  })

  it('tier 3 summarize fallback preserves file paths and key decisions', () => {
    const oversized = [msg('user', 'y'.repeat(700_000))]
    vi.spyOn(truncateStrategy, 'compact').mockReturnValue(oversized)
    vi.spyOn(summarizeStrategy, 'compact').mockReturnValue([
      msg(
        'system',
        'Summary: Updated src/core/router.ts, touched src/components/panels/DiffReview.tsx, decision: keep strict parsing for git tool output.'
      ),
      msg('assistant', 'recent context retained'),
    ])

    const compacted = tieredCompactionStrategy.compact([msg('system', 'sys')], 128_000)
    const summaryText = String(compacted[0]?.content)

    expect(summaryText).toContain('src/core/router.ts')
    expect(summaryText).toContain('src/components/panels/DiffReview.tsx')
    expect(summaryText).toContain('keep strict parsing')
  })

  it('runs tiers in order with cheap tiers before summarize fallback', () => {
    const truncateSpy = vi
      .spyOn(truncateStrategy, 'compact')
      .mockReturnValue([msg('assistant', 'x'.repeat(600_000))])
    const summarizeSpy = vi
      .spyOn(summarizeStrategy, 'compact')
      .mockReturnValue([msg('system', 'Summary of earlier conversation')])

    const messages: ChatMessage[] = [
      msg('assistant', [{ type: 'tool_use', id: 't1', name: 'read_file', input: {} }]),
      msg('user', [{ type: 'tool_result', tool_use_id: 't1', content: 'a'.repeat(3_500) }]),
    ]

    tieredCompactionStrategy.compact(messages, 64_000)

    expect(truncateSpy).toHaveBeenCalledTimes(1)
    expect(summarizeSpy).toHaveBeenCalledTimes(1)
    expect(truncateSpy.mock.invocationCallOrder[0]).toBeLessThan(
      summarizeSpy.mock.invocationCallOrder[0] ?? Number.MAX_SAFE_INTEGER
    )

    const truncateInput = truncateSpy.mock.calls[0]?.[0] ?? []
    const truncatedToolOutput = extractFirstToolResultContent(truncateInput as ChatMessage[])
    expect(truncatedToolOutput.length).toBeLessThan(2_000)
    expect(truncatedToolOutput).toContain('[tool output truncated by tiered compaction]')
  })

  it('handles empty message arrays', () => {
    expect(tieredCompactionStrategy.compact([], 64_000)).toEqual([])
  })
})
