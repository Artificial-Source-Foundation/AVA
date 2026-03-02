/**
 * Core tools smoke test — verifies all 6 core tools load and execute.
 */

import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { installMockPlatform, type MockPlatform } from '../__test-utils__/mock-platform.js'
import { bashTool } from './bash.js'
import { editTool } from './edit.js'
import { globTool } from './glob.js'
import { grepTool } from './grep.js'
import { readFileTool } from './read.js'
import type { ToolContext } from './types.js'
import { writeFileTool } from './write.js'

function createCtx(overrides?: Partial<ToolContext>): ToolContext {
  return {
    sessionId: 'smoke-test',
    workingDirectory: '/project',
    signal: new AbortController().signal,
    ...overrides,
  }
}

describe('Core tools smoke test', () => {
  let platform: MockPlatform

  beforeEach(() => {
    platform = installMockPlatform()
    platform.fs.addDir('/project')
    platform.fs.addDir('/project/src')
    platform.fs.addFile('/project/src/index.ts', 'export const hello = "world"\n')
    platform.fs.addFile(
      '/project/src/utils.ts',
      'export function add(a: number, b: number) { return a + b }\n'
    )
    platform.shell.setResult('bash -c echo hello', { stdout: 'hello\n', stderr: '', exitCode: 0 })
  })

  afterEach(() => {
    platform.fs.files.clear()
    platform.fs.dirs.clear()
  })

  describe('tool definitions', () => {
    it.each([
      ['read_file', readFileTool],
      ['write_file', writeFileTool],
      ['edit', editTool],
      ['bash', bashTool],
      ['glob', globTool],
      ['grep', grepTool],
    ])('%s has correct definition', (name, tool) => {
      expect(tool.definition.name).toBe(name)
      expect(tool.definition.description).toBeTruthy()
      expect(tool.definition.input_schema).toBeTruthy()
    })
  })

  describe('read_file', () => {
    it('reads an existing file', async () => {
      const result = await readFileTool.execute({ path: '/project/src/index.ts' }, createCtx())
      expect(result.success).toBe(true)
      expect(result.output).toContain('hello')
    })

    it('fails on non-existent file', async () => {
      await expect(
        readFileTool.execute({ path: '/project/src/missing.ts' }, createCtx())
      ).rejects.toThrow()
    })

    it('respects abort signal', async () => {
      const controller = new AbortController()
      controller.abort()
      await expect(
        readFileTool.execute(
          { path: '/project/src/index.ts' },
          createCtx({ signal: controller.signal })
        )
      ).rejects.toThrow()
    })
  })

  describe('write_file', () => {
    it('creates a new file', async () => {
      const result = await writeFileTool.execute(
        { path: '/project/src/new.ts', content: 'new content' },
        createCtx()
      )
      expect(result.success).toBe(true)
      expect(result.output).toContain('Created')
    })

    it('overwrites existing file', async () => {
      const result = await writeFileTool.execute(
        { path: '/project/src/index.ts', content: 'updated' },
        createCtx()
      )
      expect(result.success).toBe(true)
      expect(result.output).toContain('Updated')
    })
  })

  describe('edit', () => {
    it('replaces text in a file', async () => {
      const result = await editTool.execute(
        { filePath: '/project/src/index.ts', oldString: 'hello', newString: 'goodbye' },
        createCtx()
      )
      expect(result.success).toBe(true)
      const content = await platform.fs.readFile('/project/src/index.ts')
      expect(content).toContain('goodbye')
    })
  })

  describe('bash', () => {
    it('executes a shell command', async () => {
      const result = await bashTool.execute(
        { command: 'echo hello', description: 'Print hello' },
        createCtx()
      )
      expect(result.success).toBe(true)
      expect(result.output).toContain('hello')
    })
  })

  describe('glob', () => {
    it('finds files matching pattern', async () => {
      const result = await globTool.execute({ pattern: '**/*.ts' }, createCtx())
      expect(result.success).toBe(true)
      // Mock fs glob returns files containing the pattern fragment
      expect(result.output).toContain('.ts')
    })
  })

  describe('grep', () => {
    it('searches file contents', async () => {
      const result = await grepTool.execute({ pattern: 'hello' }, createCtx())
      expect(result.success).toBe(true)
      expect(result.output).toContain('index.ts')
    })
  })
})
