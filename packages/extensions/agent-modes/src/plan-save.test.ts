import { installMockPlatform } from '@ava/core-v2/__test-utils__/mock-platform'
import { afterEach, describe, expect, it, vi } from 'vitest'
import { savePlanToFile } from './plan-save.js'

describe('savePlanToFile', () => {
  afterEach(() => {
    vi.restoreAllMocks()
  })

  it('saves plan content to .ava/plans/ directory', async () => {
    const platform = installMockPlatform()
    const path = await savePlanToFile('# My Plan\n\n- Step 1\n- Step 2')

    expect(path).toContain('.ava/plans/')
    expect(path).toContain('-plan.md')

    const content = await platform.fs.readFile(path)
    expect(content).toBe('# My Plan\n\n- Step 1\n- Step 2')
  })

  it('uses slug in filename when provided', async () => {
    const platform = installMockPlatform()
    const path = await savePlanToFile('plan content', 'refactor-auth')

    expect(path).toContain('-refactor-auth.md')

    const content = await platform.fs.readFile(path)
    expect(content).toBe('plan content')
  })

  it('sanitizes slug characters', async () => {
    installMockPlatform()
    const path = await savePlanToFile('content', 'my plan / special chars!')

    expect(path).toContain('-my-plan---special-chars-.md')
  })

  it('truncates long slugs to 50 characters', async () => {
    installMockPlatform()
    const longSlug = 'a'.repeat(100)
    const path = await savePlanToFile('content', longSlug)

    const filename = path.split('/').pop()!
    // Timestamp (19 chars) + dash (1) + slug (50) + .md (3) = 73
    expect(filename.length).toBeLessThanOrEqual(73)
  })

  it('creates .ava/plans/ directory if it does not exist', async () => {
    const platform = installMockPlatform()

    expect(await platform.fs.exists('.ava/plans')).toBe(false)
    await savePlanToFile('content')
    expect(await platform.fs.exists('.ava/plans')).toBe(true)
  })

  it('does not fail if directory already exists', async () => {
    const platform = installMockPlatform()
    platform.fs.addDir('.ava/plans')

    const path = await savePlanToFile('content')
    expect(path).toContain('.ava/plans/')
  })

  it('includes ISO timestamp in filename', async () => {
    installMockPlatform()
    const path = await savePlanToFile('content')
    const filename = path.split('/').pop()!

    // Should match pattern like 2026-03-02T12-34-56
    expect(filename).toMatch(/^\d{4}-\d{2}-\d{2}T\d{2}-\d{2}-\d{2}-plan\.md$/)
  })
})
