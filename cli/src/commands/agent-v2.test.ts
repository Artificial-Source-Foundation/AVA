/**
 * Tests for the agent-v2 command — extension loading + CLI integration.
 */

import { loadAllBuiltInExtensions } from '@ava/core-v2/extensions'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

// Mock platform-node/v2
vi.mock('@ava/platform-node/v2', () => ({
  createNodePlatform: () => ({
    fs: {
      exists: vi.fn(async () => false),
      readFile: vi.fn(async () => ''),
      readDir: vi.fn(async () => []),
      readDirWithTypes: vi.fn(async () => []),
    },
    shell: {},
    credentials: {
      get: vi.fn(async () => null),
      set: vi.fn(async () => {}),
      delete: vi.fn(async () => {}),
      has: vi.fn(async () => false),
    },
    database: {
      open: vi.fn(),
      close: vi.fn(),
    },
  }),
}))

// Mock the extension loader
vi.mock('@ava/core-v2/extensions', async (importOriginal) => {
  const actual = (await importOriginal()) as Record<string, unknown>
  return {
    ...actual,
    loadAllBuiltInExtensions: vi.fn(async () => []),
  }
})

const mockLoadAll = vi.mocked(loadAllBuiltInExtensions)

describe('agent-v2 command', () => {
  beforeEach(() => {
    mockLoadAll.mockClear()
    mockLoadAll.mockResolvedValue([])
    process.exitCode = undefined
  })

  afterEach(() => {
    vi.restoreAllMocks()
  })

  it('runs with mock provider when no extensions loaded', async () => {
    const { runAgentV2Command } = await import('./agent-v2.js')

    await runAgentV2Command(['run', 'hello'])

    expect(process.exitCode).toBe(0)
  })

  it('calls loadAllBuiltInExtensions during startup', async () => {
    const { runAgentV2Command } = await import('./agent-v2.js')

    await runAgentV2Command(['run', 'hello'])

    expect(mockLoadAll).toHaveBeenCalled()
    // Verify it was called with a path ending in packages/extensions
    const callArg = mockLoadAll.mock.calls[0]![0] as string
    expect(callArg).toContain('packages/extensions')
  })

  it('handles extension loading failure gracefully', async () => {
    mockLoadAll.mockRejectedValueOnce(new Error('Load failed'))
    const stderrSpy = vi.spyOn(process.stderr, 'write').mockImplementation(() => true)

    const { runAgentV2Command } = await import('./agent-v2.js')
    await runAgentV2Command(['run', 'hello'])

    // Should still run with mock provider
    expect(process.exitCode).toBe(0)

    // Should warn about load failure
    const warnings = stderrSpy.mock.calls.map(([msg]) => String(msg))
    expect(warnings.some((w) => w.includes('Failed to load extensions'))).toBe(true)
  })

  it('does not load extensions when args are invalid', async () => {
    mockLoadAll.mockClear()
    const { runAgentV2Command } = await import('./agent-v2.js')

    await runAgentV2Command([])

    // parseArgs returns null, so the function returns early before loading extensions
    expect(mockLoadAll).not.toHaveBeenCalled()
  })

  it('prints extension count in verbose mode', async () => {
    const stderrSpy = vi.spyOn(process.stderr, 'write').mockImplementation(() => true)

    const { runAgentV2Command } = await import('./agent-v2.js')
    await runAgentV2Command(['run', 'hello', '--verbose'])

    const output = stderrSpy.mock.calls.map(([msg]) => String(msg))
    expect(output.some((line) => line.includes('Extensions loaded:'))).toBe(true)
  })
})
