import { beforeEach, describe, expect, it } from 'vitest'
import { createMockExtensionAPI } from '../../../core-v2/src/__test-utils__/mock-extension-api.js'

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
})
