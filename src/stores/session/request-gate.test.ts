import { describe, expect, it } from 'vitest'
import { createLatestRequestGate } from './request-gate'

describe('createLatestRequestGate', () => {
  it('accepts only the latest token', () => {
    const gate = createLatestRequestGate()

    const first = gate.begin()
    const second = gate.begin()

    expect(gate.isCurrent(first)).toBe(false)
    expect(gate.isCurrent(second)).toBe(true)
  })
})
