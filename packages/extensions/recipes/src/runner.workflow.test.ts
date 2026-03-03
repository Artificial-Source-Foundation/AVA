import { describe, expect, it } from 'vitest'
import { createMockExtensionAPI } from '../../../core-v2/src/__test-utils__/mock-extension-api.js'

import { executeRecipe } from './runner.js'
import type { Recipe } from './types.js'

function execute(recipe: Recipe, api: unknown) {
  return executeRecipe(recipe, {}, api as never)
}

function registerResponder(
  eventHandlers: ReturnType<typeof createMockExtensionAPI>['eventHandlers'],
  event: string,
  fn: (payload: Record<string, unknown>) => void
): void {
  let handlers = eventHandlers.get(event)
  if (!handlers) {
    handlers = new Set()
    eventHandlers.set(event, handlers)
  }
  handlers.add((data: unknown) => fn(data as Record<string, unknown>))
}

describe('executeRecipe workflow behavior', () => {
  it('retries a failed step and succeeds within maxAttempts', async () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    let attempts = 0

    registerResponder(eventHandlers, 'recipe:execute-tool', (payload) => {
      attempts++
      if (attempts < 2) throw new Error('temporary failure')
      const respond = payload.respond as ((result: string) => void) | undefined
      respond?.('ok')
    })

    const recipe: Recipe = {
      name: 'retry-success',
      steps: [{ name: 'step1', tool: 'bash', retry: { maxAttempts: 3 } }],
    }

    const result = await execute(recipe, api)
    expect(result.success).toBe(true)
    expect(result.steps[0]?.success).toBe(true)
    expect(attempts).toBe(2)
  })

  it('fails when retries are exhausted', async () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    let attempts = 0

    registerResponder(eventHandlers, 'recipe:execute-tool', () => {
      attempts++
      throw new Error('always failing')
    })

    const recipe: Recipe = {
      name: 'retry-fail',
      steps: [{ name: 'step1', tool: 'bash', retry: { maxAttempts: 2 } }],
    }

    const result = await execute(recipe, api)
    expect(result.success).toBe(false)
    expect(result.steps[0]?.success).toBe(false)
    expect(attempts).toBe(2)
  })

  it('emits recipe:execute-recipe for sub-recipe steps', async () => {
    const { api, emittedEvents, eventHandlers } = createMockExtensionAPI()

    registerResponder(eventHandlers, 'recipe:execute-recipe', (payload) => {
      const respond = payload.respond as ((result: string) => void) | undefined
      respond?.('subrecipe-ok')
    })

    const recipe: Recipe = {
      name: 'sub-recipe',
      steps: [{ name: 'call-sub', recipe: 'bootstrap' }],
    }

    const result = await execute(recipe, api)
    expect(result.success).toBe(true)

    const event = emittedEvents.find((e) => e.event === 'recipe:execute-recipe')
    expect(event).toBeDefined()
    expect((event!.data as { recipe: string }).recipe).toBe('bootstrap')
  })

  it('aborts remaining steps when onError is abort', async () => {
    const { api, eventHandlers } = createMockExtensionAPI()

    registerResponder(eventHandlers, 'recipe:execute-tool', (payload) => {
      if (payload.step === 'fail') throw new Error('stop-now')
      const respond = payload.respond as ((result: string) => void) | undefined
      respond?.('ok')
    })

    const recipe: Recipe = {
      name: 'abort-flow',
      steps: [
        { name: 'fail', tool: 'bash', onError: 'abort' },
        { name: 'should-not-run', tool: 'bash' },
      ],
    }

    const result = await execute(recipe, api)
    expect(result.success).toBe(false)
    expect(result.steps).toHaveLength(1)
    expect(result.steps[0]?.name).toBe('fail')
  })

  it('continues after failure when onError is continue', async () => {
    const { api, eventHandlers } = createMockExtensionAPI()

    registerResponder(eventHandlers, 'recipe:execute-tool', (payload) => {
      if (payload.step === 'fail') throw new Error('recoverable')
      const respond = payload.respond as ((result: string) => void) | undefined
      respond?.('ok')
    })

    const recipe: Recipe = {
      name: 'continue-flow',
      steps: [
        { name: 'fail', tool: 'bash', onError: 'continue' },
        { name: 'next', tool: 'bash' },
      ],
    }

    const result = await execute(recipe, api)
    expect(result.success).toBe(false)
    expect(result.steps).toHaveLength(2)
    expect(result.steps[0]?.success).toBe(false)
    expect(result.steps[1]?.success).toBe(true)
  })
})
