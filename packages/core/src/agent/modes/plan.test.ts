/**
 * Tests for Plan Mode
 * Research-only mode that restricts tool usage to read-only operations
 */

import { afterEach, describe, expect, it } from 'vitest'
import type { ToolContext } from '../../tools/types.js'
import {
  checkPlanModeAccess,
  clearAllPlanModeStates,
  enterPlanMode,
  exitPlanMode,
  getPlanModeState,
  getPlanModeStatus,
  getRestrictionReason,
  isPlanModeEnabled,
  isPlanModeRestricted,
  PLAN_MODE_ALLOWED_TOOLS,
  PLAN_MODE_BLOCKED_TOOLS,
  planEnterTool,
  planExitTool,
  setPlanModeState,
} from './plan.js'

// ============================================================================
// Test Helpers
// ============================================================================

function makeCtx(sessionId: string = 'test-session'): ToolContext {
  return {
    sessionId,
    workingDirectory: '/tmp/test',
    signal: new AbortController().signal,
  }
}

// ============================================================================
// State Management Tests
// ============================================================================

describe('State Management', () => {
  afterEach(() => {
    clearAllPlanModeStates()
  })

  it('getPlanModeState returns {enabled: false} for unknown session', () => {
    const state = getPlanModeState('nonexistent')
    expect(state).toEqual({ enabled: false })
  })

  it('getPlanModeState uses default session when no id provided', () => {
    enterPlanMode(undefined, 'test reason')
    const state = getPlanModeState()
    expect(state.enabled).toBe(true)
    expect(state.reason).toBe('test reason')
  })

  it('setPlanModeState creates new state', () => {
    setPlanModeState('session-1', { enabled: true, reason: 'testing' })
    const state = getPlanModeState('session-1')
    expect(state.enabled).toBe(true)
    expect(state.reason).toBe('testing')
  })

  it('setPlanModeState merges partial state', () => {
    setPlanModeState('session-1', { enabled: true, reason: 'initial' })
    setPlanModeState('session-1', { reason: 'updated' })

    const state = getPlanModeState('session-1')
    expect(state.enabled).toBe(true)
    expect(state.reason).toBe('updated')
  })

  it('isPlanModeEnabled returns false by default', () => {
    expect(isPlanModeEnabled()).toBe(false)
    expect(isPlanModeEnabled('any-session')).toBe(false)
  })

  it('isPlanModeEnabled returns true after enterPlanMode', () => {
    enterPlanMode('session-1')
    expect(isPlanModeEnabled('session-1')).toBe(true)
  })

  it('enterPlanMode sets all fields', () => {
    const before = Date.now()
    enterPlanMode('session-1', 'investigating bug')
    const after = Date.now()

    const state = getPlanModeState('session-1')
    expect(state.enabled).toBe(true)
    expect(state.reason).toBe('investigating bug')
    expect(state.sessionId).toBe('session-1')
    expect(state.enteredAt).toBeInstanceOf(Date)
    expect(state.enteredAt!.getTime()).toBeGreaterThanOrEqual(before)
    expect(state.enteredAt!.getTime()).toBeLessThanOrEqual(after)
  })

  it('exitPlanMode clears enabled and related fields', () => {
    enterPlanMode('session-1', 'research')

    exitPlanMode('session-1')

    const state = getPlanModeState('session-1')
    expect(state.enabled).toBe(false)
    expect(state.enteredAt).toBeUndefined()
    expect(state.reason).toBeUndefined()
  })

  it('clearAllPlanModeStates clears all sessions', () => {
    enterPlanMode('session-1', 'reason-1')
    enterPlanMode('session-2', 'reason-2')
    enterPlanMode('session-3', 'reason-3')

    expect(isPlanModeEnabled('session-1')).toBe(true)
    expect(isPlanModeEnabled('session-2')).toBe(true)
    expect(isPlanModeEnabled('session-3')).toBe(true)

    clearAllPlanModeStates()

    expect(isPlanModeEnabled('session-1')).toBe(false)
    expect(isPlanModeEnabled('session-2')).toBe(false)
    expect(isPlanModeEnabled('session-3')).toBe(false)
  })

  it('multiple sessions are independent', () => {
    enterPlanMode('session-a', 'reason-a')
    enterPlanMode('session-b', 'reason-b')

    expect(isPlanModeEnabled('session-a')).toBe(true)
    expect(isPlanModeEnabled('session-b')).toBe(true)

    exitPlanMode('session-a')

    expect(isPlanModeEnabled('session-a')).toBe(false)
    expect(isPlanModeEnabled('session-b')).toBe(true)
    expect(getPlanModeState('session-b').reason).toBe('reason-b')
  })
})

// ============================================================================
// Tool Restriction Tests
// ============================================================================

describe('Tool Restrictions', () => {
  afterEach(() => {
    clearAllPlanModeStates()
  })

  it('isPlanModeRestricted returns false when plan mode disabled', () => {
    expect(isPlanModeRestricted('write')).toBe(false)
    expect(isPlanModeRestricted('bash')).toBe(false)
    expect(isPlanModeRestricted('delete')).toBe(false)
  })

  it('isPlanModeRestricted returns false for allowed tools', () => {
    enterPlanMode('test')

    for (const tool of PLAN_MODE_ALLOWED_TOOLS) {
      expect(isPlanModeRestricted(tool, 'test')).toBe(false)
    }
  })

  it('isPlanModeRestricted returns true for blocked tools', () => {
    enterPlanMode('test')

    for (const tool of PLAN_MODE_BLOCKED_TOOLS) {
      expect(isPlanModeRestricted(tool, 'test')).toBe(true)
    }
  })

  it('isPlanModeRestricted returns true for unknown tools when plan mode enabled', () => {
    enterPlanMode('test')
    expect(isPlanModeRestricted('some_unknown_tool', 'test')).toBe(true)
  })

  it('isPlanModeRestricted with custom allowedTools config', () => {
    enterPlanMode('test')

    const config = { allowedTools: ['read', 'custom_tool'] }

    expect(isPlanModeRestricted('read', 'test', config)).toBe(false)
    expect(isPlanModeRestricted('custom_tool', 'test', config)).toBe(false)
    expect(isPlanModeRestricted('glob', 'test', config)).toBe(true)
    expect(isPlanModeRestricted('write', 'test', config)).toBe(true)
  })

  it('getRestrictionReason returns descriptive message for blocked tool', () => {
    const reason = getRestrictionReason('write')

    expect(reason).toContain('write')
    expect(reason).toContain('blocked in plan mode')
    expect(reason).toContain('plan_exit')
  })

  it('getRestrictionReason returns fallback message for unknown tool', () => {
    const reason = getRestrictionReason('some_random_tool')

    expect(reason).toContain('some_random_tool')
    expect(reason).toContain('not in the allowed list')
    expect(reason).toContain('Allowed tools:')
  })

  it('checkPlanModeAccess returns {allowed: true} when not restricted', () => {
    const result = checkPlanModeAccess('write')
    expect(result).toEqual({ allowed: true })
  })

  it('checkPlanModeAccess returns {allowed: true} for allowed tool in plan mode', () => {
    enterPlanMode('test')

    const result = checkPlanModeAccess('read', 'test')
    expect(result.allowed).toBe(true)
    expect(result.error).toBeUndefined()
  })

  it('checkPlanModeAccess returns {allowed: false, error} when restricted', () => {
    enterPlanMode('test')

    const result = checkPlanModeAccess('write', 'test')
    expect(result.allowed).toBe(false)
    expect(result.error).toBeDefined()
    expect(result.error!.success).toBe(false)
    expect(result.error!.error).toBe('PLAN_MODE_RESTRICTED')
    expect(result.error!.output).toContain('write')
  })
})

// ============================================================================
// Plan Mode Tools Tests
// ============================================================================

describe('planEnterTool', () => {
  afterEach(() => {
    clearAllPlanModeStates()
  })

  it('has correct tool definition', () => {
    expect(planEnterTool.definition.name).toBe('plan_enter')
    expect(planEnterTool.definition.description).toContain('plan mode')
  })

  it('execute enters plan mode with reason', async () => {
    const ctx = makeCtx('test-session')
    const result = await planEnterTool.execute({ reason: 'investigating auth bug' }, ctx)

    expect(result.success).toBe(true)
    expect(result.output).toContain('Entered plan mode')
    expect(result.output).toContain('investigating auth bug')
    expect(result.metadata?.planModeEnabled).toBe(true)
    expect(result.metadata?.reason).toBe('investigating auth bug')
    expect(isPlanModeEnabled('test-session')).toBe(true)
  })

  it('execute enters plan mode without reason', async () => {
    const ctx = makeCtx('test-session')
    const result = await planEnterTool.execute({}, ctx)

    expect(result.success).toBe(true)
    expect(result.output).toContain('Entered plan mode')
    expect(isPlanModeEnabled('test-session')).toBe(true)
  })

  it('execute fails when already in plan mode', async () => {
    const ctx = makeCtx('test-session')
    enterPlanMode('test-session', 'already active')

    const result = await planEnterTool.execute({ reason: 'second attempt' }, ctx)

    expect(result.success).toBe(false)
    expect(result.output).toContain('Already in plan mode')
    expect(result.error).toBe('ALREADY_IN_PLAN_MODE')
  })

  it('execute lists allowed tools in output', async () => {
    const ctx = makeCtx('test-session')
    const result = await planEnterTool.execute({}, ctx)

    expect(result.output).toContain('plan_exit')
    expect(result.metadata?.allowedTools).toEqual([...PLAN_MODE_ALLOWED_TOOLS])
  })
})

describe('planExitTool', () => {
  afterEach(() => {
    clearAllPlanModeStates()
  })

  it('has correct tool definition', () => {
    expect(planExitTool.definition.name).toBe('plan_exit')
    expect(planExitTool.definition.description).toContain('Exit plan mode')
  })

  it('execute exits plan mode with summary', async () => {
    const ctx = makeCtx('test-session')
    enterPlanMode('test-session', 'research')

    const result = await planExitTool.execute({ summary: 'Found the root cause' }, ctx)

    expect(result.success).toBe(true)
    expect(result.output).toContain('Exited plan mode')
    expect(result.output).toContain('Found the root cause')
    expect(result.output).toContain('All tools are now available')
    expect(result.metadata?.planModeEnabled).toBe(false)
    expect(result.metadata?.summary).toBe('Found the root cause')
    expect(isPlanModeEnabled('test-session')).toBe(false)
  })

  it('execute exits plan mode without summary', async () => {
    const ctx = makeCtx('test-session')
    enterPlanMode('test-session')

    const result = await planExitTool.execute({}, ctx)

    expect(result.success).toBe(true)
    expect(result.output).toContain('Exited plan mode')
    expect(isPlanModeEnabled('test-session')).toBe(false)
  })

  it('execute fails when not in plan mode', async () => {
    const ctx = makeCtx('test-session')

    const result = await planExitTool.execute({ summary: 'done' }, ctx)

    expect(result.success).toBe(false)
    expect(result.output).toContain('Not currently in plan mode')
    expect(result.error).toBe('NOT_IN_PLAN_MODE')
  })

  it('execute includes duration in output', async () => {
    const ctx = makeCtx('test-session')
    enterPlanMode('test-session')

    const result = await planExitTool.execute({}, ctx)

    expect(result.success).toBe(true)
    expect(result.output).toContain('Duration:')
    expect(result.output).toContain('minute(s)')
    expect(typeof result.metadata?.duration).toBe('number')
  })
})

// ============================================================================
// Constants Tests
// ============================================================================

describe('Constants', () => {
  it('PLAN_MODE_ALLOWED_TOOLS contains expected tools', () => {
    expect(PLAN_MODE_ALLOWED_TOOLS).toContain('read')
    expect(PLAN_MODE_ALLOWED_TOOLS).toContain('glob')
    expect(PLAN_MODE_ALLOWED_TOOLS).toContain('grep')
    expect(PLAN_MODE_ALLOWED_TOOLS).toContain('ls')
    expect(PLAN_MODE_ALLOWED_TOOLS).toContain('websearch')
    expect(PLAN_MODE_ALLOWED_TOOLS).toContain('webfetch')
    expect(PLAN_MODE_ALLOWED_TOOLS).toContain('todo_read')
    expect(PLAN_MODE_ALLOWED_TOOLS).toContain('plan_exit')
    expect(PLAN_MODE_ALLOWED_TOOLS).toContain('attempt_completion')
  })

  it('PLAN_MODE_BLOCKED_TOOLS contains expected tools', () => {
    expect(PLAN_MODE_BLOCKED_TOOLS).toContain('write')
    expect(PLAN_MODE_BLOCKED_TOOLS).toContain('create')
    expect(PLAN_MODE_BLOCKED_TOOLS).toContain('edit')
    expect(PLAN_MODE_BLOCKED_TOOLS).toContain('delete')
    expect(PLAN_MODE_BLOCKED_TOOLS).toContain('bash')
    expect(PLAN_MODE_BLOCKED_TOOLS).toContain('browser')
    expect(PLAN_MODE_BLOCKED_TOOLS).toContain('task')
    expect(PLAN_MODE_BLOCKED_TOOLS).toContain('todo_write')
    expect(PLAN_MODE_BLOCKED_TOOLS).toContain('question')
  })

  it('no overlap between allowed and blocked tools', () => {
    const overlap = PLAN_MODE_ALLOWED_TOOLS.filter((tool) => PLAN_MODE_BLOCKED_TOOLS.includes(tool))
    expect(overlap).toEqual([])
  })
})

// ============================================================================
// getPlanModeStatus Tests
// ============================================================================

describe('getPlanModeStatus', () => {
  afterEach(() => {
    clearAllPlanModeStates()
  })

  it('returns "not active" when disabled', () => {
    const status = getPlanModeStatus('nonexistent')
    expect(status).toContain('not active')
  })

  it('returns "ACTIVE" with duration when enabled', () => {
    enterPlanMode('test', 'research phase')

    const status = getPlanModeStatus('test')
    expect(status).toContain('ACTIVE')
    expect(status).toContain('minute(s)')
    expect(status).toContain('research phase')
    expect(status).toContain('Allowed tools:')
  })

  it('returns "ACTIVE" without reason when none provided', () => {
    enterPlanMode('test')

    const status = getPlanModeStatus('test')
    expect(status).toContain('ACTIVE')
    expect(status).not.toContain('Reason:')
  })
})
