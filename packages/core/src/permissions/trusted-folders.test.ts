/**
 * Trusted Folders Tests
 */

import { mkdir, readFile, rm, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { TrustedFolderManager } from './trusted-folders.js'

describe('TrustedFolderManager', () => {
  let manager: TrustedFolderManager
  let testDir: string
  let storagePath: string

  beforeEach(async () => {
    testDir = join(
      tmpdir(),
      `estela-trust-test-${Date.now()}-${Math.random().toString(36).slice(2, 8)}`
    )
    await mkdir(testDir, { recursive: true })
    storagePath = join(testDir, 'trusted-folders.json')
    manager = new TrustedFolderManager(storagePath)
  })

  afterEach(async () => {
    try {
      await rm(testDir, { recursive: true })
    } catch {
      // Ignore cleanup errors
    }
  })

  // =========================================================================
  // Add / Remove
  // =========================================================================

  describe('folder management', () => {
    it('should add a trusted folder', async () => {
      await manager.addFolder('/home/user/project')

      const folders = await manager.listFolders()
      expect(folders).toHaveLength(1)
      expect(folders[0]!.path).toContain('project')
    })

    it('should add folder with reason', async () => {
      await manager.addFolder('/home/user/project', 'My main project')

      const folders = await manager.listFolders()
      expect(folders[0]!.reason).toBe('My main project')
    })

    it('should remove a trusted folder', async () => {
      await manager.addFolder('/home/user/project')
      const removed = await manager.removeFolder('/home/user/project')

      expect(removed).toBe(true)
      const folders = await manager.listFolders()
      expect(folders).toHaveLength(0)
    })

    it('should return false when removing non-existent folder', async () => {
      const removed = await manager.removeFolder('/nonexistent')
      expect(removed).toBe(false)
    })

    it('should list folders sorted by path', async () => {
      await manager.addFolder('/home/user/z-project')
      await manager.addFolder('/home/user/a-project')

      const folders = await manager.listFolders()
      expect(folders[0]!.path).toContain('a-project')
      expect(folders[1]!.path).toContain('z-project')
    })

    it('should handle duplicate adds (overwrite)', async () => {
      await manager.addFolder('/home/user/project', 'first')
      await manager.addFolder('/home/user/project', 'second')

      const folders = await manager.listFolders()
      expect(folders).toHaveLength(1)
      expect(folders[0]!.reason).toBe('second')
    })

    it('should clear all folders', async () => {
      await manager.addFolder('/home/user/a')
      await manager.addFolder('/home/user/b')

      await manager.clear()

      const folders = await manager.listFolders()
      expect(folders).toHaveLength(0)
    })
  })

  // =========================================================================
  // Trust Checking
  // =========================================================================

  describe('trust checking', () => {
    it('should trust files within trusted folder', async () => {
      await manager.addFolder('/home/user/project')

      const result = await manager.isTrusted('/home/user/project/src/app.ts')
      expect(result.trusted).toBe(true)
      expect(result.folder).toBeDefined()
    })

    it('should trust the folder itself', async () => {
      await manager.addFolder('/home/user/project')

      const result = await manager.isTrusted('/home/user/project')
      expect(result.trusted).toBe(true)
    })

    it('should trust deeply nested files', async () => {
      await manager.addFolder('/home/user/project')

      const result = await manager.isTrusted('/home/user/project/src/components/deep/file.tsx')
      expect(result.trusted).toBe(true)
    })

    it('should not trust files outside trusted folder', async () => {
      await manager.addFolder('/home/user/project')

      const result = await manager.isTrusted('/etc/passwd')
      expect(result.trusted).toBe(false)
    })

    it('should not trust sibling directories', async () => {
      await manager.addFolder('/home/user/project-a')

      const result = await manager.isTrusted('/home/user/project-b/file.ts')
      expect(result.trusted).toBe(false)
    })

    it('should not trust parent directories', async () => {
      await manager.addFolder('/home/user/project/src')

      const result = await manager.isTrusted('/home/user/project/README.md')
      expect(result.trusted).toBe(false)
    })

    it('should not be tricked by prefix matching', async () => {
      await manager.addFolder('/home/user/project')

      // "/home/user/project-evil" should NOT match "/home/user/project"
      const result = await manager.isTrusted('/home/user/project-evil/malware.sh')
      expect(result.trusted).toBe(false)
    })

    it('should check multiple trusted folders', async () => {
      await manager.addFolder('/home/user/project-a')
      await manager.addFolder('/home/user/project-b')

      const resultA = await manager.isTrusted('/home/user/project-a/file.ts')
      const resultB = await manager.isTrusted('/home/user/project-b/file.ts')
      const resultC = await manager.isTrusted('/home/user/project-c/file.ts')

      expect(resultA.trusted).toBe(true)
      expect(resultB.trusted).toBe(true)
      expect(resultC.trusted).toBe(false)
    })

    it('should check all paths with areAllTrusted', async () => {
      await manager.addFolder('/home/user/project')

      const allTrusted = await manager.areAllTrusted([
        '/home/user/project/a.ts',
        '/home/user/project/b.ts',
      ])
      expect(allTrusted).toBe(true)

      const notAllTrusted = await manager.areAllTrusted(['/home/user/project/a.ts', '/etc/shadow'])
      expect(notAllTrusted).toBe(false)
    })
  })

  // =========================================================================
  // Sync Trust Checking
  // =========================================================================

  describe('sync trust checking', () => {
    it('should return false before load', () => {
      const result = manager.isTrustedSync('/any/path')
      expect(result.trusted).toBe(false)
    })

    it('should work after load', async () => {
      await manager.addFolder('/home/user/project')

      const result = manager.isTrustedSync('/home/user/project/file.ts')
      expect(result.trusted).toBe(true)
    })
  })

  // =========================================================================
  // Persistence
  // =========================================================================

  describe('persistence', () => {
    it('should persist folders to disk', async () => {
      await manager.addFolder('/home/user/project')

      // Read the file directly to verify
      const data = await readFile(storagePath, 'utf-8')
      const parsed = JSON.parse(data)

      expect(parsed.version).toBe(1)
      expect(parsed.folders).toHaveLength(1)
    })

    it('should load folders from disk', async () => {
      // Write directly to storage
      const data = {
        version: 1,
        folders: [{ path: '/home/user/project', addedAt: 1000, reason: 'test' }],
      }
      await writeFile(storagePath, JSON.stringify(data), 'utf-8')

      // Create new manager and load
      const newManager = new TrustedFolderManager(storagePath)
      const folders = await newManager.listFolders()

      expect(folders).toHaveLength(1)
      expect(folders[0]!.reason).toBe('test')
    })

    it('should survive round-trip serialization', async () => {
      await manager.addFolder('/home/user/project-a', 'Project A')
      await manager.addFolder('/home/user/project-b', 'Project B')

      // Create new manager pointing to same storage
      const newManager = new TrustedFolderManager(storagePath)
      const folders = await newManager.listFolders()

      expect(folders).toHaveLength(2)
    })

    it('should handle missing storage file', async () => {
      const newManager = new TrustedFolderManager(join(testDir, 'nonexistent.json'))
      const folders = await newManager.listFolders()
      expect(folders).toHaveLength(0)
    })

    it('should handle corrupted storage file', async () => {
      await writeFile(storagePath, 'not json', 'utf-8')

      const newManager = new TrustedFolderManager(storagePath)
      const folders = await newManager.listFolders()
      expect(folders).toHaveLength(0)
    })
  })

  // =========================================================================
  // Edge Cases
  // =========================================================================

  describe('edge cases', () => {
    it('should handle size property', async () => {
      expect(manager.size).toBe(0)

      await manager.addFolder('/a')
      expect(manager.size).toBe(1)

      await manager.addFolder('/b')
      expect(manager.size).toBe(2)
    })

    it('should handle reset', async () => {
      await manager.addFolder('/a')
      manager.reset()

      // After reset, load should be required again
      expect(manager.size).toBe(0)
    })
  })
})
