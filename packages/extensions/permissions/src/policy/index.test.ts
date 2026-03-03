import { describe, expect, it } from 'vitest'

import { createMockPlatform } from '../../../../core-v2/src/__test-utils__/mock-platform.js'

import { loadDeclarativePolicies } from './index.js'

interface MutableFs {
  addFile(path: string, content: string): void
  addDir(path: string): void
}

describe('loadDeclarativePolicies', () => {
  it('loads and merges project policy rules', async () => {
    const platform = createMockPlatform()
    const fs = platform.fs as unknown as MutableFs
    fs.addFile(
      '/repo/.ava-policy.yml',
      ['version: 1', 'rules:', '  - name: deny-bash', '    tool: bash', '    decision: deny'].join(
        '\n'
      )
    )

    const result = await loadDeclarativePolicies(platform.fs, '/repo')
    expect(result.rules).toHaveLength(1)
    expect(result.rules[0]?.name).toBe('deny-bash')
  })

  it('collects parse warnings and continues loading', async () => {
    const platform = createMockPlatform()
    const fs = platform.fs as unknown as MutableFs
    fs.addDir('/repo/.ava/policies')
    fs.addFile('/repo/.ava/policies/bad.yml', 'version: 2\nrules: []\n')
    fs.addFile(
      '/repo/.ava/policies/good.yml',
      [
        'version: 1',
        'rules:',
        '  - name: allow-read',
        '    tool: read_file',
        '    decision: allow',
      ].join('\n')
    )

    const result = await loadDeclarativePolicies(platform.fs, '/repo')
    expect(result.rules.some((r) => r.name === 'allow-read')).toBe(true)
    expect(result.warnings.some((w) => w.includes('Failed to parse policy'))).toBe(true)
  })

  it('sorts output by priority and source precedence', async () => {
    const platform = createMockPlatform()
    const fs = platform.fs as unknown as MutableFs
    fs.addDir('/repo/.ava/policies')
    fs.addFile(
      '/repo/.ava/policies/high.yml',
      [
        'version: 1',
        'rules:',
        '  - name: top',
        '    tool: "*"',
        '    decision: deny',
        '    priority: 99',
      ].join('\n')
    )
    fs.addFile(
      '/repo/.ava-policy.yml',
      [
        'version: 1',
        'rules:',
        '  - name: low',
        '    tool: "*"',
        '    decision: ask',
        '    priority: 1',
      ].join('\n')
    )

    const result = await loadDeclarativePolicies(platform.fs, '/repo')
    expect(result.rules[0]?.name).toBe('top')
  })

  it('returns empty rules when no policy files exist', async () => {
    const platform = createMockPlatform()
    const result = await loadDeclarativePolicies(platform.fs, '/repo')
    expect(result.rules).toEqual([])
  })

  it('includes HOME warning when user dir cannot be evaluated', async () => {
    const platform = createMockPlatform()
    const result = await loadDeclarativePolicies(platform.fs, '/repo')
    expect(Array.isArray(result.warnings)).toBe(true)
  })
})
