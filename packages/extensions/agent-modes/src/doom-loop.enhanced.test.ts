import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import { beforeEach, describe, expect, it } from 'vitest'

import { configureEnhanced, registerDoomLoop, resetDoomLoop } from './doom-loop.js'

function register(api: unknown): void {
  registerDoomLoop(api as never)
}

describe('enhanced stuck detection', () => {
  beforeEach(() => {
    resetDoomLoop()
    configureEnhanced({
      repeatedCallThreshold: 3,
      errorCycleThreshold: 3,
      emptyTurnThreshold: 3,
      monologueThreshold: 5,
      tokenWasteRatio: 0.5,
      tokenWasteMinTurns: 3,
      selfAssessmentEveryTurns: 4,
    })
  })

  it('detects repeated identical tool calls from turn:end', () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    register(api)

    for (let turn = 1; turn <= 3; turn++) {
      api.emit('turn:end', {
        agentId: 'a1',
        turn,
        toolCalls: [{ name: 'read_file', args: { path: 'a.ts' }, success: true, result: 'ok' }],
      })
    }

    const stuck = emittedEvents.find((e) => e.event === 'stuck:detected')
    expect(stuck).toBeDefined()
    expect((stuck!.data as { scenario: string }).scenario).toBe('repeated-tool-call')
  })

  it('detects error cycling for repeated identical errors', () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    register(api)

    for (let turn = 1; turn <= 3; turn++) {
      api.emit('turn:end', {
        agentId: 'a1',
        turn,
        toolCalls: [
          {
            name: 'bash',
            args: { command: 'bad' },
            success: false,
            result: 'command failed: denied',
          },
        ],
      })
    }

    const stuck = emittedEvents.find(
      (e) =>
        e.event === 'stuck:detected' &&
        (e.data as { scenario: string }).scenario === 'error-cycling'
    )
    expect(stuck).toBeDefined()
  })

  it('detects empty/no-op turn loops', () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    register(api)

    for (let turn = 1; turn <= 3; turn++) {
      api.emit('turn:end', { agentId: 'a1', turn, toolCalls: [] })
    }

    const stuck = emittedEvents.find(
      (e) =>
        e.event === 'stuck:detected' &&
        (e.data as { scenario: string }).scenario === 'empty-response-loop'
    )
    expect(stuck).toBeDefined()
  })

  it('detects monologue loop after configured threshold', () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    register(api)

    for (let turn = 1; turn <= 5; turn++) {
      api.emit('turn:end', { agentId: 'a1', turn, toolCalls: [] })
    }

    const stuck = emittedEvents.find(
      (e) =>
        e.event === 'stuck:detected' &&
        (e.data as { scenario: string }).scenario === 'monologue-loop'
    )
    expect(stuck).toBeDefined()
  })

  it('detects token waste with no progress turns and high spend ratio', () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    register(api)

    for (let turn = 1; turn <= 4; turn++) {
      api.emit('turn:end', { agentId: 'a1', turn, toolCalls: [] })
      api.emit('llm:usage', { sessionId: 'a1', inputTokens: 1_000, outputTokens: 400 })
    }

    const stuck = emittedEvents.find(
      (e) =>
        e.event === 'stuck:detected' && (e.data as { scenario: string }).scenario === 'token-waste'
    )
    expect(stuck).toBeDefined()
  })

  it('runs self-assessment fallback every configured N turns', () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    register(api)

    for (let turn = 1; turn <= 4; turn++) {
      api.emit('turn:end', { agentId: 'a1', turn, toolCalls: [] })
    }

    const stuck = emittedEvents.find(
      (e) =>
        e.event === 'stuck:detected' &&
        (e.data as { scenario: string }).scenario === 'self-assessment'
    )
    expect(stuck).toBeDefined()
  })

  it('detects alternating pairs pattern (A,B repeated 3x)', () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    register(api)

    api.emit('turn:end', {
      agentId: 'a2',
      turn: 1,
      toolCalls: [
        { name: 'read_file', args: { path: 'a.ts' }, success: true, result: 'ok' },
        { name: 'grep', args: { pattern: 'foo' }, success: true, result: 'ok' },
      ],
    })
    api.emit('turn:end', {
      agentId: 'a2',
      turn: 2,
      toolCalls: [
        { name: 'read_file', args: { path: 'a.ts' }, success: true, result: 'ok' },
        { name: 'grep', args: { pattern: 'foo' }, success: true, result: 'ok' },
      ],
    })
    api.emit('turn:end', {
      agentId: 'a2',
      turn: 3,
      toolCalls: [
        { name: 'read_file', args: { path: 'a.ts' }, success: true, result: 'ok' },
        { name: 'grep', args: { pattern: 'foo' }, success: true, result: 'ok' },
      ],
    })

    const stuck = emittedEvents.find(
      (e) =>
        e.event === 'stuck:detected' &&
        (e.data as { scenario: string }).scenario === 'alternating-pairs'
    )
    expect(stuck).toBeDefined()
  })

  it('does not detect alternating pairs for mixed calls', () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    register(api)

    api.emit('turn:end', {
      agentId: 'a3',
      turn: 1,
      toolCalls: [
        { name: 'read_file', args: { path: 'a.ts' }, success: true, result: 'ok' },
        { name: 'grep', args: { pattern: 'foo' }, success: true, result: 'ok' },
      ],
    })
    api.emit('turn:end', {
      agentId: 'a3',
      turn: 2,
      toolCalls: [
        { name: 'read_file', args: { path: 'b.ts' }, success: true, result: 'ok' },
        { name: 'grep', args: { pattern: 'foo' }, success: true, result: 'ok' },
      ],
    })
    api.emit('turn:end', {
      agentId: 'a3',
      turn: 3,
      toolCalls: [
        { name: 'read_file', args: { path: 'a.ts' }, success: true, result: 'ok' },
        { name: 'grep', args: { pattern: 'bar' }, success: true, result: 'ok' },
      ],
    })

    const stuck = emittedEvents.find(
      (e) =>
        e.event === 'stuck:detected' &&
        (e.data as { scenario: string }).scenario === 'alternating-pairs'
    )
    expect(stuck).toBeUndefined()
  })

  it('detects context window loop after consecutive compactions', () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    register(api)

    for (let i = 0; i < 5; i++) {
      api.emit('context:compacting', { sessionId: 'a4' })
      api.emit('turn:end', { agentId: 'a4', turn: i + 1, toolCalls: [] })
    }

    const stuck = emittedEvents.find(
      (e) =>
        e.event === 'stuck:detected' &&
        (e.data as { scenario: string }).scenario === 'context-window-loop'
    )
    expect(stuck).toBeDefined()
  })

  it('does not detect context window loop when productive work occurs between compactions', () => {
    const { api, emittedEvents } = createMockExtensionAPI()
    register(api)

    for (let i = 0; i < 3; i++) {
      api.emit('context:compacting', { sessionId: 'a5' })
      api.emit('turn:end', { agentId: 'a5', turn: i + 1, toolCalls: [] })
    }

    api.emit('turn:end', {
      agentId: 'a5',
      turn: 4,
      toolCalls: [{ name: 'read_file', args: { path: 'x.ts' }, success: true, result: 'ok' }],
    })

    for (let i = 0; i < 2; i++) {
      api.emit('context:compacting', { sessionId: 'a5' })
      api.emit('turn:end', { agentId: 'a5', turn: i + 5, toolCalls: [] })
    }

    const stuck = emittedEvents.find(
      (e) =>
        e.event === 'stuck:detected' &&
        (e.data as { scenario: string }).scenario === 'context-window-loop'
    )
    expect(stuck).toBeUndefined()
  })
})
