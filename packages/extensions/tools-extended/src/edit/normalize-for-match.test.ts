import { describe, expect, it } from 'vitest'
import { normalizeForMatch } from './normalize-for-match'

describe('normalizeForMatch', () => {
  it('normalizes trailing whitespace and smart punctuation', () => {
    const input = 'const x = 1;  \nconst label = \u201cHello\u201d\u2014test\u00A0'
    const normalized = normalizeForMatch(input)

    expect(normalized).toBe('const x = 1;\nconst label = "Hello"-test')
  })

  it('preserves line count', () => {
    const input = 'a\n b\n\n c '
    const normalized = normalizeForMatch(input)
    expect(normalized.split('\n')).toHaveLength(4)
  })
})
