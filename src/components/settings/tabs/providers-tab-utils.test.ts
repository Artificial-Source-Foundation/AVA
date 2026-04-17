import { describe, expect, it } from 'vitest'
import { formatContextWindow } from './providers-tab-utils'

describe('providers-tab-utils formatContextWindow', () => {
  it('returns a placeholder when context window is missing', () => {
    expect(formatContextWindow(undefined)).toBe('N/A')
  })
})
