import { describe, expect, it } from 'vitest'
import { getDefaultModelConfig } from './model-config.js'

describe('model config', () => {
  it('returns defaults', () => {
    const defaults = getDefaultModelConfig()
    expect(defaults.director.model).toBe('anthropic/claude-opus-4-6')
    expect(defaults['tech-lead'].model).toBe('anthropic/claude-sonnet-4-6')
    expect(defaults.engineer.model).toBe('anthropic/claude-haiku-4-5')
    expect(defaults.reviewer.model).toBe('anthropic/claude-sonnet-4-6')
    expect(defaults.subagent.model).toBe('anthropic/claude-haiku-4-5')
  })
})
