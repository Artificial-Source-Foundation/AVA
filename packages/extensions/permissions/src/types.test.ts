import { describe, expect, it } from 'vitest'
import { BUILTIN_RULES, classifyRisk, DEFAULT_SETTINGS } from './types.js'

describe('classifyRisk', () => {
  it('classifies reads as low', () => {
    expect(classifyRisk('read_file', {})).toBe('low')
    expect(classifyRisk('glob', {})).toBe('low')
    expect(classifyRisk('grep', {})).toBe('low')
    expect(classifyRisk('ls', {})).toBe('low')
  })

  it('classifies writes as medium', () => {
    expect(classifyRisk('write_file', {})).toBe('medium')
    expect(classifyRisk('create_file', {})).toBe('medium')
    expect(classifyRisk('edit', {})).toBe('medium')
  })

  it('classifies deletes as high', () => {
    expect(classifyRisk('delete_file', {})).toBe('high')
  })

  it('classifies bash as high', () => {
    expect(classifyRisk('bash', {})).toBe('high')
  })

  it('classifies unknown tools as medium', () => {
    expect(classifyRisk('unknown_tool', {})).toBe('medium')
  })
})

describe('DEFAULT_SETTINGS', () => {
  it('has safe defaults', () => {
    expect(DEFAULT_SETTINGS.yolo).toBe(false)
    expect(DEFAULT_SETTINGS.autoApproveReads).toBe(true)
    expect(DEFAULT_SETTINGS.autoApproveWrites).toBe(false)
    expect(DEFAULT_SETTINGS.autoApproveCommands).toBe(false)
    expect(DEFAULT_SETTINGS.blockedPatterns).toEqual([])
  })
})

describe('BUILTIN_RULES', () => {
  it('has expected rules', () => {
    expect(BUILTIN_RULES.length).toBeGreaterThanOrEqual(5)
    const names = BUILTIN_RULES.map((r) => r.name)
    expect(names).toContain('protect-git')
    expect(names).toContain('protect-node-modules')
    expect(names).toContain('warn-env-files')
    expect(names).toContain('deny-rm-rf-root')
    expect(names).toContain('warn-sudo')
  })

  it('has proper priorities', () => {
    const gitRule = BUILTIN_RULES.find((r) => r.name === 'protect-git')
    expect(gitRule?.priority).toBe(1000)
    const rmRule = BUILTIN_RULES.find((r) => r.name === 'deny-rm-rf-root')
    expect(rmRule?.priority).toBe(1000)
  })
})
