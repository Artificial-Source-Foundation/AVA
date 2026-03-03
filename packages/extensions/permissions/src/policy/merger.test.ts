import { describe, expect, it } from 'vitest'

import type { DeclarativePolicyRule } from '../types.js'
import { mergePolicyRules } from './merger.js'

describe('mergePolicyRules', () => {
  it('sorts by descending priority first', () => {
    const input: DeclarativePolicyRule[] = [
      { name: 'low', tool: '*', decision: 'allow', priority: 1, source: 'project' },
      { name: 'high', tool: '*', decision: 'deny', priority: 100, source: 'project' },
    ]
    const merged = mergePolicyRules(input)
    expect(merged[0]?.name).toBe('high')
  })

  it('uses source precedence when priorities match', () => {
    const input: DeclarativePolicyRule[] = [
      { name: 'user-rule', tool: '*', decision: 'ask', priority: 10, source: 'user' },
      { name: 'project-rule', tool: '*', decision: 'allow', priority: 10, source: 'project' },
    ]
    const merged = mergePolicyRules(input)
    expect(merged[0]?.name).toBe('project-rule')
  })

  it('keeps all rules after sorting', () => {
    const input: DeclarativePolicyRule[] = [
      { name: 'a', tool: '*', decision: 'allow', priority: 1, source: 'builtin' },
      { name: 'b', tool: '*', decision: 'ask', priority: 1, source: 'runtime' },
      { name: 'c', tool: '*', decision: 'deny', priority: 1, source: 'project' },
    ]
    const merged = mergePolicyRules(input)
    expect(merged.map((r) => r.name)).toEqual(expect.arrayContaining(['a', 'b', 'c']))
  })

  it('returns empty array when input is empty', () => {
    expect(mergePolicyRules([])).toEqual([])
  })

  it('does not mutate original array', () => {
    const input: DeclarativePolicyRule[] = [
      { name: 'a', tool: '*', decision: 'allow', priority: 1, source: 'project' },
      { name: 'b', tool: '*', decision: 'deny', priority: 99, source: 'project' },
    ]
    const copy = [...input]
    mergePolicyRules(input)
    expect(input).toEqual(copy)
  })
})
