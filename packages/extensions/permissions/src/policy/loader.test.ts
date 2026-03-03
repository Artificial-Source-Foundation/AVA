import { describe, expect, it } from 'vitest'

import { createMockPlatform } from '../../../../core-v2/src/__test-utils__/mock-platform.js'

import { loadPolicyFiles } from './loader.js'

interface MutableFs {
  addFile(path: string, content: string): void
  addDir(path: string): void
}

describe('loadPolicyFiles', () => {
  it('loads project root policy files', async () => {
    const platform = createMockPlatform()
    const fs = platform.fs as unknown as MutableFs
    fs.addFile('/repo/.ava-policy.yml', 'version: 1\nrules: []\n')

    const result = await loadPolicyFiles(platform.fs, '/repo', '/home/test')
    expect(result.files.some((f) => f.path.endsWith('.ava-policy.yml'))).toBe(true)
  })

  it('loads project .ava/policies directory files', async () => {
    const platform = createMockPlatform()
    const fs = platform.fs as unknown as MutableFs
    fs.addDir('/repo/.ava/policies')
    fs.addFile('/repo/.ava/policies/main.yaml', 'version: 1\nrules: []\n')

    const result = await loadPolicyFiles(platform.fs, '/repo', '/home/test')
    expect(result.files.some((f) => f.path.endsWith('/main.yaml'))).toBe(true)
  })

  it('loads user policy files from home directory', async () => {
    const platform = createMockPlatform()
    const fs = platform.fs as unknown as MutableFs
    fs.addDir('/home/test/.ava/policies')
    fs.addFile('/home/test/.ava/policies/user.toml', 'version = 1\nrules = []\n')

    const result = await loadPolicyFiles(platform.fs, '/repo', '/home/test')
    expect(result.files.some((f) => f.path.endsWith('/user.toml') && f.source === 'user')).toBe(
      true
    )
  })

  it('ignores non-policy extensions in policy directories', async () => {
    const platform = createMockPlatform()
    const fs = platform.fs as unknown as MutableFs
    fs.addDir('/repo/.ava/policies')
    fs.addFile('/repo/.ava/policies/readme.txt', 'ignore')

    const result = await loadPolicyFiles(platform.fs, '/repo', '/home/test')
    expect(result.files).toHaveLength(0)
  })

  it('adds warning when HOME is not provided', async () => {
    const platform = createMockPlatform()
    const result = await loadPolicyFiles(platform.fs, '/repo', '')
    expect(result.warnings.some((w) => w.includes('HOME is undefined'))).toBe(true)
  })
})
