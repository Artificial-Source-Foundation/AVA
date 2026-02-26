/**
 * create_file tool — creates a new file (fails if it exists).
 */

import { installMockPlatform, type MockPlatform } from '@ava/core-v2/__test-utils__/mock-platform'
import { resetLogger } from '@ava/core-v2/logger'
import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { createFileTool } from './create.js'

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

describe('createFileTool', () => {
  it('has correct name', () => {
    expect(createFileTool.definition.name).toBe('create_file')
  })

  it('creates a new file', async () => {
    const result = await createFileTool.execute(
      { path: '/new-file.ts', content: 'console.log("hi")' },
      makeCtx()
    )
    expect(result.success).toBe(true)
    expect(result.output).toContain('Created /new-file.ts')
    const content = await platform.fs.readFile('/new-file.ts')
    expect(content).toBe('console.log("hi")')
  })

  it('creates parent directories', async () => {
    const result = await createFileTool.execute(
      { path: '/deep/nested/file.ts', content: 'hello' },
      makeCtx()
    )
    expect(result.success).toBe(true)
    expect(result.output).toContain('Created /deep/nested/file.ts')
  })

  it('fails if file already exists', async () => {
    platform.fs.addFile('/existing.ts', 'old content')
    const result = await createFileTool.execute(
      { path: '/existing.ts', content: 'new content' },
      makeCtx()
    )
    expect(result.success).toBe(false)
    expect(result.error).toContain('File already exists')
  })

  it('reports character count in output', async () => {
    const content = 'abcdefghij'
    const result = await createFileTool.execute({ path: '/charcount.ts', content }, makeCtx())
    expect(result.success).toBe(true)
    expect(result.output).toContain(`(${content.length} chars)`)
  })

  it('returns error when aborted', async () => {
    const controller = new AbortController()
    controller.abort()
    const result = await createFileTool.execute(
      { path: '/file.ts', content: 'hello' },
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
