import { describe, expect, it } from 'vitest'
import { parseRuleFile } from './loader.js'

describe('parseRuleFile', () => {
  it('parses a valid auto rule with globs', () => {
    const raw = [
      '---',
      'description: Testing conventions',
      'globs:',
      '  - "**/*.test.ts"',
      '---',
      'Always use describe/it blocks.',
    ].join('\n')

    const rule = parseRuleFile(raw, '/project/.ava/rules/testing.md')
    expect(rule).not.toBeNull()
    expect(rule!.name).toBe('testing')
    expect(rule!.description).toBe('Testing conventions')
    expect(rule!.globs).toEqual(['**/*.test.ts'])
    expect(rule!.activation).toBe('auto')
    expect(rule!.content).toBe('Always use describe/it blocks.')
    expect(rule!.source).toBe('/project/.ava/rules/testing.md')
  })

  it('parses an always rule without globs', () => {
    const raw = [
      '---',
      'description: Global coding style',
      'activation: always',
      '---',
      'Use semicolons everywhere.',
    ].join('\n')

    const rule = parseRuleFile(raw, '/project/.ava/rules/style.md')
    expect(rule).not.toBeNull()
    expect(rule!.activation).toBe('always')
    expect(rule!.globs).toEqual([])
  })

  it('handles Cursor compatibility: alwaysApply', () => {
    const raw = [
      '---',
      'description: Apply everywhere',
      'alwaysApply: true',
      '---',
      'Be concise.',
    ].join('\n')

    const rule = parseRuleFile(raw, '/project/.cursor/rules/concise.md')
    expect(rule).not.toBeNull()
    expect(rule!.activation).toBe('always')
  })

  it('returns null for auto rule missing globs', () => {
    const raw = ['---', 'description: No globs here', '---', 'Some content.'].join('\n')
    expect(parseRuleFile(raw, '/project/.ava/rules/bad.md')).toBeNull()
  })

  it('returns null for empty content', () => {
    const raw = ['---', 'description: Empty', 'globs:', '  - "*.ts"', '---', ''].join('\n')
    expect(parseRuleFile(raw, '/project/.ava/rules/empty.md')).toBeNull()
  })

  it('derives name from filename', () => {
    const raw = ['---', 'description: Test', 'activation: always', '---', 'Content'].join('\n')
    const rule = parseRuleFile(raw, '/deep/path/my-rule.md')
    expect(rule!.name).toBe('my-rule')
  })

  it('parses manual activation', () => {
    const raw = [
      '---',
      'description: Manual rule',
      'activation: manual',
      'globs:',
      '  - "*.ts"',
      '---',
      'Manual content.',
    ].join('\n')

    const rule = parseRuleFile(raw, '/project/.ava/rules/manual.md')
    expect(rule!.activation).toBe('manual')
  })
})
