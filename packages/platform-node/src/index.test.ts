import { describe, expect, it } from 'vitest'
import { createNodePlatform } from '../src/index.js'

/**
 * Platform Node Integration Tests
 *
 * These tests verify that the Node platform implementation
 * works correctly with the core-v2 interfaces.
 */
describe('platform-node integration', () => {
  describe('createNodePlatform', () => {
    it('should create a valid platform instance', () => {
      const platform = createNodePlatform(':memory:')

      expect(platform).toBeDefined()
      expect(platform.fs).toBeDefined()
      expect(platform.shell).toBeDefined()
      expect(platform.credentials).toBeDefined()
      expect(platform.database).toBeDefined()
    })

    it('should have working file system', async () => {
      const platform = createNodePlatform(':memory:')

      // Test basic operations
      const testContent = 'Hello, World!'
      await platform.fs.writeFile('/tmp/test-file.txt', testContent)
      const readContent = await platform.fs.readFile('/tmp/test-file.txt')

      expect(readContent).toBe(testContent)

      // Cleanup
      await platform.fs.remove('/tmp/test-file.txt')
    })

    it('should have working shell', async () => {
      const platform = createNodePlatform(':memory:')

      const result = await platform.shell.exec('echo hello')

      expect(result.exitCode).toBe(0)
      expect(result.stdout).toContain('hello')
    })

    it('should have working spawn with streams', async () => {
      const platform = createNodePlatform(':memory:')

      const child = platform.shell.spawn('echo', ['test'])

      // Node platform should have streams
      expect(child.stdout).not.toBeNull()
      expect(child.stderr).not.toBeNull()

      const result = await child.wait()
      expect(result.exitCode).toBe(0)
      expect(result.stdout).toContain('test')
    })

    it('should have working credential store', async () => {
      const platform = createNodePlatform(':memory:')

      await platform.credentials.set('test-key', 'test-value')
      const value = await platform.credentials.get('test-key')

      expect(value).toBe('test-value')

      // Cleanup
      await platform.credentials.delete('test-key')
    })
  })
})
