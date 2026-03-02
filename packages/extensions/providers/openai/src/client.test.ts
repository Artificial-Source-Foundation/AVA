import { describe, expect, it } from 'vitest'
import { shouldUseResponsesAPI } from './client.js'

describe('shouldUseResponsesAPI', () => {
  it('returns true for gpt-5 models', () => {
    expect(shouldUseResponsesAPI('gpt-5')).toBe(true)
    expect(shouldUseResponsesAPI('gpt-5-turbo')).toBe(true)
    expect(shouldUseResponsesAPI('gpt-5.3-codex')).toBe(true)
  })

  it('returns true for o3 models', () => {
    expect(shouldUseResponsesAPI('o3-mini')).toBe(true)
    expect(shouldUseResponsesAPI('o3-2025-04-16')).toBe(true)
  })

  it('returns true for o4 models', () => {
    expect(shouldUseResponsesAPI('o4-mini')).toBe(true)
    expect(shouldUseResponsesAPI('o4-2025-04-16')).toBe(true)
  })

  it('returns true for codex models', () => {
    expect(shouldUseResponsesAPI('codex-mini')).toBe(true)
    expect(shouldUseResponsesAPI('codex-mini-latest')).toBe(true)
  })

  it('is case insensitive', () => {
    expect(shouldUseResponsesAPI('GPT-5')).toBe(true)
    expect(shouldUseResponsesAPI('O3-mini')).toBe(true)
    expect(shouldUseResponsesAPI('CODEX')).toBe(true)
  })

  it('returns false for gpt-4 models', () => {
    expect(shouldUseResponsesAPI('gpt-4o')).toBe(false)
    expect(shouldUseResponsesAPI('gpt-4o-mini')).toBe(false)
    expect(shouldUseResponsesAPI('gpt-4-turbo')).toBe(false)
  })

  it('returns false for o1 models', () => {
    expect(shouldUseResponsesAPI('o1-mini')).toBe(false)
    expect(shouldUseResponsesAPI('o1-preview')).toBe(false)
  })

  it('returns false for other models', () => {
    expect(shouldUseResponsesAPI('claude-3-opus')).toBe(false)
    expect(shouldUseResponsesAPI('gemini-pro')).toBe(false)
  })
})
