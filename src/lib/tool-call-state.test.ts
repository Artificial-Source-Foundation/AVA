import { describe, expect, it } from 'vitest'
import type { Message } from '../types'
import {
  extractToolCallsFromMetadata,
  mergeMessagesWithExisting,
  mergeMessageWithBackend,
  normalizeToolCalls,
} from './tool-call-state'

describe('tool-call-state', () => {
  it('normalizes legacy arguments payloads into args', () => {
    const toolCalls = normalizeToolCalls([
      {
        id: 'tool-1',
        name: 'read',
        arguments: { path: '/tmp/file.txt' },
        status: 'success',
        startedAt: 0,
      },
    ])

    expect(toolCalls).toEqual([
      expect.objectContaining({
        id: 'tool-1',
        name: 'read',
        args: { path: '/tmp/file.txt' },
        status: 'success',
        startedAt: 0,
        filePath: '/tmp/file.txt',
      }),
    ])
  })

  it('extracts normalized tool calls from message metadata', () => {
    const toolCalls = extractToolCallsFromMetadata({
      toolCalls: [{ id: 'tool-1', name: 'bash', arguments: { command: 'pwd' }, startedAt: 0 }],
    })

    expect(toolCalls?.[0]?.args).toEqual({ command: 'pwd' })
  })

  it('keeps richer local tool calls when backend sync is thinner', () => {
    const existing: Message = {
      id: 'assistant-local',
      sessionId: 'session-1',
      role: 'assistant',
      content: 'Done',
      createdAt: 1,
      toolCalls: [
        {
          id: 'tool-1',
          name: 'bash',
          args: { command: 'pwd' },
          status: 'success',
          startedAt: 10,
          completedAt: 20,
          output: '/workspace',
          contentOffset: 4,
        },
      ],
    }
    const incoming: Message = {
      id: 'assistant-backend',
      sessionId: 'session-1',
      role: 'assistant',
      content: 'Done',
      createdAt: 1,
      toolCalls: [
        {
          id: 'tool-1',
          name: 'bash',
          args: { command: 'pwd' },
          status: 'success',
          startedAt: 0,
        },
      ],
    }

    expect(mergeMessageWithBackend(existing, incoming).toolCalls).toEqual(existing.toolCalls)
  })

  it('preserves richer existing tool calls when recovered content changes', () => {
    const existing: Message = {
      id: 'assistant-final',
      sessionId: 'session-1',
      role: 'assistant',
      content: 'partial answer',
      createdAt: 1,
      toolCalls: [
        {
          id: 'tool-1',
          name: 'bash',
          args: { command: 'pwd' },
          status: 'success',
          startedAt: 10,
          completedAt: 20,
          output: '/workspace',
          contentOffset: 42,
        },
      ],
      metadata: {
        toolCalls: [
          {
            id: 'tool-1',
            name: 'bash',
            args: { command: 'pwd' },
            status: 'success',
            startedAt: 10,
            completedAt: 20,
            output: '/workspace',
            contentOffset: 42,
          },
        ],
      },
    }
    const incoming: Message = {
      id: 'assistant-final',
      sessionId: 'session-1',
      role: 'assistant',
      content: 'authoritative final answer',
      createdAt: 2,
      toolCalls: [
        {
          id: 'tool-1',
          name: 'bash',
          args: { command: 'pwd' },
          status: 'success',
          startedAt: 0,
        },
      ],
      metadata: {
        toolCalls: [
          {
            id: 'tool-1',
            name: 'bash',
            arguments: { command: 'pwd' },
            status: 'success',
          },
        ],
      },
    }

    const [merged] = mergeMessagesWithExisting([existing], [incoming])

    expect(merged).toMatchObject({
      id: 'assistant-final',
      content: 'authoritative final answer',
      toolCalls: [
        expect.objectContaining({
          id: 'tool-1',
          name: 'bash',
          args: { command: 'pwd' },
          status: 'success',
          output: '/workspace',
          startedAt: 10,
          completedAt: 20,
          contentOffset: 42,
        }),
      ],
      metadata: {
        toolCalls: [
          expect.objectContaining({
            id: 'tool-1',
            name: 'bash',
            args: { command: 'pwd' },
            output: '/workspace',
            contentOffset: 42,
          }),
        ],
      },
    })
  })
})
