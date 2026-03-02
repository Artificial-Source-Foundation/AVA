import { describe, expect, it } from 'vitest'
import { truncateMistralIds } from './transform.js'

describe('truncateMistralIds', () => {
  it('passes through short alphanumeric IDs unchanged', () => {
    expect(truncateMistralIds('abc123')).toBe('abc123')
  })

  it('truncates IDs longer than 9 characters', () => {
    expect(truncateMistralIds('abcdefghij')).toBe('abcdefghi')
    expect(truncateMistralIds('toolcall_12345678')).toBe('toolcall1')
  })

  it('strips non-alphanumeric characters before truncating', () => {
    expect(truncateMistralIds('call_abc-123')).toBe('callabc12')
    expect(truncateMistralIds('tool-use_001')).toBe('tooluse00')
  })

  it('handles empty string', () => {
    expect(truncateMistralIds('')).toBe('')
  })

  it('handles IDs with only special characters', () => {
    expect(truncateMistralIds('---___...')).toBe('')
  })

  it('handles exactly 9 character alphanumeric IDs', () => {
    expect(truncateMistralIds('abcdefgh9')).toBe('abcdefgh9')
  })
})
