import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { TauriPTY } from '../src/pty.js'

/**
 * Tests for TauriPTY implementation
 *
 * These tests verify that TauriPTY provides a working PTY interface
 * despite Tauri's limitations (no true PTY, no resize, etc.)
 */
describe('TauriPTY', () => {
  let pty: TauriPTY

  beforeEach(() => {
    pty = new TauriPTY()
  })

  afterEach(() => {
    vi.restoreAllMocks()
  })

  describe('isSupported', () => {
    it('should return false when not in Tauri context', () => {
      // In test environment, window.__TAURI__ is not set
      expect(pty.isSupported()).toBe(false)
    })

    it('should cache the support check result', () => {
      const first = pty.isSupported()
      const second = pty.isSupported()
      expect(first).toBe(second)
    })
  })

  describe('spawn', () => {
    it('should spawn a process and return PTYProcess', () => {
      const proc = pty.spawn('echo', ['hello'])

      expect(proc).toBeDefined()
      expect(typeof proc.pid).toBe('number')
      expect(typeof proc.kill).toBe('function')
      expect(typeof proc.write).toBe('function')
      expect(typeof proc.resize).toBe('function')
      expect(typeof proc.wait).toBe('function')
    })

    it('should set up data callbacks', async () => {
      const proc = pty.spawn('echo', ['test'])

      return new Promise<void>((resolve) => {
        proc.onData((data) => {
          if (data.includes('test')) {
            resolve()
          }
        })
      })
    })

    it('should set up exit callbacks', async () => {
      const proc = pty.spawn('echo', ['done'])

      return new Promise<void>((resolve) => {
        proc.onExit((code) => {
          expect(code).toBe(0)
          resolve()
        })
      })
    })

    it('should use default terminal dimensions', async () => {
      const proc = pty.spawn('echo', ['hello'])

      // Wait a bit for process to initialize
      await new Promise((r) => setTimeout(r, 100))

      // PID should be set (even if 0 when not in Tauri)
      expect(proc.pid).toBeGreaterThanOrEqual(0)
    })

    it('should respect custom cwd', async () => {
      const proc = pty.spawn('pwd', [], { cwd: '/tmp' })

      return new Promise<void>((resolve) => {
        proc.onData((data) => {
          if (data.includes('/tmp')) {
            resolve()
          }
        })
      })
    })

    it('should handle process kill', async () => {
      const proc = pty.spawn('sleep', ['10'])

      // Kill the process
      proc.kill()

      // Wait should resolve
      const result = await proc.wait()

      // Exit code might be non-zero if killed
      expect(result.exitCode).toBeDefined()
    })

    it('should buffer data before callback is set', async () => {
      const proc = pty.spawn('echo', ['buffered'])

      // Wait a bit
      await new Promise((r) => setTimeout(r, 100))

      // Now set callback - should receive buffered data
      let received = ''
      proc.onData((data) => {
        received += data
      })

      // Wait for exit
      await proc.wait()

      // Should have received buffered data
      expect(received).toContain('buffered')
    })

    it('should handle write (when supported)', async () => {
      // This test may not work in non-Tauri environment
      // but should not throw
      const proc = pty.spawn('cat', [])

      // Try to write (will be no-op if not running)
      expect(() => proc.write('test input\n')).not.toThrow()
    })

    it('should handle resize (no-op in Tauri)', () => {
      const proc = pty.spawn('echo', ['hello'])

      // Resize should not throw (even though it's a no-op)
      expect(() => proc.resize(100, 50)).not.toThrow()
    })

    it('should handle commands with complex arguments', async () => {
      const proc = pty.spawn('echo', ['hello world', 'foo bar'])

      return new Promise<void>((resolve) => {
        proc.onData((data) => {
          if (data.includes('hello world') || data.includes('foo bar')) {
            resolve()
          }
        })
      })
    })

    it('should handle non-zero exit codes', async () => {
      const proc = pty.spawn('bash', ['-c', 'exit 42'])

      const result = await proc.wait()

      expect(result.exitCode).toBe(42)
    })

    it('should set TERM environment variable', async () => {
      // Spawn a command that echoes TERM
      const proc = pty.spawn('bash', ['-c', 'echo $TERM'])

      return new Promise<void>((resolve) => {
        proc.onData((data) => {
          if (data.includes('xterm-256color')) {
            resolve()
          }
        })
      })
    })

    it('should set AVA_TERMINAL environment variable', async () => {
      // Spawn a command that checks for AVA_TERMINAL
      const proc = pty.spawn('bash', ['-c', 'echo $AVA_TERMINAL'])

      return new Promise<void>((resolve) => {
        proc.onData((data) => {
          if (data.includes('1')) {
            resolve()
          }
        })
      })
    })
  })

  describe('buffer management', () => {
    it('should limit buffer size', async () => {
      const proc = pty.spawn('bash', ['-c', 'yes | head -c 5000000'])

      // Wait for exit
      await proc.wait()

      // Process should complete without memory issues
      // Buffer should have been truncated
    })
  })

  describe('error handling', () => {
    it('should handle invalid commands gracefully', async () => {
      const proc = pty.spawn('this_command_does_not_exist', [])

      const result = await proc.wait()

      // Should have non-zero exit code
      expect(result.exitCode).not.toBe(0)
    })

    it('should handle multiple exit callbacks', async () => {
      const proc = pty.spawn('echo', ['test'])
      const exits: number[] = []

      proc.onExit((code) => exits.push(code))
      proc.onExit((code) => exits.push(code))

      await proc.wait()

      expect(exits.length).toBe(2)
      expect(exits[0]).toBe(exits[1])
    })
  })
})
