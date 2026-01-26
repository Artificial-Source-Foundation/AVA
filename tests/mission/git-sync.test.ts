/**
 * Delta9 Git Sync Tests
 */

import { describe, it, expect, beforeEach } from 'vitest'
import { GitSync } from '../../src/mission/git-sync.js'
import type { Epic } from '../../src/mission/epic.js'

// =============================================================================
// Test Helpers
// =============================================================================

function createTestEpic(): Epic {
  return {
    id: 'epic-abc123',
    title: 'Test Feature Epic',
    description: 'A test epic for unit tests',
    status: 'planning',
    priority: 'normal',
    objectives: [],
    tasks: [],
    acceptanceCriteria: [],
    createdAt: new Date().toISOString(),
    updatedAt: new Date().toISOString(),
  }
}

// =============================================================================
// Git Sync Tests (Dry Run Mode)
// =============================================================================

describe('GitSync (Dry Run)', () => {
  let gitSync: GitSync

  beforeEach(() => {
    gitSync = new GitSync({ cwd: process.cwd(), dryRun: true })
  })

  describe('createEpicBranch()', () => {
    it('should return branch info in dry run mode', async () => {
      const epic = createTestEpic()
      const result = await gitSync.createEpicBranch(epic)

      expect(result.success).toBe(true)
      expect(result.branch).toBeDefined()
      expect(result.branch!.name).toContain('epic/')
      expect(result.branch!.isNew).toBe(true)
      expect(result.branch!.baseBranch).toBe('main')
    })

    it('should generate branch name from epic title', async () => {
      const epic = createTestEpic()
      epic.title = 'User Authentication & Security!'
      const result = await gitSync.createEpicBranch(epic)

      expect(result.branch!.name).toMatch(/epic\/abc123\/user-authentication-security/)
    })
  })

  describe('switchBranch()', () => {
    it('should return success in dry run mode', async () => {
      const result = await gitSync.switchBranch('feature/test')
      expect(result.success).toBe(true)
      expect(result.stdout).toContain('Would switch to')
    })
  })

  describe('getCurrentBranch()', () => {
    it('should return main in dry run mode', async () => {
      const branch = await gitSync.getCurrentBranch()
      expect(branch).toBe('main')
    })
  })

  describe('getDefaultBranch()', () => {
    it('should return main in dry run mode', async () => {
      const branch = await gitSync.getDefaultBranch()
      expect(branch).toBe('main')
    })
  })

  describe('commitTask()', () => {
    it('should return success in dry run mode', async () => {
      const result = await gitSync.commitTask('task-123', 'Add user model')
      expect(result.success).toBe(true)
      expect(result.stdout).toContain('Would commit')
    })
  })

  describe('getChangedFiles()', () => {
    it('should return empty array in dry run mode', async () => {
      const files = await gitSync.getChangedFiles()
      expect(files).toEqual([])
    })
  })

  describe('getFilesSinceCommit()', () => {
    it('should return empty array in dry run mode', async () => {
      const files = await gitSync.getFilesSinceCommit('abc123')
      expect(files).toEqual([])
    })
  })

  describe('checkpointObjective()', () => {
    it('should return tag name in dry run mode', async () => {
      const result = await gitSync.checkpointObjective('obj-1', 'Complete auth system')
      expect(result.success).toBe(true)
      expect(result.tag).toContain('checkpoint/')
    })

    it('should include epic ID in tag when provided', async () => {
      const result = await gitSync.checkpointObjective('obj-1', 'Description', 'epic-123')
      expect(result.tag).toContain('epic-123')
    })
  })

  describe('listCheckpoints()', () => {
    it('should return empty array in dry run mode', async () => {
      const checkpoints = await gitSync.listCheckpoints()
      expect(checkpoints).toEqual([])
    })
  })

  describe('isGitRepo()', () => {
    it('should return true in dry run mode', async () => {
      const isRepo = await gitSync.isGitRepo()
      expect(isRepo).toBe(true)
    })
  })

  describe('isClean()', () => {
    it('should return true in dry run mode', async () => {
      const isClean = await gitSync.isClean()
      expect(isClean).toBe(true)
    })
  })

  describe('getCurrentCommit()', () => {
    it('should return placeholder in dry run mode', async () => {
      const commit = await gitSync.getCurrentCommit()
      expect(commit).toBe('abc123')
    })
  })
})

// =============================================================================
// Git Sync Tests (Real Mode - Requires Git)
// =============================================================================

describe('GitSync (Real Mode)', () => {
  let gitSync: GitSync

  beforeEach(() => {
    gitSync = new GitSync({ cwd: process.cwd(), dryRun: false })
  })

  describe('isGitRepo()', () => {
    it('should detect git repository', async () => {
      const isRepo = await gitSync.isGitRepo()
      // This test runs in a git repo (the project)
      expect(isRepo).toBe(true)
    })
  })

  describe('getCurrentBranch()', () => {
    it('should return current branch', async () => {
      const branch = await gitSync.getCurrentBranch()
      expect(branch).toBeTruthy()
      expect(typeof branch).toBe('string')
    })
  })

  describe('getChangedFiles()', () => {
    it('should return array of strings', async () => {
      const files = await gitSync.getChangedFiles()
      expect(Array.isArray(files)).toBe(true)
    })
  })

  describe('getDefaultBranch()', () => {
    it('should return main or master', async () => {
      const branch = await gitSync.getDefaultBranch()
      expect(['main', 'master']).toContain(branch)
    })
  })
})
