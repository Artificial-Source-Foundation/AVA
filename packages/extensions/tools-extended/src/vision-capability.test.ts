import { describe, expect, it } from 'vitest'
import { isVisionCapable } from './vision-capability.js'

describe('isVisionCapable', () => {
  it('detects known vision-capable models', () => {
    expect(isVisionCapable('gpt-4o')).toBe(true)
    expect(isVisionCapable('gpt-4.1')).toBe(true)
    expect(isVisionCapable('claude-3-5-sonnet')).toBe(true)
    expect(isVisionCapable('gemini-1.5-pro')).toBe(true)
    expect(isVisionCapable('llava:latest')).toBe(true)
  })

  it('returns false for non-vision models', () => {
    expect(isVisionCapable('gpt-3.5-turbo')).toBe(false)
    expect(isVisionCapable('text-embedding-3-large')).toBe(false)
    expect(isVisionCapable('unknown-model')).toBe(false)
  })
})
