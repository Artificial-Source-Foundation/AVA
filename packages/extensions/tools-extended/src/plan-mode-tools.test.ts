import { afterEach, describe, expect, it } from 'vitest'
import { resetPlanMode } from '../../agent-modes/src/plan-mode.js'
import { planEnterTool, planExitTool } from './plan-mode-tools.js'

const mockCtx = {
  sessionId: 'test-session',
  workingDirectory: '/tmp',
  signal: new AbortController().signal,
}

describe('planEnterTool', () => {
  afterEach(() => {
    resetPlanMode()
  })

  it('has correct definition', () => {
    expect(planEnterTool.definition.name).toBe('plan_enter')
  })

  it('enters plan mode', async () => {
    const result = await planEnterTool.execute({}, mockCtx)
    expect(result.success).toBe(true)
    expect(result.output).toContain('Entered plan mode')
  })

  it('returns error if already in plan mode', async () => {
    await planEnterTool.execute({}, mockCtx)
    const result = await planEnterTool.execute({}, mockCtx)
    expect(result.success).toBe(false)
    expect(result.output).toContain('Already in plan mode')
  })

  it('accepts optional reason', async () => {
    const result = await planEnterTool.execute({ reason: 'research first' }, mockCtx)
    expect(result.success).toBe(true)
  })
})

describe('planExitTool', () => {
  afterEach(() => {
    resetPlanMode()
  })

  it('has correct definition', () => {
    expect(planExitTool.definition.name).toBe('plan_exit')
  })

  it('exits plan mode', async () => {
    await planEnterTool.execute({}, mockCtx)
    const result = await planExitTool.execute({}, mockCtx)
    expect(result.success).toBe(true)
    expect(result.output).toContain('Exited plan mode')
  })

  it('returns error if not in plan mode', async () => {
    const result = await planExitTool.execute({}, mockCtx)
    expect(result.success).toBe(false)
    expect(result.output).toContain('Not in plan mode')
  })
})
