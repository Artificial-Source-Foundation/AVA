import { MockFileSystem } from '@ava/core-v2/__test-utils__/mock-platform'
import { describe, expect, it } from 'vitest'
import { resolveSubdirectoryInstructions } from './subdirectory.js'
import { DEFAULT_INSTRUCTION_CONFIG } from './types.js'

describe('resolveSubdirectoryInstructions', () => {
  const config = DEFAULT_INSTRUCTION_CONFIG

  it('finds instruction files between file directory and cwd', async () => {
    const fs = new MockFileSystem()
    fs.addFile('/project/src/components/AGENTS.md', '# Component rules')
    const alreadyLoaded = new Set<string>()

    const result = await resolveSubdirectoryInstructions(
      '/project/src/components/Button.tsx',
      '/project',
      fs,
      config,
      alreadyLoaded
    )

    expect(result).toHaveLength(1)
    expect(result[0].path).toBe('/project/src/components/AGENTS.md')
    expect(result[0].content).toBe('# Component rules')
    expect(result[0].scope).toBe('directory')
  })

  it('finds instruction files at multiple directories in the walk path', async () => {
    const fs = new MockFileSystem()
    fs.addFile('/project/src/components/AGENTS.md', '# Component rules')
    fs.addFile('/project/src/AGENTS.md', '# Source rules')
    const alreadyLoaded = new Set<string>()

    const result = await resolveSubdirectoryInstructions(
      '/project/src/components/Button.tsx',
      '/project',
      fs,
      config,
      alreadyLoaded
    )

    expect(result).toHaveLength(2)
    expect(result.map((f) => f.path)).toContain('/project/src/components/AGENTS.md')
    expect(result.map((f) => f.path)).toContain('/project/src/AGENTS.md')
  })

  it('does not walk beyond cwd', async () => {
    const fs = new MockFileSystem()
    fs.addFile('/AGENTS.md', '# Root rules (should not be found)')
    fs.addFile('/project/src/AGENTS.md', '# Source rules')
    const alreadyLoaded = new Set<string>()

    const result = await resolveSubdirectoryInstructions(
      '/project/src/file.ts',
      '/project',
      fs,
      config,
      alreadyLoaded
    )

    expect(result).toHaveLength(1)
    expect(result[0].path).toBe('/project/src/AGENTS.md')
    // The root AGENTS.md should NOT be included
    expect(result.map((f) => f.path)).not.toContain('/AGENTS.md')
  })

  it('includes instruction files at cwd itself', async () => {
    const fs = new MockFileSystem()
    fs.addFile('/project/AGENTS.md', '# Project rules')
    const alreadyLoaded = new Set<string>()

    const result = await resolveSubdirectoryInstructions(
      '/project/file.ts',
      '/project',
      fs,
      config,
      alreadyLoaded
    )

    expect(result).toHaveLength(1)
    expect(result[0].path).toBe('/project/AGENTS.md')
  })

  it('skips paths in alreadyLoaded set (layer 1: system dedup)', async () => {
    const fs = new MockFileSystem()
    fs.addFile('/project/src/AGENTS.md', '# Source rules')
    const alreadyLoaded = new Set(['/project/src/AGENTS.md'])

    const result = await resolveSubdirectoryInstructions(
      '/project/src/file.ts',
      '/project',
      fs,
      config,
      alreadyLoaded
    )

    expect(result).toHaveLength(0)
  })

  it('deduplicates within a single invocation (layer 2: per-turn dedup)', async () => {
    const fs = new MockFileSystem()
    // Both AGENTS.md and CLAUDE.md exist in the same dir
    fs.addFile('/project/src/AGENTS.md', '# Agent rules')
    fs.addFile('/project/src/CLAUDE.md', '# Claude rules')
    const alreadyLoaded = new Set<string>()

    const result = await resolveSubdirectoryInstructions(
      '/project/src/file.ts',
      '/project',
      fs,
      config,
      alreadyLoaded
    )

    // Each should appear exactly once
    const agentsCount = result.filter((f) => f.path === '/project/src/AGENTS.md').length
    const claudeCount = result.filter((f) => f.path === '/project/src/CLAUDE.md').length
    expect(agentsCount).toBe(1)
    expect(claudeCount).toBe(1)
  })

  it('skips files with duplicate content hash (layer 3: content dedup)', async () => {
    const fs = new MockFileSystem()
    const sameContent = '# Identical instructions'
    fs.addFile('/project/src/AGENTS.md', sameContent)
    fs.addFile('/project/src/CLAUDE.md', sameContent)
    const alreadyLoaded = new Set<string>()

    const result = await resolveSubdirectoryInstructions(
      '/project/src/file.ts',
      '/project',
      fs,
      config,
      alreadyLoaded
    )

    // Only one should be loaded because the content is identical
    expect(result).toHaveLength(1)
  })

  it('respects maxSize config', async () => {
    const fs = new MockFileSystem()
    fs.addFile('/project/src/AGENTS.md', 'x'.repeat(200))
    const alreadyLoaded = new Set<string>()

    const result = await resolveSubdirectoryInstructions(
      '/project/src/file.ts',
      '/project',
      fs,
      { ...config, maxSize: 100 },
      alreadyLoaded
    )

    expect(result).toHaveLength(0)
  })

  it('returns empty array when no instruction files exist', async () => {
    const fs = new MockFileSystem()
    const alreadyLoaded = new Set<string>()

    const result = await resolveSubdirectoryInstructions(
      '/project/src/deep/nested/file.ts',
      '/project',
      fs,
      config,
      alreadyLoaded
    )

    expect(result).toHaveLength(0)
  })

  it('handles file at the cwd root level', async () => {
    const fs = new MockFileSystem()
    fs.addFile('/project/CLAUDE.md', '# Project instructions')
    const alreadyLoaded = new Set<string>()

    const result = await resolveSubdirectoryInstructions(
      '/project/README.md',
      '/project',
      fs,
      config,
      alreadyLoaded
    )

    // File dir is /project, which is cwd — should find CLAUDE.md
    expect(result).toHaveLength(1)
    expect(result[0].path).toBe('/project/CLAUDE.md')
  })

  it('finds all configured file names at each level', async () => {
    const fs = new MockFileSystem()
    fs.addFile('/project/src/AGENTS.md', '# Agents')
    fs.addFile('/project/src/.ava-instructions', 'ava rules')
    const alreadyLoaded = new Set<string>()

    const result = await resolveSubdirectoryInstructions(
      '/project/src/file.ts',
      '/project',
      fs,
      config,
      alreadyLoaded
    )

    expect(result).toHaveLength(2)
    expect(result.map((f) => f.path)).toContain('/project/src/AGENTS.md')
    expect(result.map((f) => f.path)).toContain('/project/src/.ava-instructions')
  })
})
