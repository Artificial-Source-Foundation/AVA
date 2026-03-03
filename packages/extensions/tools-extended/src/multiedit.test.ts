/**
 * multiedit tool — apply multiple edits to a single file atomically.
 */

import { resetLogger } from '@ava/core-v2/logger'
import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import {
  installMockPlatform,
  type MockPlatform,
} from '../../../core-v2/src/__test-utils__/mock-platform.js'
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

  it('resolves relative path against workingDirectory', async () => {
    platform.fs.addFile('/project/src/app.ts', 'const x = 1;\n')
    const result = await multieditTool.execute(
      {
        filePath: 'src/app.ts',
        edits: [{ oldString: 'const x = 1;', newString: 'const x = 42;' }],
      },
      {
        sessionId: 'test',
        workingDirectory: '/project',
        signal: AbortSignal.timeout(5000),
      }
    )
    expect(result.success).toBe(true)
    expect(result.output).toContain('/project/src/app.ts')
    const content = await platform.fs.readFile('/project/src/app.ts')
    expect(content).toBe('const x = 42;\n')
  })

  it('keeps absolute paths as-is', async () => {
    platform.fs.addFile('/abs/file.ts', 'aaa\n')
    const result = await multieditTool.execute(
      {
        filePath: '/abs/file.ts',
        edits: [{ oldString: 'aaa', newString: 'bbb' }],
      },
      {
        sessionId: 'test',
        workingDirectory: '/project',
        signal: AbortSignal.timeout(5000),
      }
    )
    expect(result.success).toBe(true)
    expect(result.output).toContain('/abs/file.ts')
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

  it('applies edits across multiple files', async () => {
    platform.fs.addFile('/project/a.ts', 'const a = 1;\n')
    platform.fs.addFile('/project/b.ts', 'const b = 2;\n')

    const result = await multieditTool.execute(
      {
        files: [
          {
            filePath: '/project/a.ts',
            edits: [{ oldString: 'const a = 1;', newString: 'const a = 11;' }],
          },
          {
            filePath: '/project/b.ts',
            edits: [{ oldString: 'const b = 2;', newString: 'const b = 22;' }],
          },
        ],
        concurrency: 2,
      },
      makeCtx()
    )

    expect(result.success).toBe(true)
    expect(result.output).toContain('2 succeeded, 0 failed')
    expect(await platform.fs.readFile('/project/a.ts')).toContain('const a = 11;')
    expect(await platform.fs.readFile('/project/b.ts')).toContain('const b = 22;')
  })

  it('supports partial failures while preserving successful file edits', async () => {
    platform.fs.addFile('/project/ok.ts', 'export const ok = 1;\n')
    platform.fs.addFile('/project/bad.ts', 'export const bad = 2;\n')

    const result = await multieditTool.execute(
      {
        files: [
          {
            filePath: '/project/ok.ts',
            edits: [{ oldString: 'export const ok = 1;', newString: 'export const ok = 99;' }],
          },
          {
            filePath: '/project/bad.ts',
            edits: [{ oldString: 'missing token', newString: 'replacement' }],
          },
        ],
      },
      makeCtx()
    )

    expect(result.success).toBe(false)
    expect(result.error).toContain('1 file edit(s) failed')
    expect(result.output).toContain('1 succeeded, 1 failed')
    expect(result.output).toContain('FAIL /project/bad.ts')
    expect(await platform.fs.readFile('/project/ok.ts')).toContain('export const ok = 99;')
    expect(await platform.fs.readFile('/project/bad.ts')).toContain('export const bad = 2;')
  })

  it('resolves relative paths for multi-file input', async () => {
    platform.fs.addFile('/workspace/src/a.ts', 'const x = 1;\n')
    platform.fs.addFile('/workspace/src/b.ts', 'const y = 2;\n')

    const result = await multieditTool.execute(
      {
        files: [
          {
            filePath: 'src/a.ts',
            edits: [{ oldString: 'const x = 1;', newString: 'const x = 10;' }],
          },
          {
            filePath: 'src/b.ts',
            edits: [{ oldString: 'const y = 2;', newString: 'const y = 20;' }],
          },
        ],
      },
      {
        sessionId: 'test',
        workingDirectory: '/workspace',
        signal: AbortSignal.timeout(5000),
      }
    )

    expect(result.success).toBe(true)
    expect(await platform.fs.readFile('/workspace/src/a.ts')).toContain('const x = 10;')
    expect(await platform.fs.readFile('/workspace/src/b.ts')).toContain('const y = 20;')
  })
})
