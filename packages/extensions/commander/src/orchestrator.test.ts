import { describe, expect, it } from 'vitest'
import { applyTierToolPolicy, TIER_TOOLS } from './orchestrator.js'

describe('orchestrator tier tool policy', () => {
  it('director cannot use edit tools', () => {
    const available = ['read_file', 'edit', 'bash', 'invoke_team']
    expect(applyTierToolPolicy('director', available)).toEqual(['read_file', 'invoke_team'])
    expect(TIER_TOOLS.director.denied).toContain('edit')
  })

  it('tech lead has edit access', () => {
    const available = ['read_file', 'edit', 'bash', 'invoke_team']
    expect(applyTierToolPolicy('tech-lead', available)).toEqual(available)
  })

  it('engineer cannot invoke_team or websearch', () => {
    const available = ['read_file', 'invoke_team', 'websearch', 'edit']
    expect(applyTierToolPolicy('engineer', available)).toEqual(['read_file', 'edit'])
  })

  it('reviewer can only read and bash subset', () => {
    const available = ['read_file', 'glob', 'grep', 'bash', 'edit', 'invoke_team']
    expect(applyTierToolPolicy('reviewer', available)).toEqual([
      'read_file',
      'glob',
      'grep',
      'bash',
    ])
  })
})
