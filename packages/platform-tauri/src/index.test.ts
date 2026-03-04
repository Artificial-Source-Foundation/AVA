import { describe, expect, it } from 'vitest'
import { createTauriPlatform } from '../src/index.js'

/**
 * Platform Tauri Integration Tests
 *
 * These tests verify that the Tauri platform implementation
 * provides the expected interface and behavior.
 *
 * Note: Some tests are limited in a non-Tauri environment since
 * they require the actual Tauri runtime to be available.
 */
describe('platform-tauri integration', () => {
  describe('createTauriPlatform', () => {
    it('should create a valid platform instance', () => {
      const platform = createTauriPlatform(':memory:')

      expect(platform).toBeDefined()
      expect(platform.fs).toBeDefined()
      expect(platform.shell).toBeDefined()
      expect(platform.credentials).toBeDefined()
      expect(platform.database).toBeDefined()
    })

    it('should have null streams in shell.spawn (Tauri limitation)', () => {
      const platform = createTauriPlatform(':memory:')
      const child = platform.shell.spawn('echo', ['test'])

      // Tauri streams are null
      expect(child.stdout).toBeNull()
      expect(child.stderr).toBeNull()
      expect(child.stdin).toBeNull()
    })

    it('should still return output via wait()', async () => {
      const platform = createTauriPlatform(':memory:')
      const child = platform.shell.spawn('echo', ['hello'])

      const result = await child.wait()

      // Should still get output despite null streams
      expect(result.stdout).toBeDefined()
      expect(result.exitCode).toBe(0)
    })
  })

  describe('platform parity checks', () => {
    it('should implement IFileSystem interface', () => {
      const platform = createTauriPlatform(':memory:')
      const fs = platform.fs

      // Check all required methods exist
      expect(typeof fs.readFile).toBe('function')
      expect(typeof fs.writeFile).toBe('function')
      expect(typeof fs.readDir).toBe('function')
      expect(typeof fs.exists).toBe('function')
      expect(typeof fs.glob).toBe('function')
      expect(typeof fs.realpath).toBe('function')
    })

    it('should implement IShell interface', () => {
      const platform = createTauriPlatform(':memory:')
      const shell = platform.shell

      expect(typeof shell.exec).toBe('function')
      expect(typeof shell.spawn).toBe('function')
    })

    it('should implement ICredentialStore interface', () => {
      const platform = createTauriPlatform(':memory:')
      const creds = platform.credentials

      expect(typeof creds.get).toBe('function')
      expect(typeof creds.set).toBe('function')
      expect(typeof creds.delete).toBe('function')
      expect(typeof creds.has).toBe('function')
    })

    it('should implement IDatabase interface', () => {
      const platform = createTauriPlatform(':memory:')
      const db = platform.database

      expect(typeof db.query).toBe('function')
      expect(typeof db.execute).toBe('function')
      expect(typeof db.migrate).toBe('function')
      expect(typeof db.close).toBe('function')
    })
  })

  describe('Tauri-specific behaviors', () => {
    it('should handle PTY limitations', () => {
      const platform = createTauriPlatform(':memory:')

      if (platform.pty) {
        // PTY may not be supported in test environment
        const isSupported = platform.pty.isSupported()

        if (isSupported) {
          const proc = platform.pty.spawn('echo', ['test'])
          expect(proc).toBeDefined()
          expect(typeof proc.kill).toBe('function')
          expect(typeof proc.write).toBe('function')
          // resize is a no-op in Tauri
          expect(() => proc.resize(80, 24)).not.toThrow()
        }
      }
    })
  })
})
