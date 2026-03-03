import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { TauriShell } from '../src/shell.js'

/**
 * Tests for TauriShell output buffering fix
 *
 * These tests verify that TauriShell.spawn() properly returns command output
 * even when the child process streams are null (Tauri limitation).
 *
 * The fix ensures bash tool gets output via the wait() method instead of
 * trying to read from null streams.
 */
describe('TauriShell', () => {
  let shell: TauriShell

  beforeEach(() => {
    shell = new TauriShell()
  })

  afterEach(() => {
    vi.restoreAllMocks()
  })

  describe('spawn', () => {
    it('should return child process with null streams (Tauri limitation)', async () => {
      const child = shell.spawn('echo', ['hello'])

      // Streams should be null in Tauri implementation
      expect(child.stdout).toBeNull()
      expect(child.stderr).toBeNull()
      expect(child.stdin).toBeNull()
    })

    it('should return command output via wait() method', async () => {
      const child = shell.spawn('echo', ['test output'])

      const result = await child.wait()

      // Should have stdout from the command
      expect(result.stdout).toBeDefined()
      expect(result.exitCode).toBe(0)
    })

    it('should capture stderr output via wait()', async () => {
      const child = shell.spawn('bash', ['-c', 'echo error >&2'])

      const result = await child.wait()

      // Should have stderr from the command
      expect(result.stderr).toBeDefined()
    })

    it('should return correct exit code for failed commands', async () => {
      const child = shell.spawn('bash', ['-c', 'exit 42'])

      const result = await child.wait()

      expect(result.exitCode).toBe(42)
    })

    it('should handle commands with special characters', async () => {
      const child = shell.spawn('echo', ['hello world! @#$%'])

      const result = await child.wait()

      expect(result.stdout).toContain('hello world!')
      expect(result.exitCode).toBe(0)
    })

    it('should respect cwd option', async () => {
      const child = shell.spawn('pwd', [], { cwd: '/tmp' })

      const result = await child.wait()

      expect(result.stdout).toContain('/tmp')
    })

    it('should kill process when kill() is called', async () => {
      const child = shell.spawn('sleep', ['10'])

      // Kill the process
      child.kill()

      // Wait should complete (even if killed)
      const result = await child.wait()

      // Process should have been terminated
      expect(result.exitCode).not.toBe(0)
    })

    it('should kill process due to inactivity timeout', async () => {
      // Spawn a command that produces no output for a while
      const child = shell.spawn('sleep', ['5'], { inactivityTimeout: 100 })

      // Wait should throw or return error due to inactivity
      await expect(child.wait()).rejects.toThrow('inactivity')
    })

    it('should reset inactivity timer on output', async () => {
      // This test verifies that output resets the inactivity timer
      // We spawn a command that produces output periodically
      const child = shell.spawn('bash', ['-c', 'for i in 1 2 3; do echo $i; sleep 0.05; done'], {
        inactivityTimeout: 200,
      })

      const result = await child.wait()

      // Should complete successfully (not killed for inactivity)
      expect(result.exitCode).toBe(0)
      expect(result.stdout).toContain('1')
      expect(result.stdout).toContain('2')
      expect(result.stdout).toContain('3')
    })

    it('should log warning when killProcessGroup is requested (unsupported)', async () => {
      const consoleSpy = vi.spyOn(console, 'warn').mockImplementation(() => {})

      const child = shell.spawn('echo', ['hello'], { killProcessGroup: true })
      await child.wait()

      // Kill to trigger the warning
      child.kill()

      // Should have warned about unsupported feature
      expect(consoleSpy).toHaveBeenCalledWith(expect.stringContaining('killProcessGroup'))
    })
  })

  describe('exec', () => {
    it('should execute command and return result', async () => {
      const result = await shell.exec('echo hello')

      expect(result.stdout).toContain('hello')
      expect(result.exitCode).toBe(0)
    })

    it('should capture stderr', async () => {
      const result = await shell.exec('echo error >&2')

      expect(result.stderr).toContain('error')
    })

    it('should return non-zero exit code for failures', async () => {
      const result = await shell.exec('false')

      expect(result.exitCode).toBe(1)
    })

    it('should respect cwd option', async () => {
      const result = await shell.exec('pwd', { cwd: '/tmp' })

      expect(result.stdout).toContain('/tmp')
    })

    it('should timeout after specified duration', async () => {
      // This test might be flaky in CI, so we use a short timeout
      await expect(shell.exec('sleep 5', { timeout: 100 })).rejects.toThrow('timed out')
    })
  })
})
