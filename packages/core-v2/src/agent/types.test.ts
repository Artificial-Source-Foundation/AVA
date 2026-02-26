/**
 * Agent types — enum values and config defaults.
 */

import { describe, expect, it } from 'vitest'
import { AgentTerminateMode, COMPLETE_TASK_TOOL, DEFAULT_AGENT_CONFIG } from './types.js'

describe('AgentTerminateMode', () => {
  it('has all expected values', () => {
    expect(AgentTerminateMode.ERROR).toBe('ERROR')
    expect(AgentTerminateMode.TIMEOUT).toBe('TIMEOUT')
    expect(AgentTerminateMode.GOAL).toBe('GOAL')
    expect(AgentTerminateMode.MAX_TURNS).toBe('MAX_TURNS')
    expect(AgentTerminateMode.ABORTED).toBe('ABORTED')
  })

  it('has exactly 5 members', () => {
    const values = Object.values(AgentTerminateMode)
    expect(values).toHaveLength(5)
  })
})

describe('DEFAULT_AGENT_CONFIG', () => {
  it('has anthropic as default provider', () => {
    expect(DEFAULT_AGENT_CONFIG.provider).toBe('anthropic')
  })

  it('has a default model', () => {
    expect(DEFAULT_AGENT_CONFIG.model).toBe('claude-sonnet-4-20250514')
  })

  it('does not include maxTimeMinutes or maxTurns', () => {
    expect('maxTimeMinutes' in DEFAULT_AGENT_CONFIG).toBe(false)
    expect('maxTurns' in DEFAULT_AGENT_CONFIG).toBe(false)
  })
})

describe('COMPLETE_TASK_TOOL', () => {
  it('is attempt_completion', () => {
    expect(COMPLETE_TASK_TOOL).toBe('attempt_completion')
  })
})
