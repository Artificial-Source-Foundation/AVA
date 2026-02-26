import { afterEach, describe, expect, it } from 'vitest'
import {
  enterPlanMode,
  exitPlanMode,
  getPlanModeState,
  isPlanModeEnabled,
  isToolAllowedInPlanMode,
  planAgentMode,
  resetPlanMode,
} from './plan-mode.js'

describe('Plan Mode', () => {
  afterEach(() => resetPlanMode())

  it('is disabled by default', () => {
    expect(isPlanModeEnabled('session-1')).toBe(false)
  })

  it('can be entered', () => {
    enterPlanMode('session-1', 'Research phase')
    expect(isPlanModeEnabled('session-1')).toBe(true)
    const state = getPlanModeState('session-1')
    expect(state?.reason).toBe('Research phase')
    expect(state?.enteredAt).toBeInstanceOf(Date)
  })

  it('can be exited', () => {
    enterPlanMode('session-1')
    exitPlanMode('session-1')
    expect(isPlanModeEnabled('session-1')).toBe(false)
  })

  it('tracks separate sessions', () => {
    enterPlanMode('a')
    expect(isPlanModeEnabled('a')).toBe(true)
    expect(isPlanModeEnabled('b')).toBe(false)
  })

  it('allows read tools', () => {
    expect(isToolAllowedInPlanMode('read_file')).toBe(true)
    expect(isToolAllowedInPlanMode('glob')).toBe(true)
    expect(isToolAllowedInPlanMode('grep')).toBe(true)
    expect(isToolAllowedInPlanMode('ls')).toBe(true)
  })

  it('blocks write tools', () => {
    expect(isToolAllowedInPlanMode('write_file')).toBe(false)
    expect(isToolAllowedInPlanMode('edit')).toBe(false)
    expect(isToolAllowedInPlanMode('bash')).toBe(false)
    expect(isToolAllowedInPlanMode('delete_file')).toBe(false)
  })

  it('filters tools in agent mode', () => {
    const tools = [
      {
        name: 'read_file',
        description: '',
        input_schema: { type: 'object' as const, properties: {} },
      },
      {
        name: 'write_file',
        description: '',
        input_schema: { type: 'object' as const, properties: {} },
      },
      { name: 'glob', description: '', input_schema: { type: 'object' as const, properties: {} } },
      { name: 'bash', description: '', input_schema: { type: 'object' as const, properties: {} } },
    ]

    const filtered = planAgentMode.filterTools!(tools)
    expect(filtered.map((t) => t.name)).toEqual(['read_file', 'glob'])
  })

  it('adds plan mode instructions to system prompt', () => {
    const result = planAgentMode.systemPrompt!('You are AVA.')
    expect(result).toContain('You are AVA.')
    expect(result).toContain('PLAN MODE')
  })
})
