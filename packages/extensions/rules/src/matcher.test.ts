import { describe, expect, it } from 'vitest'
import { matchRules } from './matcher.js'
import type { Rule } from './types.js'

const makeRule = (overrides: Partial<Rule>): Rule => ({
  name: 'test-rule',
  description: 'Test',
  globs: [],
  activation: 'auto',
  content: 'Rule content',
  source: '/test/.ava/rules/test.md',
  ...overrides,
})

describe('matchRules', () => {
  it('always rules are always included', () => {
    const rules = [makeRule({ name: 'global', activation: 'always' })]
    const matches = matchRules(rules, [])
    expect(matches).toHaveLength(1)
    expect(matches[0]!.rule.name).toBe('global')
    expect(matches[0]!.matchedGlob).toBe('*')
  })

  it('auto rules match against current files', () => {
    const rules = [makeRule({ name: 'ts-rule', globs: ['**/*.ts'], activation: 'auto' })]
    const matches = matchRules(rules, ['src/index.ts'])
    expect(matches).toHaveLength(1)
    expect(matches[0]!.matchedFile).toBe('src/index.ts')
  })

  it('auto rules do not match unrelated files', () => {
    const rules = [makeRule({ name: 'py-rule', globs: ['**/*.py'], activation: 'auto' })]
    const matches = matchRules(rules, ['src/index.ts'])
    expect(matches).toHaveLength(0)
  })

  it('manual rules are excluded from automatic matching', () => {
    const rules = [makeRule({ name: 'manual', globs: ['**/*.ts'], activation: 'manual' })]
    const matches = matchRules(rules, ['src/index.ts'])
    expect(matches).toHaveLength(0)
  })

  it('respects maxActive config', () => {
    const rules = [
      makeRule({ name: 'a', activation: 'always' }),
      makeRule({ name: 'b', activation: 'always' }),
      makeRule({ name: 'c', activation: 'always' }),
    ]
    const matches = matchRules(rules, [], { maxActive: 2, maxContentLength: 15_000 })
    expect(matches).toHaveLength(2)
  })

  it('mixes always and auto rules', () => {
    const rules = [
      makeRule({ name: 'global', activation: 'always' }),
      makeRule({ name: 'ts', globs: ['**/*.ts'], activation: 'auto' }),
      makeRule({ name: 'py', globs: ['**/*.py'], activation: 'auto' }),
    ]
    const matches = matchRules(rules, ['src/app.ts'])
    expect(matches).toHaveLength(2)
    expect(matches.map((m) => m.rule.name)).toEqual(['global', 'ts'])
  })
})
