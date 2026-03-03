import { beforeEach, describe, expect, it } from 'vitest'
import {
  installMockPlatform,
  type MockPlatform,
} from '../../../../core-v2/src/__test-utils__/mock-platform.js'

import { applyPatchTool } from './index.js'

let platform: MockPlatform

function ctx() {
  return {
    sessionId: 'streaming-test',
    workingDirectory: '/repo',
    signal: AbortSignal.timeout(5000),
  }
}

describe('apply_patch streaming pipeline', () => {
  beforeEach(() => {
    platform = installMockPlatform()
    platform.fs.addDir('/repo')
  })

  it('applies streamed add file chunks', async () => {
    const result = await applyPatchTool.execute(
      {
        patch: '',
        streamChunks: ['*** Begin Patch\n*** Add File: a.txt\n+hello', '\n*** End Patch\n'],
      },
      ctx()
    )
    expect(result.success).toBe(true)
    expect(await platform.fs.readFile('/repo/a.txt')).toBe('hello')
  })

  it('applies streamed update chunks', async () => {
    platform.fs.addFile('/repo/app.ts', 'const x = 1\n')

    const result = await applyPatchTool.execute(
      {
        patch: '',
        streamChunks: [
          '*** Begin Patch\n*** Update File: app.ts\n@@ const x = 1 @@\n-const x = 1',
          '\n+const x = 2\n*** End Patch\n',
        ],
      },
      ctx()
    )
    expect(result.success).toBe(true)
    expect(await platform.fs.readFile('/repo/app.ts')).toContain('const x = 2')
  })

  it('returns streaming failure when operation is invalid', async () => {
    const result = await applyPatchTool.execute(
      {
        patch: '',
        streamChunks: [
          '*** Begin Patch\n*** Update File: missing.ts\n@@ x @@\n-x\n+y\n*** End Patch\n',
        ],
      },
      ctx()
    )
    expect(result.success).toBe(false)
    expect(result.error).toBe('STREAMING_PATCH_FAILED')
    expect(result.output).toContain('Streaming Patch Results')
  })

  it('supports dry-run streaming without writing files', async () => {
    const result = await applyPatchTool.execute(
      {
        patch: '',
        dryRun: true,
        streamChunks: ['*** Begin Patch\n*** Add File: dry.txt\n+dry\n*** End Patch\n'],
      },
      ctx()
    )
    expect(result.success).toBe(true)
    expect(await platform.fs.exists('/repo/dry.txt')).toBe(false)
    expect(result.output).toContain('Applied operations')
  })

  it('falls back to classic patch path when streamChunks absent', async () => {
    const result = await applyPatchTool.execute(
      {
        patch: '*** Begin Patch\n*** Add File: classic.txt\n+ok\n*** End Patch\n',
      },
      ctx()
    )
    expect(result.success).toBe(true)
    expect(await platform.fs.readFile('/repo/classic.txt')).toBe('ok')
  })
})
