import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { resetLogger } from '../logger/logger.js'
import type { ChildProcess, IPlatformProvider } from '../platform.js'
import { setPlatform } from '../platform.js'
import { bashTool } from './bash.js'
import type { ToolContext } from './types.js'

/**
 * Tests for bash tool with null streams (Tauri platform simulation)
 *
 * These tests verify that the bash tool properly handles platforms where
 * spawn() returns null for stdin/stdout/stderr (like Tauri).
 */
describe('bash tool with null streams', () => {
  beforeEach(() => {
    resetLogger()
  })

  afterEach(() => {
    vi.restoreAllMocks()
  })

  function makeCtx(cwd = '/project'): ToolContext {
    return {
      sessionId: 'test',
      workingDirectory: cwd,
      signal: new AbortController().signal,
    }
  }

  it('should use wait() result when streams are null', async () => {
    // Mock platform with null streams (like Tauri)
    const mockShell = {
      spawn: vi.fn().mockReturnValue({
        pid: 123,
        stdin: null,
        stdout: null,
        stderr: null,
        kill: vi.fn(),
        wait: vi.fn().mockResolvedValue({
          stdout: 'output from wait',
          stderr: 'error from wait',
          exitCode: 0,
        }),
      } as unknown as ChildProcess),
      exec: vi.fn(),
    }

    const mockPlatform: IPlatformProvider = {
      fs: {} as IPlatformProvider['fs'],
      shell: mockShell,
      credentials: {} as IPlatformProvider['credentials'],
      database: {} as IPlatformProvider['database'],
    }

    setPlatform(mockPlatform)

    const result = await bashTool.execute(
      { command: 'echo hello', description: 'Test null streams' },
      makeCtx()
    )

    expect(result.success).toBe(true)
    expect(result.output).toContain('output from wait')
    expect(mockShell.spawn).toHaveBeenCalledWith('bash', ['-c', 'echo hello'], expect.any(Object))
  })

  it('should handle stderr from wait() when streams are null', async () => {
    const mockShell = {
      spawn: vi.fn().mockReturnValue({
        pid: 123,
        stdin: null,
        stdout: null,
        stderr: null,
        kill: vi.fn(),
        wait: vi.fn().mockResolvedValue({
          stdout: '',
          stderr: 'error message',
          exitCode: 1,
        }),
      } as unknown as ChildProcess),
      exec: vi.fn(),
    }

    const mockPlatform: IPlatformProvider = {
      fs: {} as IPlatformProvider['fs'],
      shell: mockShell,
      credentials: {} as IPlatformProvider['credentials'],
      database: {} as IPlatformProvider['database'],
    }

    setPlatform(mockPlatform)

    const result = await bashTool.execute(
      { command: 'false', description: 'Test stderr' },
      makeCtx()
    )

    expect(result.success).toBe(false)
    expect(result.output).toContain('error message')
    expect(result.output).toContain('Exit code: 1')
  })

  it('should handle non-zero exit codes from wait()', async () => {
    const mockShell = {
      spawn: vi.fn().mockReturnValue({
        pid: 123,
        stdin: null,
        stdout: null,
        stderr: null,
        kill: vi.fn(),
        wait: vi.fn().mockResolvedValue({
          stdout: 'some output',
          stderr: '',
          exitCode: 42,
        }),
      } as unknown as ChildProcess),
      exec: vi.fn(),
    }

    const mockPlatform: IPlatformProvider = {
      fs: {} as IPlatformProvider['fs'],
      shell: mockShell,
      credentials: {} as IPlatformProvider['credentials'],
      database: {} as IPlatformProvider['database'],
    }

    setPlatform(mockPlatform)

    const result = await bashTool.execute(
      { command: 'exit 42', description: 'Test exit code' },
      makeCtx()
    )

    expect(result.success).toBe(false)
    expect(result.output).toContain('Exit code: 42')
  })

  it('should prefer streaming output when streams are available', async () => {
    // Create mock readable streams
    const mockReader = {
      read: vi
        .fn()
        .mockResolvedValueOnce({ done: false, value: new TextEncoder().encode('streamed ') })
        .mockResolvedValueOnce({ done: false, value: new TextEncoder().encode('output') })
        .mockResolvedValueOnce({ done: true }),
      releaseLock: vi.fn(),
    }

    const mockStream = {
      getReader: vi.fn().mockReturnValue(mockReader),
    }

    const mockShell = {
      spawn: vi.fn().mockReturnValue({
        pid: 123,
        stdin: null,
        stdout: mockStream as unknown as ReadableStream<Uint8Array>,
        stderr: null,
        kill: vi.fn(),
        wait: vi.fn().mockResolvedValue({
          stdout: 'wait output',
          stderr: '',
          exitCode: 0,
        }),
      } as unknown as ChildProcess),
      exec: vi.fn(),
    }

    const mockPlatform: IPlatformProvider = {
      fs: {} as IPlatformProvider['fs'],
      shell: mockShell,
      credentials: {} as IPlatformProvider['credentials'],
      database: {} as IPlatformProvider['database'],
    }

    setPlatform(mockPlatform)

    const result = await bashTool.execute(
      { command: 'echo hello', description: 'Test streaming' },
      makeCtx()
    )

    // Should use streamed output, not wait() output
    expect(result.success).toBe(true)
    expect(result.output).toContain('streamed output')
    expect(result.output).not.toContain('wait output')
  })

  it('should handle mixed scenario: stdout streamed, stderr from wait() in failure case', async () => {
    // Mock stdout stream
    const mockStdoutReader = {
      read: vi
        .fn()
        .mockResolvedValueOnce({ done: false, value: new TextEncoder().encode('stdout data') })
        .mockResolvedValueOnce({ done: true }),
      releaseLock: vi.fn(),
    }

    const mockStdoutStream = {
      getReader: vi.fn().mockReturnValue(mockStdoutReader),
    }

    const mockShell = {
      spawn: vi.fn().mockReturnValue({
        pid: 123,
        stdin: null,
        stdout: mockStdoutStream as unknown as ReadableStream<Uint8Array>,
        stderr: null, // No stderr stream
        kill: vi.fn(),
        wait: vi.fn().mockResolvedValue({
          stdout: 'wait stdout',
          stderr: 'stderr from wait',
          exitCode: 1, // Non-zero exit code to trigger stderr display
        }),
      } as unknown as ChildProcess),
      exec: vi.fn(),
    }

    const mockPlatform: IPlatformProvider = {
      fs: {} as IPlatformProvider['fs'],
      shell: mockShell,
      credentials: {} as IPlatformProvider['credentials'],
      database: {} as IPlatformProvider['database'],
    }

    setPlatform(mockPlatform)

    const result = await bashTool.execute(
      { command: 'echo hello', description: 'Test mixed' },
      makeCtx()
    )

    expect(result.success).toBe(false) // Failed due to non-zero exit
    expect(result.output).toContain('stdout data')
    expect(result.output).toContain('stderr from wait')
  })
})
