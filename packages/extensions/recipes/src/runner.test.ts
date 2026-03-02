import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import { describe, expect, it } from 'vitest'
import { evaluateCondition, executeRecipe } from './runner.js'
import type { Recipe } from './types.js'

// ─── Helper ──────────────────────────────────────────────────────────────────

/**
 * Install a handler that auto-responds to recipe execution events.
 * Calls `respond()` with the given result string.
 */
function installAutoResponder(
  _api: ReturnType<typeof createMockExtensionAPI>['api'],
  eventHandlers: ReturnType<typeof createMockExtensionAPI>['eventHandlers'],
  result = 'ok'
): void {
  const handler = (data: unknown) => {
    const d = data as Record<string, unknown>
    if (typeof d.respond === 'function') {
      ;(d.respond as (r: string) => void)(result)
    }
  }

  // Register handler for all recipe execution events
  for (const event of ['recipe:execute-tool', 'recipe:execute-command', 'recipe:execute-goal']) {
    let handlers = eventHandlers.get(event)
    if (!handlers) {
      handlers = new Set()
      eventHandlers.set(event, handlers)
    }
    handlers.add(handler)
  }
}

// ─── Tests ───────────────────────────────────────────────────────────────────

describe('executeRecipe', () => {
  it('executes a single-step recipe', async () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    installAutoResponder(api, eventHandlers, 'build-output')

    const recipe: Recipe = {
      name: 'build',
      steps: [{ name: 'compile', tool: 'bash', args: { command: 'npm run build' } }],
    }

    const result = await executeRecipe(recipe, {}, api)
    expect(result.recipe).toBe('build')
    expect(result.success).toBe(true)
    expect(result.steps).toHaveLength(1)
    expect(result.steps[0]!.name).toBe('compile')
    expect(result.steps[0]!.success).toBe(true)
    expect(result.steps[0]!.result).toBe('build-output')
  })

  it('executes multiple sequential steps', async () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    installAutoResponder(api, eventHandlers, 'done')

    const recipe: Recipe = {
      name: 'ci',
      steps: [
        { name: 'lint', tool: 'bash', args: { command: 'npm run lint' } },
        { name: 'test', tool: 'bash', args: { command: 'npm test' } },
        { name: 'build', tool: 'bash', args: { command: 'npm run build' } },
      ],
    }

    const result = await executeRecipe(recipe, {}, api)
    expect(result.success).toBe(true)
    expect(result.steps).toHaveLength(3)
    expect(result.steps.map((s) => s.name)).toEqual(['lint', 'test', 'build'])
  })

  it('executes parallel steps concurrently', async () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    installAutoResponder(api, eventHandlers, 'parallel-done')

    const recipe: Recipe = {
      name: 'parallel-build',
      steps: [
        { name: 'lint', tool: 'bash', parallel: true },
        { name: 'typecheck', tool: 'bash', parallel: true },
        { name: 'deploy', tool: 'bash' },
      ],
    }

    const result = await executeRecipe(recipe, {}, api)
    expect(result.success).toBe(true)
    expect(result.steps).toHaveLength(3)
    // All steps should complete
    expect(result.steps.every((s) => s.success)).toBe(true)
  })

  it('handles step errors gracefully', async () => {
    const { api, eventHandlers } = createMockExtensionAPI()

    // Install a handler that throws for 'fail' step
    const handler = (data: unknown) => {
      const d = data as Record<string, unknown>
      if (d.step === 'fail') {
        throw new Error('Step failed deliberately')
      }
      if (typeof d.respond === 'function') {
        ;(d.respond as (r: string) => void)('ok')
      }
    }
    for (const event of ['recipe:execute-tool', 'recipe:execute-command', 'recipe:execute-goal']) {
      let handlers = eventHandlers.get(event)
      if (!handlers) {
        handlers = new Set()
        eventHandlers.set(event, handlers)
      }
      handlers.add(handler)
    }

    const recipe: Recipe = {
      name: 'failing',
      steps: [
        { name: 'ok-step', tool: 'bash' },
        { name: 'fail', tool: 'bash' },
      ],
    }

    const result = await executeRecipe(recipe, {}, api)
    expect(result.success).toBe(false)
    expect(result.steps[0]!.success).toBe(true)
    expect(result.steps[1]!.success).toBe(false)
    expect(result.steps[1]!.error).toContain('Step failed deliberately')
  })

  it('skips steps when condition is not met', async () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    installAutoResponder(api, eventHandlers, 'ok')

    const recipe: Recipe = {
      name: 'conditional',
      steps: [{ name: 'deploy', tool: 'bash', condition: 'steps.build.success' }],
    }

    // No 'build' step ran, so condition should fail
    const result = await executeRecipe(recipe, {}, api)
    expect(result.success).toBe(true)
    expect(result.steps[0]!.result).toBe('Skipped (condition not met)')
  })

  it('runs conditional step when condition is met', async () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    installAutoResponder(api, eventHandlers, 'ok')

    const recipe: Recipe = {
      name: 'conditional',
      steps: [
        { name: 'build', tool: 'bash' },
        { name: 'deploy', tool: 'bash', condition: 'steps.build.success' },
      ],
    }

    const result = await executeRecipe(recipe, {}, api)
    expect(result.success).toBe(true)
    expect(result.steps[1]!.result).toBe('ok')
  })

  it('substitutes params in step args', async () => {
    const { api, emittedEvents, eventHandlers } = createMockExtensionAPI()
    installAutoResponder(api, eventHandlers, 'ok')

    const recipe: Recipe = {
      name: 'parameterized',
      params: [{ name: 'target' }],
      steps: [{ name: 'build', tool: 'bash', args: { dir: '{{target}}' } }],
    }

    await executeRecipe(recipe, { target: 'production' }, api)

    // Find the emitted tool event
    const toolEvent = emittedEvents.find((e) => e.event === 'recipe:execute-tool')
    expect(toolEvent).toBeDefined()
    const eventData = toolEvent!.data as Record<string, unknown>
    expect((eventData.args as Record<string, string>).dir).toBe('production')
  })

  it('emits events for command steps', async () => {
    const { api, emittedEvents, eventHandlers } = createMockExtensionAPI()
    installAutoResponder(api, eventHandlers, 'ok')

    const recipe: Recipe = {
      name: 'cmd-recipe',
      steps: [{ name: 'format', command: '/format' }],
    }

    await executeRecipe(recipe, {}, api)

    const cmdEvent = emittedEvents.find((e) => e.event === 'recipe:execute-command')
    expect(cmdEvent).toBeDefined()
    expect((cmdEvent!.data as Record<string, unknown>).command).toBe('/format')
  })

  it('emits events for goal steps', async () => {
    const { api, emittedEvents, eventHandlers } = createMockExtensionAPI()
    installAutoResponder(api, eventHandlers, 'ok')

    const recipe: Recipe = {
      name: 'goal-recipe',
      steps: [{ name: 'implement', goal: 'Build auth feature' }],
    }

    await executeRecipe(recipe, {}, api)

    const goalEvent = emittedEvents.find((e) => e.event === 'recipe:execute-goal')
    expect(goalEvent).toBeDefined()
    expect((goalEvent!.data as Record<string, unknown>).goal).toBe('Build auth feature')
  })

  it('records timing for each step', async () => {
    const { api, eventHandlers } = createMockExtensionAPI()
    installAutoResponder(api, eventHandlers, 'ok')

    const recipe: Recipe = {
      name: 'timed',
      steps: [{ name: 'step1', tool: 'bash' }],
    }

    const result = await executeRecipe(recipe, {}, api)
    expect(result.startedAt).toBeLessThanOrEqual(result.completedAt)
    expect(result.steps[0]!.duration).toBeGreaterThanOrEqual(0)
  })
})

describe('evaluateCondition', () => {
  it('returns true for steps.X.success when step exists', () => {
    const results = new Map([['build', 'ok']])
    expect(evaluateCondition('steps.build.success', results)).toBe(true)
  })

  it('returns false for steps.X.success when step does not exist', () => {
    const results = new Map<string, string>()
    expect(evaluateCondition('steps.build.success', results)).toBe(false)
  })

  it('returns true for steps.X.result when step has non-empty result', () => {
    const results = new Map([['build', 'output']])
    expect(evaluateCondition('steps.build.result', results)).toBe(true)
  })

  it('returns false for steps.X.result when step has empty result', () => {
    const results = new Map([['build', '']])
    expect(evaluateCondition('steps.build.result', results)).toBe(false)
  })

  it('falls back to checking step name existence', () => {
    const results = new Map([['build', 'ok']])
    expect(evaluateCondition('build', results)).toBe(true)
    expect(evaluateCondition('deploy', results)).toBe(false)
  })
})
