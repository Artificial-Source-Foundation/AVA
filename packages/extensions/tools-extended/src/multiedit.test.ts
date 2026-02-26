/**
 * multiedit tool — apply multiple edits to a single file atomically.
 */

import { installMockPlatform, type MockPlatform } from '@ava/core-v2/__test-utils__/mock-platform'
import { resetLogger } from '@ava/core-v2/logger'
import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { multieditTool } from './multiedit.js'

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

describe('multieditTool', () => {
  it('has correct name', () => {
    expect(multieditTool.definition.name).toBe('multiedit')
  })

  it('applies a single edit correctly', async () => {
    platform.fs.addFile('/test.ts', 'const x = 1;\nconst y = 2;\n')
    const result = await multieditTool.execute(
      {
        filePath: '/test.ts',
        edits: [{ oldString: 'const x = 1;', newString: 'const x = 42;' }],
      },
      makeCtx()
    )
    expect(result.success).toBe(true)
    expect(result.output).toContain('1 edit(s)')
    const content = await platform.fs.readFile('/test.ts')
    expect(content).toBe('const x = 42;\nconst y = 2;\n')
  })

  it('applies multiple edits in order', async () => {
    platform.fs.addFile('/test.ts', 'aaa bbb ccc\n')
    const result = await multieditTool.execute(
      {
        filePath: '/test.ts',
        edits: [
          { oldString: 'aaa', newString: 'AAA' },
          { oldString: 'bbb', newString: 'BBB' },
          { oldString: 'ccc', newString: 'CCC' },
        ],
      },
      makeCtx()
    )
    expect(result.success).toBe(true)
    expect(result.output).toContain('3 edit(s)')
    const content = await platform.fs.readFile('/test.ts')
    expect(content).toBe('AAA BBB CCC\n')
  })

  it('returns error when oldString is not found', async () => {
    platform.fs.addFile('/test.ts', 'hello world\n')
    const result = await multieditTool.execute(
      {
        filePath: '/test.ts',
        edits: [{ oldString: 'missing', newString: 'replacement' }],
      },
      makeCtx()
    )
    expect(result.success).toBe(false)
    expect(result.error).toContain('Edit 1: oldString not found')
  })

  it('returns error on second edit when oldString not found', async () => {
    platform.fs.addFile('/test.ts', 'hello world\n')
    const result = await multieditTool.execute(
      {
        filePath: '/test.ts',
        edits: [
          { oldString: 'hello', newString: 'HELLO' },
          { oldString: 'missing', newString: 'x' },
        ],
      },
      makeCtx()
    )
    expect(result.success).toBe(false)
    expect(result.error).toContain('Edit 2: oldString not found')
  })

  it('returns error when file does not exist', async () => {
    const result = await multieditTool.execute(
      {
        filePath: '/nonexistent.ts',
        edits: [{ oldString: 'a', newString: 'b' }],
      },
      makeCtx()
    )
    expect(result.success).toBe(false)
    expect(result.error).toContain('File not found')
  })

  it('returns error when aborted', async () => {
    const controller = new AbortController()
    controller.abort()
    const result = await multieditTool.execute(
      {
        filePath: '/test.ts',
        edits: [{ oldString: 'a', newString: 'b' }],
      },
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
