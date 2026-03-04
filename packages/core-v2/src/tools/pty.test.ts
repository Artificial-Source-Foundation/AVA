/**
 * PTY tool tests — mocked platform PTY interface.
 */

import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { installMockPlatform, type MockPlatform } from '../__test-utils__/mock-platform.js'
import { resetLogger } from '../logger/logger.js'
import type { IPTY, PTYProcess } from '../platform.js'
import { setPlatform } from '../platform.js'
import { ptyTool } from './pty.js'
import type { ToolContext } from './types.js'

// ─── Helpers ────────────────────────────────────────────────────────────────

function makeCtx(overrides?: Partial<ToolContext>): ToolContext {
  return {
    sessionId: 'test',
    workingDirectory: '/project',
    signal: new AbortController().signal,
    ...overrides,
  }
}

function createMockPTYProcess(
  output: string,
  exitCode: number,
  options?: { delay?: number }
): PTYProcess {
  const dataCallbacks: ((data: string) => void)[] = []
  const exitCallbacks: ((code: number, signal?: number) => void)[] = []

  return {
    pid: 42,
    onData(cb) {
      dataCallbacks.push(cb)
    },
    onExit(cb) {
      exitCallbacks.push(cb)
    },
    write: vi.fn(),
    resize: vi.fn(),
    kill: vi.fn(() => {
      for (const cb of exitCallbacks) cb(137, 9)
    }),
    async wait() {
      // Simulate async output delivery
      if (options?.delay) {
        await new Promise((resolve) => setTimeout(resolve, options.delay))
      }
      for (const cb of dataCallbacks) cb(output)
      for (const cb of exitCallbacks) cb(exitCode)
      return { exitCode }
    },
  }
}

function createMockPTY(
  supported: boolean,
  processFactory?: (command: string, args: string[]) => PTYProcess
): IPTY {
  return {
    isSupported: () => supported,
    spawn: vi.fn((command: string, args: string[]) => {
      if (processFactory) return processFactory(command, args)
      return createMockPTYProcess('', 0)
    }),
  }
}

// ─── Tests ──────────────────────────────────────────────────────────────────

describe('pty tool', () => {
  let platform: MockPlatform

  beforeEach(() => {
    platform = installMockPlatform()
    platform.fs.addDir('/project')
  })

  afterEach(() => {
    resetLogger()
  })

  describe('definition', () => {
    it('has correct name and description', () => {
      expect(ptyTool.definition.name).toBe('pty')
      expect(ptyTool.definition.description).toContain('PTY')
    })

    it('has command as required param', () => {
      expect(ptyTool.definition.input_schema.required).toContain('command')
    })

    it('has optional timeout param', () => {
      expect(ptyTool.definition.input_schema.properties).toHaveProperty('timeout')
    })

    it('has execute permission', () => {
      expect(ptyTool.permissions).toContain('execute')
    })
  })

  describe('when PTY is not available', () => {
    it('returns error when platform has no pty', async () => {
      // Default mock platform has no pty field
      const result = await ptyTool.execute({ command: 'ls' }, makeCtx())

      expect(result.success).toBe(false)
      expect(result.output).toContain('PTY is not supported')
      expect(result.output).toContain('bash tool')
    })

    it('returns error when pty.isSupported() is false', async () => {
      const pty = createMockPTY(false)
      setPlatform({ ...platform, pty })

      const result = await ptyTool.execute({ command: 'ls' }, makeCtx())

      expect(result.success).toBe(false)
      expect(result.output).toContain('PTY is not supported')
    })
  })

  describe('when PTY is available', () => {
    it('executes command and returns output', async () => {
      const pty = createMockPTY(true, () => createMockPTYProcess('hello world\n', 0))
      setPlatform({ ...platform, pty })

      const result = await ptyTool.execute({ command: 'echo hello world' }, makeCtx())

      expect(result.success).toBe(true)
      expect(result.output).toContain('hello world')
    })

    it('spawns with correct arguments', async () => {
      const pty = createMockPTY(true, () => createMockPTYProcess('', 0))
      setPlatform({ ...platform, pty })

      await ptyTool.execute({ command: 'my-command' }, makeCtx())

      expect(pty.spawn).toHaveBeenCalledWith('bash', ['-c', 'my-command'], {
        cols: 120,
        rows: 40,
        cwd: '/project',
      })
    })

    it('uses custom working directory', async () => {
      platform.fs.addDir('/other')
      const pty = createMockPTY(true, () => createMockPTYProcess('', 0))
      setPlatform({ ...platform, pty })

      const result = await ptyTool.execute({ command: 'ls', workdir: '/other' }, makeCtx())

      expect(result.success).toBe(true)
      expect(pty.spawn).toHaveBeenCalledWith(
        'bash',
        ['-c', 'ls'],
        expect.objectContaining({
          cwd: '/other',
        })
      )
    })

    it('handles non-zero exit code', async () => {
      const pty = createMockPTY(true, () => createMockPTYProcess('error: something failed\n', 1))
      setPlatform({ ...platform, pty })

      const result = await ptyTool.execute({ command: 'false' }, makeCtx())

      expect(result.success).toBe(false)
      expect(result.output).toContain('Exit code: 1')
      expect(result.output).toContain('something failed')
    })

    it('returns metadata with pid and command', async () => {
      const pty = createMockPTY(true, () => createMockPTYProcess('ok\n', 0))
      setPlatform({ ...platform, pty })

      const result = await ptyTool.execute({ command: 'test-cmd' }, makeCtx())

      expect(result.metadata).toBeDefined()
      expect(result.metadata!.command).toBe('test-cmd')
      expect(result.metadata!.pid).toBe(42)
      expect(result.metadata!.exitCode).toBe(0)
    })

    it('returns exec location', async () => {
      const pty = createMockPTY(true, () => createMockPTYProcess('', 0))
      setPlatform({ ...platform, pty })

      const result = await ptyTool.execute({ command: 'ls' }, makeCtx())

      expect(result.locations).toBeDefined()
      expect(result.locations![0]).toMatchObject({ path: '/project', type: 'exec' })
    })

    it('strips ANSI escape sequences from output', async () => {
      const ansiOutput = '\x1B[32mgreen text\x1B[0m and \x1B[1mbold\x1B[0m\n'
      const pty = createMockPTY(true, () => createMockPTYProcess(ansiOutput, 0))
      setPlatform({ ...platform, pty })

      const result = await ptyTool.execute({ command: 'colored' }, makeCtx())

      expect(result.success).toBe(true)
      expect(result.output).toContain('green text')
      expect(result.output).toContain('bold')
      expect(result.output).not.toContain('\x1B[')
    })

    it('streams progress via onProgress callback', async () => {
      const chunks: string[] = []
      const pty = createMockPTY(true, () => createMockPTYProcess('hello\n', 0))
      setPlatform({ ...platform, pty })

      await ptyTool.execute(
        { command: 'echo hello' },
        makeCtx({
          onProgress: (data) => chunks.push(data.chunk),
        })
      )

      expect(chunks.length).toBeGreaterThan(0)
      expect(chunks.join('')).toContain('hello')
    })
  })

  describe('abort handling', () => {
    it('throws on pre-aborted signal', async () => {
      const controller = new AbortController()
      controller.abort()

      await expect(
        ptyTool.execute({ command: 'ls' }, makeCtx({ signal: controller.signal }))
      ).rejects.toThrow('Aborted')
    })
  })
})
