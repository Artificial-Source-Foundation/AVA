/**
 * Tests for Delta9 Session Isolation Manager
 */

import { describe, it, expect, beforeEach, afterEach } from 'vitest'
import {
  SessionIsolationManager,
  getSessionIsolationManager,
  clearSessionIsolationManager,
  registerSession,
  getRootSession,
  areSessionsRelated,
  cleanupSessionTree,
} from '../../src/lib/session-isolation.js'

describe('SessionIsolationManager', () => {
  let manager: SessionIsolationManager

  beforeEach(() => {
    manager = new SessionIsolationManager()
  })

  afterEach(() => {
    manager.clear()
  })

  describe('registerSession', () => {
    it('registers a root session', () => {
      const info = manager.registerSession('root-1')

      expect(info.id).toBe('root-1')
      expect(info.parentId).toBeNull()
      expect(info.rootId).toBe('root-1')
      expect(info.createdAt).toBeDefined()
    })

    it('registers a child session', () => {
      manager.registerSession('root-1')
      const child = manager.registerSession('child-1', 'root-1')

      expect(child.id).toBe('child-1')
      expect(child.parentId).toBe('root-1')
      expect(child.rootId).toBe('root-1')
    })

    it('registers nested child sessions', () => {
      manager.registerSession('root-1')
      manager.registerSession('child-1', 'root-1')
      const grandchild = manager.registerSession('grandchild-1', 'child-1')

      expect(grandchild.id).toBe('grandchild-1')
      expect(grandchild.parentId).toBe('child-1')
      expect(grandchild.rootId).toBe('root-1')
    })

    it('stores metadata', () => {
      const info = manager.registerSession('root-1', null, { agentType: 'commander' })

      expect(info.metadata).toEqual({ agentType: 'commander' })
    })

    it('returns existing session if already registered', () => {
      const first = manager.registerSession('root-1')
      const second = manager.registerSession('root-1')

      expect(first).toEqual(second)
    })

    it('handles unknown parent as root', () => {
      // If parent doesn't exist, treat parent ID as root
      const info = manager.registerSession('orphan', 'unknown-parent')

      expect(info.rootId).toBe('unknown-parent')
    })
  })

  describe('unregisterSession', () => {
    it('unregisters a session', () => {
      manager.registerSession('root-1')
      expect(manager.getSession('root-1')).not.toBeNull()

      const result = manager.unregisterSession('root-1')
      expect(result).toBe(true)
      expect(manager.getSession('root-1')).toBeNull()
    })

    it('returns false for non-existent session', () => {
      const result = manager.unregisterSession('non-existent')
      expect(result).toBe(false)
    })

    it('removes session from root tracking', () => {
      manager.registerSession('root-1')
      manager.registerSession('child-1', 'root-1')

      manager.unregisterSession('child-1')

      const sessions = manager.getSessionsInTree('root-1')
      expect(sessions).toContain('root-1')
      expect(sessions).not.toContain('child-1')
    })
  })

  describe('getRootSession', () => {
    it('returns root ID for root session', () => {
      manager.registerSession('root-1')
      expect(manager.getRootSession('root-1')).toBe('root-1')
    })

    it('returns root ID for child session', () => {
      manager.registerSession('root-1')
      manager.registerSession('child-1', 'root-1')
      manager.registerSession('grandchild-1', 'child-1')

      expect(manager.getRootSession('child-1')).toBe('root-1')
      expect(manager.getRootSession('grandchild-1')).toBe('root-1')
    })

    it('returns null for unknown session', () => {
      expect(manager.getRootSession('unknown')).toBeNull()
    })
  })

  describe('isRootSession', () => {
    it('returns true for root sessions', () => {
      manager.registerSession('root-1')
      expect(manager.isRootSession('root-1')).toBe(true)
    })

    it('returns false for child sessions', () => {
      manager.registerSession('root-1')
      manager.registerSession('child-1', 'root-1')
      expect(manager.isRootSession('child-1')).toBe(false)
    })

    it('returns false for unknown sessions', () => {
      expect(manager.isRootSession('unknown')).toBe(false)
    })
  })

  describe('getSession', () => {
    it('returns session info', () => {
      manager.registerSession('root-1', null, { type: 'test' })
      const info = manager.getSession('root-1')

      expect(info).not.toBeNull()
      expect(info?.id).toBe('root-1')
      expect(info?.metadata).toEqual({ type: 'test' })
    })

    it('returns null for unknown session', () => {
      expect(manager.getSession('unknown')).toBeNull()
    })
  })

  describe('getSessionsInTree', () => {
    it('returns all sessions in a root tree', () => {
      manager.registerSession('root-1')
      manager.registerSession('child-1', 'root-1')
      manager.registerSession('child-2', 'root-1')
      manager.registerSession('grandchild-1', 'child-1')

      const sessions = manager.getSessionsInTree('root-1')
      expect(sessions).toHaveLength(4)
      expect(sessions).toContain('root-1')
      expect(sessions).toContain('child-1')
      expect(sessions).toContain('child-2')
      expect(sessions).toContain('grandchild-1')
    })

    it('returns empty array for unknown root', () => {
      const sessions = manager.getSessionsInTree('unknown')
      expect(sessions).toEqual([])
    })

    it('isolates different root trees', () => {
      manager.registerSession('root-1')
      manager.registerSession('child-1', 'root-1')
      manager.registerSession('root-2')
      manager.registerSession('child-2', 'root-2')

      const tree1 = manager.getSessionsInTree('root-1')
      const tree2 = manager.getSessionsInTree('root-2')

      expect(tree1).toHaveLength(2)
      expect(tree1).toContain('root-1')
      expect(tree1).toContain('child-1')
      expect(tree1).not.toContain('root-2')

      expect(tree2).toHaveLength(2)
      expect(tree2).toContain('root-2')
      expect(tree2).toContain('child-2')
    })
  })

  describe('isInSameTree', () => {
    it('returns true for sessions in same tree', () => {
      manager.registerSession('root-1')
      manager.registerSession('child-1', 'root-1')
      manager.registerSession('child-2', 'root-1')

      expect(manager.isInSameTree('root-1', 'child-1')).toBe(true)
      expect(manager.isInSameTree('child-1', 'child-2')).toBe(true)
    })

    it('returns false for sessions in different trees', () => {
      manager.registerSession('root-1')
      manager.registerSession('child-1', 'root-1')
      manager.registerSession('root-2')
      manager.registerSession('child-2', 'root-2')

      expect(manager.isInSameTree('root-1', 'root-2')).toBe(false)
      expect(manager.isInSameTree('child-1', 'child-2')).toBe(false)
    })

    it('returns false for unknown sessions', () => {
      manager.registerSession('root-1')
      expect(manager.isInSameTree('root-1', 'unknown')).toBe(false)
      expect(manager.isInSameTree('unknown-1', 'unknown-2')).toBe(false)
    })
  })

  describe('getChildren', () => {
    it('returns direct children', () => {
      manager.registerSession('root-1')
      manager.registerSession('child-1', 'root-1')
      manager.registerSession('child-2', 'root-1')
      manager.registerSession('grandchild-1', 'child-1')

      const children = manager.getChildren('root-1')
      expect(children).toHaveLength(2)
      expect(children).toContain('child-1')
      expect(children).toContain('child-2')
      expect(children).not.toContain('grandchild-1')
    })

    it('returns empty array for leaf sessions', () => {
      manager.registerSession('root-1')
      manager.registerSession('child-1', 'root-1')

      const children = manager.getChildren('child-1')
      expect(children).toEqual([])
    })
  })

  describe('getDescendants', () => {
    it('returns all descendants recursively', () => {
      manager.registerSession('root-1')
      manager.registerSession('child-1', 'root-1')
      manager.registerSession('child-2', 'root-1')
      manager.registerSession('grandchild-1', 'child-1')
      manager.registerSession('greatgrandchild-1', 'grandchild-1')

      const descendants = manager.getDescendants('root-1')
      expect(descendants).toHaveLength(4)
      expect(descendants).toContain('child-1')
      expect(descendants).toContain('child-2')
      expect(descendants).toContain('grandchild-1')
      expect(descendants).toContain('greatgrandchild-1')
    })

    it('returns empty array for leaf sessions', () => {
      manager.registerSession('root-1')
      const descendants = manager.getDescendants('root-1')
      expect(descendants).toEqual([])
    })
  })

  describe('cleanup', () => {
    it('removes all sessions in a root tree', () => {
      manager.registerSession('root-1')
      manager.registerSession('child-1', 'root-1')
      manager.registerSession('child-2', 'root-1')

      const count = manager.cleanup('root-1')

      expect(count).toBe(3)
      expect(manager.getSession('root-1')).toBeNull()
      expect(manager.getSession('child-1')).toBeNull()
      expect(manager.getSession('child-2')).toBeNull()
    })

    it('does not affect other root trees', () => {
      manager.registerSession('root-1')
      manager.registerSession('child-1', 'root-1')
      manager.registerSession('root-2')
      manager.registerSession('child-2', 'root-2')

      manager.cleanup('root-1')

      expect(manager.getSession('root-2')).not.toBeNull()
      expect(manager.getSession('child-2')).not.toBeNull()
    })

    it('returns 0 for unknown root', () => {
      const count = manager.cleanup('unknown')
      expect(count).toBe(0)
    })
  })

  describe('clear', () => {
    it('removes all sessions', () => {
      manager.registerSession('root-1')
      manager.registerSession('root-2')
      manager.registerSession('child-1', 'root-1')

      manager.clear()

      expect(manager.getSession('root-1')).toBeNull()
      expect(manager.getSession('root-2')).toBeNull()
      expect(manager.getSession('child-1')).toBeNull()
    })
  })

  describe('filterByRoot', () => {
    it('filters items by root session', () => {
      manager.registerSession('root-1')
      manager.registerSession('child-1', 'root-1')
      manager.registerSession('root-2')
      manager.registerSession('child-2', 'root-2')

      const items = [
        { id: 'task-1', sessionId: 'root-1' },
        { id: 'task-2', sessionId: 'child-1' },
        { id: 'task-3', sessionId: 'root-2' },
        { id: 'task-4', sessionId: 'child-2' },
      ]

      const filtered = manager.filterByRoot(items, (item) => item.sessionId, 'root-1')

      expect(filtered).toHaveLength(2)
      expect(filtered.map((i) => i.id)).toEqual(['task-1', 'task-2'])
    })

    it('handles items with undefined session ID', () => {
      manager.registerSession('root-1')

      const items = [
        { id: 'task-1', sessionId: 'root-1' },
        { id: 'task-2' }, // No sessionId
      ]

      const filtered = manager.filterByRoot(
        items,
        (item) => (item as { sessionId?: string }).sessionId,
        'root-1'
      )

      expect(filtered).toHaveLength(1)
      expect(filtered[0].id).toBe('task-1')
    })
  })

  describe('getStats', () => {
    it('returns session statistics', () => {
      manager.registerSession('root-1')
      manager.registerSession('child-1', 'root-1')
      manager.registerSession('child-2', 'root-1')
      manager.registerSession('root-2')

      const stats = manager.getStats()

      expect(stats.totalSessions).toBe(4)
      expect(stats.rootSessions).toBe(2)
      expect(stats.sessionsPerRoot.get('root-1')).toBe(3)
      expect(stats.sessionsPerRoot.get('root-2')).toBe(1)
    })
  })

  describe('getRootSessionIds', () => {
    it('returns all root session IDs', () => {
      manager.registerSession('root-1')
      manager.registerSession('child-1', 'root-1')
      manager.registerSession('root-2')
      manager.registerSession('root-3')

      const rootIds = manager.getRootSessionIds()

      expect(rootIds).toHaveLength(3)
      expect(rootIds).toContain('root-1')
      expect(rootIds).toContain('root-2')
      expect(rootIds).toContain('root-3')
    })
  })
})

describe('singleton functions', () => {
  beforeEach(() => {
    clearSessionIsolationManager()
  })

  afterEach(() => {
    clearSessionIsolationManager()
  })

  it('getSessionIsolationManager returns singleton', () => {
    const manager1 = getSessionIsolationManager()
    const manager2 = getSessionIsolationManager()
    expect(manager1).toBe(manager2)
  })

  it('registerSession adds to singleton', () => {
    const info = registerSession('test-session')
    expect(info.id).toBe('test-session')
    expect(getSessionIsolationManager().getSession('test-session')).not.toBeNull()
  })

  it('getRootSession returns root from singleton', () => {
    registerSession('root-1')
    registerSession('child-1', 'root-1')

    expect(getRootSession('child-1')).toBe('root-1')
  })

  it('areSessionsRelated checks singleton', () => {
    registerSession('root-1')
    registerSession('child-1', 'root-1')
    registerSession('root-2')

    expect(areSessionsRelated('root-1', 'child-1')).toBe(true)
    expect(areSessionsRelated('root-1', 'root-2')).toBe(false)
  })

  it('cleanupSessionTree cleans singleton', () => {
    registerSession('root-1')
    registerSession('child-1', 'root-1')

    const count = cleanupSessionTree('root-1')

    expect(count).toBe(2)
    expect(getSessionIsolationManager().getSession('root-1')).toBeNull()
  })
})
