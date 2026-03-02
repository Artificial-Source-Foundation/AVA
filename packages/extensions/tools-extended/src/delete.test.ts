/**
 * delete_file tool — removes a single file.
 */

import { installMockPlatform, type MockPlatform } from '@ava/core-v2/__test-utils__/mock-platform'
import { resetLogger } from '@ava/core-v2/logger'
import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { deleteFileTool } from './delete.js'

let platform: MockPlatform

function makeCtx() {
  return {
    sessionId: 'test',
    workingDirectory: '/tmp',
    signal: AbortSignal.timeout(5000),
  }
}

beforeEach(() => {
  platform = installMockPlatform()
})

afterEach(() => {
  resetLogger()
})

describe('deleteFileTool', () => {
  it('has correct name', () => {
    expect(deleteFileTool.definition.name).toBe('delete_file')
  })

  it('deletes an existing file', async () => {
    platform.fs.addFile('/target.ts', 'to be deleted')
    const result = await deleteFileTool.execute({ path: '/target.ts' }, makeCtx())
    expect(result.success).toBe(true)
    expect(result.output).toContain('Deleted /target.ts')
    const exists = await platform.fs.exists('/target.ts')
    expect(exists).toBe(false)
  })

  it('fails on nonexistent file', async () => {
    const result = await deleteFileTool.execute({ path: '/missing.ts' }, makeCtx())
    expect(result.success).toBe(false)
    expect(result.error).toContain('File not found')
  })

  it('fails on directory', async () => {
    platform.fs.addDir('/mydir')
    const result = await deleteFileTool.execute({ path: '/mydir' }, makeCtx())
    expect(result.success).toBe(false)
    expect(result.error).toContain('is a directory')
  })

  it('resolves relative path against workingDirectory', async () => {
    platform.fs.addFile('/project/src/target.ts', 'delete me')
    const result = await deleteFileTool.execute(
      { path: 'src/target.ts' },
      {
        sessionId: 'test',
        workingDirectory: '/project',
        signal: AbortSignal.timeout(5000),
      }
    )
    expect(result.success).toBe(true)
    expect(result.output).toContain('/project/src/target.ts')
    const exists = await platform.fs.exists('/project/src/target.ts')
    expect(exists).toBe(false)
  })

  it('keeps absolute paths as-is', async () => {
    platform.fs.addFile('/abs/file.ts', 'content')
    const result = await deleteFileTool.execute(
      { path: '/abs/file.ts' },
      {
        sessionId: 'test',
        workingDirectory: '/project',
        signal: AbortSignal.timeout(5000),
      }
    )
    expect(result.success).toBe(true)
    expect(result.output).toContain('/abs/file.ts')
  })

  it('returns error when aborted', async () => {
    const controller = new AbortController()
    controller.abort()
    const result = await deleteFileTool.execute(
      { path: '/file.ts' },
      {
        sessionId: 'test',
        workingDirectory: '/tmp',
        signal: controller.signal,
      }
    )
    expect(result.success).toBe(false)
    expect(result.error).toBe('Aborted')
  })
})
